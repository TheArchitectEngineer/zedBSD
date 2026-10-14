# q1192-i01 — horizontal Grid second vertical absolute child border

The bounded vertical inline text path now also accepts a border-only vertical-rl absolute child in row 1/column 2 of an unframed horizontal LTR/RTL 2×2 inline Grid with a physical `right` inset. Parent frame, child padding/margin and the row 2 vertical RTL child retain their previous path.

The [minimal LTR/RTL page](horizontal-grid-second-vertical-border.html) changed from `168,100,72,50|68,100,72,50` in the [before layout](browser-before.layout.txt) to `180,88,60,62|80,88,60,62` in the [after layout](browser-after.layout.txt), matching [independent Chromium](chromium.dom.html). In fixed [non-static positioned 009](wpt-009.json), both horizontal Grid callbacks now pass all checks for their first two children; their first failures are the fourth vertical RTL child (`width expected 70 but got 90`). The two vertical callbacks remain whole PASS; the file remains FAIL. Previously passing [007](wpt-007.json) and [008](wpt-008.json) remain whole PASS. Fixed WPT commit `2d66b9b7998bb58c336138c178323ddee857b586`, q1012 manifest, localhost HTTP, fixed fonts, 5 s settle, 20 s process bound.

[Warning-free build](build.log), [host CSS](css.log) 51/51, [JS](js.log) 14/14, [Grid](grid.log) 48/48 PASS. [Acid report](acid-report.json): Acid3 score100/inventory100/100 and 0/480,000 different pixels; Acid2 1,865/120,000 differences, unchanged. `git diff --check` PASS; `clang-format` unavailable.

q1192-i01 cleared its finite partial criteria; p217/p189/p173 whole remain unmet. Next map the fourth vertical RTL border child. WPT, runner, public ABI and Master unchanged; shared projection/GitHub publication pending with Q1, no push.
