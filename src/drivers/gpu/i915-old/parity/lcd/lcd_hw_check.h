/* WS031 Linux-parity — the scanout buffer on the real GGTT (lcd_hw_check.c). */
#ifndef PARITY_LCD_HW_CHECK_H
#define PARITY_LCD_HW_CHECK_H

struct parity_gt_mem;
/* Display test configuration only.  No plane / pipe / link register is touched.  0 = PASS. */
int parity_lcd_scanout_hw_check(struct parity_gt_mem *gm);

#endif /* PARITY_LCD_HW_CHECK_H */
