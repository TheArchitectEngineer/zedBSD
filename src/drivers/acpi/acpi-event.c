/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ACPI events (ACPI 6.5 sections 4.8 and 5.6): the fixed events of the
 * PM1 registers (the power and sleep buttons), the general-purpose events
 * of the GPE blocks and the _Lxx and _Exx methods that handle them, the
 * switch into ACPI mode, and the S5 soft-off that turns the power off
 * (section 7.4 and 16.1).
 *
 * The work is split the way the SCI requires.  drv_acpi_sci_interrupt()
 * runs in the interrupt: it only reads the status registers, masks every
 * event that fired and records it.  drv_acpi_events_process() runs in a
 * thread: it runs the handlers and the AML, clears the status and unmasks
 * the events again.
 */

#include <kern/kcrt.h>
#include <uapi/errno.h>

#include <drivers/acpi/acpi.h>

#include "aml-internal.h"
#include "aml-os.h"

/*
 * The FADT fields the event code reads (ACPI 6.5 table 5.9).
 */
#define FADT_SCI_INT		46U
#define FADT_SMI_CMD		48U
#define FADT_ACPI_ENABLE	52U
#define FADT_PM1A_EVT_BLK	56U
#define FADT_PM1B_EVT_BLK	60U
#define FADT_PM1A_CNT_BLK	64U
#define FADT_PM1B_CNT_BLK	68U
#define FADT_GPE0_BLK		80U
#define FADT_GPE1_BLK		84U
#define FADT_PM1_EVT_LEN	88U
#define FADT_PM1_CNT_LEN	89U
#define FADT_GPE0_BLK_LEN	92U
#define FADT_GPE1_BLK_LEN	93U
#define FADT_GPE1_BASE		94U
#define FADT_FLAGS		112U
#define FADT_X_PM1A_EVT_BLK	148U
#define FADT_X_PM1B_EVT_BLK	160U
#define FADT_X_PM1A_CNT_BLK	172U
#define FADT_X_PM1B_CNT_BLK	184U
#define FADT_X_GPE0_BLK		220U
#define FADT_X_GPE1_BLK		232U
#define FADT_SLEEP_CONTROL_REG	244U
#define FADT_V1_LENGTH		116U

/*
 * The length of a Generic Address Structure, and where its address is.
 */
#define GAS_LENGTH		12U
#define GAS_ADDRESS		4U

/*
 * The FADT flags the event code reads.
 */
#define FADT_FLAG_POWER_BUTTON	(1U << 4)
#define FADT_FLAG_SLEEP_BUTTON	(1U << 5)
#define FADT_FLAG_HW_REDUCED	(1U << 20)

/*
 * The SCI_EN bit of PM1_CNT: set, the platform raises SCIs instead of SMIs.
 */
#define PM1_CNT_SCI_EN		0x0001U

/*
 * The GBL_RLS bit of PM1_CNT: written as one, it tells firmware that the
 * operating system let the Global Lock go while firmware waited for it.
 * SLP_EN is written as one only to sleep, so writes keep it clear.
 */
#define PM1_CNT_GBL_RLS		0x0004U
#define PM1_CNT_SLP_EN		0x2000U

/*
 * The SLP_TYP field of PM1_CNT: the sleep state the platform enters when
 * SLP_EN is written, in the platform's own numbering that \_S5 gives.
 */
#define PM1_CNT_SLP_TYP_SHIFT	10U
#define PM1_CNT_SLP_TYP_MASK	0x1c00U

/*
 * The WAK_STS bit of PM1_STS: set by a wake, written as one to clear it
 * before a sleep so that the wake is seen.
 */
#define PM1_STS_WAK_STS		0x8000U

/*
 * The sleep control register of a hardware-reduced platform (ACPI 6.5
 * section 4.8.3.7): SLP_TYP in bits 2 to 4 and SLP_EN in bit 5.
 */
#define SLEEP_CONTROL_SLP_TYP_SHIFT	2U
#define SLEEP_CONTROL_SLP_EN		0x20U

/*
 * The system state number of the soft-off state, the argument of _PTS.
 */
#define SLEEP_STATE_S5		5U

/*
 * How long the power may take to go after SLP_EN was written, in the
 * 100-nanosecond units of the interpreter's timer, and how many polls
 * of the timer bound the wait when the timer does not run.
 */
#define POWEROFF_WAIT		30000000ULL
#define POWEROFF_POLLS		100000000U

/*
 * The address space of a Generic Address Structure that is I/O ports.
 */
#define GAS_SPACE_SYSTEM_IO	1U

/*
 * How many GPEs the interpreter handles: the ones _Lxx and _Exx can name.
 */
#define GPE_MAX			256U

/*
 * How long the switch into ACPI mode may take, in 10-microsecond polls.
 */
#define ACPI_ENABLE_POLLS	30000U

/*
 * The fixed events: bit numbers of PM1_STS and PM1_EN.
 */
