/*
 * WS031 V0 -- resident mode: the parity probe stays up and serves /dev/gpuN (E-127).
 *
 * The probe does not return while it serves: it publishes the GPU node from inside
 * drv_i915_parity_attach(), executes the requests the ops layer queues, and when told to stop it
 * falls into the teardown every test mode already uses.  Nothing the probe holds on its stack or in
 * its statics has to move for that.
 */
#ifndef PARITY_RESIDENT_H
#define PARITY_RESIDENT_H

#include <stdint.h>

#ifndef PARITY_RESIDENT
#define PARITY_RESIDENT 0            /* build with -DPARITY_RESIDENT=1 to serve instead of stopping */
#endif
#ifndef PARITY_RESIDENT_DISPLAY
#define PARITY_RESIDENT_DISPLAY 0    /* E-129: the panel is a Vulkan display of the published node (implies the explicit VBT) */
#endif

struct i915_device;
struct osdep_mmio;
struct spinlock;
struct parity_gt_mem;
struct parity_gt_engines;
struct parity_lcd_kernel_deps;

struct parity_resident_ctx {
	struct i915_device *device;
	struct osdep_mmio *mmio;
	struct spinlock *uncore_lock;
	struct parity_gt_mem *gm;
	struct parity_gt_engines *es;
	const struct parity_lcd_kernel_deps *lcd;	/* E-129: the panel (0 = the node has no display) */
};

/* Publishes the GPU node, serves until asked to stop, withdraws the node.  Forcewake is held by the caller. */
int parity_resident_serve(struct parity_resident_ctx *ctx);

/* i915.c: the software half of the legacy start path + drv_gpu_register / unregister. */
int drv_i915_resident_publish(struct i915_device *device);
int drv_i915_resident_unpublish(struct i915_device *device);

/*
 * E-129: the panel behind the node's display operations (resident_display.c).  The frames are shown by the serving
 * thread: the first presentation lights the panel (the LCD-C body), later ones flip, the release stops it through the
 * reference's stop path.  Each call below sleeps until the serving thread has done it.
 */
struct i915_device;
int parity_shim_display_present(struct i915_device *device, const void *pixels, uint32_t width, uint32_t height,
	uint32_t stride, int bgra);
int parity_shim_display_release(struct i915_device *device);

/*
 * E-130: one frame the GPU copies into the panel's back buffer.  The serving thread maps both panel buffers into
 * `vm` (once), asks `build` for a batch that writes the frame into the back buffer (dst_va is the buffer's address
 * in `vm`), runs it in `context` and flips.  No CPU touches the pixels.
 */
struct i915_context;
struct i915_ppgtt;
typedef int (*parity_shim_blit_fn)(void *ctx, uint64_t dst_va, uint32_t width, uint32_t height, uint32_t pitch,
	uint64_t *batch_va);
/* the frame as the CPU sees it (for the check of the first frames), NULL if unknown */
const uint32_t *parity_shim_blit_source(void *ctx, uint32_t *width, uint32_t *height, uint32_t *pitch);
int parity_shim_display_present_blob(struct i915_device *device, struct i915_context *context, struct i915_ppgtt *vm,
	parity_shim_blit_fn build, void *ctx);
const struct parity_lcd_kernel_deps *parity_shim_display_deps(void);

#endif /* PARITY_RESIDENT_H */
