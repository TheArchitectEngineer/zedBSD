# q1176-i01 — framed vertical positioned Grid inset area

The bounded vertical-lr/rl Grid absolute path now accepts a parent with border and padding when the child has definite explicit row and column lines plus a physical inset. The common absolute-position pass already adds the parent's frame; the Grid path supplies content-relative track coordinates. A frame without a definite explicit inset keeps its previous path.

The [minimal framed page](framed-vertical-inset.html) was `lr=21,17|rl=21,17` [before](browser-before.layout.txt) and is `lr=174,118|rl=24,118` [after](browser-after.layout.txt), matching [independent Chromium](chromium.dom.html). Fixed Interop 2025 [non-static positioned 006](wpt-006.json) is whole PASS, 4/4 callbacks. Earlier [001](wpt-001.json)–[005](wpt-005.json) remain whole PASS, giving the six-file series 6/6 whole and 24/24 callbacks. Fixed WPT commit `2d66b9b7998bb58c336138c178323ddee857b586`, q1012 manifest, localhost HTTP, fixed fonts, 5 s settle, 20 s process bound.

[Warning-free build](build.log), [host CSS](css.log) 51/51, [JS](js.log) 14/14, [Grid](grid.log) 48/48 PASS. [Final-source Acid report](acid-report.json): Acid3 score100/inventory100/100 and 0/480,000 different pixels; Acid2 1,865/120,000 differences, unchanged. `git diff --check` PASS; `clang-format` unavailable.

q1176-i01 cleared its finite partial criteria; p217/p189/p173 whole remain unmet. Next measure fixed Grid502 again, then address the largest remaining failure family. WPT, runner, public ABI and Master unchanged; shared projection/GitHub publication pending with Q1, no push.