#define FIXED_EVENT_COUNT	16U

/*
 * The kinds of GPE handling.
 */
enum gpe_kind {
	GPE_NONE = 0,
	GPE_METHOD = 1,
	GPE_HANDLER = 2
};

/*
 * One register block the FADT names: an I/O port address and its length.
 */
struct register_block {
	uint32_t port;
	unsigned length;
};

/*
 * How one GPE is handled: by its _Lxx or _Exx method or by a driver's C
 * handler.  edge says the event is edge-triggered (_Exx), which clears
 * its status before the handler runs instead of after.  enabled is what
 * the event should be once its handling ends.
 */
struct gpe_entry {
	struct drv_acpi_node *method;
	drv_acpi_gpe_handler_t handler;
	void *argument;
	uint8_t kind;
	uint8_t edge;
	uint8_t enabled;
	uint8_t wake;
};

/*
 * The handler of one fixed event.
 */
struct fixed_entry {
	drv_acpi_fixed_handler_t handler;
	void *argument;
};

/*
 * The event hardware the FADT describes, and how each event is handled.
 *
 * drv_acpi_events_init() fills it; the interrupt and the processing
 * thread share it, and the pending masks are read and written only under
 * the event lock of aml-os.h.  ready is zero until the FADT was read.
 */
static struct {
	struct register_block pm1a_event;
	struct register_block pm1b_event;
	struct register_block pm1a_control;
	struct register_block pm1b_control;
	struct register_block gpe0;
	struct register_block gpe1;
	unsigned gpe1_base;
	unsigned gpe_count;
	uint32_t smi_command;
	uint32_t flags;
	uint16_t sci_interrupt;
	uint16_t fixed_enabled;
	uint16_t fixed_pending;
	uint8_t acpi_enable;
	uint8_t ready;
	uint8_t gpe_pending[GPE_MAX / 8U];
	struct gpe_entry gpes[GPE_MAX];
	struct fixed_entry fixed[FIXED_EVENT_COUNT];
} events;

/*
 * What the S5 soft-off needs: the SLP_TYP values \_S5 names for the PM1a
 * and PM1b control registers, and on a hardware-reduced platform the
 * sleep control register when it is in I/O space.  drv_acpi_events_init()
 * fills it from the namespace and the FADT, while the machine still runs
 * AML freely; drv_acpi_poweroff() only reads it.  known is zero until
 * \_S5 was read, and stays zero on a platform that cannot be turned off.
 */
static struct {
	struct register_block sleep_control;
	uint8_t type_a;
	uint8_t type_b;
	uint8_t reduced;
	uint8_t known;
} soft_off;

static uint32_t load_u32(const uint8_t *bytes);
static void read_soft_off(const uint8_t *fadt, size_t length, uint32_t flags);
static struct register_block fadt_block(const uint8_t *fadt, size_t length, unsigned legacy, unsigned wide, unsigned block_length);
static int enable_acpi_mode(void);
static void gpe_register(unsigned gpe, uint32_t *port, uint8_t *bit);
static uint8_t port_read8(uint32_t port);
static void port_write8(uint32_t port, uint8_t value);
static uint16_t pm1_read(unsigned offset);
static void pm1_write(unsigned offset, uint16_t value);
static void pm1_control_set(const struct register_block *block, uint16_t bits);
static void pm1_control_sleep(const struct register_block *block, uint8_t type, bool enable);
static void gpe_set_enable(unsigned gpe, bool enable);
static void gpe_clear(unsigned gpe);
static int gpe_method_visitor(struct drv_acpi_node *node, unsigned depth, void *argument);
static int wake_visitor(struct drv_acpi_node *node, unsigned depth, void *argument);
static int hex_digit(uint8_t character);
static void process_gpe(unsigned gpe);

/*
 * Reads the event hardware from the FADT, switches the platform into ACPI
 * mode, masks and clears every event, and enables the GPEs that have an
 * _Lxx or _Exx method and are not only for waking the system.
 */
