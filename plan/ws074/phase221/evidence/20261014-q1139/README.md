# q1139-i01 — nonnegative computed Grid track lengths

Grid track lengths are nonnegative. The parser already allows calculated lengths whose result can be negative; `cascade_tracks` now clamps a resolved negative absolute track size or `minmax()` minimum to 0px before layout and computed CSSOM read the style. Positive lengths, percentages and flexible tracks follow the existing path. The [minimal fixture](negative-calc-tracks.html) [observed](calc-output.txt) both `grid-auto-columns` and `grid-template-columns` compute `calc(10px - 0.5em)` to `0px` at 40px font size. [CSS Grid §7.2.1](https://www.w3.org/TR/css-grid-2/#track-sizes) gives the track size range.

[Fixed WPT comparison](comparison.json) shows auto columns/rows computed suites each **9→10/25** subtests, with the negative calc case now passing. Auto columns/rows invalid remain all PASS (16/16, 15/15). Template columns/rows computed representatives remained 3/25 and 3/24 PASS; their remaining issues are unrelated. `empty-grid-001` and implicit placement remain whole PASS (9/9 and 12/12). No measured subtest moved PASS→FAIL.

| Final-source check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, [log](host-build.log), warning0/error0 |
| `host-css-rule-model`, `run-js-tests.py`, `run-grid-justify-tests.py`, `test-grid-implicit-tracks.py` | 51/51, 14/14, 48/48, PASS |
| fixed `run-interop2025-harness.py` ×8, pinned fonts | auto/template computed, invalid and old PASS, no measured regression |
| `run-acid-tests.py --out build/ws074-acid-q1139` | [report](acid-report.json): Acid3 score100/inventory100/100/pixel diff0 of480000; Acid2 diff1865 of120000 |
| `git diff --check` | PASS |

q1139-i01 partial cleared. p221 whole and p189/p173 all-focus work remain uncleared. `clang-format` unavailable; near-final WS conformance remains required. Shared projection/GitHub publication pending with Q1; no push.
