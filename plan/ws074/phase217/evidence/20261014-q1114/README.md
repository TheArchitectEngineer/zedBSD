# q1114-i01 — positioned Grid `auto / 1` inline boundary

For a direct absolute child of a horizontal Grid with four auto insets and `grid-column:auto / 1`, the auto inline-start boundary now uses the container padding edge. The explicit end line remains at the first track edge. `justify-content:center` therefore moves the track but does not move the auto boundary. The private area width and q1110–q1112 self alignment still use the definite 100px area in this bounded case. Other auto-line forms and vertical writing remain separate.

The [minimal fixture](grid-abspos-auto-start.html) and `test-grid-abspos-auto-start.py` verify LTR and RTL start/end/center positions with a 450px Grid and centered 100px+150px tracks, plus explicit `1 / 2` controls. The [fixed five-WPT comparison](comparison.json) shows that the initial 100px mismatch in each row-axis 012–016 horizontal LTR/RTL callback is gone; the first failure now concerns a later child or another axis. All ten horizontal callbacks and all ten vertical callbacks still FAIL, and all five files remain whole FAIL. q1105 **110 PASS/392 FAIL** remains the latest uniform Grid502 count.

| Final-source check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, [log](host-build.log) warning0/error0 |
| `test-grid-abspos-auto-start.py`, prior physical/inline/block align fixtures | PASS |
| `run-grid-justify-tests.py`, `host-css-rule-model`, `run-js-tests.py` | 48/48, 51/51, 14/14 PASS |
| fixed `run-interop2025-harness.py` ×7 | first 100px mismatch advances; two old whole PASS sentinels maintained |
| `run-acid-tests.py --out build/ws074-acid-q1114` | Acid3 score100/inventory100/100/pixel diff0 of480000; Acid2 diff1865 of120000 |
| `git diff --check` | PASS |

q1114-i01 partial cleared for its exact auto-start output. p217/p189/p173 whole and all3,805 PASS remain uncleared. `clang-format` is unavailable; near-final WS conformance remains required. Shared projection/GitHub publication pending with Q1; no push.
