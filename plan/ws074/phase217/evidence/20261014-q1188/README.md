# q1188-i01 — vertical-lr Grid fourth horizontal RTL absolute area

A direct horizontal-tb/RTL absolute child in row 2/column 2 of a fixed vertical-lr/LTR 2×2 inline Grid now receives its definite Grid area when it has physical `left` and `top` insets. Both insets resolve against that area. The predicate is restricted to the explicit lines, writing modes, insets and unframed child in the approved scope.

The [minimal page](vertical-lr-fourth-horizontal-rtl.html) changed from `5,10,36,40` in the [before layout](browser-before.layout.txt) to `155,110,36,40` in the [after layout](browser-after.layout.txt), matching [independent Chromium](chromium.dom.html). Fixed [non-static positioned 007](wpt-007.json) is now whole PASS with all four callbacks PASS. Previously passing [vertical positioned column 001](wpt-vertical-old.json) and [non-static 001](wpt-nonstatic-001.json) remain whole PASS. Fixed WPT commit `2d66b9b7998bb58c336138c178323ddee857b586`, q1012 manifest, localhost HTTP, fixed fonts, 5 s settle, 20 s process bound.

[Warning-free build](build.log), [host CSS](css.log) 51/51, [JS](js.log) 14/14, [Grid](grid.log) 48/48 PASS. [Acid report](acid-report.json): Acid3 score100/inventory100/100 and 0/480,000 different pixels; Acid2 1,865/120,000 differences, unchanged. `git diff --check` PASS; `clang-format` unavailable.

q1188-i01 cleared its finite partial criteria; p217/p189/p173 whole remain unmet. Next recensus the fixed Grid subset and classify the remaining failures. WPT, runner, public ABI and Master unchanged; shared projection/GitHub publication pending with Q1, no push.
