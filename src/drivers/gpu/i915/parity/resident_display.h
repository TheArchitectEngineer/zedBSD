/*
 * WS031 E-129: the display operations of the resident node (resident_display.c).
 */
#ifndef PARITY_RESIDENT_DISPLAY_H
#define PARITY_RESIDENT_DISPLAY_H

struct i915_device;
struct drv_gpu_display_ops;
struct drv_gpu_scanout_ops;

extern const struct drv_gpu_display_ops drv_i915_resident_display_ops;
extern const struct drv_gpu_scanout_ops drv_i915_resident_scanout_ops;

/* A closing session that still holds the lease gives it back (the panel is stopped first). */
void drv_i915_resident_display_close(struct i915_device *device, void *session);

#endif /* PARITY_RESIDENT_DISPLAY_H */
