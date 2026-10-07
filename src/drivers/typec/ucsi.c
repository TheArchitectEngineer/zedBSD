/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The UCSI core (ws050-p002; ucsi.h): runs one command at a time over a
 * transport, acknowledges each completion and each connector change as
 * the specification asks, and turns the PPM's answers into the Type-C
 * layer's connector records.
 *
 * Section and table numbers are those of UCSI 1.2 unless 3.1 is named
 * (the documents are named in ucsi.h).  Only the normal path is here: a
 * command that fails is reported to the caller, which gives up; CANCEL,
 * the recovery by PPM_RESET and GET_ERROR_STATUS come later.
 */

#include <kern/kcrt.h>
#include <uapi/errno.h>

#include <drivers/typec/typec.h>

#include "typec-os.h"
#include "ucsi.h"

/*
 * The command codes (Table A-1; the same in 3.1 Table A-1).
 */
#define UCSI_PPM_RESET 0x01U
#define UCSI_ACK_CC_CI 0x04U
#define UCSI_SET_NOTIFICATION_ENABLE 0x05U
#define UCSI_GET_CAPABILITY 0x06U
#define UCSI_GET_CONNECTOR_CAPABILITY 0x07U
#define UCSI_GET_ALTERNATE_MODES 0x0CU
#define UCSI_GET_CAM_SUPPORTED 0x0DU
#define UCSI_GET_CURRENT_CAM 0x0EU
#define UCSI_GET_PDOS 0x10U
#define UCSI_GET_CONNECTOR_STATUS 0x12U

/*
 * The fields of CCI (Table 3-2): the connector a change occurred on (bits
 * 1-7), the length of MESSAGE IN (bits 8-15), and the indicators.
 */
#define UCSI_CCI_CONNECTOR_SHIFT 1U
#define UCSI_CCI_CONNECTOR_MASK 0x7FU
#define UCSI_CCI_LENGTH_SHIFT 8U
#define UCSI_CCI_LENGTH_MASK 0xFFU
#define UCSI_CCI_NOT_SUPPORTED (1UL << 25)
#define UCSI_CCI_RESET_COMPLETED (1UL << 27)
#define UCSI_CCI_BUSY (1UL << 28)
#define UCSI_CCI_ACKNOWLEDGED (1UL << 29)
#define UCSI_CCI_ERROR (1UL << 30)
#define UCSI_CCI_COMPLETED (1UL << 31)

/*
 * Where the command-specific fields of CONTROL start (Table 3-3), and the
 * two acknowledgements of ACK_CC_CI (Table 4-7).
 */
#define UCSI_CONTROL_SPECIFIC_SHIFT 16U
#define UCSI_ACK_CONNECTOR_CHANGE (1ULL << 16)
#define UCSI_ACK_COMMAND_COMPLETED (1ULL << 17)

/*
 * The notifications of SET_NOTIFICATION_ENABLE (Table 4-9, bits of the
 * field at bit 16 of CONTROL).
 */
#define UCSI_NOTIFY_COMMAND_COMPLETED (1U << 0)
#define UCSI_NOTIFY_EXTERNAL_SUPPLY (1U << 1)
#define UCSI_NOTIFY_POWER_OPERATION (1U << 2)
#define UCSI_NOTIFY_PROVIDER_CAPABILITIES (1U << 5)
#define UCSI_NOTIFY_POWER_LEVEL (1U << 6)
#define UCSI_NOTIFY_PD_RESET (1U << 7)
#define UCSI_NOTIFY_SUPPORTED_CAM (1U << 8)
#define UCSI_NOTIFY_BATTERY_CHARGING (1U << 9)
#define UCSI_NOTIFY_PARTNER (1U << 11)
#define UCSI_NOTIFY_POWER_DIRECTION (1U << 12)
#define UCSI_NOTIFY_CONNECT (1U << 14)
#define UCSI_NOTIFY_ERROR (1U << 15)

/*
 * The optional features of GET_CAPABILITY's bmOptionalFeatures (Table
 * 4-54).
 */
#define UCSI_FEATURE_ALT_MODE_DETAILS (1U << 2)
#define UCSI_FEATURE_PDO_DETAILS (1U << 4)
#define UCSI_FEATURE_EXTERNAL_SUPPLY (1U << 6)
#define UCSI_FEATURE_PD_RESET (1U << 7)

/*
 * The recipients of GET_ALTERNATE_MODES (Table 4-24).
 */
#define UCSI_RECIPIENT_CONNECTOR 0U
#define UCSI_RECIPIENT_SOP 1U
#define UCSI_RECIPIENT_SOP_PRIME 2U

/*
 * How many Alternate Modes one GET_ALTERNATE_MODES returns at most (its
 * 2-bit count is the number less one, at most 1: Table 4-24, and the same
 * in 3.1), and the bytes of one (a 16-bit SVID and a 32-bit MID, Table
 * 4-26).
 */
#define UCSI_ALT_MODES_PER_COMMAND 2U
#define UCSI_ALT_MODE_BYTES 6U

