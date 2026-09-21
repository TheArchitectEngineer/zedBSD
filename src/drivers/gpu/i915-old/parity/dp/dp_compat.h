/*
 * WS031 Linux-parity — the environment the generated DP AUX / PPS / DRM-helper
 * files (intel_dp_aux_port.c, intel_pps_port.c, drm_dp_helper_port.c,
 * drm_edid_port.c) are compiled in.  zedBSD project code.
 *
 * It supplies what the reference text expects from the kernel: register access,
 * waits, sleeps, a clock, power-domain references, the PPS mutex, delayed work
 * and the few objects (intel_dp, intel_digital_port, intel_connector) the kept
 * functions touch -- fixed to the one platform this port drives (ADL-P, display
 * version 13, PCH ADP).  Everything that reaches hardware or time goes through
 * struct parity_dp_env, so the SAME production functions run against the real
 * GPU and against the GPU-free register model.
 *
 * The reference register definitions and `struct intel_pps` are NOT retyped:
 * they are copied / extracted by plan/ws031/handover/tools/port_dp_aux_pps.py.
 */
#ifndef PARITY_DP_COMPAT_H
#define PARITY_DP_COMPAT_H

/* struct drm_i915_private is shared with the VBT port's compat header; the DP side adds members. */
struct parity_dp_env;
/* a lock of the env (PARITY_DP_LOCK_*); `held` serves the reference's lockdep assertions */
struct parity_dp_mutex { int held; unsigned acquisitions; struct parity_dp_env *env; int id; };
#define PARITY_I915_EXTRA_DISPLAY_MEMBERS \
	struct { struct parity_dp_mutex mutex; unsigned mmio_base; } pps;
#define PARITY_I915_EXTRA_MEMBERS \
	struct parity_dp_env *dp_env; \
	struct { u32 rawclk_freq; } display_runtime; \
	void *unordered_wq;

#include "../vbt/vbt_compat.h"
#include "parity_edp.h"          /* struct parity_dp_env, the PARITY_EDP_E* numbers */

/*
 * errno: the reference text returns Linux-numbered negative errnos and the
 * results travel to the caller as such (parity_edp.h names them).  zedBSD's own
 * numbering differs, so inside these translation units the Linux values are
 * forced, whatever <errno.h> may have defined.
 */
#undef EIO
#undef ENXIO
#undef E2BIG
#undef EBUSY
#undef EINVAL
#undef EPROTO
#undef ETIMEDOUT
#undef EREMOTEIO
#define EIO PARITY_EDP_EIO
#define ENXIO PARITY_EDP_ENXIO
#define E2BIG PARITY_EDP_E2BIG
#define EBUSY PARITY_EDP_EBUSY
#define EINVAL PARITY_EDP_EINVAL
#define EPROTO PARITY_EDP_EPROTO
#define ETIMEDOUT PARITY_EDP_ETIMEDOUT
#define EREMOTEIO PARITY_EDP_EREMOTEIO

/* memcpy(NULL, ..., 0): the reference's bare-address I2C read does this; ISO C calls it undefined */
static inline void *parity_dp_memcpy(void *d, const void *s, size_t n) { return n != 0 ? memcpy(d, s, n) : d; }
#define memcpy(d, s, n) parity_dp_memcpy(d, s, n)
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"   /* intel_pps_init(): `ret` is set inside with_intel_pps_lock */
#endif

typedef int64_t s64;
typedef long ssize_t_parity;
#define ssize_t ssize_t_parity
typedef unsigned int uint;
typedef u16 __le16;
#define le16_to_cpu(x) ((u16)(x))
#define __read_mostly
#define EXPORT_SYMBOL(x)
#define clamp_t(t, v, lo, hi) min_t(t, max_t(t, v, lo), hi)
#define roundup(x, y) ((((x) + ((y) - 1)) / (y)) * (y))
#define fetch_and_zero(ptr) ({ __typeof__(*(ptr)) _v = *(ptr); *(ptr) = 0; _v; })

