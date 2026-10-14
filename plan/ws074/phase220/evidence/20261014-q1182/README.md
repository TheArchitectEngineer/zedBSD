# q1182-i01 — horizontal Grid vertical-lr RTL absolute text geometry

The bounded horizontal 2×2 Grid path now accepts a direct vertical-lr/RTL absolute ASCII child in row 2, column 2 with physical `left` and `top` insets. The definite second row supplies vertical inline room. Other orthogonal child variants and RTL parents retain their previous path.

The [minimal page](horizontal-grid-vertical-rtl-abs.html) changed from `105,160,36,40` in the [before layout](browser-before.layout.txt) to `105,160,40,36` in the [after layout](browser-after.layout.txt), exactly matching [independent Chromium](chromium.dom.html). In fixed Interop 2025 [non-static positioned 007](wpt-007.json), the horizontal LTR callback now passes whole, all four children, versus its previous fourth-child `width expected 40 but got 60`. The other three callbacks remain FAIL, so the WPT file remains FAIL. Previously passing [non-static 001](wpt-nonstatic-001.json) and [vertical positioned column 001](wpt-vertical-old.json) remain whole PASS. Fixed WPT commit `2d66b9b7998bb58c336138c178323ddee857b586`, q1012 manifest, localhost HTTP, fixed fonts, 5 s settle, 20 s process bound.

[Warning-free build](build.log), [host CSS](css.log) 51/51, [JS](js.log) 14/14, [Grid](grid.log) 48/48 PASS. [Acid report](acid-report.json): Acid3 score100/inventory100/100 and 0/480,000 different pixels; Acid2 1,865/120,000 differences, unchanged. `git diff --check` PASS; `clang-format` unavailable.

q1182-i01 cleared its finite partial criteria; p220/p217/p189/p173 whole remain unmet. Next handle the RTL horizontal Grid callback, then vertical Grid horizontal-child static area. WPT, runner, public ABI and Master unchanged; shared projection/GitHub publication pending with Q1, no push.
