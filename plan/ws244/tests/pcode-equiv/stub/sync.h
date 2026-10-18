/* Host stand-in for the register wait of the PCODE equivalence test. */
#ifndef PCODE_EQUIV_SYNC_H
#define PCODE_EQUIV_SYNC_H
#include <stdint.h>
struct i915_mmio;
int drv_i915_wait_reg(struct i915_mmio *mmio, uint32_t reg, uint32_t mask, uint32_t value, unsigned fast_us, unsigned slow_ms, uint32_t *last);
#endif
