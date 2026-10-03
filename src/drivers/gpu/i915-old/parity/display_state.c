/*
 * WS031 Linux-parity — P3 tail: intel_mode_config_init() .. intel_fbc_init().
 * See display_state.h for the ported call sequence and the two distinct "bw"
 * states the reference keeps apart.
 */
#include "display_state.h"
#include "dram_bw.h"
#include "pcode.h"
#include "backend_sync.h"
#include "osdep/mmio.h"
#include <kern/klog.h>
#include <kern/lock.h>
#include <errno.h>

volatile unsigned parity_display_state_test_alloc_fail_at;
volatile int parity_display_state_test_dmi_match;

/* ---------------------------------------------------------------- *
 * i915_reg.h: the SAGV / QGV PCODE mailbox
 * ---------------------------------------------------------------- */

#define ICL_PCODE_SAGV_DE_MEM_SS_CONFIG   0xeu

/* REG_GENMASK(1, 0) / REG_FIELD_PREP(mask, 0) */
#define ICL_PCODE_REP_QGV_MASK            0x3u
#define ICL_PCODE_REP_QGV_SAFE            0x0u
/* REG_GENMASK(3, 2) / REG_FIELD_PREP(mask, 0) */
#define ADLS_PCODE_REP_PSF_MASK           0xcu
#define ADLS_PCODE_REP_PSF_SAFE           0x0u

/* REG_GENMASK(7, 0) / REG_GENMASK(10, 8) with REG_FIELD_PREP */
#define ICL_PCODE_REQ_QGV_PT_MASK         0xffu
#define ICL_PCODE_REQ_QGV_PT(x)           ((((unsigned)(x)) << 0) & ICL_PCODE_REQ_QGV_PT_MASK)
#define ADLS_PCODE_REQ_PSF_PT_MASK        0x700u
#define ADLS_PCODE_REQ_PSF_PT(x)          ((((unsigned)(x)) << 8) & ADLS_PCODE_REQ_PSF_PT_MASK)

/* Wa_14016740474 (DISPLAY_VER 14 A0..C0 only; never taken on ADL-P). */
#define XELPD_CHICKEN_DCPR_3              0x46434u
#define DMD_RSP_TIMEOUT_DISABLE           (1u << 19)

#define PARITY_UINT_MAX                   0xffffffffu

/* ---------------------------------------------------------------- *
 * Small helpers
 * ---------------------------------------------------------------- */

static void
zero_mem(void *p, unsigned n)
{
	unsigned i;
	char *c = (char *)p;

	for (i = 0u; i < n; i++)
		c[i] = 0;
}

/* GENMASK(num - 1, 0) */
static unsigned
genmask_low(unsigned num)
{
	if (num == 0u)
		return 0u;
	if (num >= 32u)
		return 0xffffffffu;
	return (1u << num) - 1u;
}

/* linux/log2.h is_power_of_2() */
static int
is_power_of_2(unsigned n)
{
	return (n != 0u) && ((n & (n - 1u)) == 0u);
}

/* intel_de_rmw() (same shape as display_core.c's local helper). */
static uint32_t
rmw(struct osdep_mmio *m, uint32_t reg, uint32_t clear, uint32_t set)
{
	uint32_t old = osdep_mmio_read32(m, reg);
	uint32_t val = (old & ~clear) | set;

	if (val != old)
		osdep_mmio_write32(m, reg, val);

	return old;
}

/* ---------------------------------------------------------------- *
 * intel_global_state.c: intel_atomic_global_obj_init()
 * ---------------------------------------------------------------- */

static void
parity_atomic_global_obj_init(struct parity_display_state *d,
	struct parity_global_obj *obj, struct parity_global_state *state,
	const struct parity_global_state_funcs *funcs)
{
	zero_mem(obj, (unsigned)sizeof(*obj));

	state->obj = obj;

	/* kref_init(&state->ref) */
	state->ref = 1u;

	obj->state = state;
	obj->funcs = funcs;

	/* list_add_tail(&obj->head, &dev_priv->display.global.obj_list) */
	obj->next = 0;
	if (d->obj_list_tail != 0)
		d->obj_list_tail->next = obj;
	else
		d->obj_list_head = obj;
	d->obj_list_tail = obj;
	d->obj_count++;
}

