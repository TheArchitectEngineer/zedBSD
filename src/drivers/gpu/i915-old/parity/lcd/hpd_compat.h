/*
 * WS031 Linux-parity -- what the extracted hotplug text needs (E-123, HDMI hotplug receive).  zedBSD project code.
 * The reference chain icp_irq_handler -> intel_get_hpd_pins -> intel_hpd_irq_handler -> i915_hotplug_work_func ->
 * intel_ddi_hotplug -> drm_helper_probe_detect -> intel_hdmi_detect -> intel_digital_port_connected is generated from
 * Linux v6.8.12 (intel_hotplug_port.c, intel_hotplug_irq_port.c, intel_ddi_hotplug_port.c, intel_dp_connected_port.c,
 * intel_hdmi_detect_port.c, drm_probe_detect_port.c, drm_connector_status_port.c).  This header maps the Linux types
 * the text touches onto zedBSD: the kernel spinlock / mutex (same names and contracts), the shared work queue and
 * delayed work (backend_sync / backend_delayed), sched_ticks() as jiffies, and a device holding only the hotplug state.
 * The objects (encoders, connectors) are the subset of fields the text reads; they are created by the glue
 * (parity_hotplug_glue.inc) from the encoders intel_setup_outputs() made.
 */
#ifndef PARITY_HPD_COMPAT_H
#define PARITY_HPD_COMPAT_H

/* the extracted reference text keeps its own style (unused parameters / helpers), as in lcd_compat.h */
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wsign-compare"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <kern/lock.h>
#include <kern/klog.h>
#include <kern/sched.h>
#include <kern/clock.h>
#include "../backend_sync.h"
#include "../backend_delayed.h"

/*
 * errno: the GMBUS / I2C results cross into the EDID reader of dp/ (drm_do_probe_ddc_edid tests -ENXIO), which uses the
 * reference's LINUX numbering (dp/parity_edp.h); the same numbers are forced here for the errnos this unit returns
 */
#undef EIO
#undef ENXIO
#undef EAGAIN
#undef ETIMEDOUT
#undef EDEADLK
#define EIO 5
#define ENXIO 6
#define EAGAIN 11
#define EDEADLK 35
#define ETIMEDOUT 110

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef unsigned long long u64;     /* the Linux x86-64 kernel type (the text prints it with %llu) */
typedef long long s64;
#ifndef BIT
#define BIT(n) (1u << (n))
#endif
#define BITS_PER_TYPE(t) (8u * (unsigned)sizeof(t))
#ifndef container_of
#define container_of(ptr, type, member) ((type *)(void *)((char *)(ptr) - offsetof(type, member)))
#endif
#define BUILD_BUG_ON(c) _Static_assert(!(c), "BUILD_BUG_ON")
#define REG_BIT(n) ((u32)1u << (n))
typedef struct { u32 reg; } i915_reg_t;
#define _MMIO(r) ((const i915_reg_t){ .reg = (r) })

/* ---- link names: every global of this unit (reference text and glue) is linked as parity_hpd_<name>, so it cannot
 *      collide with the same reference function of another unit (e.g. intel_port_to_phy in intel_display_port.c) ---- */
