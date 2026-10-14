# q1175-i01 — vertical positioned Grid explicit inset area

For a direct absolute child of a fixed 2×2 vertical-lr/rl inline Grid, explicit physical insets with valid row and column lines now use its definite Grid area as the containing block. All-auto insets retain the prior static alignment path. The [minimal page](vertical-inset.html) changed from `lr=5,10|rl=5,10` in the [before layout](browser-before.layout.txt) to `lr=155,110|rl=5,110` in the [after layout](browser-after.layout.txt), exactly matching [independent Chromium](chromium.dom.html).

Fixed Interop 2025 `grid-self-alignment-non-static-positioned-items` [001](wpt-001.json)–[004](wpt-004.json) now pass whole, 16/16 callbacks. Adjacent [005](wpt-005.json) also passes whole, while [006](wpt-006.json) remains FAIL in the vertical callbacks because the parent has border and padding, outside this Queue; horizontal callbacks pass. Previously passing [all-auto positioned representative](wpt-old.json) remains whole PASS. Fixed WPT commit `2d66b9b7998bb58c336138c178323ddee857b586`, q1012 manifest, localhost HTTP, fixed fonts, 5 s settle, 20 s process bound.

[Warning-free build](build.log), [host CSS](css.log) 51/51, [JS](js.log) 14/14, [Grid](grid.log) 48/48 PASS. [Acid report](acid-report.json): Acid3 score100/inventory100/100 and 0/480,000 different pixels; Acid2 1,865/120,000 differences, unchanged. `git diff --check` PASS; `clang-format` unavailable.

q1175-i01 cleared its finite partial criteria; p217/p189/p173 whole remain unmet. Next candidate: parent border/padding adjustment for positioned vertical Grid explicit inset in 006. WPT, runner, public ABI and Master unchanged; shared projection/GitHub publication pending with Q1, no push.
