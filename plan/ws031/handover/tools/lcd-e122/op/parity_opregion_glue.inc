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

static struct drm_i915_private parity_opregion_dev;
static const char *parity_opregion_backend = "NONE";
static uint32_t parity_opregion_asls;                   /* what pci_read_config_dword(ASLS) returns */
static unsigned parity_opregion_epoch;
static struct { resource_size_t phys; void *ptr; size_t size; } parity_opregion_maps[4];
static unsigned parity_opregion_nmaps, parity_opregion_unported, parity_opregion_boundaries, parity_opregion_unmaps;

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
void parity_opregion_register(void) { intel_opregion_register(&parity_opregion_dev); }
void parity_opregion_unregister(void) { intel_opregion_unregister(&parity_opregion_dev); }
void parity_opregion_cleanup(void)
{
	intel_opregion_cleanup(&parity_opregion_dev);
	parity_opregion_backend = "NONE";
	parity_opregion_nmaps = 0u;
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
	intel_opregion_asle_intr(&parity_opregion_dev);
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