/*
 * The reference kzalloc()s every global state and returns -ENOMEM on failure.
 * Storage here is device-owned; this allocator exists so that failure path stays
 * reachable (and is what the test hook drives).  Returns 0 on success.
 */
static int
alloc_global_state(struct parity_display_state *d, void *storage, unsigned size)
{
	unsigned n = d->state_allocs + 1u;

	if (parity_display_state_test_alloc_fail_at != 0u &&
	    parity_display_state_test_alloc_fail_at == n)
		return -ENOMEM;

	zero_mem(storage, size);        /* kzalloc */
	d->state_allocs = n;
	return 0;
}

/*
 * The duplicate/destroy vtables belong to the atomic commit machinery (P5+), so
 * they are not implemented here.  They are registered as NULL-op tables whose
 * identity is still distinct per user, exactly as the reference registers four
 * different intel_global_state_funcs.
 */
static const struct parity_global_state_funcs parity_cdclk_funcs = { 0, 0 };
static const struct parity_global_state_funcs parity_dbuf_funcs = { 0, 0 };
static const struct parity_global_state_funcs parity_bw_funcs = { 0, 0 };
static const struct parity_global_state_funcs parity_pmdemand_funcs = { 0, 0 };

/* ---------------------------------------------------------------- *
 * intel_display_driver.c: intel_mode_config_init()
 * ---------------------------------------------------------------- */

void
parity_intel_mode_config_init(struct parity_display_state *d, int display_ver,
	int legacy_platform)
{
	struct parity_mode_config *mc = &d->mode_config;

	/* drm_mode_config_init(&i915->drm) */
	zero_mem(mc, (unsigned)sizeof(*mc));
	mc->drm_mode_config_inited = 1;

	/* INIT_LIST_HEAD(&i915->display.global.obj_list) */
	d->obj_list_head = 0;
	d->obj_list_tail = 0;
	d->obj_count = 0u;
	d->obj_list_inited = 1;

	mc->min_width = 0u;
	mc->min_height = 0u;

	mc->preferred_depth = 24u;
	mc->prefer_shadow = 1;

	mc->funcs_set = 1;             /* &intel_mode_funcs */
	mc->helper_private_set = 1;    /* &intel_mode_config_funcs */

	/* HAS_ASYNC_FLIPS(i915) == DISPLAY_VER(i915) >= 5 */
	mc->async_page_flip = (display_ver >= 5) ? 1 : 0;

	/*
	 * Maximum framebuffer dimensions, chosen to match
	 * the maximum render engine surface size on gen4+.
	 */
	if (display_ver >= 7) {
		mc->max_width = 16384u;
		mc->max_height = 16384u;
	} else if (display_ver >= 4) {
		mc->max_width = 8192u;
		mc->max_height = 8192u;
	} else if (display_ver == 3) {
		mc->max_width = 4096u;
		mc->max_height = 4096u;
	} else {
		mc->max_width = 2048u;
		mc->max_height = 2048u;
	}

	if (legacy_platform == PARITY_PLAT_I845G ||
	    legacy_platform == PARITY_PLAT_I865G) {
		mc->cursor_width = (legacy_platform == PARITY_PLAT_I845G) ? 64u : 512u;
		mc->cursor_height = 1023u;
	} else if (legacy_platform == PARITY_PLAT_I830 ||
		   legacy_platform == PARITY_PLAT_I85X ||
		   legacy_platform == PARITY_PLAT_I915G ||
		   legacy_platform == PARITY_PLAT_I915GM) {
		mc->cursor_width = 64u;
		mc->cursor_height = 64u;
	} else {
		mc->cursor_width = 256u;
		mc->cursor_height = 256u;
	}

	d->inited = 1;
}

/* ---------------------------------------------------------------- *
 * intel_cdclk.c: intel_cdclk_init()
 * ---------------------------------------------------------------- */

int
parity_intel_cdclk_init(struct parity_display_state *d)
{
	int ret;

	ret = alloc_global_state(d, &d->cdclk_state, (unsigned)sizeof(d->cdclk_state));
	if (ret != 0) {
		d->fail_where = "intel_cdclk_init";
		return ret;
	}

	parity_atomic_global_obj_init(d, &d->cdclk_obj, &d->cdclk_state.base,
		&parity_cdclk_funcs);
	d->cdclk_obj.name = "cdclk";

	return 0;
}

