#!/usr/bin/env python3
"""WS031 E-122 round 72: LCD-O expectation per the reference clamp_user_to_hw(): scale(level, 0..user_max -> 0..max)
then clamp to [min, max] (the earlier test assumed scale into [min, max]: the test was wrong, the hardware matched the
reference).  A level below the clamp (10/255) is added.  usage: round72.py <repo root>"""
import sys
root = sys.argv[1].rstrip("/") + "/"
p = root + "src/drivers/gpu/i915/parity/lcd/parity_lcd_kernel.c"
s = open(p).read()
def rep(old, new):
    global s
    assert s.count(old) == 1, old[:80]
    s = s.replace(old, new)
rep("""/* the reference scale(): source [0, 255] -> target [min, max], DIV_ROUND_CLOSEST */
static uint32_t lcdo_expected_duty(uint32_t level, uint32_t min, uint32_t max)
{
	uint64_t t = (uint64_t)level * (uint64_t)(max - min);

	return (uint32_t)((t + 127u) / 255u) + min;
}""", """/* the reference clamp_user_to_hw(): scale(level, 0, 255, 0, max) (DIV_ROUND_CLOSEST), then clamp to [min, max] */
static uint32_t lcdo_expected_duty(uint32_t level, uint32_t min, uint32_t max)
{
	uint32_t hw = (uint32_t)(((uint64_t)level * (uint64_t)max + 127u) / 255u);

	return hw < min ? min : hw > max ? max : hw;
}""")
rep("static const uint32_t levels[3] = { 64u, 160u, 255u };", "static const uint32_t levels[4] = { 10u, 64u, 160u, 255u };   /* 10: below the clamp */")
rep("for (n = 0u; n < 3u; n++) {", "for (n = 0u; n < 4u; n++) {")
rep("(reference scale to [%u, %u])", "(reference clamp_user_to_hw: scale to [0, %u], clamp at %u)")
rep("lcdo_rd(0x304), lcdo_rd(0x318), want_cblv, lcdo_rc, duty, duty, want, st.backlight_min, st.backlight_max, started,",
    "lcdo_rd(0x304), lcdo_rd(0x318), want_cblv, lcdo_rc, duty, duty, want, st.backlight_max, st.backlight_min, started,")
rep("lcdo_steps_ok == 3 &&", "lcdo_steps_ok == 4 &&")
rep("(ASLE steps %d/3 with the PWM duty = the reference scale,", "(ASLE steps %d/4 with the PWM duty = the reference clamp_user_to_hw,")
open(p, "w").write(s)
print("done")
