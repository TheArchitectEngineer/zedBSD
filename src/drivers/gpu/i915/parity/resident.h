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

#ifndef PARITY_RESIDENT
#define PARITY_RESIDENT 0            /* build with -DPARITY_RESIDENT=1 to serve instead of stopping */
#endif

struct i915_device;
struct osdep_mmio;
struct spinlock;
struct parity_gt_mem;
struct parity_gt_engines;

struct parity_resident_ctx {
	struct i915_device *device;
	struct osdep_mmio *mmio;
	struct spinlock *uncore_lock;
	struct parity_gt_mem *gm;
	struct parity_gt_engines *es;
};

/* Publishes the GPU node, serves until asked to stop, withdraws the node.  Forcewake is held by the caller. */
int parity_resident_serve(struct parity_resident_ctx *ctx);

/* i915.c: the software half of the legacy start path + drv_gpu_register / unregister. */
int drv_i915_resident_publish(struct i915_device *device);
int drv_i915_resident_unpublish(struct i915_device *device);

#endif /* PARITY_RESIDENT_H */