/* ---------------------------------------------------------------- *
 * intel_color.c: intel_color_init()
 * ---------------------------------------------------------------- */

int
parity_intel_color_init(struct parity_display_state *d, int display_ver)
{
	if (display_ver != 10) {
		d->color_done = 1;
		return 0;
	}

	/*
	 * GLK only: create_linear_lut(degamma_lut_size) into
	 * display.color.glk_linear_degamma_lut.  DRM property blobs do not exist
	 * in this port, so report it unimplemented instead of a silent success.
	 */
	d->fail_where = "intel_color_init";
	return -ENOSYS;
}

/* ---------------------------------------------------------------- *
 * skl_watermark.c: intel_dbuf_init()
 * ---------------------------------------------------------------- */

int
parity_intel_dbuf_init(struct parity_display_state *d)
{
	int ret;

	ret = alloc_global_state(d, &d->dbuf_state, (unsigned)sizeof(d->dbuf_state));
	if (ret != 0) {
		d->fail_where = "intel_dbuf_init";
		return ret;
	}

	parity_atomic_global_obj_init(d, &d->dbuf_obj, &d->dbuf_state.base,
		&parity_dbuf_funcs);
	d->dbuf_obj.name = "dbuf";

	return 0;
}

/* ---------------------------------------------------------------- *
 * intel_bw.c: the QGV / PSF point masks and the forced SAGV disable
 * ---------------------------------------------------------------- */

/* skl_watermark.c intel_has_sagv(): HAS_SAGV && status != NOT_CONTROLLED. */
int
parity_intel_has_sagv(int display_ver, int legacy_platform, int sagv_status)
{
	int has_sagv;
	int is_lp;

	/* HAS_SAGV(i915) == DISPLAY_VER(i915) >= 9 && !IS_LP(i915) */
	is_lp = (legacy_platform == PARITY_PLAT_BROXTON) ? 1 : 0;
	has_sagv = (display_ver >= 9 && !is_lp) ? 1 : 0;

	return has_sagv && (sagv_status != PARITY_SAGV_NOT_CONTROLLED);
}

uint16_t
parity_icl_qgv_points_mask(const struct parity_bw_state *bw)
{
	unsigned num_psf_gv_points = bw->max[0].num_psf_gv_points;
	unsigned num_qgv_points = bw->max[0].num_qgv_points;
	uint16_t qgv_points = 0u, psf_points = 0u;

	/*
	 * We can _not_ use the whole ADLS_QGV_PT_MASK here, as PCode rejects
	 * it with failure if we try masking any unadvertised points.
	 * So need to operate only with those returned from PCode.
	 */
	if (num_qgv_points > 0u)
		qgv_points = (uint16_t)genmask_low(num_qgv_points);

	if (num_psf_gv_points > 0u)
		psf_points = (uint16_t)genmask_low(num_psf_gv_points);

	return (uint16_t)(ICL_PCODE_REQ_QGV_PT(qgv_points) |
			  ADLS_PCODE_REQ_PSF_PT(psf_points));
}

/*
 * NOTE: icl_max_bw_index() and tgl_max_bw_index() are NOT variants of one
 * another -- they scan in opposite directions, compare num_planes with opposite
 * senses, and fall back to different values.  Both are kept verbatim.
 */
static unsigned
icl_max_bw_index(const struct parity_bw_state *bw, int num_planes, int qgv_point)
{
	int i;

	/* Let's return max bw for 0 planes */
	if (num_planes < 1)
		num_planes = 1;

	for (i = 0; i < (int)PARITY_BW_GROUPS; i++) {
		const struct parity_bw_group *bi = &bw->max[i];

		/*
		 * Pcode will not expose all QGV points when
		 * SAGV is forced to off/min/med/max.
		 */
		if (qgv_point >= (int)bi->num_qgv_points)
			return PARITY_UINT_MAX;

		if (num_planes >= (int)bi->num_planes)
			return (unsigned)i;
	}

	return PARITY_UINT_MAX;
}