int
drv_acpi_events_init(
	const uint8_t *fadt,
	size_t length)
{
	uint32_t flags;
	unsigned gpe;
	unsigned total;
	int error;

	/* Refuses a FADT too short to describe the hardware. */
	if (fadt == NULL || length < FADT_V1_LENGTH)
		return EINVAL;
	kern_memset(&events, 0, sizeof(events));

	/* Learns how the power is turned off, while AML still runs freely. */
	flags = load_u32(fadt + FADT_FLAGS);
	read_soft_off(fadt, length, flags);

	/* A hardware-reduced platform has no fixed hardware and no GPE blocks. */
	if ((flags & FADT_FLAG_HW_REDUCED) != 0) {
		drv_acpi_os_log("ACPI: hardware-reduced platform; no fixed events\n");
		return ENOTSUP;
	}

	/* Reads where the registers are. */
	events.sci_interrupt = (uint16_t)(fadt[FADT_SCI_INT] | fadt[FADT_SCI_INT + 1U] << 8);
	events.smi_command = load_u32(fadt + FADT_SMI_CMD);
	events.acpi_enable = fadt[FADT_ACPI_ENABLE];
	events.pm1a_event = fadt_block(fadt, length, FADT_PM1A_EVT_BLK, FADT_X_PM1A_EVT_BLK, fadt[FADT_PM1_EVT_LEN]);
	events.pm1b_event = fadt_block(fadt, length, FADT_PM1B_EVT_BLK, FADT_X_PM1B_EVT_BLK, fadt[FADT_PM1_EVT_LEN]);
	events.pm1a_control = fadt_block(fadt, length, FADT_PM1A_CNT_BLK, FADT_X_PM1A_CNT_BLK, fadt[FADT_PM1_CNT_LEN]);
	events.pm1b_control = fadt_block(fadt, length, FADT_PM1B_CNT_BLK, FADT_X_PM1B_CNT_BLK, fadt[FADT_PM1_CNT_LEN]);
	events.gpe0 = fadt_block(fadt, length, FADT_GPE0_BLK, FADT_X_GPE0_BLK, fadt[FADT_GPE0_BLK_LEN]);
	events.gpe1 = fadt_block(fadt, length, FADT_GPE1_BLK, FADT_X_GPE1_BLK, fadt[FADT_GPE1_BLK_LEN]);
	events.gpe1_base = fadt[FADT_GPE1_BASE];

	/* Counts the GPEs the blocks carry, up to the ones methods can name. */
	total = events.gpe0.length / 2U * 8U;
	if (events.gpe1.length != 0)
		total = events.gpe1_base + events.gpe1.length / 2U * 8U;
	if (total > GPE_MAX)
		total = GPE_MAX;
	events.gpe_count = total;

	/* Refuses a platform without PM1 event registers. */
	if (events.pm1a_event.port == 0 || events.pm1a_event.length < 4U)
		return ENODEV;

	/* Switches into ACPI mode, so that events raise SCIs. */
	error = enable_acpi_mode();
	if (error != 0)
		return error;

	/* Masks and clears every fixed event. */
	pm1_write(events.pm1a_event.length / 2U, 0);
	pm1_write(0, 0xffffU);

	/* Masks and clears every GPE. */
	for (gpe = 0; gpe < events.gpe_count; gpe++) {
		/* Masks one GPE and clears its status. */
		gpe_set_enable(gpe, false);
		gpe_clear(gpe);
	}

	/*
	 * Enables the Global Lock event, which firmware raises when it lets
	 * the lock go while the operating system waits; the waiter polls the
	 * lock, so the event needs no handler beyond clearing it.
	 */
	events.fixed_enabled = (uint16_t)(1U << DRV_ACPI_EVENT_GLOBAL_LOCK);
	pm1_write(events.pm1a_event.length / 2U, events.fixed_enabled);

	/* The registers are known and quiet: the SCI may be taken now. */
	events.ready = 1;

	/* Keeps the flags, which say whether the buttons are fixed hardware. */
	events.flags = flags;

	/* Finds the GPEs that only wake the system, then the GPE methods. */
	drv_acpi_walk(NULL, wake_visitor, NULL);
	drv_acpi_walk(NULL, gpe_method_visitor, NULL);

	/* Enables the runtime GPEs. */
	for (gpe = 0; gpe < events.gpe_count; gpe++) {
		/* A GPE with a method that does not only wake is a runtime event. */
		if (events.gpes[gpe].kind != GPE_METHOD || events.gpes[gpe].wake)
			continue;
		events.gpes[gpe].enabled = 1;
		gpe_set_enable(gpe, true);
	}

	/* Succeeded. */
	return 0;
}

/*
 * Reports the interrupt the SCI arrives on, or zero before initialization.
 */
unsigned
drv_acpi_sci_irq(void)
{
	/* The FADT names it. */
	return events.sci_interrupt;
}

/*
 * Installs the handler of a fixed event and enables the event.
 */
int
drv_acpi_fixed_event_install(
	enum drv_acpi_fixed_event event,
	drv_acpi_fixed_handler_t handler,
	void *argument)
{
	unsigned long state;
	uint16_t enable;

	/* Refuses an event before initialization or outside PM1. */
	if (!events.ready || (unsigned)event >= FIXED_EVENT_COUNT)
		return EINVAL;

	/* A button the platform has as a device, not as fixed hardware, has no fixed event. */
	if (event == DRV_ACPI_EVENT_POWER_BUTTON && (events.flags & FADT_FLAG_POWER_BUTTON) != 0)
		return ENODEV;
	if (event == DRV_ACPI_EVENT_SLEEP_BUTTON && (events.flags & FADT_FLAG_SLEEP_BUTTON) != 0)
		return ENODEV;

	/* Installs the handler. */
	events.fixed[event].handler = handler;
	events.fixed[event].argument = argument;

	/* Enables the event in PM1_EN. */
	state = drv_acpi_os_event_lock();
	events.fixed_enabled |= (uint16_t)(1U << event);
	enable = pm1_read(events.pm1a_event.length / 2U);
	pm1_write(events.pm1a_event.length / 2U, (uint16_t)(enable | (1U << event)));
	drv_acpi_os_event_unlock(state);

	/* Succeeded. */
	return 0;
}

