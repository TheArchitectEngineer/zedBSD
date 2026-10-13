# q1089-i01 — definite horizontal Grid static origin

After the existing in-flow grid tracks are sized, `layout/grid.c` now derives the static origin for a direct absolute child in a positioned horizontal LTR grid when both axes have auto insets and both line placements are definite and inside the explicit tracks. It adds the column/row track sizes and gaps, plus the container's horizontal content shift, to the child's static coordinates. The child remains outside track sizing and auto-placement. Other writing directions, invalid/auto lines and explicit insets retain their former path. This addresses the [q1088 minimal diagnosis](../20261014-q1088/README.md), within its bounded scope; full absolute self-alignment remains open.

The [four-case fixture](grid-static-cases.html) has a normal in-flow child at (8,8), a definite-area absolute child at **(108,88)**, an explicit-inset child at (15,17) and an invalid-line child at (8,8). The [layout dump](grid-static-cases.layout.txt) and black-box `test-grid-abspos-static.py` verify all four, including the 250×160 grid dimensions.

In fixed WPT, [column-axis](column001.json) and [row-axis](row001.json) positioned-item representatives each changed their horizontal LTR subtest from FAIL to PASS. Their whole files remain FAIL: the RTL subtest still reports offsetLeft 0 and the two vertical cases still have width mismatches. The old PASS [module Grid sentinel](module-sentinel.json) remains PASS. [Comparison](comparison.json) preserves old/new subtest statuses; no whole-file PASS gain is claimed. The q1087 full Grid 103 PASS/399 FAIL remains the latest uniform 502-ID measure.

| Check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, no warning/error (`build/ws074-q1089-build-conformance.log`) |
| `python3 plan/ws074/tests/test-grid-abspos-static.py` | PASS: area, explicit inset, invalid line and in-flow geometry |
| 3 × fixed `run-interop2025-harness.py` with three fonts | horizontal subtest FAIL→PASS in two FAIL files; old PASS sentinel stays PASS |
| `python3 plan/ws074/tests/run-js-tests.py --program build/ws074-host/plain/browser` | classic JS 14/14 PASS |
| `python3 plan/ws074/tests/run-acid-tests.py --out build/ws074-acid-q1089` | Acid3 score100, inventory100/100, pixel diff0/480000; Acid2 diff1865/120000 unchanged |
| `git diff --check` | PASS |

q1089-i01 partial cleared. RTL, vertical writing, auto/invalid line semantics, full self-alignment and the other Grid FAIL cases remain; p217/p189/p173 whole criteria are uncleared. Shared projection/GitHub publication pending with Q1; no push.