#define intel_port_to_phy parity_hpd_intel_port_to_phy
#define intel_phy_is_tc parity_hpd_intel_phy_is_tc
#define intel_tc_port_link_reset parity_hpd_intel_tc_port_link_reset
#define intel_dp_phy_test parity_hpd_intel_dp_phy_test
#define intel_dp_retrain_link parity_hpd_intel_dp_retrain_link
#define drm_edid_free parity_hpd_drm_edid_free
#define intel_display_power_get parity_hpd_intel_display_power_get
#define intel_display_power_put parity_hpd_intel_display_power_put
#define intel_hpd_irq_setup parity_hpd_intel_hpd_irq_setup
#define intel_gmbus_irq_handler parity_hpd_intel_gmbus_irq_handler
#define drm_kms_helper_poll_reschedule parity_hpd_drm_kms_helper_poll_reschedule
#define drm_kms_helper_connector_hotplug_event parity_hpd_drm_kms_helper_connector_hotplug_event
#define drm_kms_helper_hotplug_event parity_hpd_drm_kms_helper_hotplug_event
#define intel_hpd_irq_handler parity_hpd_intel_hpd_irq_handler
#define icp_irq_handler parity_hpd_icp_irq_handler
#define intel_encoder_hotplug parity_hpd_intel_encoder_hotplug
#define intel_hpd_init_early parity_hpd_intel_hpd_init_early
#define intel_hpd_cancel_work parity_hpd_intel_hpd_cancel_work
#define intel_digital_port_connected parity_hpd_intel_digital_port_connected
#define drm_helper_probe_detect parity_hpd_drm_helper_probe_detect
#define drm_get_connector_status_name parity_hpd_drm_get_connector_status_name
#define intel_gmbus_reset parity_hpd_intel_gmbus_reset
#define intel_gmbus_force_bit parity_hpd_intel_gmbus_force_bit
#define intel_gmbus_is_forced_bit parity_hpd_intel_gmbus_is_forced_bit
#define drm_edid_read_ddc parity_hpd_drm_edid_read_ddc
#define drm_edid_connector_update parity_hpd_drm_edid_connector_update
#define drm_edid_is_digital parity_hpd_drm_edid_is_digital
#define intel_irqs_enabled parity_hpd_intel_irqs_enabled

#include "lcd_ddi_types.h"              /* reference, extracted: enum port, phy, intel_output_type */
#include "hpd_pin_enum.h"               /* reference, extracted: enum hpd_pin */
#include "hpd_for_each_pin.h"           /* reference, extracted: for_each_hpd_pin */
#include "hpd_hotplug_state.h"          /* reference, extracted: enum intel_hotplug_state */
#include "hpd_drm_connector_status.h"   /* reference, extracted: enum drm_connector_status, DRM_CONNECTOR_POLL_* */
#include "hpd_mreg_i915_reg.h"          /* reference, extracted: SDEISR, SHOTPLUG_CTL_DDI / _TC, SDE_*_ICP */
#include "hpd_mreg_drm_dp.h"            /* reference, extracted: DP_TEST_LINK_PHY_TEST_PATTERN */
#include "hpd_mreg_gmbus.h"             /* reference, extracted: GMBUS0..5 and their fields */
#include "hpd_mreg_gmbus_pins.h"        /* reference, extracted: GMBUS_PIN_2_BXT, GMBUS_NUM_PINS */
#define PCH_DISPLAY_BASE 0xc0000u       /* i915_reg.h */
#define min(a, b) ((a) < (b) ? (a) : (b))

/* ---- diagnostics: the reference's messages, printed (hotplug events are rare; the IRQ-context ones included) ---- */
#define drm_dbg(dev, fmt, ...) kern_logf("i915: parity hpd (drm_dbg) " fmt, ##__VA_ARGS__)
#define drm_dbg_kms(dev, fmt, ...) kern_logf("i915: parity hpd (drm_dbg_kms) " fmt, ##__VA_ARGS__)
#define drm_info(dev, fmt, ...) kern_logf("i915: parity hpd (drm_info) " fmt, ##__VA_ARGS__)
#define drm_err(dev, fmt, ...) kern_logf("i915: parity hpd (drm_err) " fmt, ##__VA_ARGS__)
int parity_hpd_warn(int cond, const char *what, const char *where);
#define WARN_ON(x) parity_hpd_warn(!!(x), #x, __func__)
#define drm_WARN_ON(dev, x) parity_hpd_warn(!!(x), #x, __func__)
#define drm_WARN_ONCE(dev, cond, fmt, ...) parity_hpd_warn(!!(cond), fmt, __func__)
#define lockdep_assert_held(l) ((void)(l))

