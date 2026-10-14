# q1193-i01 — horizontal Grid fourth vertical RTL absolute child border

The bounded vertical inline text path now also accepts a border-only RTL absolute child in row 2/column 2 of an unframed horizontal LTR/RTL 2×2 inline Grid, with physical `left` and `top` insets. The child uses vertical-lr for the LTR parent and vertical-rl for the RTL parent. Parent frame and child padding/margin retain their previous path.

The [minimal LTR/RTL page](horizontal-grid-fourth-vertical-rtl-border.html) changed from `105,160,66,60|5,160,66,60` in the [before layout](browser-before.layout.txt) to `105,160,70,56|5,160,70,56` in the [after layout](browser-after.layout.txt), matching [independent Chromium](chromium.dom.html). Fixed [non-static positioned 009](wpt-009.json) is now whole PASS with all four callbacks PASS. Previously passing [007](wpt-007.json) and [008](wpt-008.json) remain whole PASS. Fixed WPT commit `2d66b9b7998bb58c336138c178323ddee857b586`, q1012 manifest, localhost HTTP, fixed fonts, 5 s settle, 20 s process bound.

[Warning-free build](build.log), [host CSS](css.log) 51/51, [JS](js.log) 14/14, [Grid](grid.log) 48/48 PASS. [Acid report](acid-report.json): Acid3 score100/inventory100/100 and 0/480,000 different pixels; Acid2 1,865/120,000 differences, unchanged. `git diff --check` PASS; `clang-format` unavailable.

q1193-i01 cleared its finite partial criteria; p217/p189/p173 whole remain unmet. Next handle padded absolute children in non-static010. WPT, runner, public ABI and Master unchanged; shared projection/GitHub publication pending with Q1, no push.
