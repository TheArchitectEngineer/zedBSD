# q1101-i01 — empty vertical-rl Grid baseline x

For a horizontal Grid container with one fixed pixel column, an empty direct vertical-rl item with explicit pixel width and `justify-self:baseline` now shares a border-left baseline with eligible items in that column. The group uses the largest extent from that border-left edge through the item's right border and right margin, so it fits at the area's right edge. An item changed to `justify-self:end` leaves the group and keeps its normal end placement. Other writing modes, content items, auto widths, non-fixed/multiple columns, and other alignments keep their existing path. This is a deliberately bounded implementation of the [CSS Align baseline grouping/synthesis rules](https://drafts.csswg.org/css-align/#baseline-export).

The [two-grid fixture](vertical-empty-baseline-cases.html) and [host layout](vertical-empty-baseline-cases.layout.txt) verify margin-bearing 50px and 20px items share border-left x48 (area x8 + group x40), while an excluded 50px end item remains x48 and the lone 20px baseline item moves to x88. `test-grid-vertical-empty-baseline.py` passes.

[Fixed WPT comparison](comparison.json): style-changes-003/004 each changed 1/6→6/6 subtests and whole FAIL→PASS. Ten subtests changed FAIL→PASS, zero PASS→FAIL. Old whole PASS 001/002/005/006 and the 100-subtest module sentinel remained PASS. q1093 103 PASS/399 FAIL remains the latest **uniform** Grid502 measure; 007/008 vertical-lr text were not rerun here and remain unresolved in q1099.

| Final-source check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, [log](host-build.log) warning0/error0 |
| `test-grid-vertical-empty-baseline.py`, `test-grid-line-baseline.py`, `test-grid-empty-baseline.py`, `test-grid-explicit-shorthand.py` | PASS |
| `run-grid-justify-tests.py`, `host-css-rule-model`, `run-js-tests.py` | 48/48, 51/51, 14/14 PASS |
| fixed `run-interop2025-harness.py` ×7 | 003/004 whole FAIL→PASS; old 001/002/005/006 and 100-subtest module sentinel PASS |
| `run-acid-tests.py --out build/ws074-acid-q1101` | Acid3 score100/inventory100/100/pixel diff0 of480000; Acid2 diff1865 of120000 |
| `git diff --check` | PASS |

q1101-i01 partial cleared. p217/p219/p189/p173 whole and all3,805 PASS remain uncleared. `clang-format` is unavailable; near-final WS conformance remains required. Shared projection/GitHub publication pending with Q1; no push.