/* ---- time: jiffies are the kernel tick (KERN_CLOCK_HZ) ---- */
#define jiffies ((unsigned long)sched_ticks())
#define msecs_to_jiffies(ms) ((unsigned long)(((unsigned long)(ms) * KERN_CLOCK_HZ + 999u) / 1000u))
#define parity_hpd_jiffies_to_ms(j) ((unsigned)((unsigned long)(j) * 1000u / KERN_CLOCK_HZ))
#define time_after_eq(a, b) ((long)((a) - (b)) >= 0)
#define time_in_range(a, b, c) (time_after_eq(a, b) && time_after_eq(c, a))

/* ---- locks: the kernel's spin_lock / spin_unlock / mutex_lock / mutex_unlock have the Linux names and contracts;
 *      spin_lock_irq keeps the saved interrupt state under the lock it protects (one irq_lock) ---- */
extern unsigned long parity_hpd_irq_saved;
#define spin_lock_irq(l) do { unsigned long _f = spin_lock_irqsave(l); parity_hpd_irq_saved = _f; } while (0)
#define spin_unlock_irq(l) spin_unlock_irqrestore((l), parity_hpd_irq_saved)
#define mutex_is_locked(m) mutex_owned(m)

/* ---- work: struct work_struct / delayed_work on the shared kernel work queue + timer queue ---- */
struct workqueue_struct { struct parity_kworkqueue wq; struct parity_ktimerq tq; };
struct work_struct { struct parity_kwork kwork; void (*func)(struct work_struct *work); struct workqueue_struct *q; };
struct delayed_work { struct work_struct work; struct parity_kdelayed dw; };
void parity_hpd_work_trampoline(void *ctx);             /* runs w->func(w); the glue records the run */
#define INIT_WORK(w, f) do { (w)->func = (f); (w)->q = NULL; parity_kwork_init(&(w)->kwork, parity_hpd_work_trampoline, (w)); } while (0)
#define INIT_DELAYED_WORK(d, f) do { (d)->work.func = (f); (d)->work.q = NULL; \
	parity_kdelayed_init(&(d)->dw, parity_hpd_work_trampoline, &(d)->work); } while (0)
static inline bool parity_hpd_queue_work(struct workqueue_struct *q, struct work_struct *w)
{
	w->q = q;
	return parity_kqueue_work(&q->wq, &w->kwork) == 1;
}
static inline bool parity_hpd_queue_delayed_work(struct workqueue_struct *q, struct delayed_work *d, unsigned long delay)
{
	d->work.q = q;
	return parity_kdelayed_queue(&q->tq, &d->dw, parity_hpd_jiffies_to_ms(delay)) == 1;
}
/* mod_delayed_work(): the new delay replaces a pending one (disarm, then arm) */
static inline bool parity_hpd_mod_delayed_work(struct workqueue_struct *q, struct delayed_work *d, unsigned long delay)
{
	d->work.q = q;
	(void)parity_kdelayed_cancel(&q->tq, &d->dw);
	return parity_kdelayed_queue(&q->tq, &d->dw, parity_hpd_jiffies_to_ms(delay)) == 1;
}
uint64_t parity_hpd_sync_deadline(void);
static inline bool parity_hpd_cancel_work_sync(struct work_struct *w)
{
	return w->q != NULL ? parity_kcancel_work_sync(&w->q->wq, &w->kwork, parity_hpd_sync_deadline()) == 1 : false;
}
static inline bool parity_hpd_cancel_delayed_work_sync(struct delayed_work *d)
{
	return d->work.q != NULL ? parity_kdelayed_cancel_sync(&d->work.q->tq, &d->dw, parity_hpd_sync_deadline()) == 1 : false;
}
#define queue_work(q, w) parity_hpd_queue_work((q), (w))
#define queue_delayed_work(q, d, delay) parity_hpd_queue_delayed_work((q), (d), (delay))
#define mod_delayed_work(q, d, delay) parity_hpd_mod_delayed_work((q), (d), (delay))
#define cancel_work_sync(w) parity_hpd_cancel_work_sync(w)
#define cancel_delayed_work_sync(d) parity_hpd_cancel_delayed_work_sync(d)

