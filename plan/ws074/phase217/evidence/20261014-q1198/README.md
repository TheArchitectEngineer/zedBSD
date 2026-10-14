# q1198-i01 — framed horizontal Grid second vertical auto-inset child

(Judged by B1 on 2026-10-14 from the evidence Codex left; the Codex session stopped before writing this file.)

The second child of the framed horizontal LTR/RTL 2×2 inline Grid (row 1/column 2, `vertical-rl`, all insets auto, `justify-self:center`, `align-self:end`, framed child) now uses the vertical inline measurement and the Grid-area centred static X. Source change: `layout/grid.c`, `layout/inline.c` (commit 631744a88).

The [minimal page](framed-grid-second-vertical-auto.html) changed from `119,8,58,40|99,8,58,40` ([before](browser-before.layout.txt)) to `165,98,46,52|65,98,46,52` ([after](browser-after.layout.txt)); [independent Chromium](chromium.dom.html) gives `165,98,46,52|65,98,46,52` (exact match). In fixed [non-static positioned 012](wpt-012.json) both horizontal callbacks now pass the first two children and first fail on the fourth (row 2/column 2, vertical-lr RTL: `width expected 56 but got 76`). Previously passing [007](wpt-007.json)–[011](wpt-011.json) remain whole PASS. Same fixed WPT commit, q1012 manifest, fonts, 5 s settle, 20 s bound.

Build warning 0 ([build.log](build.log)); host CSS 51/51, JS 14/14, Grid 48/48 PASS. Acid ([acid.log](acid.log)): Acid3 score 100, inventory 100/100, 0/480,000 different pixels; Acid2 98.45% = 1,865/120,000 different, unchanged from q1197 (measured in /tmp/q1198-acid/report.json).

Outcome: cleared (finite partial criteria met; B1 reran the build and 012/007 on 2026-10-14 and reproduced the results). p217/p189/p173 whole unmet. Next: the fourth child of 012 (row 2/column 2, vertical-lr RTL). WPT, runner, ABI and Master unchanged.
