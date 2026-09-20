/*
 * WS031 Linux-parity -- zedBSD glue at the end of intel_opregion_port.c (E-122: the OpRegion service on a SHADOW
 * backend).  The reference functions above run unchanged; this glue provides the instance, its mailbox mapping table
 * (memremap), its PCI view (ASLS), its connectors, its worker queue, and the recorded boundaries.
 *   SHADOW:   driver-owned RAM in the OpRegion format (tests).  The firmware's region is never mapped writable here.
 *   FIRMWARE: the real shared region -- NOT enabled (production runs VBT_ONLY; see native_precheck.c).
 */
#include <kern/lock.h>
#include <kern/sched.h>
#include "parity_opregion.h"
#include <hal/hal.h>

static struct drm_i915_private parity_opregion_dev;
static const char *parity_opregion_backend = "NONE";
static uint32_t parity_opregion_asls;                   /* what pci_read_config_dword(ASLS) returns */
static unsigned parity_opregion_epoch;
static struct { resource_size_t phys; void *ptr; size_t size; } parity_opregion_maps[4];
static unsigned parity_opregion_nmaps, parity_opregion_unported, parity_opregion_boundaries, parity_opregion_unmaps;
static int parity_opregion_accepting;            /* the producer gate: GSE requests are taken only while 1 */
static unsigned parity_opregion_dropped, parity_opregion_cleanup_refused;
static struct { void *va; size_t size; } parity_opregion_hal[2];   /* FIRMWARE: the real HAL mappings */
static unsigned parity_opregion_nhal;

/* ---------------- instance surroundings used by the reference text ---------------- */
int parity_opregion_warn_on(int cond, const char *what)
{
	if (cond)
		kern_logf("i915: parity opregion WARN_ON(%s)\n", what);
	return cond;
}

int parity_opregion_pci_read32(struct pci_dev *pdev, int where, u32 *val)
{
	(void)pdev;
	*val = where == ASLS ? parity_opregion_asls : 0u;
	if (where != ASLS)
		(void)parity_opregion_pci_access_unported("pci_read_config_dword (not ASLS)");
	return 0;
}

int parity_opregion_pci_access_unported(const char *what)
{
	parity_opregion_unported++;
	kern_logf("i915: parity opregion ERROR %s reached: SWSCI config access is not provided (no SWSCI mailbox on the target)\n",
		what);
	return -ENODEV;
}

void *parity_opregion_memremap(resource_size_t phys, size_t size, unsigned long flags)
{
	unsigned i;

	(void)flags;
	for (i = 0u; i < parity_opregion_nmaps; i++)
		if (phys >= parity_opregion_maps[i].phys && phys + size <= parity_opregion_maps[i].phys + parity_opregion_maps[i].size)
			return (char *)parity_opregion_maps[i].ptr + (phys - parity_opregion_maps[i].phys);
	kern_logf("i915: parity opregion memremap(0x%llx, %u): not in the %s mapping table\n", (unsigned long long)phys,
		(unsigned)size, parity_opregion_backend);
	return NULL;
}

void parity_opregion_memunmap(void *p)
{
	(void)p;
	parity_opregion_unmaps++;               /* SHADOW memory stays owned by the test */
}

int parity_opregion_dmi_check_system(const struct dmi_system_id *list)
{
	(void)list;
	return 0;                               /* no DMI data source: nothing matches (recorded adaptation) */
}

void parity_opregion_boundary(const char *what)
{
	parity_opregion_boundaries++;
	kern_logf("i915: parity opregion boundary: %s -- not evaluated\n", what);
}

int parity_opregion_cancel_work_sync(struct work_struct *w)
{
	if (parity_opregion_dev.unordered_wq == 0)
		return 0;                       /* no queue: nothing can be pending or running */
	return parity_kcancel_work_sync(parity_opregion_dev.unordered_wq, &w->kwork, sched_ticks() + 500u);
}

/* ---------------- the instance ---------------- */
const char *parity_opregion_mailbox_backend(void) { return parity_opregion_backend; }
unsigned parity_opregion_service_epoch(void) { return parity_opregion_epoch; }

int parity_opregion_shadow_map(uint64_t phys, void *ptr, uint32_t size)
{
	if (parity_opregion_nmaps >= 4u)
		return -ENOSPC;
	parity_opregion_maps[parity_opregion_nmaps].phys = phys;
	parity_opregion_maps[parity_opregion_nmaps].ptr = ptr;
	parity_opregion_maps[parity_opregion_nmaps].size = size;
	parity_opregion_nmaps++;
	return 0;
}