#include "hpd_hotplug_types.h"          /* reference, extracted: struct intel_hotplug */

struct drm_i915_private;
/* ---- I2C: the same types as dp/dp_compat.h (the EDID reader parity_drm_edid_read of dp/ takes this adapter) ---- */
#define I2C_M_RD   0x0001
struct i2c_msg { u16 addr; u16 flags; u16 len; u8 *buf; };
struct i2c_adapter;
struct i2c_algorithm {
	int (*master_xfer)(struct i2c_adapter *adap, struct i2c_msg *msgs, int num);
	u32 (*functionality)(struct i2c_adapter *adap);
};
struct i2c_adapter { const struct i2c_algorithm *algo; void *algo_data; int retries; char name[48]; };
struct i2c_algo_bit_data { int unused; };
extern const struct i2c_algorithm i2c_bit_algo;         /* bit-banging over GPIO: not ported (a step, -EIO) */
int parity_drm_edid_read(struct i2c_adapter *ddc, u8 *buf, unsigned max_blocks, unsigned *extensions);   /* dp/ */

/* ---- waits (i915_utils.h / intel_de.h contracts): poll the condition, sleeping between polls ---- */
#define wait_for(COND, MS) ({ 	uint64_t _wf_dl = sched_ticks() + ((uint64_t)(MS) * KERN_CLOCK_HZ + 999u) / 1000u + 1u; 	int _wf_ret; 	for (;;) { 		bool _wf_expired = sched_ticks() > _wf_dl; 		if (COND) { _wf_ret = 0; break; } 		if (_wf_expired) { _wf_ret = -ETIMEDOUT; break; } 		kern_usleep_range(10u, 50u); 	} 	_wf_ret; })
#define wait_for_us(COND, US) ({ 	int _wu_ret = -ETIMEDOUT; 	for (unsigned _wu_i = 0u; _wu_i <= (unsigned)(US); _wu_i++) { 		if (COND) { _wu_ret = 0; break; } 		kern_usleep_range(1u, 1u); 	} 	_wu_ret; })
u32 parity_hpd_write(u32 reg, u32 val);
#define intel_de_read_fw(i915, r) parity_hpd_read((r).reg)
#define intel_de_write_fw(i915, r, v) ((void)parity_hpd_write((r).reg, (v)))
#define intel_de_write(i915, r, v) ((void)parity_hpd_write((r).reg, (v)))
#define intel_de_wait_for_register_fw(i915, r, mask, value, ms) 	wait_for((parity_hpd_read((r).reg) & (mask)) == (value), (ms))
/* the GMBUS wait queue: the reference polls GMBUS2 in wait_for(); the queue only wakes the poller early */
#define DEFINE_WAIT(w) int w = 0
#define add_wait_queue(q, w) ((void)(q), (void)(w))
#define remove_wait_queue(q, w) ((void)(q), (void)(w))
void parity_hpd_gmbus_woken(void);
#define wake_up_all(q) parity_hpd_gmbus_woken()
#define HAS_GMBUS_IRQ(i915) (DISPLAY_VER(i915) >= 4)
#define HAS_GMBUS_BURST_READ(i915) (DISPLAY_VER(i915) >= 10)
#define IS_GEMINILAKE(i915) (0)
#define IS_BROXTON(i915) (0)
#define HAS_PCH_SPT(i915) (0)
#define HAS_PCH_CNP(i915) (0)
static inline void bxt_gmbus_clock_gating(struct drm_i915_private *i915, bool enable) { (void)i915; (void)enable; }
static inline void pch_gmbus_clock_gating(struct drm_i915_private *i915, bool enable) { (void)i915; (void)enable; }