/*
 * How many PDOs one GET_PDOS returns at most, and the largest offset plus
 * count field it accepts (Table 4-34 and the text under Table 4-36).
 */
#define UCSI_PDOS_PER_COMMAND 4U
#define UCSI_PDO_LAST_INDEX 7U

/*
 * GET_CURRENT_CAM's value for "in no Alternate Mode" (Table 4-31).
 */
#define UCSI_NO_CURRENT_MODE 0xFFU

/*
 * The first UCSI version whose GET_CONNECTOR_STATUS has the orientation
 * (3.1 Table 6-43 bit 86; that 2.0 has it is unconfirmed), and the bytes
 * the status needs to hold it.
 */
#define UCSI_VERSION_2 0x0200U
#define UCSI_STATUS_ORIENTATION_BIT 86U
#define UCSI_STATUS_ORIENTATION_BYTES 11U

/*
 * How long a command may take, how long PPM_RESET may take, and the
 * first and the longest step of the wait between two looks at CCI when
 * no notification comes.
 */
#define UCSI_COMMAND_TIMEOUT_MS 5000U
#define UCSI_RESET_TIMEOUT_MS 1000U
#define UCSI_STEP_FIRST_MS 20U
#define UCSI_STEP_LONGEST_MS 100U

static int ucsi_execute(struct drv_ucsi *ucsi, uint64_t control, uint8_t *message_in, size_t *length);
static int ucsi_command(struct drv_ucsi *ucsi, uint64_t control);
static int ucsi_acknowledge(struct drv_ucsi *ucsi, uint64_t which);
static int ucsi_reset(struct drv_ucsi *ucsi);
static int ucsi_notifications(struct drv_ucsi *ucsi, uint16_t notifications);
static int ucsi_capability(struct drv_ucsi *ucsi);
static int ucsi_connector_start(struct drv_ucsi *ucsi, unsigned number);
static int ucsi_connector_update(struct drv_ucsi *ucsi, unsigned number);
static int ucsi_status(struct drv_ucsi *ucsi, unsigned number, struct drv_typec_connector *record);
static void ucsi_partner_clear(struct drv_typec_connector *record);
static int ucsi_alt_modes(struct drv_ucsi *ucsi, unsigned number, unsigned recipient, struct drv_typec_alt_mode_list *list);
static int ucsi_current_modes(struct drv_ucsi *ucsi, unsigned number, struct drv_typec_connector *record);
static int ucsi_partner_pdos(struct drv_ucsi *ucsi, unsigned number, struct drv_typec_connector *record);
static int ucsi_pending_handle(struct drv_ucsi *ucsi);
static void ucsi_latch(struct drv_ucsi *ucsi, uint32_t cci);
static uint64_t ucsi_connector_control(unsigned command, unsigned number);
static uint32_t ucsi_get16(const uint8_t *data);
static uint32_t ucsi_get32(const uint8_t *data);
static uint32_t ucsi_bits(const uint8_t *data, size_t length, unsigned offset, unsigned width);

/*
 * The arrangement of UCSI 1.x (1.2 Table 3-1): VERSION, CCI and CONTROL,
 * then 16 bytes of MESSAGE IN at 16 and of MESSAGE OUT at 32.
 */
const struct drv_ucsi_layout drv_ucsi_layout_1 = {
	.name = "1.x",
	.size = 0x30U,
	.version_offset = 0U,
	.cci_offset = 4U,
	.control_offset = 8U,
	.message_in_offset = 16U,
	.message_out_offset = 32U,
	.message_in_size = 16U,
	.message_out_size = 16U,
};

/*
 * The arrangement of UCSI 2.x and later (3.1 Table 4-1, unconfirmed for
 * 2.0): 255 bytes of MESSAGE IN at 16 and of MESSAGE OUT at 272.
 */
const struct drv_ucsi_layout drv_ucsi_layout_2 = {
	.name = "2.x",
	.size = 0x210U,
	.version_offset = 0U,
	.cci_offset = 4U,
	.control_offset = 8U,
	.message_in_offset = 16U,
	.message_out_offset = 272U,
	.message_in_size = 255U,
	.message_out_size = 255U,
};

/*
 * Picks the mailbox arrangement that a region of a size holds: the 2.x one
 * when the region is large enough for it, else the 1.x one.  Returns NULL
 * for a region too small for either.
 */
const struct drv_ucsi_layout *
drv_ucsi_layout_select(
	size_t region_size)
{
	/* A region that holds the large messages. */
	if (region_size >= drv_ucsi_layout_2.size)
		return &drv_ucsi_layout_2;

	/* A region that holds the 1.x mailbox (a PC's is often a little larger). */
	if (region_size >= drv_ucsi_layout_1.size)
		return &drv_ucsi_layout_1;

	/* Neither fits. */
	return NULL;
}

