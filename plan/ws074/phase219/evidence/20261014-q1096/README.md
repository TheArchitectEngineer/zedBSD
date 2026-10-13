# q1096-i01 — synthesized baseline for empty Grid items in fixed rows

For an empty, in-flow Grid item with explicit pixel height in a fixed pixel row and horizontal writing, the engine now synthesizes its alignment baseline at the bottom border edge. Items in the same row whose effective `align-self` is baseline share the maximum such baseline. Their physical y positions shift to make those baselines coincide. A non-baseline sibling such as `align-self: end` keeps its existing placement. The scoped path excludes text line baselines, auto rows, spans and vertical writing; those require wider baseline and track sizing work. [CSS Align §9.1](https://drafts.csswg.org/css-align/#baseline-export) describes synthesized baselines from grid item border edges.

The [two-row fixture](empty-baseline-cases.html), [host dump](empty-baseline-cases.layout.txt) and `test-grid-empty-baseline.py` verify six child geometries. Row 1 children of heights20/50/30 occupy y38/8/28, sharing baseline y58. Row 2 children of heights10/40 occupy y138/108, sharing baseline y148; its 20px `align-self:end` sibling occupies y168 independently.

[Fixed WPT comparison](comparison.json): `grid-alignment-style-changes-001.html` changed from 3/6 PASS subtests after q1095 to **6/6 PASS**, whole FAIL→PASS. The old PASS `positioned-grid-descendants-001.html` stays PASS with 100/100 subtests. This is a local result for two fixed IDs; q1093 **103 PASS/399 FAIL** remains the latest uniform Grid502 census.

| Final-source check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, [log](host-build.log) warning0/error0 |
| `test-grid-empty-baseline.py`, `test-grid-explicit-shorthand.py` | PASS, six baseline child geometries and shorthand regression |
| `run-grid-justify-tests.py`, `host-css-rule-model` | 48/48, 51/51 PASS |
| `run-js-tests.py` | classic JS14/14 PASS |
| fixed `run-interop2025-harness.py` ×2 | style-changes whole FAIL→PASS; old PASS 100/100 maintained |
| `run-acid-tests.py --out build/ws074-acid-q1096` | Acid3 score100/inventory100/100/pixel diff0 of480000; Acid2 diff1865 of120000 |
| `git diff --check` | PASS |

q1096-i01 partial cleared. The other baseline conditions, p219/p217/p189/p173 whole goals and all3,805 PASS remain uncleared. `clang-format` is unavailable; WS074 near-final conformance remains required. Shared projection/GitHub publication pending with Q1; no push.
