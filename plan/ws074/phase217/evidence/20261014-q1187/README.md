# q1187-i01 — vertical-rl Grid horizontal RTL absolute area

A direct horizontal-tb/RTL absolute child in row 2/column 1 of a fixed vertical-rl/LTR 2×2 inline Grid now receives its definite Grid area when it has a physical `top` inset. `align-self:center` positions its static X within the second row's 100 px track; the explicit top remains 10 px. Other orthogonal cases retain their existing path.

The [minimal page](vertical-rl-third-horizontal-rtl.html) changed from `0,10,36,10` in the [before layout](browser-before.layout.txt) to `32,10,36,10` in the [after layout](browser-after.layout.txt), matching [independent Chromium](chromium.dom.html). In fixed [non-static positioned 007](wpt-007.json), both horizontal callbacks and the vertical-rl callback are whole PASS. The vertical-lr callback remains FAIL on its fourth child (`offsetLeft expected 155 but got 5`). Previously passing [vertical positioned column 001](wpt-vertical-old.json) remains whole PASS. Fixed WPT commit `2d66b9b7998bb58c336138c178323ddee857b586`, q1012 manifest, localhost HTTP, fixed fonts, 5 s settle, 20 s process bound.

[Warning-free build](build.log), [host CSS](css.log) 51/51, [JS](js.log) 14/14, [Grid](grid.log) 48/48 PASS. [Acid report](acid-report.json): Acid3 score100/inventory100/100 and 0/480,000 different pixels; Acid2 1,865/120,000 differences, unchanged. `git diff --check` PASS; `clang-format` unavailable.

q1187-i01 cleared its finite partial criteria; p217/p189/p173 whole remain unmet. Next map the fourth horizontal RTL child in the vertical-lr Grid callback. WPT, runner, public ABI and Master unchanged; shared projection/GitHub publication pending with Q1, no push.
