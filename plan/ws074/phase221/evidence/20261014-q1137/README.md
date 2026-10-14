# q1137-i01 — non-pixel fixed Grid auto tracks

The `grid-auto-columns/rows` list parser now accepts nonnegative fixed lengths, percentages, and complete `calc()` length functions alongside `auto` and px; `fr`, `minmax()` and `fit-content()` remain outside this Queue. The existing cascade resolves relative lengths, while Grid layout uses definite percentages and treats unresolved percentages as zero during intrinsic measurement. The [minimal fixture](auto-track-lengths.html) [observed](lengths-output.txt) column widths 40/20/20px and row heights 40/20/20px for `2em 10% calc(10px + 0.5em)` with 20px font and 200px container, with computed `40px 10% 20px`. This follows [CSS Grid §7.6](https://www.w3.org/TR/css-grid-2/#propdef-grid-auto-columns) for repeating implicit track sizes.

[Fixed WPT comparison](comparison.json) against q1136 shows `grid-auto-columns/rows-valid` each **6→10/30** subtest and `-computed` each **4→7/25**. Both `-invalid` files remain all PASS (columns16/16, rows15/15). `grid-auto-placement-implicit-tracks-001` remains 12/12 whole PASS and `empty-grid-001` remains 9/9 whole PASS. No measured subtest moved PASS→FAIL. The four valid/computed suites remain whole FAIL because other legitimate track types are still absent.

| Final-source check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, [log](host-build.log), warning0/error0 |
| `host-css-rule-model`, `run-js-tests.py`, `run-grid-justify-tests.py`, `test-grid-implicit-tracks.py` | 51/51, 14/14, 48/48, PASS |
| fixed `run-interop2025-harness.py` ×8, pinned fonts | six parsing reports plus two old PASS, no measured regression |
| `run-acid-tests.py --out build/ws074-acid-q1137` | [report](acid-report.json): Acid3 score100/inventory100/100/pixel diff0 of480000; Acid2 diff1865 of120000 |
| `git diff --check` | PASS |

q1137-i01 partial cleared. p221 whole and p189/p173 all-focus work remain uncleared. `clang-format` unavailable; near-final WS conformance remains required. Shared projection/GitHub publication pending with Q1; no push.
