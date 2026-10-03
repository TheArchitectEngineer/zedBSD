/*
 * WS031 Linux-parity — deferred runner (internal).
 *
 * The i915 attach runs in the boot device-probe context, where a timer-driven
 * wait-queue sleep is not reliable (the NVMe driver documents the same and polls
 * there).  So parity does NOT run its probe from attach: attach only REGISTERS
 * the device, and a readiness hook (drv_i915_parity_runner_start, called from
 * boot once the execution base is ready) starts a managed kernel thread that runs
 * the GPU-free concurrency tests and then the parity P0..P2 probe — once.
 */
#ifndef PARITY_RUNNER_H
#define PARITY_RUNNER_H

struct i915_device;

/* Early side: record the parity device for deferred execution (no probe here). */
void drv_i915_parity_runner_register(struct i915_device *device);

#endif /* PARITY_RUNNER_H */