/*
 * Installs a driver's C handler for a GPE, in place of any method, and
 * enables the GPE.
 */
int
drv_acpi_gpe_install(
	unsigned gpe,
	bool edge,
	drv_acpi_gpe_handler_t handler,
	void *argument)
{
	unsigned long state;

	/* Refuses a GPE the blocks do not have. */
	if (!events.ready || gpe >= events.gpe_count)
		return EINVAL;

	/* Installs the handler and enables the GPE. */
	state = drv_acpi_os_event_lock();
	events.gpes[gpe].kind = GPE_HANDLER;
	events.gpes[gpe].handler = handler;
	events.gpes[gpe].argument = argument;
	events.gpes[gpe].edge = (uint8_t)edge;
	events.gpes[gpe].enabled = 1;
	gpe_set_enable(gpe, true);
	drv_acpi_os_event_unlock(state);

	/* Succeeded. */
	return 0;
}

/*
 * The SCI's interrupt part: records every event whose status and enable
 * bits are both set and masks it, touching only hardware registers.
 *
 * It reports whether anything is pending for drv_acpi_events_process().
 */
bool
drv_acpi_sci_interrupt(void)
{
	unsigned long state;
	uint16_t status;
	uint16_t enable;
	uint16_t fired;
	uint32_t status_port;
	uint32_t enable_port;
	uint8_t status_byte;
	uint8_t enable_byte;
	uint8_t bit;
	unsigned gpe;
	bool pending;

	/* Nothing is pending before initialization. */
	if (!events.ready)
		return false;
	state = drv_acpi_os_event_lock();

	/* Records and masks the fixed events that fired. */
	status = pm1_read(0);
	enable = pm1_read(events.pm1a_event.length / 2U);
	fired = (uint16_t)(status & enable & events.fixed_enabled);
	if (fired != 0) {
		events.fixed_pending |= fired;
		pm1_write(events.pm1a_event.length / 2U, (uint16_t)(enable & ~fired));
	}

	/* Records and masks the GPEs that fired, a register at a time. */
	for (gpe = 0; gpe < events.gpe_count; gpe += 8U) {
		gpe_register(gpe, &status_port, &bit);
		enable_port = status_port;
		if (gpe < events.gpe1_base || events.gpe1.length == 0) {
			enable_port += events.gpe0.length / 2U;
		} else {
			enable_port += events.gpe1.length / 2U;
		}

		/* Reads the register's status and enable bits. */
		status_byte = port_read8(status_port);
		enable_byte = port_read8(enable_port);
		if ((status_byte & enable_byte) == 0)
			continue;

		/* Masks the fired events and records them as pending. */
		port_write8(enable_port, (uint8_t)(enable_byte & ~(status_byte & enable_byte)));
		events.gpe_pending[gpe / 8U] |= (uint8_t)(status_byte & enable_byte);
	}

	/* Reports whether anything is pending. */
	pending = events.fixed_pending != 0;
	for (gpe = 0; gpe < GPE_MAX / 8U && !pending; gpe++) {
		/* Stops at the first pending register. */
		if (events.gpe_pending[gpe] != 0)
			pending = true;
	}

	/* Lets the lock go and reports it. */
	drv_acpi_os_event_unlock(state);
	return pending;
}

/*
 * The SCI's thread part: runs the handler of every pending event, clears
 * its status and unmasks it again.
 */
void
drv_acpi_events_process(void)
{
	struct fixed_entry *entry;
	unsigned long state;
	uint16_t pending;
	uint16_t enable;
	uint8_t gpe_pending[GPE_MAX / 8U];
	unsigned event;
	unsigned gpe;

	/* Takes the pending events. */
	state = drv_acpi_os_event_lock();
	pending = events.fixed_pending;
	events.fixed_pending = 0;
	kern_memcpy(gpe_pending, events.gpe_pending, sizeof(gpe_pending));
	kern_memset(events.gpe_pending, 0, sizeof(events.gpe_pending));
	drv_acpi_os_event_unlock(state);

	/* Handles each pending fixed event, then clears and unmasks it. */
	for (event = 0; event < FIXED_EVENT_COUNT; event++) {
		/* Skips an event that did not fire. */
		if ((pending & (1U << event)) == 0)
			continue;

		/* Clears its status, calls its handler and unmasks it. */
		pm1_write(0, (uint16_t)(1U << event));
		entry = &events.fixed[event];
		if (entry->handler != NULL)
			entry->handler((enum drv_acpi_fixed_event)event, entry->argument);
		state = drv_acpi_os_event_lock();
		enable = pm1_read(events.pm1a_event.length / 2U);
		pm1_write(events.pm1a_event.length / 2U, (uint16_t)(enable | (1U << event)));
		drv_acpi_os_event_unlock(state);
	}

	/* Handles each pending GPE. */
	for (gpe = 0; gpe < events.gpe_count; gpe++) {
		/* Skips a GPE that did not fire. */
		if ((gpe_pending[gpe / 8U] & (1U << (gpe % 8U))) == 0)
			continue;
		process_gpe(gpe);
	}
}