/*
 * Starts a UCSI interface: resets the PPM, enables the completion
 * notification, reads the capability and every connector, then enables
 * the change notifications the PPM supports and reads every connector
 * again, so a change made before the notifications were on is not lost.
 *
 * Returns 0, or an errno value (the interface is not usable).
 */
int
drv_ucsi_start(
	struct drv_ucsi *ucsi,
	const struct drv_ucsi_transport *transport,
	const struct drv_ucsi_layout *layout,
	uint16_t version)
{
	uint16_t notifications;
	unsigned number;
	int error;

	/* Begins with nothing known. */
	kern_memset(ucsi, 0, sizeof(*ucsi));
	ucsi->transport = transport;
	ucsi->layout = layout;
	ucsi->version = version;
	drv_typec_os_log("ucsi: version %x.%x.%x, %s mailbox\n", version >> 8, (version >> 4) & 0xFU, version & 0xFU, layout->name);

	/* Resets the PPM, which leaves every notification off (section 4.5.1). */
	error = ucsi_reset(ucsi);
	if (error != 0)
		return error;

	/* Turns on the completion and error notifications first (section 4.3). */
	error = ucsi_notifications(ucsi, UCSI_NOTIFY_COMMAND_COMPLETED | UCSI_NOTIFY_ERROR);
	if (error != 0)
		return error;

	/* Reads how many connectors there are and what the PPM supports. */
	error = ucsi_capability(ucsi);
	if (error != 0)
		return error;

	/* Reads what each connector can do and its state now. */
	drv_typec_connectors_reset(ucsi->connector_count);
	for (number = 1; number <= ucsi->connector_count; number++) {
		error = ucsi_connector_start(ucsi, number);
		if (error != 0)
			return error;
	}

	/*
	 * Turns on the change notifications: the ones every PPM supports, and
	 * the optional ones of the features it reported.
	 */
	notifications = UCSI_NOTIFY_COMMAND_COMPLETED | UCSI_NOTIFY_ERROR | UCSI_NOTIFY_CONNECT | UCSI_NOTIFY_PARTNER | UCSI_NOTIFY_POWER_DIRECTION | UCSI_NOTIFY_POWER_OPERATION | UCSI_NOTIFY_BATTERY_CHARGING;
	if ((ucsi->optional_features & UCSI_FEATURE_ALT_MODE_DETAILS) != 0)
		notifications |= UCSI_NOTIFY_SUPPORTED_CAM;
	if ((ucsi->optional_features & UCSI_FEATURE_PDO_DETAILS) != 0)
		notifications |= UCSI_NOTIFY_PROVIDER_CAPABILITIES | UCSI_NOTIFY_POWER_LEVEL;
	if ((ucsi->optional_features & UCSI_FEATURE_EXTERNAL_SUPPLY) != 0)
		notifications |= UCSI_NOTIFY_EXTERNAL_SUPPLY;
	if ((ucsi->optional_features & UCSI_FEATURE_PD_RESET) != 0)
		notifications |= UCSI_NOTIFY_PD_RESET;
	error = ucsi_notifications(ucsi, notifications);
	if (error != 0)
		return error;

	/* Reads every connector again, catching what changed meanwhile. */
	for (number = 1; number <= ucsi->connector_count; number++) {
		error = ucsi_connector_update(ucsi, number);
		if (error != 0)
			return error;
	}

	/* Acknowledges the changes the PPM indicated meanwhile. */
	error = ucsi_pending_handle(ucsi);
	if (error != 0)
		return error;

	/* Succeeded: the records are current and changes will be notified. */
	return 0;
}

/*
 * Handles a notification that came while no command was running: reads
 * CCI as the notification left it and each connector whose change it
 * indicates, acknowledging each change.
 *
 * Returns 0, or an errno value.
 */
int
drv_ucsi_service(
	struct drv_ucsi *ucsi)
{
	uint32_t cci;
	int status;
	int error;

	/* Reads CCI as the notification left it, keeping the change it indicates. */
	status = ucsi->transport->read(ucsi->transport->context, false, &cci, ucsi->message_in, ucsi->layout->message_in_size);
	if (status < 0)
		return EIO;
	ucsi_latch(ucsi, cci);

	/* Reads and acknowledges each connector that changed. */
	error = ucsi_pending_handle(ucsi);
	if (error != 0)
		return error;

	/* Succeeded: the records of the changed connectors are current. */
	return 0;
}

/*
 * Runs one command: writes CONTROL, waits for the PPM to complete it (or,
 * for ACK_CC_CI, to acknowledge), and keeps MESSAGE IN.  A completion is
 * not acknowledged here.  Returns 0, ENOTSUP when the PPM does not support
 * the command, EIO when it reports an error or the transport fails, or
 * ETIMEDOUT.
 */
