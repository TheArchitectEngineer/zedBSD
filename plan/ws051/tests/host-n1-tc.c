/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws051-p004b: the host test of what lets the takeover read out and stop
 * a crtc the firmware left on a Type-C port (src/drivers/gpu/i915/display/
 * takeover.c, ddi.c and modeset-internal.h):
 *
 *   - the registry's walks answer with every encoder and connector it
 *     holds (the bound screen's first, then the Type-C ports'), and with
 *     nothing past the last one or while the registry is not live;
 *   - a disable of a state that walks encoders of its own runs the hook of
 *     each encoder on the crtc, with the connector state of that
 *     encoder's object, and no other encoder's; a state without a walk
 *     runs the world's bound encoder as before;
 *   - the DPCD accessors reach the sink of an AUX channel whose object
 *     names its own access, and the backend's hooks otherwise.
 *
 *   sh plan/ws051/tests/host-n1-tc.sh
 */

#include "drivers/gpu/i915/display/modeset-internal.h"
#include "drivers/gpu/i915/display/takeover-internal.h"
#include "drivers/gpu/i915/display/ddi.h"
#include "drivers/gpu/i915/tests/display/host-test.h"

#include <stdio.h>
#include <string.h>

/* How many hook calls the recorder keeps. */
#define RECORD_MAX	8

/*
 * One encoder hook call the recorder saw: which hook, on which encoder,
 * with which crtc and connector states.
 */
struct record {
	int hook;
	const struct intel_encoder *encoder;
	const struct intel_crtc_state *crtc_state;
	const struct drm_connector_state *conn_state;
};

static void record_disable(struct intel_atomic_state *state, struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state, const struct drm_connector_state *conn_state);
static void record_post_disable(struct intel_atomic_state *state, struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state, const struct drm_connector_state *conn_state);
static void record_post_pll_disable(struct intel_atomic_state *state, struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state, const struct drm_connector_state *conn_state);
static void record_call(int hook, const struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state, const struct drm_connector_state *conn_state);
static struct intel_encoder *test_walk(void *walk, unsigned idx);
static long backend_dpcd_read(void *ctx, unsigned offset, uint8_t *buf, size_t size);
static long own_dpcd_read(void *ctx, unsigned offset, uint8_t *buf, size_t size);
static long own_dpcd_write(void *ctx, unsigned offset, const uint8_t *buf, size_t size);
static int own_read_caps(void *ctx, uint8_t dpcd[15]);
static void bind_object(struct i915_lcd_modeset *ms, struct intel_crtc *crtc);
static void test_registry_walks(void);
static void test_disable_walk(void);
static void test_dpcd_routing(void);

/* The hook calls seen, in order; cleared by each part. */
static struct record records[RECORD_MAX];
static unsigned record_count;

/* The display's modeset world the objects belong to.  Zeroed. */
static struct i915_lcd_world world;

/* The takeover world whose registry the walks read, and its display (the register trace off).  Zeroed. */
static struct i915_takeover_world takeover;
static struct i915_display display;

/* Two modeset objects: the bound screen's (on pipe A) and a Type-C port's (on pipe B). */
static struct i915_lcd_modeset screen;
static struct i915_lcd_modeset tc_object;

/* The walk test_walk() answers with: the two objects' encoders. */
static struct intel_encoder *walk_encoders[2];

/* Which DPCD hook answered, and the offset it was asked. */
static int dpcd_answered;
static unsigned dpcd_offset;

/*
 * Runs the checks and reports the tally.
 */
int
main(int argc, char **argv)
{
	int report;
	int verbose_asked;

	/* -v prints every check. */
	verbose_asked = 0;
	if (argc > 1)
		verbose_asked = (strcmp(argv[1], "-v") == 0);
	if (verbose_asked)
		i915_host_verbose = 1;

	/* Runs each part. */
	test_registry_walks();
	test_disable_walk();
	test_dpcd_routing();

	/* Reports the tally. */
	report = i915_host_report("host-n1-tc");
	return report;
}

