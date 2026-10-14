# q1131-i01 — row dense Grid placement

The Grid placement pass now resets its search cursor for each auto-placed item when `grid-auto-flow` is row dense. The existing row sparse cursor still advances. Column flow and column dense remain separate work.

The [minimal fixture](row-dense.html) and `test-grid-row-dense.py` verify a two-column, three-item layout with a span-created hole: sparse places the third item at `(0,40)`, while dense backfills `(40,0)`. The pre-change run placed both at `(0,40)`; the final run passed. The selected [fixed WPT comparison](comparison.json) for `grid-auto-placement-implicit-tracks-001` stayed **0/12 whole FAIL**; its first failure is an implicit column width of 494px versus 100px, because `grid-auto-columns` is still unsupported. This is not evidence that the WPT page's later row-dense assertions pass. The selected sparse WPT also remains FAIL. Four old Grid whole PASS representatives stayed PASS.

| Final-source check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, [log](host-build.log) warning0/error0 |
| `test-grid-row-dense.py`, prior auto-flow CSS fixture | PASS |
| `host-css-rule-model`, `run-js-tests.py`, `run-grid-justify-tests.py` | 51/51, 14/14, 48/48 PASS |
| fixed `run-interop2025-harness.py` with pinned fonts | selected placement0/12 before and after; old whole PASS4 maintained |
| `run-acid-tests.py --out build/ws074-acid-q1131` | Acid3 score100/inventory100/100/pixel diff0 of480000; Acid2 diff1865 of120000 |
| `git diff --check` | PASS |

q1131-i01 partial cleared on its row-dense criterion. p222/p189/p173 whole and all3,805 PASS remain uncleared. `clang-format` is unavailable; near-final WS conformance remains required. Shared projection/GitHub publication pending with Q1; no push.
