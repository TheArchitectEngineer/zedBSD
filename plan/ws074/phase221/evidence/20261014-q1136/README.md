# q1136-i01 — repeated fixed Grid auto tracks

`grid-auto-columns` and `grid-auto-rows` now accept a bounded whitespace-separated cycle of `auto` and nonnegative pixel track sizes. `values_auto_tracks` validates every component and the `CSS_TRACKS` limit; the existing cascade stores the list, and Grid layout cycles its values for successive implicit tracks. The [minimal fixture](auto-track-cycle.html) [observed](cycle-output.txt) column widths 20/30/20px, row heights 30/40/30px, and matching inline and computed CSSOM strings. The previous single-track behavior remains the one-element cycle.

[Fixed WPT comparison](comparison.json) against q1134 shows `grid-auto-columns/rows-valid` each **3→6/30** subtest, and `-computed` each **3→4/25**. Both `-invalid` files remain all PASS (columns16/16, rows15/15). The formerly passing `grid-auto-placement-implicit-tracks-001` remains 12/12 whole PASS; q1135's recovered `empty-grid-001` remains 9/9 whole PASS. No measured subtest moved PASS→FAIL. Other legitimate Grid track kinds and calc/multiple mixed functions remain outside this Queue; the four valid/computed suites remain whole FAIL.

| Final-source check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, [log](host-build.log), warning0/error0 |
| `host-css-rule-model`, `run-js-tests.py`, `run-grid-justify-tests.py`, `test-grid-implicit-tracks.py` | 51/51, 14/14, 48/48, PASS |
| fixed `run-interop2025-harness.py` ×8, pinned fonts | six parsing reports plus two old PASS, no measured regression |
| `run-acid-tests.py --out build/ws074-acid-q1136` | [report](acid-report.json): Acid3 score100/inventory100/100/pixel diff0 of480000; Acid2 diff1865 of120000 |
| `git diff --check` | PASS |

q1136-i01 partial cleared. p221 whole and p189/p173 all-focus work remain uncleared. `clang-format` unavailable; near-final WS conformance remains required. Shared projection/GitHub publication pending with Q1; no push.
