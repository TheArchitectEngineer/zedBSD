# q1116-i01 — positioned Grid `auto / auto` inline area

For a direct absolute child of a horizontal Grid with four auto insets, `grid-column:auto / auto` and a valid explicit row, Grid now gives the child a full-width inline static area and retains its explicit row origin. LTR/RTL start and end use the appropriate physical container edge; center uses the final child width. This bounded path requires zero inline padding and borders. Other auto row forms and vertical writing remain separate.

The [minimal fixture](grid-abspos-both-auto.html) and `test-grid-abspos-both-auto.py` verify LTR/RTL start/end/center in a 450px container with centered tracks and an explicit second row, plus explicit-column controls. The [fixed five-WPT comparison](comparison.json) shows all ten horizontal callbacks in row-axis positioned-items 012–016 changed FAIL→PASS. Each file remains whole FAIL because its two vertical-writing callbacks still first fail width expected10/actual60; this belongs to [p220](../../../phase220/phase.md). q1105 **110 PASS/392 FAIL** remains the latest uniform Grid502 count; targeted subtest results are not a new whole-file count.

| Final-source check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, [log](host-build.log) warning0/error0 |
| `test-grid-abspos-both-auto.py`, prior auto-start/auto-end/physical-edge fixtures | PASS |
| `run-grid-justify-tests.py`, `host-css-rule-model`, `run-js-tests.py` | 48/48, 51/51, 14/14 PASS |
| fixed `run-interop2025-harness.py` ×7 | ten horizontal callbacks FAIL→PASS; two old whole PASS sentinels maintained |
| `run-acid-tests.py --out build/ws074-acid-q1116` | Acid3 score100/inventory100/100/pixel diff0 of480000; Acid2 diff1865 of120000 |
| `git diff --check` | PASS |

q1116-i01 partial cleared. p217/p189/p173 whole and all3,805 PASS remain uncleared. `clang-format` is unavailable; near-final WS conformance remains required. Shared projection/GitHub publication pending with Q1; no push.