/* Records a disable hook call. */
static void
record_disable(
	struct intel_atomic_state *state,
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state,
	const struct drm_connector_state *conn_state)
{
	(void)state;

	/* Keeps the call. */
	record_call(1, encoder, crtc_state, conn_state);
}

/* Records a post-disable hook call. */
static void
record_post_disable(
	struct intel_atomic_state *state,
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state,
	const struct drm_connector_state *conn_state)
{
	(void)state;

	/* Keeps the call. */
	record_call(2, encoder, crtc_state, conn_state);
}

/* Records a post-PLL-disable hook call. */
static void
record_post_pll_disable(
	struct intel_atomic_state *state,
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state,
	const struct drm_connector_state *conn_state)
{
	(void)state;

	/* Keeps the call. */
	record_call(3, encoder, crtc_state, conn_state);
}

/* Keeps one hook call, while there is room. */
static void
record_call(
	int hook,
	const struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state,
	const struct drm_connector_state *conn_state)
{
	/* A full recorder drops the call (the checks count the calls). */
	if (record_count >= RECORD_MAX)
		return;

	/* Keeps the call. */
	records[record_count].hook = hook;
	records[record_count].encoder = encoder;
	records[record_count].crtc_state = crtc_state;
	records[record_count].conn_state = conn_state;
	record_count++;
}

/* Answers the test's walk: the two objects' encoders, then nothing. */
static struct intel_encoder *
test_walk(
	void *walk,
	unsigned idx)
{
	(void)walk;

	/* Nothing past the second encoder. */
	if (idx >= 2u)
		return NULL;

	/* The encoder of the index. */
	return walk_encoders[idx];
}

/* The backend's DPCD read: records that the backend answered. */
static long
backend_dpcd_read(
	void *ctx,
	unsigned offset,
	uint8_t *buf,
	size_t size)
{
	(void)ctx;

	/* Answers with zeros. */
	dpcd_answered = 1;
	dpcd_offset = offset;
	memset(buf, 0, size);
	return (long)size;
}

/* The channel's own DPCD read: records that the channel answered. */
static long
own_dpcd_read(
	void *ctx,
	unsigned offset,
	uint8_t *buf,
	size_t size)
{
	(void)ctx;

	/* Answers with 0x14. */
	dpcd_answered = 2;
	dpcd_offset = offset;
	memset(buf, 0x14, size);
	return (long)size;
}

/* The channel's own DPCD write: records that the channel took it. */
static long
own_dpcd_write(
	void *ctx,
	unsigned offset,
	const uint8_t *buf,
	size_t size)
{
	(void)ctx;
	(void)buf;

	/* Takes the bytes. */
	dpcd_answered = 3;
	dpcd_offset = offset;
	return (long)size;
}

/* The channel's own capabilities: records that the channel answered. */
static int
own_read_caps(
	void *ctx,
	uint8_t dpcd[15])
{
	(void)ctx;

	/* Answers DPCD 1.4. */
	dpcd_answered = 4;
	memset(dpcd, 0, 15);
	dpcd[0] = 0x14;
	return 0;
}

/* Binds a modeset object's encoder to the recorders, in the world, on a crtc. */
static void
bind_object(
	struct i915_lcd_modeset *ms,
	struct intel_crtc *crtc)
{
	/* The object's world and the recorders as its disable hooks. */
	ms->world = &world;
	ms->dig_port.base.disable = record_disable;
	ms->dig_port.base.post_disable = record_post_disable;
	ms->dig_port.base.post_pll_disable = record_post_pll_disable;

	/* The encoder is on the crtc. */
	ms->dig_port.base.base.crtc = &crtc->base;
}