static int
ucsi_execute(
	struct drv_ucsi *ucsi,
	uint64_t control,
	uint8_t *message_in,
	size_t *length)
{
	const struct drv_ucsi_transport *transport;
	uint32_t elapsed;
	uint32_t step;
	uint32_t cci;
	uint32_t done;
	size_t size;
	int notified;
	int status;
	bool refresh;

	/* Tells the PPM the command. */
	transport = ucsi->transport;
	size = ucsi->layout->message_in_size;
	status = transport->write(transport->context, control, NULL, 0);
	if (status < 0)
		return EIO;

	/*
	 * The indicator that ends the wait: the acknowledgement for ACK_CC_CI
	 * (whose completion is not acknowledged), else the completion.
	 */
	done = UCSI_CCI_COMPLETED;
	if ((control & 0xFFU) == UCSI_ACK_CC_CI)
		done = UCSI_CCI_ACKNOWLEDGED;

	/*
	 * Waits for a notification and reads CCI after it; without one, looks
	 * again after a step that grows to the longest, until the timeout.  A
	 * busy PPM completes the command later (section 4).
	 */
	elapsed = 0;
	step = UCSI_STEP_FIRST_MS;
	for (;;) {
		/* The notification, or the step. */
		notified = transport->wait(transport->context, step);
		if (notified < 0)
			return EIO;

		/* CCI and MESSAGE IN, fetched from the PPM when nothing said they changed. */
		refresh = notified == 0;
		status = transport->read(transport->context, refresh, &cci, message_in, size);
		if (status < 0)
			return EIO;
		ucsi_latch(ucsi, cci);

		/* Done unless the PPM is busy or has not answered yet. */
		if ((cci & UCSI_CCI_BUSY) == 0 && (cci & done) != 0)
			break;

		/* Gives up after the timeout. */
		elapsed += step;
		if (elapsed >= UCSI_COMMAND_TIMEOUT_MS) {
			drv_typec_os_log("ucsi: command 0x%02x timed out (CCI 0x%08x)\n", (unsigned)(control & 0xFFU), (unsigned)cci);
			return ETIMEDOUT;
		}

		/* The next step is longer, up to the longest. */
		if (!notified && step < UCSI_STEP_LONGEST_MS)
			step *= 2U;
		if (step > UCSI_STEP_LONGEST_MS)
			step = UCSI_STEP_LONGEST_MS;
	}

	/* The length of MESSAGE IN, which cannot be more than the mailbox holds. */
	*length = (cci >> UCSI_CCI_LENGTH_SHIFT) & UCSI_CCI_LENGTH_MASK;
	if (*length > size)
		*length = size;

	/* A command the PPM does not support. */
	if ((cci & UCSI_CCI_NOT_SUPPORTED) != 0)
		return ENOTSUP;

	/* A command the PPM could not complete. */
	if ((cci & UCSI_CCI_ERROR) != 0)
		return EIO;

	/* Succeeded: MESSAGE IN holds the answer. */
	return 0;
}

/*
 * Runs a command and acknowledges its completion (section 4.5.4), keeping
 * MESSAGE IN in the instance.  Returns 0, or the command's errno value
 * (the completion of a failed command is acknowledged too).
 */
static int
ucsi_command(
	struct drv_ucsi *ucsi,
	uint64_t control)
{
	int command_error;
	int error;

	/* Runs it. */
	ucsi->message_length = 0;
	command_error = ucsi_execute(ucsi, control, ucsi->message_in, &ucsi->message_length);
	if (command_error == ETIMEDOUT || command_error == EIO)
		return command_error;

	/* Acknowledges the completion before the next command (section 4). */
	error = ucsi_acknowledge(ucsi, UCSI_ACK_COMMAND_COMPLETED);
	if (error != 0)
		return error;

	/* Reports a command the PPM did not support or could not complete. */
	if (command_error != 0)
		return command_error;

	/* Succeeded: the answer is in the instance. */
	return 0;
}

/*
 * Sends ACK_CC_CI with the acknowledgements given and waits for the PPM's
 * acknowledgement.  Its MESSAGE IN (empty) goes to a buffer of its own, so
 * the answer of the command acknowledged is kept.
 */
static int
ucsi_acknowledge(
	struct drv_ucsi *ucsi,
	uint64_t which)
{
	uint8_t discarded[DRV_UCSI_MESSAGE_MAX];
	size_t length;
	int error;

	/* Sends it and waits. */
	error = ucsi_execute(ucsi, UCSI_ACK_CC_CI | which, discarded, &length);
	if (error != 0)
		return error;

	/* Succeeded: the PPM may send its next completion or change. */
	return 0;
}

/*
 * Resets the PPM and looks at CCI until it reports the reset completed
 * (section 4.5.1: the OPM polls, as notifications are off).
 */
static int
ucsi_reset(
	struct drv_ucsi *ucsi)
{
	const struct drv_ucsi_transport *transport;
	uint32_t elapsed;
	uint32_t cci;
	int status;

	/* Tells the PPM. */
	transport = ucsi->transport;
	status = transport->write(transport->context, UCSI_PPM_RESET, NULL, 0);
	if (status < 0)
		return EIO;

