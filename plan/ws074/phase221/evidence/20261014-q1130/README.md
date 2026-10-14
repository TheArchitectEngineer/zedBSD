# q1130-i01 — grid-auto-flow CSS state

The parser now accepts the [CSS Grid §7.7](https://www.w3.org/TR/css-grid-2/#propdef-grid-auto-flow) `row`/`column` and optional `dense` combinations, stores their computed state, and exposes canonical specified and computed CSSOM values. `row dense` and `dense row` serialize as `dense`; `dense column` serializes as `column dense`. Initial and `unset` resolve to `row`, and explicit `inherit` copies the parent state. This Queue covers CSS state; the Grid placement algorithm still uses its existing row-oriented behavior for column/dense values and needs a separate implementation/verification.

The [minimal fixture](auto-flow.html) and `test-grid-auto-flow.py` check seven valid forms, three invalid forms and inherit/initial/unset. The [fixed WPT comparison](comparison.json) against q1129 shows `grid-auto-flow-valid` and `grid-auto-flow-computed` each improved **0→7/7** and whole **FAIL→PASS**. The invalid suite stayed **3/3 whole PASS**; old template invalid suites and two other Grid whole PASS representatives stayed PASS. No measured subtest regressed. The q1129 uniform parsing41 remains 8 PASS/33 FAIL until rerun.

| Final-source check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, [log](host-build.log) warning0/error0 |
| `test-grid-auto-flow.py`, prior empty-line/calc/line-name fixtures | PASS |
| `host-css-rule-model`, `run-js-tests.py`, `run-grid-justify-tests.py` | 51/51, 14/14, 48/48 PASS |
| fixed `run-interop2025-harness.py` ×7 with pinned fonts | 2 whole FAIL→PASS, valid/computed14 subtests improved, old PASS5 maintained |
| `run-acid-tests.py --out build/ws074-acid-q1130` | Acid3 score100/inventory100/100/pixel diff0 of480000; Acid2 diff1865 of120000 |
| `git diff --check` | PASS |

q1130-i01 partial cleared. p221/p189/p173 whole and all3,805 PASS remain uncleared. `clang-format` is unavailable; near-final WS conformance remains required. Shared projection/GitHub publication pending with Q1; no push.