/* intel_opregion_setup() on the SHADOW backend: ASLS = the token the shadow is mapped at */
int parity_opregion_shadow_setup(uint32_t asls_token)
{
	int rc;

	if (parity_opregion_dev.display.opregion.acpi_notifier.notifier_call != 0)
		return -EBUSY;                  /* still registered */
	memset(&parity_opregion_dev.display.opregion, 0, sizeof(parity_opregion_dev.display.opregion));
	parity_opregion_dev.display.params.vbt_firmware = NULL;
	parity_opregion_asls = asls_token;
	parity_opregion_backend = "SHADOW";
	parity_opregion_epoch++;
	rc = intel_opregion_setup(&parity_opregion_dev);
	if (rc != 0)
		parity_opregion_backend = "NONE";
	return rc;
}

/* the lifecycle: the reference functions */
void parity_opregion_register(void)
{
	intel_opregion_register(&parity_opregion_dev);
	/* requests are accepted once the reference published ARDY READY (there is an ASLE mailbox) */
	parity_opregion_accepting = parity_opregion_dev.display.opregion.asle != 0;
}

/* the termination contract: stop the producer first, then the reference unregister (which syncs the worker) */
void parity_opregion_unregister(void)
{
	parity_opregion_accepting = 0;
	intel_opregion_unregister(&parity_opregion_dev);
}

/* cleanup only once the ASLE work is shown idle; otherwise the mapping and the instance stay (recorded) */
int parity_opregion_cleanup(void)
{
	struct work_struct *w = &parity_opregion_dev.display.opregion.asle_work;

	if (parity_opregion_accepting)
		return -EBUSY;                  /* still registered: unregister first */
	if (parity_opregion_dev.unordered_wq != 0 && parity_opregion_dev.display.opregion.header != 0 &&
	    (parity_kwork_is_pending(parity_opregion_dev.unordered_wq, &w->kwork) || w->kwork.state == PARITY_KWORK_RUNNING)) {
		parity_opregion_cleanup_refused++;
		kern_logf("i915: parity opregion cleanup REFUSED: the ASLE work is not shown idle -- mapping kept\n");
		return -EBUSY;
	}
	intel_opregion_cleanup(&parity_opregion_dev);
	while (parity_opregion_nhal > 0u) {
		parity_opregion_nhal--;
		(void)hal_space_unmap_device(parity_opregion_hal[parity_opregion_nhal].va, parity_opregion_hal[parity_opregion_nhal].size);
	}
	parity_opregion_backend = "NONE";
	parity_opregion_nmaps = 0u;
	return 0;
}

/*
 * The FIRMWARE backend: the real OpRegion at ASLS, mapped READ / WRITE (uncached device view), and the RVDA VBT it
 * names (read).  Then the reference intel_opregion_setup() on it -- from here the driver writes the shared mailboxes.
 */
int parity_opregion_firmware_setup(uint32_t asls)
{
	void *op = 0, *vb = 0;
	uint64_t rvda = 0u;
	uint32_t rvds = 0u, major, minor;
	int rc;

	if (asls == 0u)
		return -ENODEV;
	if (parity_opregion_dev.display.opregion.acpi_notifier.notifier_call != 0 || parity_opregion_nhal != 0u)
		return -EBUSY;
	parity_opregion_nmaps = 0u;
	if (hal_space_map_device((hal_physaddr_t)asls, OPREGION_SIZE, HAL_SPACE_READ | HAL_SPACE_WRITE, &op) != HAL_OK || op == 0)
		return -ENOMEM;
	parity_opregion_hal[parity_opregion_nhal].va = op;
	parity_opregion_hal[parity_opregion_nhal].size = OPREGION_SIZE;
	parity_opregion_nhal++;
	(void)parity_opregion_shadow_map(asls, op, OPREGION_SIZE);
	/* the RVDA the reference will memremap: mapped here (read) at the address it will compute */
	major = ((const volatile u8 *)op)[0x17];
	minor = ((const volatile u8 *)op)[0x16];
	memcpy(&rvda, (const u8 *)op + OPREGION_ASLE_OFFSET + 186, 8);
	memcpy(&rvds, (const u8 *)op + OPREGION_ASLE_OFFSET + 194, 4);
	if (major >= 2u && rvda != 0u && rvds != 0u) {
		uint64_t phys = (major > 2u || minor >= 1u) ? (uint64_t)asls + rvda : rvda;

		if (hal_space_map_device((hal_physaddr_t)phys, rvds, HAL_SPACE_READ, &vb) == HAL_OK && vb != 0) {
			parity_opregion_hal[parity_opregion_nhal].va = vb;
			parity_opregion_hal[parity_opregion_nhal].size = rvds;
			parity_opregion_nhal++;
			(void)parity_opregion_shadow_map(phys, vb, rvds);
		}
	}
	memset(&parity_opregion_dev.display.opregion, 0, sizeof(parity_opregion_dev.display.opregion));
	parity_opregion_dev.display.params.vbt_firmware = NULL;
	parity_opregion_asls = asls;
	parity_opregion_backend = "FIRMWARE";
	parity_opregion_epoch++;
	rc = intel_opregion_setup(&parity_opregion_dev);
	if (rc != 0) {
		parity_opregion_backend = "NONE";
		while (parity_opregion_nhal > 0u) {
			parity_opregion_nhal--;
			(void)hal_space_unmap_device(parity_opregion_hal[parity_opregion_nhal].va, parity_opregion_hal[parity_opregion_nhal].size);
		}
	}
	return rc;
}

