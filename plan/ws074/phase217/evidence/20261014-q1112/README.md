# q1112-i01 — positioned Grid physical inline edges

For a direct absolute child of a horizontal Grid with a valid definite explicit area and four auto insets, `justify-self/items:left` and `right` now choose physical area edges. `self-start` chooses the child's own inline-start physical edge, including when its direction differs from the container's. Placement uses the final child width and q1110's private area width. Explicit insets and invalid-area fallback retain precedence. Public ABI is unchanged.

The [minimal fixture](grid-abspos-physical-align.html) and `test-grid-abspos-physical-align.py` cover LTR/RTL left/right, opposite child direction for self-start, explicit inset and invalid Grid line. The [fixed five-WPT comparison](comparison.json) shows six horizontal subtests FAIL→PASS and no regression: horizontal LTR/RTL is now 10/10 PASS across these files. Each file remains whole FAIL because its two vertical-writing subtests still first fail width expected10/actual60; this belongs to [p220](../../../phase220/phase.md). q1105 **110 PASS/392 FAIL** remains the latest uniform Grid502 count; targeted subtest changes cannot be added to it as a new whole-file count.

| Final-source check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, [log](host-build.log) warning0/error0 |
| `test-grid-abspos-physical-align.py`, prior inline/block align fixtures | PASS |
| `run-grid-justify-tests.py`, `host-css-rule-model`, `run-js-tests.py` | 48/48, 51/51, 14/14 PASS |
| fixed `run-interop2025-harness.py` ×7 | six horizontal subtests FAIL→PASS; two old whole PASS sentinels maintained |
| `run-acid-tests.py --out build/ws074-acid-q1112` | Acid3 score100/inventory100/100/pixel diff0 of480000; Acid2 diff1865 of120000 |
| `git diff --check` | PASS |

q1112-i01 partial cleared. p217/p189/p173 whole and all3,805 PASS remain uncleared. `clang-format` is unavailable; near-final WS conformance remains required. Shared projection/GitHub publication pending with Q1; no push.
