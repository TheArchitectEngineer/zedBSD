# q1109-i01 — positioned Grid block-axis alignment

For a direct absolute child of a horizontal Grid with a valid definite explicit area and four auto insets, Grid now records the area's block-axis size and `align-self`/`align-items` value. Absolute placement uses these after the child's final height is known. `end`, `flex-end` and `self-end` align the margin box's bottom edge; `center` distributes positive remaining height. Explicit insets and prior inline-start/RTL positioning keep precedence. The metadata is private to layout; public ABI is unchanged.

The [minimal fixture](grid-abspos-block-align.html) and `test-grid-abspos-block-align.py` cover end, center, explicit top override, invalid grid lines and RTL inherited `align-items`. The [fixed five-WPT comparison](comparison.json) reports all ten horizontal LTR/RTL subtests changed FAIL→PASS. Each WPT file stays whole FAIL because both vertical-writing subtests still first fail width expected10/actual60; this belongs to [p220](../../../phase220/phase.md). q1105 **110 PASS/392 FAIL** remains the latest uniform Grid502 count; the targeted subtest changes cannot be added to it as a new whole-file count.

| Final-source check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, [log](host-build.log) warning0/error0 |
| `test-grid-abspos-block-align.py`, prior static-origin fixture | PASS |
| `run-grid-justify-tests.py`, `host-css-rule-model`, `run-js-tests.py` | 48/48, 51/51, 14/14 PASS |
| fixed `run-interop2025-harness.py` ×7 | 10 horizontal subtests FAIL→PASS; two old whole PASS sentinels maintained |
| `run-acid-tests.py --out build/ws074-acid-q1109` | Acid3 score100/inventory100/100/pixel diff0 of480000; Acid2 diff1865 of120000 |
| `git diff --check` | PASS |

q1109-i01 partial cleared. p217/p189/p173 whole and all3,805 PASS remain uncleared. `clang-format` is unavailable; near-final WS conformance remains required. Shared projection/GitHub publication pending with Q1; no push.
