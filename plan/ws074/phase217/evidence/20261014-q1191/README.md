# q1191-i01 — horizontal Grid vertical absolute child border

The bounded vertical inline text path now accepts a border-only vertical-lr absolute child in row 1/column 1 of an unframed horizontal LTR/RTL 2×2 inline Grid with a physical `left` inset. Parent frame, child padding/margin and other bordered vertical child placements retain their previous path. The existing `inline_vertical_room` subtraction supplies content room after the child's physical frame.

The [minimal LTR/RTL page](horizontal-grid-vertical-abs-border.html) changed from `5,0,66,30|155,0,66,30` in the [before layout](browser-before.layout.txt) to `5,0,40,56|155,0,40,56` in the [after layout](browser-after.layout.txt), matching [independent Chromium](chromium.dom.html). In fixed [non-static positioned 009](wpt-009.json), both horizontal Grid callbacks now pass all checks for their first child; their first failures moved to the second vertical-rl child (`width expected 60 but got 100`). The two vertical callbacks remain whole PASS; the file remains FAIL. Previously passing [007](wpt-007.json) and [008](wpt-008.json) remain whole PASS. Fixed WPT commit `2d66b9b7998bb58c336138c178323ddee857b586`, q1012 manifest, localhost HTTP, fixed fonts, 5 s settle, 20 s process bound.

[Warning-free build](build.log), [host CSS](css.log) 51/51, [JS](js.log) 14/14, [Grid](grid.log) 48/48 PASS. [Acid report](acid-report.json): Acid3 score100/inventory100/100 and 0/480,000 different pixels; Acid2 1,865/120,000 differences, unchanged. `git diff --check` PASS; `clang-format` unavailable.

q1191-i01 cleared its finite partial criteria; p217/p189/p173 whole remain unmet. Next map the second vertical-rl border child. WPT, runner, public ABI and Master unchanged; shared projection/GitHub publication pending with Q1, no push.