	/* Looks at CCI every step until the reset completed or the timeout. */
	for (elapsed = 0; elapsed < UCSI_RESET_TIMEOUT_MS; elapsed += UCSI_STEP_FIRST_MS) {
		/* The step. */
		status = transport->wait(transport->context, UCSI_STEP_FIRST_MS);
		if (status < 0)
			return EIO;

		/* CCI fetched from the PPM. */
		status = transport->read(transport->context, true, &cci, ucsi->message_in, ucsi->layout->message_in_size);
		if (status < 0)
			return EIO;

		/* The reset completed; nothing before it is pending any more. */
		if ((cci & UCSI_CCI_RESET_COMPLETED) != 0) {
			kern_memset(ucsi->pending, 0, sizeof(ucsi->pending));
			return 0;
		}
	}

	/* The PPM did not complete the reset. */
	drv_typec_os_log("ucsi: PPM_RESET did not complete\n");
	return ETIMEDOUT;
}

/* Enables the notifications given (SET_NOTIFICATION_ENABLE, section 4.5.5). */
static int
ucsi_notifications(
	struct drv_ucsi *ucsi,
	uint16_t notifications)
{
	uint64_t control;
	int error;

	/* Sends the set. */
	control = UCSI_SET_NOTIFICATION_ENABLE | ((uint64_t)notifications << UCSI_CONTROL_SPECIFIC_SHIFT);
	error = ucsi_command(ucsi, control);
	if (error != 0)
		return error;

	/* Succeeded: these are on. */
	ucsi->notifications = notifications;
	return 0;
}

/*
 * Reads GET_CAPABILITY (section 4.5.6, Table 4-13): the attributes, the
 * number of connectors (bits 32-38), the optional features (bits 40-63)
 * and the number of Alternate Modes (bits 64-71).
 */
static int
ucsi_capability(
	struct drv_ucsi *ucsi)
{
	int error;

	/* Asks. */
	error = ucsi_command(ucsi, UCSI_GET_CAPABILITY);
	if (error != 0)
		return error;

	/* Reads the fields. */
	ucsi->attributes = ucsi_bits(ucsi->message_in, ucsi->message_length, 0, 32);
	ucsi->connector_count = ucsi_bits(ucsi->message_in, ucsi->message_length, 32, 7);
	ucsi->optional_features = ucsi_bits(ucsi->message_in, ucsi->message_length, 40, 24);
	ucsi->alt_mode_count = ucsi_bits(ucsi->message_in, ucsi->message_length, 64, 8);
	drv_typec_os_log("ucsi: %u connectors, %u Alternate Modes, features 0x%06x\n", ucsi->connector_count, ucsi->alt_mode_count, (unsigned)ucsi->optional_features);

	/* Keeps the connectors the Type-C layer has room for. */
	if (ucsi->connector_count > DRV_TYPEC_CONNECTOR_MAX)
		ucsi->connector_count = DRV_TYPEC_CONNECTOR_MAX;

	/* Succeeded: the PPM's capability is known. */
	return 0;
}

/*
 * Reads what a connector can do (GET_CONNECTOR_CAPABILITY, section 4.5.7)
 * and the Alternate Modes it supports, then its state.
 */
static int
ucsi_connector_start(
	struct drv_ucsi *ucsi,
	unsigned number)
{
	struct drv_typec_connector *record;
	int error;

	/*
	 * Asks for the capability: the operation modes (bits 0-7) and the
	 * provider, consumer and swap bits (bits 8-13) are the layer's
	 * capability bits as they stand (Table 4-17).
	 */
	record = &ucsi->record;
	kern_memset(record, 0, sizeof(*record));
	error = ucsi_command(ucsi, ucsi_connector_control(UCSI_GET_CONNECTOR_CAPABILITY, number));
	if (error != 0)
		return error;
	record->capability = ucsi_bits(ucsi->message_in, ucsi->message_length, 0, 14);

	/* The connector's own Alternate Modes, which the current mode indexes. */
	if ((ucsi->optional_features & UCSI_FEATURE_ALT_MODE_DETAILS) != 0 && ucsi->alt_mode_count != 0) {
		error = ucsi_alt_modes(ucsi, number, UCSI_RECIPIENT_CONNECTOR, &record->connector_modes);
		if (error != 0)
			return error;
	}

	/* Publishes what does not change, then reads the state. */
	error = drv_typec_connector_publish(number - 1U, record);
	if (error != 0)
		return error;
	error = ucsi_connector_update(ucsi, number);
	if (error != 0)
		return error;

	/* Succeeded: the connector's record is complete. */
	return 0;
}

/*
 * Reads a connector's state (status, and when something is attached its
 * Alternate Modes and PDOs) into a new record, keeping what the connector
 * can do, and publishes it.
 */