/* The registry's walks answer with what it holds, in order, and nothing past it. */
static void
test_registry_walks(void)
{
	struct i915_n1_registry *n1;

	/* A registry that is not live answers nothing. */
	takeover.display = &display;
	n1 = &takeover.n1;
	n1->encoders[0] = &screen.dig_port.base;
	n1->encoders[1] = &tc_object.dig_port.base;
	n1->connectors[0] = &screen.connector;
	n1->connectors[1] = &tc_object.connector;
	n1->encoder_count = 2u;
	i915_host_check(drv_i915_n1_encoder_at(&takeover, 0u) == NULL, "registry: no encoder while not live");
	i915_host_check(drv_i915_n1_connector_at(&takeover, 0u) == NULL, "registry: no connector while not live");

	/* A live registry answers the bound screen's first, then the Type-C port's, then nothing. */
	n1->live = 1;
	i915_host_check(drv_i915_n1_encoder_at(&takeover, 0u) == &screen.dig_port.base, "registry: encoder 0 is the bound screen's");
	i915_host_check(drv_i915_n1_encoder_at(&takeover, 1u) == &tc_object.dig_port.base, "registry: encoder 1 is the Type-C port's");
	i915_host_check(drv_i915_n1_encoder_at(&takeover, 2u) == NULL, "registry: nothing past the last encoder");
	i915_host_check(drv_i915_n1_connector_at(&takeover, 1u) == &tc_object.connector, "registry: connector 1 is the Type-C port's");
	i915_host_check(drv_i915_n1_connector_at(&takeover, 2u) == NULL, "registry: nothing past the last connector");

	/* Leaves the registry empty. */
	memset(n1, 0, sizeof(*n1));
}

/* A disable of a state with its own walk runs the hooks of the encoders on the crtc only. */
static void
test_disable_walk(void)
{
	struct intel_atomic_state state;
	struct intel_crtc crtc_a;
	struct intel_crtc crtc_b;
	struct intel_crtc_state old_crtc_state;

	/* The screen on pipe A, the Type-C port on pipe B. */
	memset(&crtc_a, 0, sizeof(crtc_a));
	memset(&crtc_b, 0, sizeof(crtc_b));
	crtc_a.pipe = PIPE_A;
	crtc_b.pipe = PIPE_B;
	bind_object(&screen, &crtc_a);
	bind_object(&tc_object, &crtc_b);
	walk_encoders[0] = &screen.dig_port.base;
	walk_encoders[1] = &tc_object.dig_port.base;
	world.ddi_ms = &screen;

	/* A state that walks the two encoders, with an old crtc state. */
	memset(&state, 0, sizeof(state));
	memset(&old_crtc_state, 0, sizeof(old_crtc_state));
	state.world = &world;
	state.old_crtc_state = &old_crtc_state;
	state.encoder_at = test_walk;
	state.walk = NULL;

	/* Disabling pipe B runs the Type-C port's three hooks only, with its connector state. */
	record_count = 0u;
	drv_i915_encoders_disable(&state, &crtc_b);
	drv_i915_encoders_post_disable(&state, &crtc_b);
	drv_i915_encoders_post_pll_disable(&state, &crtc_b);
	i915_host_check(record_count == 3u, "walk: pipe B's disable ran three hooks");
	i915_host_check(records[0].hook == 1 && records[1].hook == 2 && records[2].hook == 3, "walk: disable, post-disable, post-PLL-disable in order");
	i915_host_check(records[0].encoder == &tc_object.dig_port.base && records[2].encoder == &tc_object.dig_port.base, "walk: the hooks ran on the Type-C port's encoder");
	i915_host_check(records[1].conn_state == &tc_object.conn_state, "walk: with the Type-C port's connector state");
	i915_host_check(records[0].crtc_state == &old_crtc_state, "walk: with the state's old crtc state");

	/* Disabling pipe A runs the screen's hook only. */
	record_count = 0u;
	drv_i915_encoders_disable(&state, &crtc_a);
	i915_host_check(record_count == 1u && records[0].encoder == &screen.dig_port.base, "walk: pipe A's disable ran the screen's hook only");

	/* A pipe no encoder is on runs nothing. */
	record_count = 0u;
	tc_object.dig_port.base.base.crtc = NULL;
	drv_i915_encoders_disable(&state, &crtc_b);
	i915_host_check(record_count == 0u, "walk: a pipe with no encoder on it runs no hook");

	/* Without a walk the world's bound encoder runs, whatever the crtc (as before). */
	state.encoder_at = NULL;
	record_count = 0u;
	drv_i915_encoders_post_disable(&state, &crtc_b);
	i915_host_check(record_count == 1u && records[0].encoder == &screen.dig_port.base, "walk: without a walk the bound screen's hook runs");
	i915_host_check(records[0].conn_state == &screen.conn_state, "walk: with the bound screen's connector state");

	/* Leaves the world unbound. */
	world.ddi_ms = NULL;
}

