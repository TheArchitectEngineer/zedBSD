# q1118-i01 — positioned Grid auto row boundaries

For a direct absolute child of a horizontal Grid with four auto insets, fixed pixel height, centered rows and zero vertical padding/borders, `grid-row:auto / 1` now spans from the padding top to the first shifted row line, `2 / auto` spans from the shifted second line to the padding bottom, and `auto / auto` uses the full container height. `align-self:end/center` uses the final child height. Other height/content forms and vertical writing remain separate.

The [minimal fixture](grid-auto-row-boundaries.html) and `test-grid-auto-row-boundaries.py` verify LTR/RTL end and LTR center positions for all three auto forms, plus a definite row control. The [fixed five-WPT comparison](comparison.json) shows the horizontal LTR callback in each column-axis 012–016 file changed FAIL→PASS. Every horizontal RTL callback still fails the same `offsetLeft` assertion (expected190/actual948), and vertical-writing callbacks remain FAIL. All five files remain whole FAIL. q1105 **110 PASS/392 FAIL** remains the latest uniform Grid502 count; targeted subtest changes are not a new whole-file count.

| Final-source check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, [log](host-build.log) warning0/error0 |
| `test-grid-auto-row-boundaries.py`, prior row-shift/both-auto fixtures | PASS |
| `run-grid-justify-tests.py`, `host-css-rule-model`, `run-js-tests.py` | 48/48, 51/51, 14/14 PASS |
| fixed `run-interop2025-harness.py` ×7 | five horizontal LTR callbacks FAIL→PASS; two old whole PASS sentinels maintained |
| `run-acid-tests.py --out build/ws074-acid-q1118` | Acid3 score100/inventory100/100/pixel diff0 of480000; Acid2 diff1865 of120000 |
| `git diff --check` | PASS |

q1118-i01 partial cleared. p217/p189/p173 whole and all3,805 PASS remain uncleared. `clang-format` is unavailable; near-final WS conformance remains required. Shared projection/GitHub publication pending with Q1; no push.