static int
ucsi_connector_update(
	struct drv_ucsi *ucsi,
	unsigned number)
{
	struct drv_typec_connector *record;
	int error;

	/* Starts from the published record, which holds the connector's capability and modes. */
	record = &ucsi->record;
	error = drv_typec_connector_get(number - 1U, record);
	if (error != 0)
		return error;

	/* The status. */
	error = ucsi_status(ucsi, number, record);
	if (error != 0)
		return error;

	/* Nothing attached: no partner, no cable, no mode, no PDO. */
	ucsi_partner_clear(record);
	if (!record->connected)
		return drv_typec_connector_publish(number - 1U, record);

	/*
	 * The partner's and the cable's Alternate Modes and the mode the
	 * connector is in.  A partner or a cable that has none can make the PPM
	 * report an error; the list is then empty.
	 */
	if ((ucsi->optional_features & UCSI_FEATURE_ALT_MODE_DETAILS) != 0) {
		(void)ucsi_alt_modes(ucsi, number, UCSI_RECIPIENT_SOP, &record->partner_modes);
		(void)ucsi_alt_modes(ucsi, number, UCSI_RECIPIENT_SOP_PRIME, &record->cable_modes);
		error = ucsi_current_modes(ucsi, number, record);
		if (error != 0)
			return error;
	}

	/* The partner's PDOs, when the PPM reports PDOs and the contract is USB PD. */
	if ((ucsi->optional_features & UCSI_FEATURE_PDO_DETAILS) != 0 && record->power_operation == DRV_TYPEC_POWER_PD)
		(void)ucsi_partner_pdos(ucsi, number, record);

	/* Publishes the new state. */
	error = drv_typec_connector_publish(number - 1U, record);
	if (error != 0)
		return error;

	/* Succeeded: the connector's record is current. */
	return 0;
}

/*
 * Reads GET_CONNECTOR_STATUS (section 4.5.17, Table 4-42; 3.1 Table 6-43
 * for the later fields) into a record.
 */
static int
ucsi_status(
	struct drv_ucsi *ucsi,
	unsigned number,
	struct drv_typec_connector *record)
{
	const uint8_t *data;
	uint32_t operation;
	uint32_t flags;
	size_t length;
	int error;

	/* Asks. */
	error = ucsi_command(ucsi, ucsi_connector_control(UCSI_GET_CONNECTOR_STATUS, number));
	if (error != 0)
		return error;
	data = ucsi->message_in;
	length = ucsi->message_length;

	/* Whether something is attached (bit 19). */
	record->connected = false;
	if (ucsi_bits(data, length, 19, 1) != 0)
		record->connected = true;

	/* How power is delivered (bits 16-18; 6, 5 A, is from 3.1), unknown when the value is reserved. */
	operation = ucsi_bits(data, length, 16, 3);
	record->power_operation = DRV_TYPEC_POWER_UNKNOWN;
	if (operation >= DRV_TYPEC_POWER_USB_DEFAULT && operation <= DRV_TYPEC_POWER_TYPEC_5A)
		record->power_operation = (enum drv_typec_power_operation)operation;

	/* The role in the power (bit 20: 1 is the provider). */
	record->power_role = DRV_TYPEC_ROLE_SINK;
	if (ucsi_bits(data, length, 20, 1) != 0)
		record->power_role = DRV_TYPEC_ROLE_SOURCE;

	/* What is carried to the partner (bits 21-28: USB, Alternate Mode; USB4 from 3.1 only). */
	flags = ucsi_bits(data, length, 21, 8);
	record->partner_flags = flags & (DRV_TYPEC_PARTNER_USB | DRV_TYPEC_PARTNER_ALT_MODE);
	if (ucsi->version >= UCSI_VERSION_2)
		record->partner_flags = flags & (DRV_TYPEC_PARTNER_USB | DRV_TYPEC_PARTNER_ALT_MODE | DRV_TYPEC_PARTNER_USB4);

	/* The partner's kind (bits 29-31; 7 is reserved). */
	record->partner_type = (enum drv_typec_partner_type)ucsi_bits(data, length, 29, 3);
	if (record->partner_type > DRV_TYPEC_PARTNER_AUDIO_ACCESSORY)
		record->partner_type = DRV_TYPEC_PARTNER_NONE;

	/* The contract's Request Data Object (bits 32-63). */
	record->request_data_object = ucsi_bits(data, length, 32, 32);

	/* The orientation (bit 86), which a 1.x status does not hold. */
	record->orientation = DRV_TYPEC_ORIENTATION_UNKNOWN;
	if (ucsi->version >= UCSI_VERSION_2 && length >= UCSI_STATUS_ORIENTATION_BYTES) {
		record->orientation = DRV_TYPEC_ORIENTATION_NORMAL;
		if (ucsi_bits(data, length, UCSI_STATUS_ORIENTATION_BIT, 1) != 0)
			record->orientation = DRV_TYPEC_ORIENTATION_FLIPPED;
	}

	/* Nothing attached: none of the attached fields mean anything. */
	if (!record->connected) {
		record->power_operation = DRV_TYPEC_POWER_UNKNOWN;
		record->power_role = DRV_TYPEC_ROLE_SINK;
		record->partner_flags = 0;
		record->partner_type = DRV_TYPEC_PARTNER_NONE;
		record->request_data_object = 0;
		record->orientation = DRV_TYPEC_ORIENTATION_UNKNOWN;
	}