static unsigned
tgl_max_bw_index(const struct parity_bw_state *bw, int num_planes, int qgv_point)
{
	int i;

	/* Let's return max bw for 0 planes */
	if (num_planes < 1)
		num_planes = 1;

	for (i = (int)PARITY_BW_GROUPS - 1; i >= 0; i--) {
		const struct parity_bw_group *bi = &bw->max[i];

		/*
		 * Pcode will not expose all QGV points when
		 * SAGV is forced to off/min/med/max.
		 */
		if (qgv_point >= (int)bi->num_qgv_points)
			return PARITY_UINT_MAX;

		if (num_planes <= (int)bi->num_planes)
			return (unsigned)i;
	}

	return 0u;
}

static unsigned
icl_qgv_bw(const struct parity_bw_state *bw, int display_ver,
	int num_active_planes, int qgv_point)
{
	unsigned idx;

	if (display_ver >= 12)
		idx = tgl_max_bw_index(bw, num_active_planes, qgv_point);
	else
		idx = icl_max_bw_index(bw, num_active_planes, qgv_point);

	if (idx >= (unsigned)PARITY_BW_GROUPS)
		return 0u;

	return bw->max[idx].deratedbw[qgv_point];
}

static unsigned
adl_psf_bw(const struct parity_bw_state *bw, int psf_gv_point)
{
	const struct parity_bw_group *bi = &bw->max[0];

	return bi->psf_bw[psf_gv_point];
}

/* icl_qgv_bw() for callers outside this file (the modeset's bandwidth check) */
unsigned
parity_icl_qgv_bw(const struct parity_bw_state *bw, int display_ver, int num_active_planes, int qgv_point)
{
	return icl_qgv_bw(bw, display_ver, num_active_planes, qgv_point);
}

unsigned
parity_icl_max_bw_qgv_point_mask(const struct parity_bw_state *bw,
	int display_ver, int num_active_planes)
{
	unsigned num_qgv_points = bw->max[0].num_qgv_points;
	unsigned max_bw_point = 0u;
	unsigned max_bw = 0u;
	unsigned i;

	for (i = 0u; i < num_qgv_points; i++) {
		unsigned max_data_rate =
			icl_qgv_bw(bw, display_ver, num_active_planes, (int)i);

		/*
		 * We need to know which qgv point gives us
		 * maximum bandwidth in order to disable SAGV
		 * if we find that we exceed SAGV block time
		 * with watermarks.
		 */
		if (max_data_rate > max_bw) {
			max_bw_point = (1u << i);
			max_bw = max_data_rate;
		}
	}

	return max_bw_point;
}

unsigned
parity_icl_max_bw_psf_gv_point_mask(const struct parity_bw_state *bw)
{
	unsigned num_psf_gv_points = bw->max[0].num_psf_gv_points;
	unsigned max_bw_point_mask = 0u;
	unsigned max_bw = 0u;
	unsigned i;

	for (i = 0u; i < num_psf_gv_points; i++) {
		unsigned max_data_rate = adl_psf_bw(bw, (int)i);

		if (max_data_rate > max_bw) {
			max_bw_point_mask = (1u << i);
			max_bw = max_data_rate;
		} else if (max_data_rate == max_bw) {
			max_bw_point_mask |= (1u << i);
		}
	}

	return max_bw_point_mask;
}

static uint16_t
icl_prepare_qgv_points_mask(const struct parity_bw_state *bw,
	unsigned qgv_points, unsigned psf_points)
{
	return (uint16_t)(~(ICL_PCODE_REQ_QGV_PT(qgv_points) |
			    ADLS_PCODE_REQ_PSF_PT(psf_points)) &
			  parity_icl_qgv_points_mask(bw));
}

static int
is_sagv_enabled(const struct parity_bw_state *bw, uint16_t points_mask)
{
	return !is_power_of_2((unsigned)(~points_mask) &
			      parity_icl_qgv_points_mask(bw) &
			      ICL_PCODE_REQ_QGV_PT_MASK);
}

