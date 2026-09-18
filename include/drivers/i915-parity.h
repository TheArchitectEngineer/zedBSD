/*
 * WS031 Linux-parity — boot readiness hook (public).
 *
 * Called once from the boot path, after the kernel execution base is ready
 * (regular threads runnable, timer/deadline wakeups delivered, VFS up), and
 * before starting the init process.  Starts the managed parity runner thread if
 * a parity device registered during attach; returns without waiting.  A no-op
 * when parity is disabled or nothing registered.
 */
#ifndef DRIVERS_I915_PARITY_H
#define DRIVERS_I915_PARITY_H

void drv_i915_parity_runner_start(void);

#endif /* DRIVERS_I915_PARITY_H */
