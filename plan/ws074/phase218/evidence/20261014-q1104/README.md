# q1104-i01 — explicit `grid-template` tracks

`css/values.c` now recognizes the supported `grid-template: <track-list> / <track-list>` form and lone `none`. It reuses the existing two-sided explicit track parser, then publishes row and column longhands only after both parse. The lone `none` resets both explicit track lists. Existing cascade order controls preceding/following longhands; gaps remain separate. Named areas/lines, implicit tracks, and CSSOM shorthand serialization remain outside this bounded implementation. CSS-wide single keywords are rejected until every represented longhand can be reset correctly.

The [seven-container fixture](template-cases.html), [host dump](template-cases.layout.txt) and `test-grid-template-shorthand.py` verify 14 child geometries: 50/50px tracks with a 5px gap, longhand override after shorthand, invalid value preserving earlier tracks, one `none` side, shorthand override after longhand, rejected named-area syntax, and lone `none` resetting both track lists to implicit auto. The existing `grid` shorthand geometry test remains PASS.

[Fixed WPT comparison](comparison.json): `grid-template-shorthand-valid.html` changed **0/40→5/40 PASS subtests**, with zero PASS→FAIL. The whole file remains FAIL because named lines/areas and CSSOM behavior are incomplete. The old 100-subtest Grid module sentinel remained PASS. q1093 **103 PASS/399 FAIL** remains the latest uniform Grid502 measure.

| Final-source check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, [log](host-build.log) warning0/error0 |
| `test-grid-template-shorthand.py`, `test-grid-explicit-shorthand.py` | PASS |
| `run-grid-justify-tests.py`, `host-css-rule-model`, `host-cssom`, `host-cssom-mutation`, `run-js-tests.py` | 48/48, 51/51, 12/12, 12/12, 14/14 PASS |
| fixed `run-interop2025-harness.py` ×3 | template-valid 0→5/40 subtests; old 100-subtest module sentinel PASS |
| `run-acid-tests.py --out build/ws074-acid-q1104` | Acid3 score100/inventory100/100/pixel diff0 of480000; Acid2 diff1865 of120000 |
| `git diff --check` | PASS |

q1104-i01 partial cleared. p218/p189/p173 whole and all3,805 PASS remain uncleared. `clang-format` is unavailable; near-final WS conformance remains required. Shared projection/GitHub publication pending with Q1; no push.
