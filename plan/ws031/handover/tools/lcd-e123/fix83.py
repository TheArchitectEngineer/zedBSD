#!/usr/bin/env python3
"""E-123 round 83 fixup: designated initialisers for the run parameters (the struct gained fields at its end)."""
import sys
p = sys.argv[1].rstrip("/") + "/src/drivers/gpu/i915/parity/lcd/parity_lcd_kernel.c"
s = open(p).read()
NL = chr(10)
TAB = chr(9)
s = s.replace("caller’s", "caller's")
reps = [
    (TAB + "static const struct lcd_run_params p = { PARITY_LCDB_PATTERN_ID, PARITY_LCDB_PATTERN_FNV," + NL +
     TAB * 2 + "PARITY_LCDB_WINDOW_MS, 0 };",
     TAB + "static const struct lcd_run_params p = { .pattern_id = PARITY_LCDB_PATTERN_ID," + NL +
     TAB * 2 + ".pattern_fnv = PARITY_LCDB_PATTERN_FNV, .window_ms = PARITY_LCDB_WINDOW_MS };"),
    (TAB * 2 + "{ 110u, PARITY_LCDB_PATTERN_FNV, 2000u, window_first },",
     TAB * 2 + "{ .pattern_id = 110u, .pattern_fnv = PARITY_LCDB_PATTERN_FNV, .window_ms = 2000u," + NL +
     TAB * 3 + ".in_window = window_first },"),
    (TAB * 2 + "{ 111u, 0u, 3000u, window_again },",
     TAB * 2 + "{ .pattern_id = 111u, .window_ms = 3000u, .in_window = window_again },"),
    (TAB * 2 + "{ 112u, 0u, 3000u, window_again },",
     TAB * 2 + "{ .pattern_id = 112u, .window_ms = 3000u, .in_window = window_again },"),
]
for o, n in reps:
    if n in s:
        continue
    assert s.count(o) == 1, o[:60]
    s = s.replace(o, n)
open(p, "w").write(s)
print("fixed")