/* a read of the bound instance's mailbox (either backend), for the records */
uint32_t parity_opregion_mbox_read(unsigned off)
{
	u32 v = 0u;

	if (parity_opregion_dev.display.opregion.header != 0 && off + 4u <= OPREGION_SIZE)
		memcpy(&v, (const u8 *)parity_opregion_dev.display.opregion.header + off, 4);
	return v;
}

void parity_opregion_mbox_write(unsigned off, uint32_t v)
{
	if (parity_opregion_dev.display.opregion.header != 0 && off + 4u <= OPREGION_SIZE)
		memcpy((u8 *)parity_opregion_dev.display.opregion.header + off, &v, 4);
}
int parity_opregion_notify_adapter(int state) { return intel_opregion_notify_adapter(&parity_opregion_dev, state); }
const void *parity_opregion_vbt(uint32_t *size)
{
	*size = parity_opregion_dev.display.opregion.vbt_size;
	return parity_opregion_dev.display.opregion.vbt;
}
int parity_opregion_notifier_registered(void) { return parity_opregion_dev.display.opregion.acpi_notifier.notifier_call != 0; }
void parity_opregion_counters(unsigned *unported, unsigned *boundaries, unsigned *unmaps)
{
	*unported = parity_opregion_unported;
	*boundaries = parity_opregion_boundaries;
	*unmaps = parity_opregion_unmaps;
}

/* ---------------- OP-ASLE: worker, policy, connectors ---------------- */
struct parity_opregion_worker_stats parity_opregion_wstats;
static int parity_opregion_policy = acpi_backlight_vendor;
static struct mutex parity_opregion_conn_lock;
static int parity_opregion_conn_lock_live;
static struct {
	void (*set_acpi)(void *ctx, uint32_t level, uint32_t max);
	void *ctx;
	struct intel_connector conn;
	struct drm_connector_state state;
} parity_opregion_bl[4];
static unsigned parity_opregion_nbl;

int parity_opregion_backlight_policy(void) { return parity_opregion_policy; }
void parity_opregion_connection_lock(void) { mutex_lock(&parity_opregion_conn_lock); }
void parity_opregion_connection_unlock(void) { mutex_unlock(&parity_opregion_conn_lock); }

struct intel_connector *parity_opregion_connector_next(struct drm_connector_list_iter *it)
{
	if (it->idx >= parity_opregion_nbl)
		return NULL;
	return &parity_opregion_bl[it->idx++].conn;
}

void parity_opregion_backlight_set_acpi(const struct drm_connector_state *st, u32 level, u32 max)
{
	/* intel_backlight_set_acpi() returns at once for a connector without a backlight: a target without a hook */
	if (st != 0 && st->target < parity_opregion_nbl && parity_opregion_bl[st->target].set_acpi != 0)
		parity_opregion_bl[st->target].set_acpi(parity_opregion_bl[st->target].ctx, level, max);
}

int parity_opregion_service_start(struct parity_kworkqueue *wq, int policy)
{
	if (!parity_opregion_conn_lock_live) {
		(void)mutex_init(&parity_opregion_conn_lock, LOCK_RANK_DEVICE, "parity-opregion-connection");
		parity_opregion_conn_lock_live = 1;
	}
	parity_opregion_dev.unordered_wq = wq;
	parity_opregion_policy = policy;
	parity_opregion_nbl = 0u;
	memset(&parity_opregion_wstats, 0, sizeof(parity_opregion_wstats));
	return 0;
}

void parity_opregion_set_policy(int policy) { parity_opregion_policy = policy; }

