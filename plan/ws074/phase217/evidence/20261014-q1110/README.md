# q1110-i01 — positioned Grid inline-axis alignment

For a direct absolute child of a horizontal Grid with a valid definite explicit area and four auto insets, Grid now records its inline-axis size and `justify-self`/`justify-items` end or center value. Absolute placement uses the child's final width and the area's physical left/right edges. `self-end` follows the child's direction; `end` follows the container's direction. Explicit insets still win, and a child without a valid area keeps its prior static path. The metadata is private to layout; public ABI is unchanged.

The [minimal fixture](grid-abspos-inline-align.html) and `test-grid-abspos-inline-align.py` cover LTR end/center, RTL end/center, RTL parent with LTR child `self-end`, explicit left override and invalid Grid line. The [fixed four-WPT comparison](comparison.json) reports all eight horizontal LTR/RTL subtests changed FAIL→PASS. Each WPT file remains whole FAIL because its two vertical-writing subtests still first fail width expected10/actual60; this belongs to [p220](../../../phase220/phase.md). q1105 **110 PASS/392 FAIL** remains the latest uniform Grid502 count; targeted subtest changes cannot be added to it as a new whole-file count.

| Final-source check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, [log](host-build.log) warning0/error0 |
| `test-grid-abspos-inline-align.py`, prior block-align and static-origin fixtures | PASS |
| `run-grid-justify-tests.py`, `host-css-rule-model`, `run-js-tests.py` | 48/48, 51/51, 14/14 PASS |
| fixed `run-interop2025-harness.py` ×6 | 8 horizontal subtests FAIL→PASS; two old whole PASS sentinels maintained |
| `run-acid-tests.py --out build/ws074-acid-q1110` | Acid3 score100/inventory100/100/pixel diff0 of480000; Acid2 diff1865 of120000 |
| `git diff --check` | PASS |

q1110-i01 partial cleared. p217/p189/p173 whole and all3,805 PASS remain uncleared. `clang-format` is unavailable; near-final WS conformance remains required. Shared projection/GitHub publication pending with Q1; no push.