int
parity_icl_pcode_restrict_qgv_points(struct mutex *sb_lock, struct osdep_mmio *m,
	int display_ver, struct parity_bw_state *bw, uint32_t points_mask)
{
	int ret;

	if (display_ver >= 14)
		return 0;

	/* bspec says to keep retrying for at least 1 ms */
	ret = parity_skl_pcode_request(sb_lock, m, ICL_PCODE_SAGV_DE_MEM_SS_CONFIG,
		points_mask,
		ICL_PCODE_REP_QGV_MASK | ADLS_PCODE_REP_PSF_MASK,
		ICL_PCODE_REP_QGV_SAFE | ADLS_PCODE_REP_PSF_SAFE,
		1);

	if (ret < 0) {
		kern_logf("i915: parity Failed to disable qgv points (%d) points: 0x%x\n",
			ret, points_mask);
		return ret;
	}

	bw->sagv_status = is_sagv_enabled(bw, (uint16_t)points_mask) ?
		PARITY_SAGV_ENABLED : PARITY_SAGV_DISABLED;

	return 0;
}

static void
icl_force_disable_sagv(struct parity_display_state *d, struct mutex *sb_lock,
	struct osdep_mmio *m, int display_ver, struct parity_bw_state *bw,
	struct parity_bw_obj_state *bw_obj)
{
	unsigned qgv_points = parity_icl_max_bw_qgv_point_mask(bw, display_ver, 0);
	unsigned psf_points = parity_icl_max_bw_psf_gv_point_mask(bw);

	bw_obj->qgv_points_mask = icl_prepare_qgv_points_mask(bw, qgv_points,
		psf_points);

	d->sagv_qgv_points = qgv_points;
	d->sagv_psf_points = psf_points;
	d->sagv_force_disable_attempted = 1;

	kern_logf("i915: parity Forcing SAGV disable: mask 0x%x\n",
		(unsigned)bw_obj->qgv_points_mask);

	d->sagv_pcode_ret = parity_icl_pcode_restrict_qgv_points(sb_lock, m,
		display_ver, bw, (uint32_t)bw_obj->qgv_points_mask);
}

int
parity_intel_bw_init(struct parity_display_state *d, int display_ver,
	struct parity_bw_state *bw, struct mutex *sb_lock, struct osdep_mmio *m)
{
	int ret;

	ret = alloc_global_state(d, &d->bw_obj_state, (unsigned)sizeof(d->bw_obj_state));
	if (ret != 0) {
		d->fail_where = "intel_bw_init";
		return ret;
	}

	parity_atomic_global_obj_init(d, &d->bw_obj, &d->bw_obj_state.base,
		&parity_bw_funcs);
	d->bw_obj.name = "bw";

	/*
	 * Limit this only if we have SAGV. And for Display version 14 onwards
	 * sagv is handled though pmdemand requests
	 */
	if (parity_intel_has_sagv(display_ver, PARITY_PLAT_NONE, bw->sagv_status) &&
	    display_ver >= 11 && display_ver <= 13)
		icl_force_disable_sagv(d, sb_lock, m, display_ver, bw, &d->bw_obj_state);

	return 0;
}

/* ---------------------------------------------------------------- *
 * intel_pmdemand.c: intel_pmdemand_init()
 * ---------------------------------------------------------------- */

int
parity_intel_pmdemand_init(struct parity_display_state *d, int display_ver,
	struct osdep_mmio *m, int ip_step)
{
	int ret;

	ret = alloc_global_state(d, &d->pmdemand_state,
		(unsigned)sizeof(d->pmdemand_state));
	if (ret != 0) {
		d->fail_where = "intel_pmdemand_init";
		return ret;
	}

	parity_atomic_global_obj_init(d, &d->pmdemand_obj, &d->pmdemand_state.base,
		&parity_pmdemand_funcs);
	d->pmdemand_obj.name = "pmdemand";

	/*
	 * IS_DISPLAY_IP_STEP(i915, IP_VER(14, 0), STEP_A0, STEP_C0)
	 * Wa_14016740474 -- DISPLAY_VER 14 only, so never taken on ADL-P (13).
	 * ip_step is the display stepping index; STEP_A0 == 0, STEP_C0 == 8.
	 */
	if (display_ver == 14 && ip_step >= 0 && ip_step < 8) {
		(void)rmw(m, XELPD_CHICKEN_DCPR_3, 0u, DMD_RSP_TIMEOUT_DISABLE);
		d->pmdemand_wa_14016740474 = 1;
	}

	return 0;
}

/* ---------------------------------------------------------------- *
 * intel_quirks.c: intel_init_quirks()
 * ---------------------------------------------------------------- */

#define PARITY_PCI_ANY_ID  ((int)(-1))

