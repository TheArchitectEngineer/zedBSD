# q1128-i01 — empty Grid line-name CSSOM

For valid JavaScript assignments to `grid-template-columns` or `grid-template-rows`, the inline style serializer now omits empty `[]` line-name groups while retaining nonempty names, tracks and `repeat()` syntax. The track parser still rejects malformed groups. [CSS Grid §7.2.2](https://www.w3.org/TR/css-grid-2/#line-names) defines the line-name group.

The [minimal fixture](empty-line-names.html) and `test-grid-empty-lines.py` check plain and repeated empty groups, named groups and invalid adjacent groups on both axes. The [fixed WPT comparison](comparison.json) against q1127 shows `grid-template-columns-valid` and `grid-template-rows-valid` each improved **27→29/34** subtests, with zero regressions. Both invalid suites remain **42/42 whole PASS**, and three old whole PASS representatives remain PASS. The two valid whole files still FAIL on five auto-repeat cases per axis. The q1123 uniform parsing41 census remains 6 PASS/35 FAIL until rerun.

| Final-source check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, [log](host-build.log) warning0/error0 |
| `test-grid-empty-lines.py`, prior calc/line-name/nonnegative fixtures | PASS |
| `host-css-rule-model`, `run-js-tests.py`, `run-grid-justify-tests.py` | 51/51, 14/14, 48/48 PASS |
| fixed `run-interop2025-harness.py` ×7 with pinned fonts | valid subtests4 FAIL→PASS; old PASS3 maintained; invalid suites 42/42 each |
| `run-acid-tests.py --out build/ws074-acid-q1128` | Acid3 score100/inventory100/100/pixel diff0 of480000; Acid2 diff1865 of120000 |
| `git diff --check` | PASS |

q1128-i01 partial cleared. p221/p189/p173 whole and all3,805 PASS remain uncleared. `clang-format` is unavailable; near-final WS conformance remains required. Shared projection/GitHub publication pending with Q1; no push.