/*
 * Tells firmware, with GBL_RLS, that the operating system let the Global
 * Lock go while firmware waited for it.
 */
void
drv_acpi_events_global_release(void)
{
	unsigned long state;

	/* Nothing can be signalled before the registers are known. */
	if (!events.ready)
		return;

	/* Sets GBL_RLS in each PM1 control block. */
	state = drv_acpi_os_event_lock();
	pm1_control_set(&events.pm1a_control, PM1_CNT_GBL_RLS);
	if (events.pm1b_control.length != 0)
		pm1_control_set(&events.pm1b_control, PM1_CNT_GBL_RLS);
	drv_acpi_os_event_unlock(state);
}

/*
 * Turns the power off: the S5 soft-off state.
 *
 * _PTS(5) tells firmware the transition is coming; then the SLP_TYP values
 * \_S5 named are written with SLP_EN into the PM1 control registers, or
 * into the sleep control register of a hardware-reduced platform.  The
 * write normally does not return.  It reports ENODEV when the platform gave
 * no way to turn itself off, and ETIMEDOUT when the power stayed on; the
 * caller halts then.
 */
int
drv_acpi_poweroff(void)
{
	struct drv_acpi_object *arguments[1];
	struct drv_acpi_object *state_number;
	struct drv_acpi_object *result;
	unsigned long state;
	uint64_t start;
	uint64_t now;
	unsigned poll;
	unsigned gpe;
	uint8_t control;
	int error;

	/* Refuses a platform whose soft-off state is unknown. */
	if (!soft_off.known)
		return ENODEV;

	/* Refuses a platform whose sleep register was not found. */
	if (soft_off.reduced) {
		if (soft_off.sleep_control.length == 0)
			return ENODEV;
	} else {
		if (!events.ready || events.pm1a_control.length == 0)
			return ENODEV;
	}

	/* Tells firmware with _PTS that S5 is coming; a firmware without _PTS needs no notice. */
	state_number = drv_acpi_object_integer_new(SLEEP_STATE_S5);
	if (state_number == NULL)
		return ENOMEM;
	arguments[0] = state_number;
	result = NULL;
	error = drv_acpi_evaluate(NULL, "\\_PTS", arguments, 1, &result);
	drv_acpi_object_release(result);
	drv_acpi_object_release(state_number);
	if (error != 0 && error != ENOENT)
		drv_acpi_os_log("ACPI: _PTS(5) failed (error %d); turning off anyway\n", error);

	/*
	 * From here no event is taken: the event lock keeps the SCI's thread
	 * out and interrupts off on this CPU until the power goes.
	 */
	state = drv_acpi_os_event_lock();

	/* A hardware-reduced platform sleeps through one byte. */
	if (soft_off.reduced) {
		control = (uint8_t)(soft_off.type_a << SLEEP_CONTROL_SLP_TYP_SHIFT);
		control |= SLEEP_CONTROL_SLP_EN;
		(void)drv_acpi_os_port_write(soft_off.sleep_control.port, 8, control);
	} else {
		/* Masks every event, so that nothing wakes the platform, and clears a stale wake. */
		pm1_write(events.pm1a_event.length / 2U, 0);
		for (gpe = 0; gpe < events.gpe_count; gpe++)
			gpe_set_enable(gpe, false);
		pm1_write(0, PM1_STS_WAK_STS);

		/* Writes the sleep type into both blocks first, then the type with SLP_EN, as ACPICA does. */
		pm1_control_sleep(&events.pm1a_control, soft_off.type_a, false);
		if (events.pm1b_control.length != 0)
			pm1_control_sleep(&events.pm1b_control, soft_off.type_b, false);
		pm1_control_sleep(&events.pm1a_control, soft_off.type_a, true);
		if (events.pm1b_control.length != 0)
			pm1_control_sleep(&events.pm1b_control, soft_off.type_b, true);
	}

	/* Waits for the power to go: by the timer when it runs, by count otherwise. */
	start = drv_acpi_os_timer();
	for (poll = 0; poll < POWEROFF_POLLS; poll++) {
		/* A platform still running this long after SLP_EN did not turn off. */
		now = drv_acpi_os_timer();
		if (now - start >= POWEROFF_WAIT)
			break;
	}

	/* Reports a platform that stayed on. */
	drv_acpi_os_event_unlock(state);
	drv_acpi_os_log("ACPI: the platform did not turn off\n");
	return ETIMEDOUT;
}