struct parity_quirk {
	int device;
	int subsystem_vendor;
	int subsystem_device;
	int quirk;                  /* enum parity_quirk_id applied by the hook */
	const char *msg;
};

/*
 * intel_quirks[] verbatim.  The hooks all reduce to intel_set_quirk(id) plus an
 * info log, so the table carries the quirk id instead of a function pointer.
 */
static const struct parity_quirk parity_quirks[] = {
	/* Lenovo U160 cannot use SSC on LVDS */
	{ 0x0046, 0x17aa, 0x3920, PARITY_QUIRK_LVDS_SSC_DISABLE, "lvds SSC disable" },

	/* Sony Vaio Y cannot use SSC on LVDS */
	{ 0x0046, 0x104d, 0x9076, PARITY_QUIRK_LVDS_SSC_DISABLE, "lvds SSC disable" },

	/* Acer Aspire 5734Z must invert backlight brightness */
	{ 0x2a42, 0x1025, 0x0459, PARITY_QUIRK_INVERT_BRIGHTNESS, "inverted panel brightness" },

	/* Acer/eMachines G725 */
	{ 0x2a42, 0x1025, 0x0210, PARITY_QUIRK_INVERT_BRIGHTNESS, "inverted panel brightness" },

	/* Acer/eMachines e725 */
	{ 0x2a42, 0x1025, 0x0212, PARITY_QUIRK_INVERT_BRIGHTNESS, "inverted panel brightness" },

	/* Acer/Packard Bell NCL20 */
	{ 0x2a42, 0x1025, 0x034b, PARITY_QUIRK_INVERT_BRIGHTNESS, "inverted panel brightness" },

	/* Acer Aspire 4736Z */
	{ 0x2a42, 0x1025, 0x0260, PARITY_QUIRK_INVERT_BRIGHTNESS, "inverted panel brightness" },

	/* Acer Aspire 5336 */
	{ 0x2a42, 0x1025, 0x048a, PARITY_QUIRK_INVERT_BRIGHTNESS, "inverted panel brightness" },

	/* Acer C720 and C720P Chromebooks (Celeron 2955U) have backlights */
	{ 0x0a06, 0x1025, 0x0a11, PARITY_QUIRK_BACKLIGHT_PRESENT, "backlight present" },

	/* Acer C720 Chromebook (Core i3 4005U) */
	{ 0x0a16, 0x1025, 0x0a11, PARITY_QUIRK_BACKLIGHT_PRESENT, "backlight present" },

	/* Apple Macbook 2,1 (Core 2 T7400) */
	{ 0x27a2, 0x8086, 0x7270, PARITY_QUIRK_BACKLIGHT_PRESENT, "backlight present" },

	/* Apple Macbook 4,1 */
	{ 0x2a02, 0x106b, 0x00a1, PARITY_QUIRK_BACKLIGHT_PRESENT, "backlight present" },

	/* Toshiba CB35 Chromebook (Celeron 2955U) */
	{ 0x0a06, 0x1179, 0x0a88, PARITY_QUIRK_BACKLIGHT_PRESENT, "backlight present" },

	/* HP Chromebook 14 (Celeron 2955U) */
	{ 0x0a06, 0x103c, 0x21ed, PARITY_QUIRK_BACKLIGHT_PRESENT, "backlight present" },

	/* Dell Chromebook 11 */
	{ 0x0a06, 0x1028, 0x0a35, PARITY_QUIRK_BACKLIGHT_PRESENT, "backlight present" },

	/* Dell Chromebook 11 (2015 version) */
	{ 0x0a16, 0x1028, 0x0a35, PARITY_QUIRK_BACKLIGHT_PRESENT, "backlight present" },

	/* Toshiba Satellite P50-C-18C */
	{ 0x191B, 0x1179, 0xF840, PARITY_QUIRK_INCREASE_T12_DELAY, "T12 delay" },

	/* GeminiLake NUC */
	{ 0x3185, 0x8086, 0x2072, PARITY_QUIRK_INCREASE_DDI_DISABLED_TIME, "Increase DDI disabled time" },
	{ 0x3184, 0x8086, 0x2072, PARITY_QUIRK_INCREASE_DDI_DISABLED_TIME, "Increase DDI disabled time" },
	/* ASRock ITX*/
	{ 0x3185, 0x1849, 0x2212, PARITY_QUIRK_INCREASE_DDI_DISABLED_TIME, "Increase DDI disabled time" },
	{ 0x3184, 0x1849, 0x2212, PARITY_QUIRK_INCREASE_DDI_DISABLED_TIME, "Increase DDI disabled time" },
	/* ECS Liva Q2 */
	{ 0x3185, 0x1019, 0xa94d, PARITY_QUIRK_INCREASE_DDI_DISABLED_TIME, "Increase DDI disabled time" },
	{ 0x3184, 0x1019, 0xa94d, PARITY_QUIRK_INCREASE_DDI_DISABLED_TIME, "Increase DDI disabled time" },
	/* HP Notebook - 14-r206nv */
	{ 0x0f31, 0x103c, 0x220f, PARITY_QUIRK_INVERT_BRIGHTNESS, "inverted panel brightness" }
};

