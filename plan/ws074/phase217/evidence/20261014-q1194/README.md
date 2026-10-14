# q1194-i01 — horizontal Grid vertical absolute child padding

The bounded vertical inline text path now accepts padding-only absolute children in the three previously mapped vertical positions of an unframed horizontal LTR/RTL 2×2 inline Grid. The existing inline room calculation subtracts their physical padding. Parent frame and child margin retain their previous path.

The [minimal LTR/RTL page](horizontal-grid-vertical-abs-padding.html) changed all six rectangles from `5,0,66,30|168,100,72,50|105,160,66,60|155,0,66,30|68,100,72,50|5,160,66,60` in the [before layout](browser-before.layout.txt) to `5,0,40,56|180,88,60,62|105,160,70,56|155,0,40,56|80,88,60,62|5,160,70,56` in the [after layout](browser-after.layout.txt), matching [independent Chromium](chromium.dom.html). Fixed [non-static positioned 010](wpt-010.json) is now whole PASS with all four callbacks PASS. Previously passing [007](wpt-007.json), [008](wpt-008.json), and [009](wpt-009.json) remain whole PASS. Fixed WPT commit `2d66b9b7998bb58c336138c178323ddee857b586`, q1012 manifest, localhost HTTP, fixed fonts, 5 s settle, 20 s process bound.

[Warning-free build](build.log), [host CSS](css.log) 51/51, [JS](js.log) 14/14, [Grid](grid.log) 48/48 PASS. [Acid report](acid-report.json): Acid3 score100/inventory100/100 and 0/480,000 different pixels; Acid2 1,865/120,000 differences, unchanged. `git diff --check` PASS; `clang-format` unavailable.

q1194-i01 cleared its finite partial criteria; p217/p189/p173 whole remain unmet. Next handle combined margin/border/padding children in non-static011. WPT, runner, public ABI and Master unchanged; shared projection/GitHub publication pending with Q1, no push.
