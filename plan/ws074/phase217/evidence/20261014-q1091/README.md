# q1091-i01 — horizontal RTL Grid absolute inline-start

In horizontal RTL Grid, unused space with `justify-content: normal` now places fixed tracks at inline-start. For a positioned grid's direct absolute child with auto insets and a valid explicit row/column, Grid records the area's physical right edge when `justify-items`/`justify-self` is `start`. `layout_absolute` moves the edge with the grid content origin. After `position_place` measures the child's width, it subtracts that width, horizontal frame and right margin from the edge. Non-target alignment, explicit insets, invalid/auto lines, and other writing modes keep their previous path. No public ABI or WPT/runner code changed.

The [five-case fixture](rtl-start-cases.html) and [host layout dump](rtl-start-cases.layout.txt) verify first-column fixed width x=388, second-column fixed width x=278, explicit left/top inset x=15/y=17, invalid column x=8, and auto text width24 x=384. The q1089 LTR fixture still passes. The Grid placement suite gained a normal RTL case with unused space, and passes 48/48; physical justify grammar passes 21/21.

[Fixed WPT comparison](comparison.json): `grid-column-axis-alignment-positioned-items-001.html` and `grid-row-axis-alignment-positioned-items-001.html` each moved horizontal RTL subtest 1→0 (FAIL→PASS) and kept horizontal LTR PASS. Both whole files remain FAIL because their two vertical-writing subtests remain FAIL. The old PASS `positioned-grid-descendants-001.html` stays PASS with 100/100 subtests. The q1087 **103 PASS/399 FAIL** remains the latest uniform 502-ID Grid census.

| Final-source check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, [log](host-build.log) with no warning/error |
| q1091 fixture 5 geometry assertions; `python3 plan/ws074/tests/test-grid-abspos-static.py` | 5/5 PASS; q1089 LTR PASS |
| `python3 plan/ws074/tests/run-grid-justify-tests.py --program build/ws074-host/plain/browser` | 48/48 PASS |
| `python3 plan/ws074/tests/run-physical-justify-tests.py --program build/ws074-host/plain/browser` | 21/21 PASS |
| 3 × fixed `run-interop2025-harness.py` with fixed fonts | horizontal RTL FAIL→PASS in two files; old PASS 100/100 retained |
| `python3 plan/ws074/tests/run-js-tests.py --program build/ws074-host/plain/browser` | classic JS 14/14 PASS |
| `python3 plan/ws074/tests/run-acid-tests.py --out build/ws074-acid-q1091-final` | Acid3 score100/inventory100/100/pixel diff0 of480000; Acid2 diff1865 of120000 |
| `git diff --check` | PASS |

q1091-i01 partial cleared. Vertical writing, other alignment, all Grid502 and Interop2025 3,805/3,805 remain unmet. Shared projection/GitHub publication pending with Q1; no push.