/*
 * intel_dmi_quirks[]: two entries, each a DMI system-id list plus a hook.  The
 * matching needs a DMI backend (dmi_check_system); this port has none, so the
 * list is walked and the absence is RECORDED rather than silently skipped.
 */
static const int parity_dmi_quirk_hooks[] = {
	PARITY_QUIRK_INVERT_BRIGHTNESS,            /* NCR / Thundersoft TST178 */
	PARITY_QUIRK_NO_PPS_BACKLIGHT_POWER_HOOK   /* Google Lillipup sku524294/5 */
};

static void
intel_set_quirk(struct parity_display_state *d, int quirk, const char *msg)
{
	d->quirk_mask |= (1u << (unsigned)quirk);
	d->quirk_hooks_fired++;
	kern_logf("i915: parity applying %s quirk\n", msg);
}

void
parity_intel_init_quirks(struct parity_display_state *d, uint16_t device,
	uint16_t subsystem_vendor, uint16_t subsystem_device)
{
	unsigned i;

	for (i = 0u; i < (unsigned)(sizeof(parity_quirks) / sizeof(parity_quirks[0])); i++) {
		const struct parity_quirk *q = &parity_quirks[i];

		if ((int)device == q->device &&
		    ((int)subsystem_vendor == q->subsystem_vendor ||
		     q->subsystem_vendor == PARITY_PCI_ANY_ID) &&
		    ((int)subsystem_device == q->subsystem_device ||
		     q->subsystem_device == PARITY_PCI_ANY_ID))
			intel_set_quirk(d, q->quirk, q->msg);
	}

	d->dmi_scanned = 1;
	d->dmi_available = (parity_display_state_test_dmi_match != 0) ? 1 : 0;
	for (i = 0u; i < (unsigned)(sizeof(parity_dmi_quirk_hooks) /
				    sizeof(parity_dmi_quirk_hooks[0])); i++) {
		/*
		 * dmi_check_system(*intel_dmi_quirks[i].dmi_id_list) != 0.
		 * With no DMI backend this cannot match; the test hook selects a
		 * 1-based entry so the hook path itself stays exercisable.
		 */
		if (parity_display_state_test_dmi_match == (int)(i + 1u))
			intel_set_quirk(d, parity_dmi_quirk_hooks[i], "DMI");
	}
}

/* ---------------------------------------------------------------- *
 * intel_fbc.c: intel_fbc_init()
 * ---------------------------------------------------------------- */

static void
fbc_underrun_work_fn(void *ctx)
{
	(void)ctx;
	/*
	 * intel_fbc_underrun_work_fn() disables FBC after an FIFO underrun.  It is
	 * only ever queued from the display IRQ path (P4+), which does not exist
	 * yet; if it somehow runs, say so rather than appearing to have worked.
	 */
	kern_logf("i915: parity intel_fbc_underrun_work_fn: UNIMPLEMENTED (queued unexpectedly)\n");
}

/* need_fbc_vtd_wa(): WaFbcTurnOffFbcWhenHyperVisorIsUsed:skl,bxt */
static int
need_fbc_vtd_wa(int legacy_platform, int vtd_active)
{
	if (vtd_active != 0 &&
	    (legacy_platform == PARITY_PLAT_SKYLAKE ||
	     legacy_platform == PARITY_PLAT_BROXTON)) {
		kern_logf("i915: parity Disabling framebuffer compression (FBC) to "
			"prevent screen flicker with VT-d enabled\n");
		return 1;
	}

	return 0;
}

