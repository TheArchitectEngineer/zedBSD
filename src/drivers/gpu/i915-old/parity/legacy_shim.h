/*
 * WS031 V0 -- the bridge of the resident mode between the GPU-core ops layer (i915.c, vk/) and the
 * Linux-parity GT stack.
 *
 * MINIMAL CONNECTION, HAPPY PATH ONLY (E-127).  The ops layer and the Vulkan executor were written
 * against the objects of the legacy driver.  Of those, gem.c, ppgtt.c and the queueing of request.c
 * are plain memory and stay as they are; what touches hardware is the context image, the ring and
 * the ELSP submit.  Only that part is redirected here, by name, in the translation units that ask
 * for it (PARITY_SHIM_REDIRECT) -- the legacy files themselves are not edited.
 *
 * XXX: everything below is for the bring-up of one path.  Reset, recovery, BCS0, preemption and
 * ring wrap are entry points that log and fail.
 */
#ifndef PARITY_LEGACY_SHIM_H
#define PARITY_LEGACY_SHIM_H

#include <stdint.h>

struct i915_device;
struct i915_engine;
struct i915_ppgtt;
struct i915_context;
struct i915_session;

int parity_shim_lrc_create(struct i915_device *device, struct i915_engine *engine,
	struct i915_ppgtt *vm, uint32_t sw_id, struct i915_context *context);
void parity_shim_lrc_destroy(struct i915_device *device, struct i915_context *context);
void parity_shim_request_kick(struct i915_engine *engine);
int parity_shim_engine_reset(struct i915_engine *engine);
int parity_shim_engine_recover(struct i915_engine *engine, struct i915_session *session, int error);
int parity_shim_gt_reset(struct i915_device *device);

/*
 * E-127: runs one PPGTT batch of `context` to its end on the render engine and returns how it ended
 * (0, ETIMEDOUT for a hang -- XXX no recovery follows --, ENODEV outside resident mode).  The caller sleeps.
 */
int parity_shim_run_sync(struct i915_device *device, struct i915_context *context, uint64_t batch_va);

#ifdef PARITY_SHIM_REDIRECT
#define drv_i915_lrc_create      parity_shim_lrc_create
#define drv_i915_lrc_destroy     parity_shim_lrc_destroy
#define drv_i915_request_kick    parity_shim_request_kick
#define drv_i915_engine_reset    parity_shim_engine_reset
#define drv_i915_engine_recover  parity_shim_engine_recover
#define drv_i915_gt_reset        parity_shim_gt_reset
#endif

#endif /* PARITY_LEGACY_SHIM_H */