/* ---- DRM objects: the fields the extracted text reads ---- */
struct drm_modeset_acquire_ctx { int unused; };
struct drm_modeset_lock { int unused; };
struct drm_device { struct { struct mutex mutex; struct drm_modeset_lock connection_mutex; } mode_config; };
/* the connection_mutex / acquire context: the hotplug work is the only connector user and holds
 * mode_config.mutex around every detection (adaptation: no modeset lock graph, no -EDEADLK) */
#define drm_modeset_acquire_init(ctx, flags) ((void)(ctx))
#define drm_modeset_lock(l, ctx) ((void)(l), 0)
#define drm_modeset_backoff(ctx) ((void)(ctx))
#define drm_modeset_drop_locks(ctx) ((void)(ctx))
#define drm_modeset_acquire_fini(ctx) ((void)(ctx))

/* DRM_MODE_CONNECTOR_* (include/uapi/drm/drm_mode.h, the UAPI values) */
#define DRM_MODE_CONNECTOR_DisplayPort 10
#define DRM_MODE_CONNECTOR_HDMIA 11
#define DRM_MODE_CONNECTOR_eDP 14

struct drm_crtc;
struct drm_connector;
struct drm_connector_state { struct drm_crtc *crtc; };
struct drm_connector_funcs { enum drm_connector_status (*detect)(struct drm_connector *connector, bool force); };
struct drm_connector_helper_funcs {
	int (*detect_ctx)(struct drm_connector *connector, struct drm_modeset_acquire_ctx *ctx, bool force);
};
struct i2c_adapter;
struct drm_connector {
	struct drm_device *dev;
	struct { int id; } base;
	const char *name;
	int connector_type;
	enum drm_connector_status status;
	u64 epoch_counter;
	int force;                      /* enum drm_connector_force: 0 = DRM_FORCE_UNSPECIFIED */
	u8 polled;
	const struct drm_connector_funcs *funcs;
	const struct drm_connector_helper_funcs *helper_private;
	struct i2c_adapter *ddc;
	struct drm_connector_state *state;
	struct { u16 source_physical_address; } display_info;
};
struct drm_encoder { struct drm_device *dev; struct { int id; } base; const char *name; };
struct drm_edid;
struct intel_connector;
struct intel_encoder {
	struct drm_encoder base;
	enum intel_output_type type;
	enum port port;
	enum hpd_pin hpd_pin;
	enum intel_hotplug_state (*hotplug)(struct intel_encoder *encoder, struct intel_connector *connector);
};
struct intel_connector {
	struct drm_connector base;
	struct intel_encoder *encoder;
	u8 polled;
	int hotplug_retries;
	const struct drm_edid *detect_edid;
};
enum drm_dp_dual_mode_type { DRM_DP_DUAL_MODE_NONE = 0 };   /* the value intel_hdmi_unset_edid() stores */
struct intel_dp { struct { bool test_active; int test_type; } compliance; bool is_mst; struct intel_connector *attached_connector; };
struct intel_hdmi { struct intel_connector *attached_connector; struct { enum drm_dp_dual_mode_type type; int max_tmds_clock; } dp_dual_mode;
	void *cec_notifier; };
enum irqreturn { IRQ_NONE = 0, IRQ_HANDLED = 1, IRQ_WAKE_THREAD = 2 };
struct intel_digital_port {
	struct intel_encoder base;
	struct intel_dp dp;
	struct intel_hdmi hdmi;
	bool (*connected)(struct intel_encoder *encoder);
	enum irqreturn (*hpd_pulse)(struct intel_digital_port *dig_port, bool long_hpd);
};

