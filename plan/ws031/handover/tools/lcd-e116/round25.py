#!/usr/bin/env python3
"""WS031 E-116 round 25: include fixes after the first kernel build. usage: round25.py <repo root>"""
import sys
NL = chr(10)
root = sys.argv[1].rstrip("/") + "/"
L = "src/drivers/gpu/i915/parity/lcd/"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

s = load(L + "parity_lcd_observe.c")
s = rep(s, '#include "lcd_modeset_compat.h"' + NL + '#include "parity_lcd_observe.h"',
        '#include "lcd_modeset_compat.h"' + NL + '#include "lcd_ddi_regs.h"           /* reference, extracted: TRANSCONF */' + NL + '#include "parity_lcd_observe.h"')
save(L + "parity_lcd_observe.c", s)
s = load(L + "parity_lcd_regs.c")
s = rep(s, '#include "lcd_mreg_backlight.h"', '#include "lcd_mreg_backlight.h"' + NL + '#include "lcd_ddi_regs.h"')
s = rep(s, '	ROW("BLC_PWM_PCH_CTL1", BLC_PWM_PCH_CTL1, 0, 0u, 0u);' + NL + '	ROW("BLC_PWM_PCH_CTL2", BLC_PWM_PCH_CTL2, 1, 0x00017700u, 0xffffffffu);',
        '	ROW("BXT_BLC_PWM_CTL", BXT_BLC_PWM_CTL(0), 0, 0u, 0u);' + NL +
        '	/* 0xc8254: the dump tool labels it BLC_PWM_PCH_CTL2; the CNP backlight code of the reference names it BXT_BLC_PWM_FREQ(0) */' + NL +
        '	ROW("BXT_BLC_PWM_FREQ", BXT_BLC_PWM_FREQ(0), 1, 0x00017700u, 0xffffffffu);' + NL +
        '	ROW("BXT_BLC_PWM_DUTY", BXT_BLC_PWM_DUTY(0), 0, 0u, 0u);')
save(L + "parity_lcd_regs.c", s)
s = load(L + "parity_lcd_kernel.c")
s = rep(s, '#include "../osdep/osdep.h"', '#include "../osdep/mmio.h"')
save(L + "parity_lcd_kernel.c", s)
s = load(L + "parity_lcd_show.c")
s = rep(s, "	unsigned disable_from, waited;" + NL + "	int rc;" + NL, "	unsigned disable_from, waited;" + NL)
save(L + "parity_lcd_show.c", s)
