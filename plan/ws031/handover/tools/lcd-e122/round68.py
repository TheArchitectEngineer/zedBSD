#!/usr/bin/env python3
"""WS031 E-122 round 68: OP-ASLE (unit 2a) -- the reference ASLE request handlers, asle_work and
intel_opregion_asle_intr on the SHADOW mailbox; a real worker on the shared kworkqueue; backlight through registered
targets (a fake in the GPU-free test; the real intel_backlight_set_acpi later).  usage: round68.py <repo root>"""
import sys, json
root = sys.argv[1].rstrip("/") + "/"
P = root + "src/drivers/gpu/i915/parity/"
L = P + "lcd/"
J = root + "plan/ws031/handover/tools/port_lcd_modeset.json"
NL = chr(10)
BS = chr(92)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

j = json.load(open(J))
for f in j["new_files"]:
    if f["out"] == "intel_opregion_port.c":
        for n in ("asle_set_als_illum", "asle_set_backlight", "asle_set_pwm_freq", "asle_set_pfit",
                  "asle_set_supported_rotation_angles", "asle_set_button_array", "asle_set_convertible", "asle_set_docking",
                  "asle_isct_state", "asle_work", "intel_opregion_asle_intr"):
            if n not in f["functions"]:
                f["functions"].insert(len(f["functions"]) - 1, n)   # before intel_opregion_video_event
json.dump(j, open(J, "w"), indent=1, sort_keys=True)

c = open(L + "opregion_compat.h").read()
c = rep(c, "/* the workqueue item of the shared backend (backend_sync.h) */" + NL + "struct work_struct { struct parity_kwork kwork; };",
"""/* the workqueue item of the shared backend (backend_sync.h): INIT_WORK / queue_work map onto parity_kwork */
struct work_struct { struct parity_kwork kwork; void (*func)(struct work_struct *work); };
struct parity_opregion_worker_stats { unsigned started, finished, queued_new, queued_pending; };
extern struct parity_opregion_worker_stats parity_opregion_wstats;
static inline void parity_opregion_work_trampoline(void *ctx)
{
	struct work_struct *w = ctx;

	parity_opregion_wstats.started++;
	w->func(w);
	parity_opregion_wstats.finished++;
}
#define INIT_WORK(w, f) do { (w)->func = (f); parity_kwork_init(&(w)->kwork, parity_opregion_work_trampoline, (w)); } while (0)
static inline int parity_opregion_queue_work(struct parity_kworkqueue *wq, struct work_struct *w)
{
	int r = wq != 0 ? parity_kqueue_work(wq, &w->kwork) : 0;

	if (r == 1)
		parity_opregion_wstats.queued_new++;
	else
		parity_opregion_wstats.queued_pending++;   /* already pending: not a failure */
	return r;
}
#define queue_work(wq, w) parity_opregion_queue_work((wq), (w))

/* diagnostics of the extracted text: kept silent (the service logs its own records) */
#define drm_dbg(dev, ...) ((void)(dev))
#define drm_dbg_kms(dev, ...) ((void)(dev))
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))

/* acpi_video_get_backlight_type(): the backlight policy -- an explicit input of the service configuration */
enum acpi_backlight_type { acpi_backlight_undef = -1, acpi_backlight_none = 0, acpi_backlight_video, acpi_backlight_vendor,
	acpi_backlight_native };
int parity_opregion_backlight_policy(void);
#define acpi_video_get_backlight_type() ((enum acpi_backlight_type)parity_opregion_backlight_policy())

/* the connection mutex and the connector walk of asle_set_backlight(): the service's registered backlight targets */
struct drm_modeset_lock { int unused; };
struct drm_device { struct { struct drm_modeset_lock connection_mutex; } mode_config; };
void parity_opregion_connection_lock(void);
void parity_opregion_connection_unlock(void);
#define drm_modeset_lock(l, ctx) (parity_opregion_connection_lock(), 0)
#define drm_modeset_unlock(l) parity_opregion_connection_unlock()
struct drm_connector_state { unsigned target; };
struct drm_connector { const struct drm_connector_state *state; };
struct intel_connector { struct drm_connector base; };
struct drm_connector_list_iter { unsigned idx; };
struct intel_connector *parity_opregion_connector_next(struct drm_connector_list_iter *it);
#define drm_connector_list_iter_begin(dev, it) ((it)->idx = 0u)
#define drm_connector_list_iter_end(it) ((void)(it))
#define for_each_intel_connector_iter(c, it) while (((c) = parity_opregion_connector_next(it)) != NULL)
void parity_opregion_backlight_set_acpi(const struct drm_connector_state *st, u32 level, u32 max);
#define intel_backlight_set_acpi(st, level, max) parity_opregion_backlight_set_acpi((st), (level), (max))""")
c = rep(c, "struct drm_i915_private { struct { struct intel_opregion opregion; } display; };",
        "struct drm_i915_private { struct drm_device drm; struct { struct intel_opregion opregion; } display; struct parity_kworkqueue *unordered_wq; };")
