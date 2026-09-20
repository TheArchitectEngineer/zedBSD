/* WS031 Linux-parity -- GPU-free checks of the LCD-G release contract (lcdg_ktest.c). */
#ifndef PARITY_LCDG_KTEST_H
#define PARITY_LCDG_KTEST_H

#include <stdint.h>

struct drv_dma_device;
typedef void (*parity_lcdg_ktest_check)(int ok, const char *msg);
void parity_lcdg_ktest(parity_lcdg_ktest_check check, struct drv_dma_device *dma, uint64_t dma_mask);

#endif /* PARITY_LCDG_KTEST_H */