int parity_opregion_add_connector(int drm_connector_type, void (*set_acpi)(void *ctx, uint32_t level, uint32_t max), void *ctx)
{
	unsigned n = parity_opregion_nbl;

	if (n >= 4u)
		return -ENOSPC;
	memset(&parity_opregion_bl[n], 0, sizeof(parity_opregion_bl[n]));
	parity_opregion_bl[n].set_acpi = set_acpi;
	parity_opregion_bl[n].ctx = ctx;
	parity_opregion_bl[n].state.target = n;
	parity_opregion_bl[n].conn.base.state = &parity_opregion_bl[n].state;
	parity_opregion_bl[n].conn.base.connector_type = drm_connector_type;
	parity_opregion_nbl = n + 1u;
	return 0;
}

int parity_opregion_add_backlight(void (*set_acpi)(void *ctx, uint32_t level, uint32_t max), void *ctx)
{
	return parity_opregion_add_connector(DRM_MODE_CONNECTOR_eDP, set_acpi, ctx);
}

/* the GSE receive entry: what the GU_MISC GSE decode calls (gen11_gu_misc_irq_handler -> intel_opregion_asle_intr) */
void parity_opregion_gse_entry(void)
{
	if (!parity_opregion_accepting) {
		parity_opregion_dropped++;      /* stopped / not started: a late request queues nothing */
		return;
	}
	intel_opregion_asle_intr(&parity_opregion_dev);
}

void parity_opregion_gate_counters(unsigned *dropped, unsigned *cleanup_refused)
{
	*dropped = parity_opregion_dropped;
	*cleanup_refused = parity_opregion_cleanup_refused;
}

int parity_opregion_asle_flush(uint64_t deadline)
{
	if (parity_opregion_dev.unordered_wq == 0)
		return 0;
	return parity_kflush_work(parity_opregion_dev.unordered_wq, &parity_opregion_dev.display.opregion.asle_work.kwork, deadline);
}

void parity_opregion_worker_stats_get(unsigned *started, unsigned *finished, unsigned *queued_new, unsigned *queued_pending)
{
	*started = parity_opregion_wstats.started;
	*finished = parity_opregion_wstats.finished;
	*queued_new = parity_opregion_wstats.queued_new;
	*queued_pending = parity_opregion_wstats.queued_pending;
}

/*
 * The firmware notification of a sanitized encoder -- intel_opregion_notify_encoder() of the reference,
 * as a boundary call of this unit (like parity_opregion_notify_adapter).  The caller hands the port and the
 * output type because the encoder objects belong to the LCD path; the port mapping, the parm layout and the
 * SWSCI request are the reference's.
 *   port E is reported as port 0; every other port as (port + 1) with BIT(port) set in parm;
 *   a disable adds 4 << 8; the display type goes to bits (16 + port * 3).
 */
#define PARITY_DISPLAY_TYPE_CRT		0
#define PARITY_DISPLAY_TYPE_TV			1
#define PARITY_DISPLAY_TYPE_EXTERNAL_FLAT_PANEL	2
#define PARITY_DISPLAY_TYPE_INTERNAL_FLAT_PANEL	3
int parity_opregion_notify_encoder(int port_in, int output_type, int enable)
{
	u32 parm = 0u, type, port;
	int ret;

	/* HAS_DDI() holds on this platform; the reference returns 0 on the older ones */
	ret = check_swsci_function(&parity_opregion_dev, SWSCI_SBCB_DISPLAY_POWER_STATE);
	if (ret)
		return ret;

	port = (u32)port_in;
	if (port == 4u) {	/* PORT_E */
		port = 0u;
	} else {
		parm |= 1u << port;
		port++;
	}
	if (port > 4u) {
		kern_logf("i915: parity opregion notify_encoder: port index %u out of bounds\n", (unsigned)port);
		return -EINVAL;
	}

	if (!enable)
		parm |= 4u << 8;

	switch (output_type) {
	case PARITY_OUTPUT_ANALOG:
		type = PARITY_DISPLAY_TYPE_CRT;
		break;
	case PARITY_OUTPUT_DDI:
	case PARITY_OUTPUT_DP:
	case PARITY_OUTPUT_HDMI:
	case PARITY_OUTPUT_DP_MST:
		type = PARITY_DISPLAY_TYPE_EXTERNAL_FLAT_PANEL;
		break;
	case PARITY_OUTPUT_EDP:
	case PARITY_OUTPUT_DSI:
		type = PARITY_DISPLAY_TYPE_INTERNAL_FLAT_PANEL;
		break;
	default:
		kern_logf("i915: parity opregion notify_encoder: unsupported encoder type %d\n", output_type);
		return -EINVAL;
	}

	parm |= type << (16u + port * 3u);

	return swsci(&parity_opregion_dev, SWSCI_SBCB_DISPLAY_POWER_STATE, parm, NULL);
}