	/* Succeeded: the record holds the status. */
	return 0;
}

/* Empties what a record knows of the partner and the cable (read again when something is attached). */
static void
ucsi_partner_clear(
	struct drv_typec_connector *record)
{
	/* The partner's and the cable's modes, the current modes and the PDOs. */
	kern_memset(&record->partner_modes, 0, sizeof(record->partner_modes));
	kern_memset(&record->cable_modes, 0, sizeof(record->cable_modes));
	kern_memset(record->supported_modes, 0, sizeof(record->supported_modes));
	kern_memset(record->current_modes, 0, sizeof(record->current_modes));
	record->current_mode_count = 0;
	kern_memset(record->partner_pdos, 0, sizeof(record->partner_pdos));
	record->partner_pdo_count = 0;
}

/*
 * Reads a list of Alternate Modes with GET_ALTERNATE_MODES (section
 * 4.5.11), two at a time with the offset moved on, until the PPM returns
 * fewer than asked or a blank SVID, or the list is full.
 */
static int
ucsi_alt_modes(
	struct drv_ucsi *ucsi,
	unsigned number,
	unsigned recipient,
	struct drv_typec_alt_mode_list *list)
{
	const uint8_t *mode;
	uint64_t control;
	unsigned offset;
	unsigned count;
	unsigned index;
	uint16_t svid;
	int error;

	/*
	 * Asks for two modes from the offset each time: the recipient (bits
	 * 16-18), the connector (bits 24-30), the offset (bits 32-39) and the
	 * count less one (bits 40-41) (Table 4-24).
	 */
	list->count = 0;
	for (offset = 0; offset < DRV_TYPEC_ALT_MODE_MAX; offset += UCSI_ALT_MODES_PER_COMMAND) {
		control = UCSI_GET_ALTERNATE_MODES;
		control |= (uint64_t)recipient << 16;
		control |= (uint64_t)number << 24;
		control |= (uint64_t)offset << 32;
		control |= (uint64_t)(UCSI_ALT_MODES_PER_COMMAND - 1U) << 40;
		error = ucsi_command(ucsi, control);
		if (error != 0)
			return error;

		/* Each mode returned: its SVID and its MID (Table 4-26); a blank one ends the list. */
		count = (unsigned)(ucsi->message_length / UCSI_ALT_MODE_BYTES);
		if (count > UCSI_ALT_MODES_PER_COMMAND)
			count = UCSI_ALT_MODES_PER_COMMAND;
		for (index = 0; index < count && list->count < DRV_TYPEC_ALT_MODE_MAX; index++) {
			mode = &ucsi->message_in[index * UCSI_ALT_MODE_BYTES];
			svid = (uint16_t)ucsi_get16(mode);
			if (svid == 0)
				return 0;
			list->modes[list->count].svid = svid;
			list->modes[list->count].vdo = ucsi_get32(&mode[2]);
			list->count++;
		}

		/* Fewer than asked: the end of the list. */
		if (count < UCSI_ALT_MODES_PER_COMMAND)
			break;
	}

	/* Succeeded: the list holds every mode returned. */
	return 0;
}

/*
 * Reads which of the connector's modes can be entered now
 * (GET_CAM_SUPPORTED, section 4.5.12: one bit per index) and which it is
 * in (GET_CURRENT_CAM, section 4.5.13: one index per byte, 0xFF for none).
 */
static int
ucsi_current_modes(
	struct drv_ucsi *ucsi,
	unsigned number,
	struct drv_typec_connector *record)
{
	size_t length;
	size_t index;
	uint8_t mode;
	int error;

	/* The modes that can be entered. */
	error = ucsi_command(ucsi, ucsi_connector_control(UCSI_GET_CAM_SUPPORTED, number));
	if (error != 0)
		return error;
	length = ucsi->message_length;
	if (length > sizeof(record->supported_modes))
		length = sizeof(record->supported_modes);
	kern_memcpy(record->supported_modes, ucsi->message_in, length);

	/* The modes it is in, each an index into the connector's list. */
	error = ucsi_command(ucsi, ucsi_connector_control(UCSI_GET_CURRENT_CAM, number));
	if (error != 0)
		return error;
	record->current_mode_count = 0;
	for (index = 0; index < ucsi->message_length; index++) {
		mode = ucsi->message_in[index];
		if (mode == UCSI_NO_CURRENT_MODE || mode >= record->connector_modes.count)
			continue;
		if (record->current_mode_count < DRV_TYPEC_CURRENT_MODE_MAX)
			record->current_modes[record->current_mode_count++] = mode;
	}

	/* Succeeded: the record holds the connector's modes. */
	return 0;
}

/*
 * Reads the partner's PDOs with GET_PDOS (section 4.5.15): its source ones
 * when this side sinks, else its sink ones, four at a time.
 */
