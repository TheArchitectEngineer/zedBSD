/* WS031 Linux-parity — GPU-free kernel checks of the one-screen LCD modeset (lcd_modeset_ktest.c). */
#ifndef PARITY_LCD_MODESET_KTEST_H
#define PARITY_LCD_MODESET_KTEST_H

typedef void (*parity_lcd_modeset_ktest_check)(int ok, const char *msg);
void parity_lcd_modeset_ktest(parity_lcd_modeset_ktest_check check);

#endif /* PARITY_LCD_MODESET_KTEST_H */