/* intel_display_types.h accessors (the reference's contracts) */
#define to_intel_connector(c) container_of((c), struct intel_connector, base)
#define enc_to_dig_port(e) container_of((e), struct intel_digital_port, base)
#define hdmi_to_dig_port(h) container_of((h), struct intel_digital_port, hdmi)
#define intel_attached_encoder(c) ((c)->encoder)
#define intel_attached_hdmi(c) (&enc_to_dig_port(intel_attached_encoder(c))->hdmi)
static inline bool intel_encoder_is_dig_port(struct intel_encoder *encoder)
{
	switch (encoder->type) {
	case INTEL_OUTPUT_DDI:
	case INTEL_OUTPUT_DP:
	case INTEL_OUTPUT_EDP:
	case INTEL_OUTPUT_HDMI:
		return true;
	default:
		return false;
	}
}

/* ---- the device: only the hotplug state ---- */
struct intel_uncore { int unused; };
struct drm_i915_private {
	struct drm_device drm;
	struct intel_uncore uncore;
	struct spinlock irq_lock;
	bool display_irqs_enabled;
	struct workqueue_struct *unordered_wq;
	struct { int unused; } runtime_pm;
	struct { struct intel_hotplug hotplug; struct { const void *hotplug; } funcs;
		struct { u32 mmio_base; struct mutex mutex; int wait_queue; } gmbus; } display;
};
#define to_i915(dev) container_of((dev), struct drm_i915_private, drm)
#define HAS_DISPLAY(i915) (1)
#define HAS_GMCH(i915) (0)
#define HAS_DP_MST(i915) (1)          /* ADL-P: display ver 13 has DP MST */
/* E-126: the display version of the device the probe found (12 = Tiger Lake, 13 = ADL-P class) */
int parity_lcd_display_ver(void);
#define DISPLAY_VER(i915) (parity_lcd_display_ver())
#define intel_display_device_enabled(i915) (true)

/* registers: the glue routes them to the MMIO BAR (or, in the model tests, to fake registers) */
u32 parity_hpd_read(u32 reg);
u32 parity_hpd_rmw(u32 reg, u32 clear, u32 set);
#define intel_de_read(i915, r) parity_hpd_read((r).reg)
#define intel_uncore_rmw(uncore, r, clear, set) parity_hpd_rmw((r).reg, (clear), (set))

/* object walks: the encoders / connectors the glue created */
struct intel_encoder *parity_hpd_encoder_at(unsigned idx);
#define for_each_intel_encoder(dev, e) \
	for (unsigned _hpd_ei = 0u; ((e) = parity_hpd_encoder_at(_hpd_ei)) != NULL; _hpd_ei++)
struct drm_connector_list_iter { unsigned idx; };
struct intel_connector *parity_hpd_connector_next(struct drm_connector_list_iter *it);
#define drm_connector_list_iter_begin(dev, it) ((it)->idx = 0u)
#define drm_connector_list_iter_end(it) ((void)(it))
#define for_each_intel_connector_iter(c, it) while (((c) = parity_hpd_connector_next(it)) != NULL)
#define drm_connector_get(c) ((void)(c))
#define drm_connector_put(c) ((void)(c))

/* power: POWER_DOMAIN_* the text names, onto the power-domain code (parity_display_power_get / _put) */
typedef unsigned long intel_wakeref_t;
enum intel_display_power_domain { POWER_DOMAIN_DISPLAY_CORE, POWER_DOMAIN_GMBUS };
struct drm_i915_private;
intel_wakeref_t intel_display_power_get(struct drm_i915_private *i915, enum intel_display_power_domain domain);
void intel_display_power_put(struct drm_i915_private *i915, enum intel_display_power_domain domain, intel_wakeref_t wf);
#define intel_display_power_put_async(i915, domain, wf) intel_display_power_put((i915), (domain), (wf))
#define with_intel_display_power(i915, domain, wf) \
	for ((wf) = intel_display_power_get((i915), (domain)); (wf); \
	     intel_display_power_put_async((i915), (domain), (wf)), (wf) = 0)
#define intel_runtime_pm_get(rpm) ((intel_wakeref_t)1)
#define intel_runtime_pm_put(rpm, wf) ((void)(wf))