static int
ucsi_partner_pdos(
	struct drv_ucsi *ucsi,
	unsigned number,
	struct drv_typec_connector *record)
{
	uint64_t control;
	unsigned offset;
	unsigned wanted;
	unsigned count;
	unsigned index;
	int error;

	/*
	 * Asks for up to four from the offset, never past the seventh: the
	 * connector (bits 16-22), the partner bit (23), the offset (bits
	 * 24-31), the count less one (bits 32-33), source or sink (bit 34),
	 * and the current capabilities (bits 35-36 zero) (Table 4-34).
	 */
	record->partner_pdo_count = 0;
	for (offset = 0; offset < DRV_TYPEC_PDO_MAX; offset += count) {
		wanted = DRV_TYPEC_PDO_MAX - offset;
		if (wanted > UCSI_PDOS_PER_COMMAND)
			wanted = UCSI_PDOS_PER_COMMAND;
		control = UCSI_GET_PDOS;
		control |= (uint64_t)number << 16;
		control |= 1ULL << 23;
		control |= (uint64_t)offset << 24;
		control |= (uint64_t)(wanted - 1U) << 32;
		if (record->power_role == DRV_TYPEC_ROLE_SINK)
			control |= 1ULL << 34;
		error = ucsi_command(ucsi, control);
		if (error != 0)
			return error;

		/* Each PDO returned (Table 4-36). */
		count = (unsigned)(ucsi->message_length / 4U);
		if (count > wanted)
			count = wanted;
		for (index = 0; index < count; index++)
			record->partner_pdos[record->partner_pdo_count++] = ucsi_get32(&ucsi->message_in[index * 4U]);

		/* Fewer than asked: the end. */
		if (count < wanted || count == 0)
			break;
	}

	/* Succeeded: the record holds the partner's PDOs. */
	return 0;
}

/*
 * Reads and acknowledges each connector whose change the PPM indicated,
 * lowest number first.  A connector stays pending until its change is
 * acknowledged: the PPM repeats the indication in every CCI until then.
 */
static int
ucsi_pending_handle(
	struct drv_ucsi *ucsi)
{
	unsigned number;
	unsigned byte;
	uint8_t bit;
	int error;

	/* Each pending connector, looking again after each (another may have come). */
	number = 1;
	while (number < 128U) {
		/* Skips a connector that did not change. */
		byte = number / 8U;
		bit = (uint8_t)(1U << (number % 8U));
		if ((ucsi->pending[byte] & bit) == 0) {
			number++;
			continue;
		}

		/* Reads its state, unless it is not one the layer keeps. */
		if (number <= ucsi->connector_count) {
			error = ucsi_connector_update(ucsi, number);
			if (error != 0)
				return error;
		}

		/* Acknowledges the change; the PPM may then indicate the next. */
		error = ucsi_acknowledge(ucsi, UCSI_ACK_CONNECTOR_CHANGE);
		if (error != 0)
			return error;
		ucsi->pending[byte] &= (uint8_t)~bit;
		number = 1;
	}

	/* Succeeded: no change is pending. */
	return 0;
}

/* Keeps the connector a CCI indicates a change on as pending. */
static void
ucsi_latch(
	struct drv_ucsi *ucsi,
	uint32_t cci)
{
	unsigned number;

	/* The connector number (bits 1-7), 0 for none. */
	number = (cci >> UCSI_CCI_CONNECTOR_SHIFT) & UCSI_CCI_CONNECTOR_MASK;
	if (number != 0)
		ucsi->pending[number / 8U] |= (uint8_t)(1U << (number % 8U));
}

/* Makes the CONTROL of a command that names a connector at bit 16. */
static uint64_t
ucsi_connector_control(
	unsigned command,
	unsigned number)
{
	/* The command and the connector. */
	return (uint64_t)command | ((uint64_t)number << UCSI_CONTROL_SPECIFIC_SHIFT);
}

/* Reads a little-endian 16-bit value. */
static uint32_t
ucsi_get16(
	const uint8_t *data)
{
	/* Low byte first. */
	return (uint32_t)data[0] | ((uint32_t)data[1] << 8);
}

/* Reads a little-endian 32-bit value. */
static uint32_t
ucsi_get32(
	const uint8_t *data)
{
	/* Low byte first. */
	return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/*
 * Reads a field of up to 32 bits at a bit offset of a little-endian
 * message (the specification numbers bits from bit 0 of byte 0); bits past
 * the message's length read as zero.
 */
static uint32_t
ucsi_bits(
	const uint8_t *data,
	size_t length,
	unsigned offset,
	unsigned width)
{
	uint32_t value;
	unsigned bit;
	unsigned at;

	/* Each bit, lowest first. */
	value = 0;
	for (bit = 0; bit < width; bit++) {
		at = offset + bit;
		if (at / 8U >= length)
			break;
		if ((data[at / 8U] & (1U << (at % 8U))) != 0)
			value |= 1UL << bit;
	}

	/* The field. */
	return value;
}
