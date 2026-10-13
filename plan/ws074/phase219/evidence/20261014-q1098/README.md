# q1098-i01 — first-line Grid baseline in fixed rows

The q1096 fixed-row baseline group now accepts a horizontal Grid item with directly formatted inline lines. Its first line's y and baseline, plus top margin/border/padding, supply the item's baseline. An empty item still synthesizes one from its lower border edge. Peers in the same fixed pixel row share the maximum baseline; a sibling aligned to end remains outside the group. Auto rows, spans, block descendants and vertical writing still take their existing path. This follows the first-line baseline rule for block containers in [CSS Align §9.1](https://drafts.csswg.org/css-align/#baseline-export).

The [two-row fixture](line-baseline-cases.html), [host dump](line-baseline-cases.layout.txt) and `test-grid-line-baseline.py` verify three text items with font sizes20/40/30 share physical baseline y42.5 in row 1. In row 2 an empty 40px item has synthesized baseline y148, a 20px text line shares y148, and an `align-self:end` empty sibling stays at y168. The q1096 empty-only fixture still passes.

[Fixed WPT comparison](comparison.json): `grid-alignment-style-changes-005.html` and `006.html` each changed from 3/6 PASS to **6/6 PASS** subtests and whole FAIL→PASS. Fixed 001/002 and the old 100-subtest PASS sentinel remained whole PASS under the same WPT/localhost/font conditions. This is a targeted five-ID check. q1093 **103 PASS/399 FAIL** remains the latest uniform Grid502 measure; the remaining four style-changes IDs were not rerun in this Queue.

| Final-source check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, [log](host-build.log) warning0/error0 |
| `test-grid-line-baseline.py`, `test-grid-empty-baseline.py`, `test-grid-explicit-shorthand.py` | PASS |
| `run-grid-justify-tests.py`, `host-css-rule-model` | 48/48, 51/51 PASS |
| `run-js-tests.py` | classic JS14/14 PASS |
| fixed `run-interop2025-harness.py` ×5 | 005/006 whole FAIL→PASS, 001/002 and old PASS sentinel maintained |
| `run-acid-tests.py --out build/ws074-acid-q1098` | Acid3 score100/inventory100/100/pixel diff0 of480000; Acid2 diff1865 of120000 |
| `git diff --check` | PASS |

q1098-i01 partial cleared. p219/p217/p189/p173 whole and all3,805 PASS remain uncleared. `clang-format` is unavailable; near-final WS conformance remains required. Shared projection/GitHub publication pending with Q1; no push.
