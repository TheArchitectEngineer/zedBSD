/* WS031 Linux-parity — GPU-free kernel checks of the scanout buffer (scanout_ktest.c). */
#ifndef PARITY_SCANOUT_KTEST_H
#define PARITY_SCANOUT_KTEST_H

#include <stdint.h>

struct drv_dma_device;
typedef void (*parity_scanout_ktest_check)(int ok, const char *msg);
void parity_scanout_ktest(parity_scanout_ktest_check check, struct drv_dma_device *dma, uint64_t dma_mask);

#endif /* PARITY_SCANOUT_KTEST_H */