/* Reads a little-endian 32-bit value. */
static uint32_t
load_u32(
	const uint8_t *bytes)
{
	uint32_t value;

	/* Assembles the bytes, lowest first. */
	value = (uint32_t)bytes[0];
	value |= (uint32_t)bytes[1] << 8;
	value |= (uint32_t)bytes[2] << 16;
	value |= (uint32_t)bytes[3] << 24;

	/* Reports the value. */
	return value;
}

/*
 * Reads what the S5 soft-off needs: the SLP_TYP values \_S5 names, and on
 * a hardware-reduced platform the sleep control register of the FADT.
 */
static void
read_soft_off(
	const uint8_t *fadt,
	size_t length,
	uint32_t flags)
{
	struct drv_acpi_object *package;
	struct drv_acpi_object *element;
	uint8_t types[2];
	unsigned index;
	int error;

	/* A hardware-reduced platform sleeps through the sleep control register, when it is in I/O space. */
	if ((flags & FADT_FLAG_HW_REDUCED) != 0) {
		soft_off.reduced = 1;
		if (length >= FADT_SLEEP_CONTROL_REG + GAS_LENGTH && fadt[FADT_SLEEP_CONTROL_REG] == GAS_SPACE_SYSTEM_IO) {
			soft_off.sleep_control.port = load_u32(fadt + FADT_SLEEP_CONTROL_REG + GAS_ADDRESS);
			soft_off.sleep_control.length = 1U;
		}
		if (soft_off.sleep_control.port == 0) {
			soft_off.sleep_control.length = 0;
			drv_acpi_os_log("ACPI: no sleep control register in I/O space; no soft-off\n");
		}
	}

	/* \_S5 is a package whose first two integers are SLP_TYPa and SLP_TYPb; without it there is no soft-off. */
	package = NULL;
	error = drv_acpi_evaluate(NULL, "\\_S5", NULL, 0, &package);
	if (error != 0) {
		drv_acpi_os_log("ACPI: no _S5 (error %d); no soft-off\n", error);
		return;
	}

	/* Takes the two sleep types; a package with one integer serves both blocks with it. */
	types[0] = 0;
	types[1] = 0;
	for (index = 0; index < 2U; index++) {
		/* A missing or non-integer element leaves that type at zero, the value most firmware gives. */
		element = drv_acpi_object_package_element(package, index);
		if (element == NULL)
			continue;
		if (drv_acpi_object_type(element) != DRV_ACPI_TYPE_INTEGER)
			continue;
		types[index] = (uint8_t)(drv_acpi_object_integer(element) & 7U);
	}
	drv_acpi_object_release(package);

	/* Remembers them; from now on the soft-off needs no AML but _PTS. */
	soft_off.type_a = types[0];
	soft_off.type_b = types[1];
	soft_off.known = 1;
	drv_acpi_os_log("ACPI: S5 is SLP_TYP %u/%u\n", types[0], types[1]);
}

/*
 * Reads one register block's port: the 64-bit Generic Address Structure
 * when the FADT has one in I/O space, the 32-bit field otherwise.
 */
static struct register_block
fadt_block(
	const uint8_t *fadt,
	size_t length,
	unsigned legacy,
	unsigned wide,
	unsigned block_length)
{
	struct register_block block;
	uint32_t address;

	/* The 32-bit field every FADT has. */
	block.port = load_u32(fadt + legacy);
	block.length = block_length;

	/* A newer FADT's structure wins when it names I/O ports. */
	if (length >= wide + 12U && fadt[wide] == GAS_SPACE_SYSTEM_IO) {
		address = load_u32(fadt + wide + 4U);
		if (address != 0)
			block.port = address;
	}

	/* Reports the block; a port of zero means the block is absent. */
	if (block.port == 0)
		block.length = 0;
	return block;
}

/* Switches the platform into ACPI mode through the SMI command port. */
static int
enable_acpi_mode(void)
{
	uint32_t value;
	unsigned poll;
	int error;

	/* A platform already in ACPI mode, or without a way into it, needs nothing. */
	error = drv_acpi_os_port_read(events.pm1a_control.port, 16, &value);
	if (error != 0)
		return error;
	if ((value & PM1_CNT_SCI_EN) != 0 || events.smi_command == 0 || events.acpi_enable == 0)
		return 0;

	/* Asks the firmware to switch. */
	error = drv_acpi_os_port_write(events.smi_command, 8, events.acpi_enable);
	if (error != 0)
		return error;

	/* Waits for SCI_EN to come on. */
	for (poll = 0; poll < ACPI_ENABLE_POLLS; poll++) {
		/* Reads PM1_CNT again. */
		error = drv_acpi_os_port_read(events.pm1a_control.port, 16, &value);
		if (error != 0)
			return error;
		if ((value & PM1_CNT_SCI_EN) != 0)
			return 0;
		drv_acpi_os_stall(10);
	}

	/* Reports a firmware that did not switch. */
	drv_acpi_os_log("ACPI: the platform did not enter ACPI mode\n");
	return ETIMEDOUT;
}

