# q1125-i01 — nonnegative Grid track sizes

[CSS Grid §7.2.1](https://www.w3.org/TR/css-grid-2/#track-sizes) requires nonnegative track lengths and percentages. The parser now rejects negative simple sizes in a Grid track or either `minmax()` argument, and rejects negative literal `fit-content()` limits. It retains the existing accepted `calc()` expressions for a later computed-value range check.

The [minimal fixture](nonnegative-tracks.html) and `test-grid-nonnegative-tracks.py` check both template axes: seven negative forms rejected and nine zero/positive or valid `calc()` forms accepted per axis. The [fixed WPT comparison](comparison.json) shows `grid-template-columns-invalid` and `grid-template-rows-invalid` each improved **33→40/42** PASS subtests. Both valid suites stayed **19/34**, with no regression. The two remaining invalid subtests per axis are malformed line-name lists (`[one] 10px [two] [three]`, `[auto] 1px`), outside this Queue. All four whole files remain FAIL; the q1123 uniform parsing41 total stays 6 PASS/35 FAIL until a new uniform census.

| Final-source check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, [log](host-build.log) warning0/error0 |
| `test-grid-nonnegative-tracks.py`, prior rows CSSOM test | PASS |
| `host-css-rule-model`, `run-js-tests.py`, `run-grid-justify-tests.py` | 51/51, 14/14, 48/48 PASS |
| fixed `run-interop2025-harness.py` ×7 with pinned fonts | invalid subtests14 FAIL→PASS; valid subtests unchanged; old whole PASS representatives3 maintained |
| `run-acid-tests.py --out build/ws074-acid-q1125` | Acid3 score100/inventory100/100/pixel diff0 of480000; Acid2 diff1865 of120000 |
| `git diff --check` | PASS |

q1125-i01 partial cleared. p221/p189/p173 whole and all3,805 PASS remain uncleared. `clang-format` is unavailable; near-final WS conformance remains required. Shared projection/GitHub publication pending with Q1; no push.