/* ---- logging: separate counters from the VBT port ---- */
int parity_dp_log_enabled(int level);
void parity_dp_note(int level, const char *fmt);
#undef parity_vbt_log
#ifdef PARITY_VBT_HOST
#define parity_vbt_log(level, fmt, ...) \
	do { if (parity_dp_log_enabled(level)) printf(fmt, ##__VA_ARGS__); } while (0)
#else
#define parity_vbt_log(level, fmt, ...) \
	do { if (0) (void)parity_vbt_fmtcheck(fmt, ##__VA_ARGS__); parity_dp_note(level, fmt); } while (0)
#endif
#define drm_dbg_dp(drm, fmt, ...)   parity_vbt_log(PARITY_VBT_LOG_DEBUG, fmt, ##__VA_ARGS__)
#define drm_dbg_kms_ratelimited(drm, fmt, ...) parity_vbt_log(PARITY_VBT_LOG_DEBUG, fmt, ##__VA_ARGS__)
#define I915_STATE_WARN(i915, cond, fmt, ...) drm_WARN(0, cond, fmt, ##__VA_ARGS__)
#define lockdep_assert_held(m) \
	do { if (!(m)->held) parity_vbt_log(PARITY_VBT_LOG_ERR, "lockdep: %s not held\n", #m); } while (0)

/* ---- registers (i915_reg_defs.h contract; the values live in the copied *_regs.h) ---- */
typedef struct { u32 reg; } i915_reg_t;
#define _MMIO(r) ((const i915_reg_t){ .reg = (r) })
#define INVALID_MMIO_REG _MMIO(0)
static inline u32 i915_mmio_reg_offset(i915_reg_t r) { return r.reg; }
static inline bool i915_mmio_reg_valid(i915_reg_t r) { return r.reg != 0; }
#define REG_BIT(n) ((u32)1u << (n))
#define REG_GENMASK(h, l) ((u32)((0xffffffffu >> (31 - (h))) & (0xffffffffu << (l))))
#define PARITY_BF_SHF(mask) (__builtin_ctz(mask))
#define REG_FIELD_PREP(mask, val) ((u32)((((u32)(val)) << PARITY_BF_SHF(mask)) & (mask)))
#define REG_FIELD_GET(mask, val) ((u32)((((u32)(val)) & (mask)) >> PARITY_BF_SHF(mask)))
#define _PICK_EVEN(index, a, b) ((a) + (index) * ((b) - (a)))
#define _PICK_EVEN_2RANGES(index, c_index, a, b, c, d) \
	((index) < (c_index) ? _PICK_EVEN(index, a, b) : _PICK_EVEN((index) - (c_index), c, d))
#define _PORT(port, a, b) _PICK_EVEN(port, a, b)
#define _MMIO_PORT(port, a, b) _MMIO(_PORT(port, a, b))
#define VLV_DISPLAY_BASE 0x180000
#define IS_DISPLAY_VER(i915, from, until) (DISPLAY_VER(i915) >= (from) && DISPLAY_VER(i915) <= (until))
#define IS_IRONLAKE(i915) 0
#define HAS_PCH_IBX(i915) 0
#define HAS_PCH_CPT(i915) 0
#define HAS_DSC(i915) 1
/* i915_reg.h (values as in the reference header) */
#define SOUTH_CHICKEN1 _MMIO(0xc2000)
#define  ICP_SECOND_PPS_IO_SELECT REG_BIT(2)
#define SOUTH_DSPCLK_GATE_D _MMIO(0xc2020)
#define  PCH_DPLSUNIT_CLOCK_GATE_DISABLE (1 << 29)
#define  PCH_DPMGUNIT_CLOCK_GATE_DISABLE (1 << 15)

/* ---- the environment: every hardware / time dependency of the DP code ---- */
enum intel_display_power_domain {
	/* numerically equal to enum parity_power_domain (asserted in parity_dp_kernel.c) */
	POWER_DOMAIN_DISPLAY_CORE = 0,
	POWER_DOMAIN_AUX_A = 53,
	POWER_DOMAIN_AUX_B, POWER_DOMAIN_AUX_C,
};
typedef int intel_wakeref_t;
struct parity_dp_env *parity_dp_env_current(void);     /* the one live eDP (NULL when none) */

static inline struct parity_dp_env *parity_dp_env_of(struct drm_i915_private *i915) { return i915->dp_env; }
#define intel_de_read(i915, r) parity_dp_env_of(i915)->read32(parity_dp_env_of(i915)->ctx, (r).reg)
#define intel_de_read_notrace(i915, r) intel_de_read(i915, r)
#define intel_de_write(i915, r, v) parity_dp_env_of(i915)->write32(parity_dp_env_of(i915)->ctx, (r).reg, (v))
#define intel_de_posting_read(i915, r) ((void)intel_de_read(i915, r))
static inline u32 intel_de_rmw(struct drm_i915_private *i915, i915_reg_t r, u32 clear, u32 set)
{
	u32 old = intel_de_read(i915, r), val = (old & ~clear) | set;

	if (val != old)          /* intel_uncore_rmw(): written only when the value changes */
		intel_de_write(i915, r, val);
	return old;
}
#define __intel_de_wait_for_register(i915, r, mask, value, fast_us, slow_ms, out) \
	parity_dp_env_of(i915)->wait_reg(parity_dp_env_of(i915)->ctx, (r).reg, mask, value, fast_us, slow_ms, out)
#define intel_de_wait_for_register(i915, r, mask, value, timeout_ms) \
	parity_dp_env_of(i915)->wait_reg(parity_dp_env_of(i915)->ctx, (r).reg, mask, value, 2, timeout_ms, (u32 *)0)
#define trace_i915_reg_rw(...) ((void)0)
#define DISPLAY_RUNTIME_INFO(i915) (&(i915)->display_runtime)

/* sleeps and clocks (the contract of msleep / usleep_range / jiffies / ktime, HZ == 1000) */
void parity_dp_sleep_us(unsigned us);
u64 parity_dp_now_ms(void);
#define msleep(ms) parity_dp_sleep_us((unsigned)(ms) * 1000u)
#define usleep_range(lo, hi) parity_dp_sleep_us((unsigned)(lo))
typedef s64 ktime_t;
#define ktime_get_boottime() ((ktime_t)parity_dp_now_ms())
#define ktime_ms_delta(later, earlier) ((s64)(later) - (s64)(earlier))
#define jiffies ((unsigned long)parity_dp_now_ms())
#define msecs_to_jiffies(ms) ((unsigned long)(ms))
/* i915_utils.h contract: sleep until `to_wait_ms` have passed since `timestamp_jiffies` */
static inline void wait_remaining_ms_from_jiffies(unsigned long timestamp_jiffies, int to_wait_ms)
{
	unsigned long target = timestamp_jiffies + (unsigned long)to_wait_ms + 1ul, now = jiffies;

	if (target > now)
		parity_dp_sleep_us((unsigned)(target - now) * 1000u);
}

/* power-domain references */
intel_wakeref_t parity_dp_power_get(struct drm_i915_private *i915, int domain);
void parity_dp_power_put(struct drm_i915_private *i915, int domain, intel_wakeref_t wakeref);
#define intel_display_power_get(i915, d) parity_dp_power_get(i915, d)
#define intel_display_power_put(i915, d, w) parity_dp_power_put(i915, d, w)
void parity_dp_power_put_async(struct drm_i915_private *i915, int domain, intel_wakeref_t wakeref);
#define intel_display_power_put_async(i915, d, w) parity_dp_power_put_async(i915, d, w)

/*
 * The PPS mutex and the AUX hardware mutex: the env's real locks.  The order is the
 * reference's own (its helpers are unmodified): intel_pps_lock() takes the DISPLAY_CORE power
 * reference, then the PPS mutex; power references taken inside (VDD) nest under it; the AUX
 * hardware mutex of drm_dp_dpcd_access() is outermost around a transfer.
 */
void parity_dp_mutex_lock(struct parity_dp_mutex *m, const char *name);
void parity_dp_mutex_unlock(struct parity_dp_mutex *m, const char *name);
#define mutex_lock(m) parity_dp_mutex_lock((m), #m)
#define mutex_unlock(m) parity_dp_mutex_unlock((m), #m)
#define mutex_init(m) do { (m)->held = 0; (m)->acquisitions = 0; } while (0)

/*
 * Delayed work: the contract of INIT_DELAYED_WORK / queue / cancel / cancel_sync, carried
 * by the env's backend (a timer + worker thread on the real GPU, the model's clock in the
 * GPU-free tests).  The body runs through parity_edp_work_run().
 */
struct work_struct { int unused; };
struct delayed_work {
	struct work_struct work;
	void (*fn)(struct work_struct *);
	int slot;                               /* PARITY_DP_WORK_* */
};
#define to_delayed_work(w) container_of(w, struct delayed_work, work)
#define INIT_DELAYED_WORK(dw, func) do { memset((dw), 0, sizeof(*(dw))); (dw)->fn = (func); } while (0)
bool parity_dp_delayed_cancel(struct delayed_work *dw, int sync);
bool parity_dp_delayed_queue(struct delayed_work *dw, unsigned long delay_ms);
#define cancel_delayed_work(dw) parity_dp_delayed_cancel((dw), 0)
#define cancel_delayed_work_sync(dw) parity_dp_delayed_cancel((dw), 1)
#define queue_delayed_work(wq, dw, delay) parity_dp_delayed_queue((dw), (delay))

/* CPU latency QoS: no CPU idle states are entered while the attaching thread polls */
struct pm_qos_request { int unused; };
#define cpu_latency_qos_update_request(req, v) ((void)(req))
#define PM_QOS_DEFAULT_VALUE (-1)

/* ---- I2C (the i2c_transfer contract: the adapter's algorithm runs the messages) ---- */
#define I2C_M_RD   0x0001
#define I2C_M_STOP 0x8000
#define I2C_FUNC_I2C 0x00000001
#define I2C_FUNC_SMBUS_EMUL 0x0eff0008
#define I2C_FUNC_SMBUS_READ_BLOCK_DATA 0x01000000
#define I2C_FUNC_SMBUS_BLOCK_PROC_CALL 0x00008000
#define I2C_FUNC_10BIT_ADDR 0x00000002
struct i2c_msg { u16 addr; u16 flags; u16 len; u8 *buf; };
struct i2c_adapter;
struct i2c_algorithm {
	int (*master_xfer)(struct i2c_adapter *adap, struct i2c_msg *msgs, int num);
	u32 (*functionality)(struct i2c_adapter *adap);
};
struct i2c_adapter { const struct i2c_algorithm *algo; void *algo_data; int retries; char name[48]; };
static inline int i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	return adap->algo->master_xfer(adap, msgs, num);
}

/* ---- DP AUX objects (drm_dp_helper.h contract: the fields the kept functions use) ---- */
struct drm_dp_aux_msg {
	unsigned int address;
	u8 request;
	u8 reply;
	void *buffer;
	size_t size;
};
struct drm_dp_aux {
	const char *name;
	struct i2c_adapter ddc;
	struct drm_device *drm_dev;
	struct parity_dp_mutex hw_mutex;
	ssize_t (*transfer)(struct drm_dp_aux *aux, struct drm_dp_aux_msg *msg);
	unsigned i2c_nack_count;
	unsigned i2c_defer_count;
	bool is_remote;
	bool powered_down;
};
#define drm_dp_mst_dpcd_read(aux, offset, buffer, size) (-EINVAL)   /* no MST remote AUX */
#define drm_dp_mst_dpcd_write(aux, offset, buffer, size) (-EINVAL)

#include "drm_dp.h"               /* reference, copied: DPCD addresses and AUX request / reply codes */

/* ---- i915 display objects: the members the kept functions read ---- */
enum pipe { INVALID_PIPE = -1, PIPE_A = 0, PIPE_B, PIPE_C, PIPE_D };
#include "dp_ref_types.h"         /* reference, extracted: struct intel_pps */

struct drm_mode_object { int id; };
struct drm_encoder { struct drm_mode_object base; const char *name; struct drm_device *dev; };
struct intel_encoder { struct drm_encoder base; enum port port; };
struct intel_connector { struct intel_panel panel; };
struct intel_dp {
	i915_reg_t output_reg;
	u32 DP;
	bool is_edp;
	struct intel_connector *attached_connector;
	u8 dpcd[DP_RECEIVER_CAP_SIZE];
	u8 edp_dpcd[EDP_DISPLAY_CTL_CAP_SIZE];
	struct drm_dp_aux aux;
	u32 aux_busy_last_status;
	struct pm_qos_request pm_qos;
	struct intel_pps pps;
	u32 (*get_aux_clock_divider)(struct intel_dp *dp, int index);
	u32 (*get_aux_send_ctl)(struct intel_dp *dp, int send_bytes, u32 aux_clock_divider);
	i915_reg_t (*aux_ch_ctl_reg)(struct intel_dp *dp);
	i915_reg_t (*aux_ch_data_reg)(struct intel_dp *dp, int index);
};
struct intel_digital_port {
	struct intel_encoder base;
	struct intel_dp dp;
	enum aux_ch aux_ch;
	struct drm_i915_private *i915;
};
static inline struct intel_digital_port *dp_to_dig_port(struct intel_dp *dp)
{
	return container_of(dp, struct intel_digital_port, dp);
}
static inline struct drm_i915_private *dp_to_i915(struct intel_dp *dp) { return dp_to_dig_port(dp)->i915; }
/* to_i915(dev): the kept functions only pass an encoder's dev, which the glue points at i915->drm */
static inline struct drm_i915_private *to_i915(struct drm_device *dev)
{
	return container_of(dev, struct drm_i915_private, drm);
}
static inline bool intel_dp_is_edp(struct intel_dp *dp) { return dp->is_edp; }
/* intel_display_power.c: a non-TBT AUX channel maps to POWER_DOMAIN_AUX_A + (aux_ch - AUX_CH_A) */
static inline int intel_aux_power_domain(struct intel_digital_port *dig_port)
{
	return POWER_DOMAIN_AUX_A + (int)(dig_port->aux_ch - AUX_CH_A);
}
/* Type-C: the eDP port is a combo PHY; the TC branches of the AUX transfer are never taken */
#define intel_tc_port_lock(d) ((void)(d))
#define intel_tc_port_unlock(d) ((void)(d))
#define intel_tc_port_connected_locked(enc) (true)
#define intel_tc_port_in_tbt_alt_mode(d) (false)
/* quirks: QUIRK_FW_SYNC_LEN (one MTL laptop + panel) and QUIRK_INCREASE_T12_DELAY (one Toshiba) do not apply */
#define QUIRK_FW_SYNC_LEN 0
#define QUIRK_INCREASE_T12_DELAY 0
#define intel_has_dpcd_quirk(dp, q) (false)
#define intel_has_quirk(i915, q) (false)
/* VLV/CHV power-sequencer stealing is not ported; IS_VALLEYVIEW/IS_CHERRYVIEW are 0 */
#define vlv_power_sequencer_pipe(dp) (0)
#define vlv_initial_power_sequencer_setup(dp) ((void)0)

#include "intel_pps.h"            /* reference, copied: the PPS prototypes and with_intel_pps_lock */
#include "intel_dp_aux.h"         /* reference, copied */

#endif /* PARITY_DP_COMPAT_H */