/* Finds the status register port and bit of a GPE. */
static void
gpe_register(
	unsigned gpe,
	uint32_t *port,
	uint8_t *bit)
{
	/* GPEs below the second block's base are in the first. */
	if (events.gpe1.length == 0 || gpe < events.gpe1_base) {
		*port = events.gpe0.port + gpe / 8U;
	} else {
		*port = events.gpe1.port + (gpe - events.gpe1_base) / 8U;
	}

	/* Reports the bit inside the register. */
	*bit = (uint8_t)(1U << (gpe % 8U));
}

/* Reads one byte-wide event register. */
static uint8_t
port_read8(
	uint32_t port)
{
	uint32_t value;
	int error;

	/* A register that cannot be read reads as zero. */
	error = drv_acpi_os_port_read(port, 8, &value);
	if (error != 0)
		return 0;

	/* Reports the byte. */
	return (uint8_t)value;
}

/* Writes one byte-wide event register. */
static void
port_write8(
	uint32_t port,
	uint8_t value)
{
	/* A register that cannot be written is left alone. */
	(void)drv_acpi_os_port_write(port, 8, value);
}

/* Reads a PM1 event register (status at 0, enable at half the length), a and b ORed. */
static uint16_t
pm1_read(
	unsigned offset)
{
	uint32_t a;
	uint32_t b;
	int error;

	/* Reads the a block. */
	a = 0;
	error = drv_acpi_os_port_read(events.pm1a_event.port + offset, 16, &a);
	if (error != 0)
		a = 0;

	/* Reads the b block when there is one. */
	b = 0;
	if (events.pm1b_event.length != 0) {
		error = drv_acpi_os_port_read(events.pm1b_event.port + offset, 16, &b);
		if (error != 0)
			b = 0;
	}

	/* Reports both. */
	return (uint16_t)(a | b);
}

/* Writes a PM1 event register to both blocks. */
static void
pm1_write(
	unsigned offset,
	uint16_t value)
{
	/* Writes the a block. */
	(void)drv_acpi_os_port_write(events.pm1a_event.port + offset, 16, value);

	/* Writes the b block when there is one. */
	if (events.pm1b_event.length != 0)
		(void)drv_acpi_os_port_write(events.pm1b_event.port + offset, 16, value);
}

/* Writes bits as one into a PM1 control register, keeping its other bits. */
static void
pm1_control_set(
	const struct register_block *block,
	uint16_t bits)
{
	uint32_t value;
	int error;

	/* Reads the register. */
	error = drv_acpi_os_port_read(block->port, 16, &value);
	if (error != 0)
		return;

	/* Writes it back with the bits, and without SLP_EN. */
	value = (value & ~(uint32_t)PM1_CNT_SLP_EN) | bits;
	(void)drv_acpi_os_port_write(block->port, 16, value);
}

/* Writes SLP_TYP, and SLP_EN when asked, into a PM1 control register, keeping its other bits. */
static void
pm1_control_sleep(
	const struct register_block *block,
	uint8_t type,
	bool enable)
{
	uint32_t value;
	int error;

	/* Reads the register, so that SCI_EN and the rest stay as they are. */
	error = drv_acpi_os_port_read(block->port, 16, &value);
	if (error != 0)
		return;

	/* Replaces the sleep fields. */
	value &= ~(uint32_t)(PM1_CNT_SLP_TYP_MASK | PM1_CNT_SLP_EN);
	value |= (uint32_t)type << PM1_CNT_SLP_TYP_SHIFT;
	if (enable)
		value |= PM1_CNT_SLP_EN;

	/* Writes it; with SLP_EN the platform sleeps on this write. */
	(void)drv_acpi_os_port_write(block->port, 16, value);
}

/* Sets or clears a GPE's enable bit. */
static void
gpe_set_enable(
	unsigned gpe,
	bool enable)
{
	uint32_t port;
	uint8_t bit;
	uint8_t value;

	/* The enable register follows the status registers of its block. */
	gpe_register(gpe, &port, &bit);
	if (events.gpe1.length == 0 || gpe < events.gpe1_base) {
		port += events.gpe0.length / 2U;
	} else {
		port += events.gpe1.length / 2U;
	}

	/* Changes the one bit. */
	value = port_read8(port);
	if (enable) {
		value |= bit;
	} else {
		value &= (uint8_t)~bit;
	}

	/* Writes the register back. */
	port_write8(port, value);
}

/* Clears a GPE's status bit, which is written as one. */
static void
gpe_clear(
	unsigned gpe)
{
	uint32_t port;
	uint8_t bit;

	/* Writes the bit alone. */
	gpe_register(gpe, &port, &bit);
	port_write8(port, bit);
}

