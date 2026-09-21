/* WS031 Linux-parity -- GPU-free kernel checks of LCD-B's production body with a real scanout object (lcd_show_ktest.c). */
#ifndef PARITY_LCD_SHOW_KTEST_H
#define PARITY_LCD_SHOW_KTEST_H

#include <stdint.h>

struct drv_dma_device;
typedef void (*parity_lcd_show_ktest_check)(int ok, const char *msg);
void parity_lcd_show_ktest(parity_lcd_show_ktest_check check, struct drv_dma_device *dma, uint64_t dma_mask);

#endif /* PARITY_LCD_SHOW_KTEST_H */
