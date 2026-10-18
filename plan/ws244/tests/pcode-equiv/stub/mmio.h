/* Host stand-in for the i915 MMIO window of the PCODE equivalence test. */
#ifndef PCODE_EQUIV_MMIO_H
#define PCODE_EQUIV_MMIO_H
#include <stdint.h>
struct i915_mmio;
uint32_t drv_i915_raw_read32(struct i915_mmio *mmio, uint32_t offset);
void drv_i915_raw_write32(struct i915_mmio *mmio, uint32_t offset, uint32_t value);
#endif