/* Records a GPE method, _Lxx or _Exx, found below \_GPE. */
static int
gpe_method_visitor(
	struct drv_acpi_node *node,
	unsigned depth,
	void *argument)
{
	struct drv_acpi_node *root;
	uint32_t gpe_scope;
	uint8_t kind;
	int high;
	int low;
	unsigned gpe;

	UNUSED_PARAMETER(depth);
	UNUSED_PARAMETER(argument);

	/* Only the methods directly below \_GPE count. */
	root = drv_acpi_root();
	gpe_scope = drv_acpi_ns_segment((const uint8_t *)"_GPE");
	if (node->object == NULL || node->object->type != DRV_ACPI_TYPE_METHOD)
		return 0;
	if (node->parent == NULL || node->parent->parent != root)
		return 0;
	if (node->parent->name != gpe_scope)
		return 0;

	/* The name is _L or _E and two hexadecimal digits. */
	kind = (uint8_t)(node->name >> 8);
	if ((node->name & 0xffU) != '_')
		return 0;
	if (kind != 'L' && kind != 'E')
		return 0;
	high = hex_digit((uint8_t)(node->name >> 16));
	low = hex_digit((uint8_t)(node->name >> 24));
	if (high < 0 || low < 0)
		return 0;

	/* Refuses a GPE the blocks do not have. */
	gpe = (unsigned)(high * 16 + low);
	if (gpe >= events.gpe_count)
		return 0;

	/* Records the method; a GPE a driver handles keeps its handler. */
	if (events.gpes[gpe].kind == GPE_NONE) {
		events.gpes[gpe].kind = GPE_METHOD;
		events.gpes[gpe].method = node;
		events.gpes[gpe].edge = (uint8_t)(kind == 'E');
	}

	/* Goes on with the walk. */
	return 0;
}

/* Marks the GPEs the devices' _PRW say they wake the system with. */
static int
wake_visitor(
	struct drv_acpi_node *node,
	unsigned depth,
	void *argument)
{
	struct drv_acpi_object *result;
	struct drv_acpi_object *first;
	uint32_t wake_name;
	uint64_t gpe;
	int error;

	UNUSED_PARAMETER(depth);
	UNUSED_PARAMETER(argument);

	/* Only a _PRW object counts. */
	wake_name = drv_acpi_ns_segment((const uint8_t *)"_PRW");
	if (node->name != wake_name)
		return 0;

	/* Evaluates it: a package whose first element is the GPE number. */
	error = drv_acpi_evaluate(node, NULL, NULL, 0, &result);
	if (error != 0 || result == NULL)
		return 0;
	first = drv_acpi_object_package_element(result, 0);
	if (first != NULL && first->type == DRV_ACPI_TYPE_INTEGER) {
		gpe = first->value.integer;
		if (gpe < events.gpe_count)
			events.gpes[gpe].wake = 1;
	}

	/* The package is no longer needed. */
	drv_acpi_object_release(result);

	/* Goes on with the walk. */
	return 0;
}

/* Decodes one upper-case hexadecimal digit, or reports -1. */
static int
hex_digit(
	uint8_t character)
{
	/* Decimal digits. */
	if (character >= '0' && character <= '9')
		return character - '0';

	/* Upper-case letters. */
	if (character >= 'A' && character <= 'F')
		return character - 'A' + 10;

	/* Reports anything else. */
	return -1;
}

/*
 * Handles one GPE: clears an edge event before its handler and a level
 * event after it, then unmasks it when it is still enabled.
 */
static void
process_gpe(
	unsigned gpe)
{
	struct gpe_entry *entry;
	struct drv_acpi_object *result;
	unsigned long state;
	int error;

	/* An edge event is cleared first, so that a new edge is not lost. */
	entry = &events.gpes[gpe];
	if (entry->edge)
		gpe_clear(gpe);

	/* Runs the driver's handler or the GPE method. */
	if (entry->kind == GPE_HANDLER && entry->handler != NULL) {
		entry->handler(gpe, entry->argument);
	} else if (entry->kind == GPE_METHOD) {
		result = NULL;
		error = drv_acpi_evaluate(entry->method, NULL, NULL, 0, &result);
		drv_acpi_object_release(result);
		if (error != 0)
			drv_acpi_os_log("ACPI: GPE 0x%x method failed (error %d)\n", gpe, error);
	} else {
		drv_acpi_os_log("ACPI: GPE 0x%x has no handler; it stays masked\n", gpe);
		entry->enabled = 0;
	}

	/* A level event is cleared once its source is handled. */
	if (!entry->edge)
		gpe_clear(gpe);

	/* Unmasks it again. */
	if (entry->enabled) {
		state = drv_acpi_os_event_lock();
		gpe_set_enable(gpe, true);
		drv_acpi_os_event_unlock(state);
	}
}