int
parity_intel_sanitize_fbc_option(int display_ver, int legacy_platform,
	unsigned fbc_mask, int enable_fbc_param)
{
	if (enable_fbc_param >= 0)
		return (enable_fbc_param != 0) ? 1 : 0;

	/* HAS_FBC(i915) == DISPLAY_RUNTIME_INFO(i915)->fbc_mask != 0 */
	if (fbc_mask == 0u)
		return 0;

	if (legacy_platform == PARITY_PLAT_BROADWELL || display_ver >= 9)
		return 1;

	return 0;
}

static struct parity_fbc *
intel_fbc_create(struct parity_display_state *d, int display_ver, int fbc_id)
{
	struct parity_fbc *fbc;

	if (fbc_id < 0 || fbc_id >= PARITY_MAX_FBCS)
		return 0;

	fbc = &d->fbc_store[fbc_id];
	zero_mem(fbc, (unsigned)sizeof(*fbc));   /* kzalloc */

	fbc->id = fbc_id;
	parity_kwork_init(&fbc->underrun_work, fbc_underrun_work_fn, fbc);
	(void)mutex_init(&fbc->lock, LOCK_RANK_DEVICE, "parity-fbc");
	fbc->lock_inited = 1;

	if (display_ver >= 7)
		fbc->funcs_kind = PARITY_FBC_FUNCS_IVB;
	else if (display_ver == 6)
		fbc->funcs_kind = PARITY_FBC_FUNCS_SNB;
	else if (display_ver == 5)
		fbc->funcs_kind = PARITY_FBC_FUNCS_ILK;
	else if (display_ver == 4)
		fbc->funcs_kind = PARITY_FBC_FUNCS_I965;
	else
		fbc->funcs_kind = PARITY_FBC_FUNCS_I8XX;

	fbc->in_use = 1;
	return fbc;
}

void
parity_intel_fbc_init(struct parity_display_state *d, int display_ver,
	unsigned fbc_mask, int vtd_active, int legacy_platform)
{
	unsigned fbc_id;

	d->fbc_mask = fbc_mask;

	if (need_fbc_vtd_wa(legacy_platform, vtd_active)) {
		d->fbc_mask = 0u;       /* DISPLAY_RUNTIME_INFO(i915)->fbc_mask = 0 */
		d->fbc_vtd_wa = 1;
	}

	d->enable_fbc_sanitized = parity_intel_sanitize_fbc_option(display_ver,
		legacy_platform, d->fbc_mask, d->enable_fbc_param);
	kern_logf("i915: parity Sanitized enable_fbc value: %d\n",
		d->enable_fbc_sanitized);

	/* for_each_fbc_id(): every bit set in fbc_mask */
	for (fbc_id = 0u; fbc_id < (unsigned)PARITY_MAX_FBCS; fbc_id++) {
		if ((d->fbc_mask & (1u << fbc_id)) == 0u)
			continue;
		d->fbc[fbc_id] = intel_fbc_create(d, display_ver, (int)fbc_id);
		if (d->fbc[fbc_id] != 0)
			d->fbc_created++;
	}
}

/* ---------------------------------------------------------------- *
 * Teardown
 * ---------------------------------------------------------------- */

void
parity_intel_display_state_fini(struct parity_display_state *d)
{
	unsigned i;

	/*
	 * intel_atomic_global_obj_cleanup(): walk the list, drop each object's
	 * state reference and unlink it.  Storage is device-owned, so only the
	 * bookkeeping is undone here.
	 */
	{
		struct parity_global_obj *obj = d->obj_list_head;

		while (obj != 0) {
			struct parity_global_obj *next = obj->next;

			if (obj->state != 0) {
				if (obj->state->ref > 0u)
					obj->state->ref--;
				obj->state = 0;
			}
			obj->next = 0;
			obj = next;
		}
		d->obj_list_head = 0;
		d->obj_list_tail = 0;
		d->obj_count = 0u;
	}

	for (i = 0u; i < (unsigned)PARITY_MAX_FBCS; i++) {
		if (d->fbc[i] != 0) {
			d->fbc[i]->in_use = 0;
			d->fbc[i] = 0;
		}
	}
	d->fbc_created = 0u;
	d->inited = 0;
}
