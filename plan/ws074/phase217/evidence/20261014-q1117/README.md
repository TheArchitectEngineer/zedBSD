# q1117-i01 — centered Grid row origin for explicit lines

For a horizontal Grid with fixed pixel height, `align-content:center` and positive unused block-axis space, layout now computes a row shift from the final track sizes. In-flow Grid items and direct absolute children with an explicit row start use it. An absolute child whose row start is auto retains its prior origin for later auto-boundary work. A zero-row Grid uses zero gap height when computing unused space. Other content values, percentage heights and vertical writing remain separate.

The [minimal fixture](grid-row-content-shift.html) and `test-grid-row-content-shift.py` verify a 450px container with 150px+100px rows: the explicit first row starts at y100, the second at y250, the auto-start boundary stays at y0, and an in-flow item shares the shifted first row. The [fixed five-WPT comparison](comparison.json) shows the initial offsetTop mismatch in horizontal LTR callbacks for column-axis 012/015 moves to a later offsetLeft mismatch. The auto-start row cases 013/014/016 retain their first mismatch; RTL also has a separate large x mismatch. All five files remain whole FAIL, including their vertical-writing callbacks. q1105 **110 PASS/392 FAIL** remains the latest uniform Grid502 count.

| Final-source check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, [log](host-build.log) warning0/error0 |
| `test-grid-row-content-shift.py`, prior both-auto fixture | PASS |
| `run-grid-justify-tests.py`, `host-css-rule-model`, `run-js-tests.py` | 48/48, 51/51, 14/14 PASS |
| fixed `run-interop2025-harness.py` ×7 | 012/015 initial y mismatch advances; two old whole PASS sentinels maintained |
| `run-acid-tests.py --out build/ws074-acid-q1117` | Acid3 score100/inventory100/100/pixel diff0 of480000; Acid2 diff1865 of120000 |
| `git diff --check` | PASS |

q1117-i01 partial cleared for its explicit-row output. p217/p189/p173 whole and all3,805 PASS remain uncleared. `clang-format` is unavailable; near-final WS conformance remains required. Shared projection/GitHub publication pending with Q1; no push.