open(L + "opregion_compat.h", "w").write(c)

g = open(L + "parity_opregion_glue.inc").read()
g = g.rstrip(NL) + NL + r"""
/* ---------------- OP-ASLE (unit 2a) ---------------- */
#include <kern/lock.h>
struct parity_opregion_worker_stats parity_opregion_wstats;
static int parity_opregion_policy = acpi_backlight_vendor;
static struct mutex parity_opregion_conn_lock;
static int parity_opregion_conn_lock_live;
static struct {
	void (*set_acpi)(void *ctx, uint32_t level, uint32_t max);
	void *ctx;
	struct intel_connector conn;
	struct drm_connector_state state;
} parity_opregion_bl[2];
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
	if (st != 0 && st->target < parity_opregion_nbl && parity_opregion_bl[st->target].set_acpi != 0)
		parity_opregion_bl[st->target].set_acpi(parity_opregion_bl[st->target].ctx, level, max);
}

/*
 * The service around the extracted ASLE text: the queue (dev_priv->unordered_wq), the worker item (INIT_WORK as
 * intel_opregion_setup does it; unit 2b ports setup and replaces this), the backlight policy (acpi_video_get_backlight_type
 * -- a configuration input: vendor / video = serve BCLP, native = the reference ignores ASLE backlight requests).
 */
int parity_opregion_service_start(struct parity_kworkqueue *wq, int policy)
{
	struct intel_opregion *opregion = &parity_opregion_dev.display.opregion;

	if (!parity_opregion_conn_lock_live) {
		(void)mutex_init(&parity_opregion_conn_lock, LOCK_RANK_DEVICE, "parity-opregion-connection");
		parity_opregion_conn_lock_live = 1;
	}
	parity_opregion_dev.unordered_wq = wq;
	parity_opregion_policy = policy;
	parity_opregion_nbl = 0u;
	memset(&parity_opregion_wstats, 0, sizeof(parity_opregion_wstats));
	INIT_WORK(&opregion->asle_work, asle_work);
	return 0;
}

void parity_opregion_set_policy(int policy) { parity_opregion_policy = policy; }

int parity_opregion_add_backlight(void (*set_acpi)(void *ctx, uint32_t level, uint32_t max), void *ctx)
{
	if (parity_opregion_nbl >= 2u)
		return -ENOSPC;
	parity_opregion_bl[parity_opregion_nbl].set_acpi = set_acpi;
	parity_opregion_bl[parity_opregion_nbl].ctx = ctx;
	parity_opregion_bl[parity_opregion_nbl].state.target = parity_opregion_nbl;
	parity_opregion_bl[parity_opregion_nbl].conn.base.state = &parity_opregion_bl[parity_opregion_nbl].state;
	parity_opregion_nbl++;
	return 0;
}

/* the GSE receive entry: what the GU_MISC GSE decode calls (gen11_gu_misc_irq_handler -> intel_opregion_asle_intr) */
void parity_opregion_gse_entry(void)
{
	intel_opregion_asle_intr(&parity_opregion_dev);
}

/* wait until the ASLE work is neither pending nor running (flush_work) */
int parity_opregion_asle_flush(uint64_t deadline)
{
	return parity_kflush_work(parity_opregion_dev.unordered_wq, &parity_opregion_dev.display.opregion.asle_work.kwork, deadline);
}

void parity_opregion_worker_stats_get(unsigned *started, unsigned *finished, unsigned *queued_new, unsigned *queued_pending)
{
	*started = parity_opregion_wstats.started;
	*finished = parity_opregion_wstats.finished;
	*queued_new = parity_opregion_wstats.queued_new;
	*queued_pending = parity_opregion_wstats.queued_pending;
}
"""
open(L + "parity_opregion_glue.inc", "w").write(g)

h = open(L + "parity_opregion.h").read()
h = rep(h, "#endif /* PARITY_OPREGION_H */", """/* OP-ASLE (unit 2a) */
struct parity_kworkqueue;
#define PARITY_OPREGION_POLICY_VIDEO  1     /* acpi_backlight_video: BCLP requests are served */
#define PARITY_OPREGION_POLICY_VENDOR 2
#define PARITY_OPREGION_POLICY_NATIVE 3     /* acpi_backlight_native: the reference ignores ASLE backlight requests */
int parity_opregion_service_start(struct parity_kworkqueue *wq, int policy);
void parity_opregion_set_policy(int policy);
int parity_opregion_add_backlight(void (*set_acpi)(void *ctx, uint32_t level, uint32_t max), void *ctx);
void parity_opregion_gse_entry(void);
int parity_opregion_asle_flush(uint64_t deadline);
void parity_opregion_worker_stats_get(unsigned *started, unsigned *finished, unsigned *queued_new, unsigned *queued_pending);

#endif /* PARITY_OPREGION_H */""")
open(L + "parity_opregion.h", "w").write(h)
print("done")
