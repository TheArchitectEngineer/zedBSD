# q1178-i01 — framed vertical Grid all-auto static area

A direct same-writing-mode LTR absolute child of a fixed 2×2 vertical-lr/rl Grid now receives its definite Grid static area when its physical insets are all auto and the parent has border/padding. Framed opposite-writing-mode or RTL-child all-auto cases retain their previous path; explicit inset behavior from q1176 is unchanged.

The [minimal framed page](framed-auto-static.html) was `lr=19,8|rl=19,8` [before](browser-before.layout.txt) and is `lr=238,108|rl=19,108` [after](browser-after.layout.txt), matching [independent Chromium](chromium.dom.html). Fixed Interop 2025 [positioned-items-with-margin-border-padding 005](wpt-005.json) is whole PASS, 4/4 callbacks. Earlier [004](wpt-004.json) and [non-static positioned 006](wpt-nonstatic-006.json) remain whole PASS. Fixed WPT commit `2d66b9b7998bb58c336138c178323ddee857b586`, q1012 manifest, localhost HTTP, fixed fonts, 5 s settle, 20 s process bound.

[Warning-free build](build.log), [host CSS](css.log) 51/51, [JS](js.log) 14/14, [Grid](grid.log) 48/48 PASS. [Acid report](acid-report.json): Acid3 score100/inventory100/100 and 0/480,000 different pixels; Acid2 1,865/120,000 differences, unchanged. `git diff --check` PASS; `clang-format` unavailable.

q1178-i01 cleared its finite partial criteria; p217/p189/p173 whole remain unmet. Next investigate the orthogonal-child and auto-line variants 006–016. WPT, runner, public ABI and Master unchanged; shared projection/GitHub publication pending with Q1, no push.
