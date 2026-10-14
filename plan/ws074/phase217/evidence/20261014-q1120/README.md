# q1120-i01 — inline-grid intrinsic width

`display:inline-grid` now retains a distinct internal display value. Its box is inline-level atomic and takes the intrinsic Grid track width when `width:auto`; its children still use Grid layout. Computed style serializes `inline-grid`.

The [minimal fixture](inline-grid-width.html) and `test-inline-grid-width.py` verify auto widths 250px in LTR and RTL, explicit width 400px, block Grid width 1008px, RTL absolute-child x198 (190 relative to its parent), and computed display values. The [fixed five-WPT comparison](comparison.json) shows horizontal RTL callbacks in column-axis positioned-items 012–016 changed FAIL→PASS; horizontal LTR remained PASS. All ten vertical-writing callbacks still FAIL, so all five whole files remain FAIL. q1105 **110 PASS/392 FAIL** remains the latest uniform Grid502 count; this targeted result is not a new full count.

| Final-source check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, [log](host-build.log) warning0/error0 |
| `test-inline-grid-width.py`, `test-grid-auto-row-boundaries.py` | PASS |
| `run-grid-justify-tests.py`, `host-css-rule-model`, `run-js-tests.py` | 48/48, 51/51, 14/14 PASS |
| fixed `run-interop2025-harness.py` ×7 with pinned fonts | five horizontal RTL callbacks FAIL→PASS; two old whole PASS sentinels maintained |
| `run-acid-tests.py --out build/ws074-acid-q1120` | Acid3 score100/inventory100/100/pixel diff0 of480000; Acid2 diff1865 of120000 |
| `git diff --check` | PASS |

q1120-i01 partial cleared. p217/p189/p173 whole and all3,805 PASS remain uncleared. `clang-format` is unavailable; near-final WS conformance remains required. Shared projection/GitHub publication pending with Q1; no push.
