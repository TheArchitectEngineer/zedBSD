# q1133-i01 — column Grid auto-placement

Grid placement now uses its row placement algorithm with the axes exchanged for `grid-auto-flow: column` and `column dense`. The item coordinates, spans and template counts are restored before sizing; the temporary occupancy map is only used while placing items. Row sparse/dense behavior remains on the original path. [CSS Grid §8.5](https://www.w3.org/TR/css-grid-2/#auto-placement-algo) defines the auto-placement algorithm.

The [minimal fixture](column-flow.html) and `test-grid-column-flow.py` check four modes and the positions and dimensions of four items with a two-axis span. The [fixed WPT comparison](comparison.json) against q1132 shows `grid-auto-placement-implicit-tracks-001` **6→12/12** and `grid-auto-flow-sparse-001` **4→8/8** subtests, both whole **FAIL→PASS**. Two old whole PASS Grid representatives stayed PASS; `grid-auto-flow` valid/computed suites stayed whole PASS. No measured subtest regressed.

| Final-source check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, [log](host-build.log) warning0/error0 |
| `test-grid-column-flow.py`, prior implicit-track/row-dense/auto-flow fixtures | PASS |
| `host-css-rule-model`, `run-js-tests.py`, `run-grid-justify-tests.py` | 51/51, 14/14, 48/48 PASS |
| fixed `run-interop2025-harness.py` ×6 with pinned fonts | 2 whole FAIL→PASS; 10 subtests improved; old PASS4 maintained |
| `run-acid-tests.py --out build/ws074-acid-q1133` | Acid3 score100/inventory100/100/pixel diff0 of480000; Acid2 diff1865 of120000 |
| `git diff --check` | PASS |

q1133-i01 partial cleared. p222/p189/p173 whole and all3,805 PASS remain uncleared. `clang-format` is unavailable; near-final WS conformance remains required. Shared projection/GitHub publication pending with Q1; no push.
