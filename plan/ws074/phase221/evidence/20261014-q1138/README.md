# q1138-i01 — flexible implicit Grid tracks

`grid-auto-columns/rows` now accept nonnegative `fr` tracks in the bounded cycle. Grid column and row sizing count implicit flexible tracks alongside explicit flexible tracks when sharing definite remaining space. The [minimal fixture](auto-track-fr.html) [observed](fr-output.txt) 1fr/2fr/1fr columns and rows of 100/200/100px in a 400px container; 50px/1fr/2fr gave 50/100/200px in 350px. Computed CSSOM retained both `fr` patterns. [CSS Grid §7.2.4](https://www.w3.org/TR/css-grid-2/#fr-unit) and [§7.6](https://www.w3.org/TR/css-grid-2/#propdef-grid-auto-columns) define the factors and repeating implicit sizes.

[Fixed WPT comparison](comparison.json) against q1137 shows `grid-auto-columns/rows-valid` each **10→12/30** subtest and `-computed` each **7→9/25**. Both `-invalid` files remain all PASS (columns16/16, rows15/15); `grid-auto-placement-implicit-tracks-001` remains 12/12 whole PASS and `empty-grid-001` remains 9/9 whole PASS. No measured subtest moved PASS→FAIL. The four valid/computed suites remain whole FAIL due to other track kinds and CSSOM limitations.

| Final-source check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, [log](host-build.log), warning0/error0 |
| `host-css-rule-model`, `run-js-tests.py`, `run-grid-justify-tests.py`, `test-grid-implicit-tracks.py` | 51/51, 14/14, 48/48, PASS |
| fixed `run-interop2025-harness.py` ×8, pinned fonts | six parsing reports plus two old PASS, no measured regression |
| `run-acid-tests.py --out build/ws074-acid-q1138` | [report](acid-report.json): Acid3 score100/inventory100/100/pixel diff0 of480000; Acid2 diff1865 of120000 |
| `git diff --check` | PASS |

q1138-i01 partial cleared. p221 whole and p189/p173 all-focus work remain uncleared. `clang-format` unavailable; near-final WS conformance remains required. Shared projection/GitHub publication pending with Q1; no push.
