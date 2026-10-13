# q1059-i01 — fractional vertical textarea pixel repair

Fixed WPT checkout `2d66b9b7998bb58c336138c178323ddee857b586`. The selected reftest `css/css-flexbox/flexbox-basic-textarea-vert-001.xhtml` was q1058's final old PASS→FAIL case: 198 pixel differences after Flex redistribution, 0 load error.

Production `--dump=layout` showed test textarea boxes at y=8.00 with height110.88 and y=118.89 with height89.11; reference boxes have explicit `height:108.8px` and `87.1px`, with line boxes starting y=8.00 and118.80. The difference was two independent causes:

1. The `font: 8px monospace` shorthand on the reference's text child retained its parent's `line-height:0`; a font shorthand without a slash must reset line-height to `normal`. Fixing this in `css/values.c` changed the diff **198→110 pixels** and aligned the line box to height10/baseline16 in both test and reference.
2. All remaining 110 pixels were anti-aliased textarea border/background edges at x72–93 around y117–119 and y206–207. Layout retains the fractional CSS sizes; `paint/list.c` now snaps the textarea border-box's four *paint* edges to CSS device pixels and adjusts the painted content box consistently. The reftest then becomes **pixel exact PASS, diff0**. This applies to fractional textarea painting generally and does not change layout geometry or the DOM.

Evidence steps: q1058 final [198-pixel report](../../../phase206/evidence/20261014-q1058-clamp/report.json), `../20261014-q1059-font/report.json` (110 pixels), `../20261014-q1059-snap/report.json` (0 pixels). `selection-manifest.json` fixes the single ID; side images accompany failures. The full old-regression trio is 3/3 local pixel exact PASS at `../20261014-q1059-three/report.json`. Fixed Flex10 is 10/10 local pixel exact PASS at `../20261014-q1059-flex/report.json`.

`sh plan/ws074/tests/host-build.sh plain` exit0 warning0; host CSS rule51/51, CSSOM12/12, submit13/13, `git diff --check` PASS. The older `host-form` suite reports 27/29, with hardcoded placeholder and caret paint-coordinate checks failing. A temporary build without this Queue's font change reproduced exactly those same two failures; those input-field checks precede the textarea paint path, and q1059 did not add a failure. Acid3 score/inventory100/100, pixel difference0/480000; Acid2 difference1865/120000, unchanged (`build/ws074-acid-q1059/report.json`). `clang-format` unavailable; changed source manually checked against C full standard.

q1059-i01 and p207 whole cleared. q1058's last old regression is resolved by later evidence, so p206 whole can also clear while its partial attempt history stays intact. p190 still has many other pixel/runner failures and p173 all 3,805 PASS remains unmet. Shared Master/Queue/GitHub publication pending with Q1; no push.
