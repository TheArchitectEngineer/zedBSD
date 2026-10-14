# q1115-i01 — positioned Grid `2 / auto` inline boundary

For a direct absolute child of a horizontal Grid with four auto insets and `grid-column:2 / auto`, the auto inline-end boundary now reaches the container padding edge. The private area width spans from the shifted explicit second line to that edge, preserving the container's `justify-content:center` track position. Other auto-line forms and vertical writing remain separate.

The [minimal fixture](grid-abspos-auto-end.html) and `test-grid-abspos-auto-end.py` verify LTR/RTL start/end/center positions with a 450px Grid and centered 100px+150px tracks, plus explicit `2 / 3` controls. The [fixed five-WPT comparison](comparison.json) shows the `2 / auto` area-width mismatch advances to a later child in 013/014/015, while 012/016 remain stopped at other auto-area or row-position differences. All ten horizontal callbacks and all ten vertical callbacks still FAIL; all five files remain whole FAIL. q1105 **110 PASS/392 FAIL** remains the latest uniform Grid502 count.

| Final-source check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, [log](host-build.log) warning0/error0 |
| `test-grid-abspos-auto-end.py`, prior auto-start and physical-edge fixtures | PASS |
| `run-grid-justify-tests.py`, `host-css-rule-model`, `run-js-tests.py` | 48/48, 51/51, 14/14 PASS |
| fixed `run-interop2025-harness.py` ×7 | `2 / auto` mismatch advances; two old whole PASS sentinels maintained |
| `run-acid-tests.py --out build/ws074-acid-q1115` | Acid3 score100/inventory100/100/pixel diff0 of480000; Acid2 diff1865 of120000 |
| `git diff --check` | PASS |

q1115-i01 partial cleared for its exact auto-end output. p217/p189/p173 whole and all3,805 PASS remain uncleared. `clang-format` is unavailable; near-final WS conformance remains required. Shared projection/GitHub publication pending with Q1; no push.
