# q1184-i01 — horizontal RTL Grid second-row vertical-rl text geometry

The bounded horizontal RTL 2×2 Grid path now accepts a direct vertical-rl/RTL absolute ASCII child in row 2, column 2 with physical `left` and `top` insets. Its second definite row supplies vertical inline room; the RTL Grid static-area path uses that explicit cell for both inset coordinates.

The [minimal page](horizontal-rtl-grid-second-row-abs.html) changed from `5,10,36,40` in the [before layout](browser-before.layout.txt) to `5,160,40,36` in the [after layout](browser-after.layout.txt), exactly matching [independent Chromium](chromium.dom.html). In fixed Interop 2025 [non-static positioned 007](wpt-007.json), both horizontal LTR and RTL callbacks now pass whole. The two vertical Grid callbacks remain FAIL, so the WPT file remains FAIL. Previously passing [non-static 001](wpt-nonstatic-001.json) and [vertical positioned column 001](wpt-vertical-old.json) remain whole PASS. Fixed WPT commit `2d66b9b7998bb58c336138c178323ddee857b586`, q1012 manifest, localhost HTTP, fixed fonts, 5 s settle, 20 s process bound.

[Warning-free build](build.log), [host CSS](css.log) 51/51, [JS](js.log) 14/14, [Grid](grid.log) 48/48 PASS. [Acid report](acid-report.json): Acid3 score100/inventory100/100 and 0/480,000 different pixels; Acid2 1,865/120,000 differences, unchanged. `git diff --check` PASS; `clang-format` unavailable.

q1184-i01 cleared its finite partial criteria; p220/p217/p189/p173 whole remain unmet. Next map horizontal absolute children in a vertical Grid to their explicit static areas. WPT, runner, public ABI and Master unchanged; shared projection/GitHub publication pending with Q1, no push.
