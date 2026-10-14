# q1127-i01 — calculated Grid track lengths

The track parser now uses the existing `calc()` length parser for direct track sizes and both `minmax()` arguments. Computed pixel and mixed percentage lengths reach the existing cascade and Grid layout; negative computed track lengths are clamped to zero. The minimum side of `minmax()` rejects an `fr` size, required to preserve the fixed invalid-value suites. [CSS Grid §7.2.1](https://www.w3.org/TR/css-grid-2/#track-sizes) defines these constraints.

The [minimal fixture](calc-tracks.html) and `test-grid-calc-tracks.py` check eight `calc()` forms per axis and a 30px positive/0px negative resolved track. Both axes passed. The [fixed WPT comparison](comparison.json) against q1126 shows `grid-template-columns-valid` and `grid-template-rows-valid` each improved **19→27/34** subtests, with zero regressions. Both invalid suites remain **42/42 whole PASS**, and three old whole PASS representatives remain PASS. The two valid whole files still FAIL on empty line-name serialization and auto-repeat forms. The q1123 uniform parsing41 census remains 6 PASS/35 FAIL until rerun.

| Final-source check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, [log](host-build.log) warning0/error0 |
| `test-grid-calc-tracks.py`, prior line-name and nonnegative fixtures | PASS |
| `host-css-rule-model`, `run-js-tests.py`, `run-grid-justify-tests.py` | 51/51, 14/14, 48/48 PASS |
| fixed `run-interop2025-harness.py` ×7 with pinned fonts | valid subtests16 FAIL→PASS; old PASS3 maintained; invalid suites 42/42 each |
| `run-acid-tests.py --out build/ws074-acid-q1127` | Acid3 score100/inventory100/100/pixel diff0 of480000; Acid2 diff1865 of120000 |
| `git diff --check` | PASS |

q1127-i01 partial cleared. p221/p189/p173 whole and all3,805 PASS remain uncleared. `clang-format` is unavailable; near-final WS conformance remains required. Shared projection/GitHub publication pending with Q1; no push.