/* the surroundings the glue provides (recorded as steps where the reference does more) */
void intel_hpd_irq_setup(struct drm_i915_private *i915);
void intel_gmbus_irq_handler(struct drm_i915_private *i915);
void drm_kms_helper_poll_reschedule(struct drm_device *dev);
void drm_kms_helper_connector_hotplug_event(struct drm_connector *connector);
void drm_kms_helper_hotplug_event(struct drm_device *dev);
static void i915_hpd_poll_init_work(struct work_struct *work);        /* parity_hotplug_glue.inc */
enum phy intel_port_to_phy(struct drm_i915_private *i915, enum port port);
bool intel_phy_is_tc(struct drm_i915_private *i915, enum phy phy);
bool intel_tc_port_link_reset(struct intel_digital_port *dig_port);
void intel_dp_phy_test(struct intel_encoder *encoder);
int intel_dp_retrain_link(struct intel_encoder *encoder, struct drm_modeset_acquire_ctx *ctx);
static int intel_hdmi_reset_link(struct intel_encoder *encoder, struct drm_modeset_acquire_ctx *ctx);   /* ddi glue */
static void intel_hdmi_dp_dual_mode_detect(struct drm_connector *connector);                               /* hdmi glue */
struct drm_edid;
const struct drm_edid *drm_edid_read_ddc(struct drm_connector *connector, struct i2c_adapter *adapter);
void drm_edid_connector_update(struct drm_connector *connector, const struct drm_edid *drm_edid);
bool drm_edid_is_digital(const struct drm_edid *drm_edid);
void intel_gmbus_force_bit(struct i2c_adapter *adapter, bool force_bit);
bool intel_gmbus_is_forced_bit(struct i2c_adapter *adapter);
void intel_gmbus_reset(struct drm_i915_private *i915);
bool intel_irqs_enabled(struct drm_i915_private *i915);
#define cec_notifier_set_phys_addr(n, pa) ((void)(n), (void)(pa))
/* the GMBUS adapter of a DDC pin (intel_gmbus_setup + intel_gmbus_get_adapter, gmbus glue) */
struct i2c_adapter *parity_hpd_gmbus_adapter(struct drm_i915_private *i915, unsigned int pin);
void parity_hpd_gmbus_forget(void);
void drm_edid_free(const struct drm_edid *drm_edid);
#define cec_notifier_phys_addr_invalidate(n) ((void)(n))
/* intel_modeset_lock_ctx_retry(): one pass (no -EDEADLK without a modeset lock graph) */
#define intel_modeset_lock_ctx_retry(ctx, state, flags, ret) \
	for (int _hpd_once = ((ret) = 0, 1); _hpd_once; _hpd_once = 0)

/* the reference's exported entry points of the generated units */
void intel_hpd_irq_handler(struct drm_i915_private *dev_priv, u32 pin_mask, u32 long_mask);
void icp_irq_handler(struct drm_i915_private *dev_priv, u32 pch_iir);
enum intel_hotplug_state intel_encoder_hotplug(struct intel_encoder *encoder, struct intel_connector *connector);
void intel_hpd_init_early(struct drm_i915_private *i915);
void intel_hpd_cancel_work(struct drm_i915_private *dev_priv);
bool intel_digital_port_connected(struct intel_encoder *encoder);
int drm_helper_probe_detect(struct drm_connector *connector, struct drm_modeset_acquire_ctx *ctx, bool force);
const char *drm_get_connector_status_name(enum drm_connector_status status);
/* the glue's getters for the static callbacks of their units */
enum intel_hotplug_state (*parity_hpd_ddi_hotplug_fn(void))(struct intel_encoder *, struct intel_connector *);
bool (*parity_hpd_lpt_connected_fn(void))(struct intel_encoder *);
const struct drm_connector_funcs *parity_hpd_hdmi_connector_funcs(void);

#endif /* PARITY_HPD_COMPAT_H */
