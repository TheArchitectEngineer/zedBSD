#!/usr/bin/env python3
"""WS031 E-119 round 44: register lcdg_ktest.c; show_ktest: a prepare that refuses leaves display_acquired = 0 and the
buffer PINNED with its owner.  usage: round44.py <repo root> <dir with lcdg_ktest.c>"""
import sys, shutil
NL = chr(10)
root = sys.argv[1].rstrip("/") + "/"
src = sys.argv[2].rstrip("/") + "/"
P = "src/drivers/gpu/i915/parity/"
L = P + "lcd/"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:100]
    return s.replace(old, new)

shutil.copy(src + "lcdg_ktest.c", root + L + "lcdg_ktest.c")
open(root + L + "lcdg_ktest.h", "w").write("""/* WS031 Linux-parity -- GPU-free checks of the LCD-G release contract (lcdg_ktest.c). */
#ifndef PARITY_LCDG_KTEST_H
#define PARITY_LCDG_KTEST_H

#include <stdint.h>

struct drv_dma_device;
typedef void (*parity_lcdg_ktest_check)(int ok, const char *msg);
void parity_lcdg_ktest(parity_lcdg_ktest_check check, struct drv_dma_device *dma, uint64_t dma_mask);

#endif /* PARITY_LCDG_KTEST_H */
""")
mk = load("platform/amd64/vmunix.mk")
mk = rep(mk, "src/drivers/gpu/i915/parity/lcd/lcd_show_ktest.c ", "src/drivers/gpu/i915/parity/lcd/lcd_show_ktest.c src/drivers/gpu/i915/parity/lcd/lcdg_ktest.c ")
save("platform/amd64/vmunix.mk", mk)
k = load(P + "ktest.c")
k = rep(k, '#include "lcd/lcd_show_ktest.h"', '#include "lcd/lcd_show_ktest.h"' + NL + '#include "lcd/lcdg_ktest.h"')
k = rep(k, "			parity_lcd_show_ktest(edp_ktest_check, c0_dma, c0_mask);" + NL,
        "			parity_lcd_show_ktest(edp_ktest_check, c0_dma, c0_mask);" + NL +
        "			/* LCD-G's release contract: TLB, mappings, retained GPU state, reclaim when never shown */" + NL +
        "			parity_lcdg_ktest(edp_ktest_check, c0_dma, c0_mask);" + NL)
save(P + "ktest.c", k)

sk = load(L + "lcd_show_ktest.c")
sk = rep(sk, '		check(parity_scanout_unpin(&so_p) == 0 && parity_scanout_destroy(&so_p) == 0 && edp_released(),' + NL +
         '			"lcd-g: G-PREPARED the OWNER releases it afterwards");',
         '''		check(parity_scanout_unpin(&so_p) == 0 && parity_scanout_destroy(&so_p) == 0 && edp_released(),
			"lcd-g: G-PREPARED the OWNER releases it afterwards");

		/* the display part refuses at its check phase: it never acquired the buffer, which stays with its owner */
		rc = bring_up(&so_p);
		rc2 = parity_scanout_create(&gm, 1920u, 1080u, PARITY_FOURCC_XRGB8888, PARITY_MOD_LINEAR, &so_p);
		(void)parity_scanout_pin(&so_p, "owner");
		show.cfg.qgv_allowed_bw = 0u;                   /* the check phase refuses an unknown memory bandwidth */
		live = gm.objects_live;
		{
			unsigned arms = lcd.plane_arms, writes = lcd.nregs;

			rc2 = parity_lcd_show_prepared(&show, &so_p, 0, 0, &rep);
			check(rc2 != 0 && rep.display_acquired == 0 && rep.display_released == 0 && !rep.abandoned && rep.prepare_rc != 0 &&
				so_p.state == PARITY_SCANOUT_PINNED && lcd.plane_arms == arms && lcd.nregs == writes && !parity_lcd_show_retained() &&
				gm.objects_live == live,
				"lcd-g: G-NOTSTARTED prepare refused: display_acquired=0 (not 'stop confirmed'), no register written, nothing retained");
		}
		check(parity_scanout_unpin(&so_p) == 0 && parity_scanout_destroy(&so_p) == 0 && edp_released(),
			"lcd-g: G-NOTSTARTED the owner reclaims the buffer at once, and the next run is not blocked");''')
save(L + "lcd_show_ktest.c", sk)
print("done")
