# q1126-i01 — Grid line-name grammar

[CSS Grid §7.2.2](https://www.w3.org/TR/css-grid-2/#line-names) defines bracketed line-name groups and reserves `auto` and `span`. The track parser now rejects adjacent groups without a track, these reserved names, non-ident tokens and unclosed brackets. It still passes valid names through without storing them; CSSOM line-name serialization is later work.

The [minimal fixture](line-names.html) and `test-grid-line-names.py` check six invalid and five valid forms per axis, including names inside `repeat()`. Both axes passed. The [fixed WPT comparison](comparison.json) against q1125 shows `grid-template-columns-invalid` and `grid-template-rows-invalid` each improved **40→42/42** subtests and whole **FAIL→PASS**. Both valid suites stayed **19/34**; their remaining 15 failures per axis are outside this Queue. Three old whole PASS representatives stayed PASS. No measured subtest regressed. The q1123 uniform parsing41 census remains 6 PASS/35 FAIL until rerun.

| Final-source check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, [log](host-build.log) warning0/error0 |
| `test-grid-line-names.py`, `test-grid-nonnegative-tracks.py` | PASS |
| `host-css-rule-model`, `run-js-tests.py`, `run-grid-justify-tests.py` | 51/51, 14/14, 48/48 PASS |
| fixed `run-interop2025-harness.py` ×7 with pinned fonts | 2 whole FAIL→PASS; 4 subtests improved; old PASS3 maintained |
| `run-acid-tests.py --out build/ws074-acid-q1126` | Acid3 score100/inventory100/100/pixel diff0 of480000; Acid2 diff1865 of120000 |
| `git diff --check` | PASS |

q1126-i01 partial cleared. p221/p189/p173 whole and all3,805 PASS remain uncleared. `clang-format` is unavailable; near-final WS conformance remains required. Shared projection/GitHub publication pending with Q1; no push.