/* The DPCD accessors reach the channel's own sink when its object names one, the backend's otherwise. */
static void
test_dpcd_routing(void)
{
	static const struct i915_lcd_aux_emit own = {
		NULL,
		own_dpcd_read,
		own_dpcd_write,
		own_read_caps
	};
	struct drm_i915_private i915;
	struct i915_lcd_emit emit;
	uint8_t buffer[4];
	uint8_t caps[15];
	long transferred;
	int error;

	/* A device whose backend reads DPCD (and has no capability hook). */
	memset(&i915, 0, sizeof(i915));
	memset(&emit, 0, sizeof(emit));
	emit.dpcd_read = backend_dpcd_read;
	i915.emit = &emit;

	/* An object without a channel of its own reads through the backend. */
	screen.aux_emit = NULL;
	dpcd_answered = 0;
	transferred = I915_LCD_DRM_DP_DPCD_READ(&i915, &screen.dig_port.dp.aux, 0x202u, buffer, sizeof(buffer));
	i915_host_check(transferred == 4 && dpcd_answered == 1 && dpcd_offset == 0x202u, "dpcd: the panel's object reads through the backend");

	/* The backend has no capability hook: an object without its own channel is refused. */
	error = I915_LCD_DRM_DP_READ_DPCD_CAPS(&i915, &screen.dig_port.dp.aux, caps);
	i915_host_check(error == I915_LCD_EIO, "dpcd: without a backend capability hook the capabilities are refused");

	/* An object with its own channel reads, writes and asks its own sink. */
	tc_object.aux_emit = &own;
	dpcd_answered = 0;
	transferred = I915_LCD_DRM_DP_DPCD_READ(&i915, &tc_object.dig_port.dp.aux, 0x600u, buffer, 1u);
	i915_host_check(transferred == 1 && dpcd_answered == 2 && buffer[0] == 0x14u, "dpcd: the Type-C port's object reads its own sink");
	transferred = I915_LCD_DRM_DP_DPCD_WRITEB(&i915, &tc_object.dig_port.dp.aux, 0x600u, 2u);
	i915_host_check(transferred == 1 && dpcd_answered == 3 && dpcd_offset == 0x600u, "dpcd: the Type-C port's object writes its own sink (DP_SET_POWER)");
	error = I915_LCD_DRM_DP_READ_DPCD_CAPS(&i915, &tc_object.dig_port.dp.aux, caps);
	i915_host_check(error == 0 && dpcd_answered == 4 && caps[0] == 0x14u, "dpcd: the Type-C port's object asks its own sink's capabilities");
	error = I915_LCD_DRM_DP_DPCD_PROBE(&i915, &tc_object.dig_port.dp.aux, 0x0u);
	i915_host_check(error == 0 && dpcd_answered == 2, "dpcd: the Type-C port's probe reads its own sink");

	/* Leaves the objects without channels. */
	tc_object.aux_emit = NULL;
}
