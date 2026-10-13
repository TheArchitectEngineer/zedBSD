# q1107-i01 — Grid block-axis auto margins in definite rows

For a horizontal Grid item spanning one definite pixel/percentage row, top and/or bottom `margin:auto` now absorb positive block-axis free space after the item's final height is known. One auto side receives all free space; two split it. Auto margins suppress stretch/self-alignment in that axis and resolve to zero on overflow. The calculation reruns when the container or item's style changes. Auto rows, row spans and vertical writing remain outside this bounded implementation. This follows [CSS Grid §11.2](https://drafts.csswg.org/css-grid/#auto-margins).

The [six-container fixture](block-auto-margin-cases.html), [host layout](block-auto-margin-cases.layout.txt) and `test-grid-block-auto-margins.py` verify top-only, bottom-only, both, overflow, alignment override and auto-height text cases. The row is 100px in each; the 20px child appears at y80/0/40 relative to its row for top-only/bottom-only/both. Overflow stays at start, `align-items:end` does not override auto margins, and auto-height text keeps its 10px intrinsic line instead of stretching.

[Fixed WPT comparison](comparison.json): the seven `grid-block-axis-alignment-auto-margins-001`–`007` files changed whole FAIL→PASS, each 0/2→2/2 subtests. Fourteen subtests changed FAIL→PASS, zero PASS→FAIL. The files change grid height, item height or font size between checks. Old style-changes001 and the 100-subtest Grid module sentinel remain whole PASS. q1105 **110 PASS/392 FAIL** remains the latest uniform Grid502 measure; the seven targeted results must not be added to it without a full recensus.

| Final-source check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, [log](host-build.log) warning0/error0 |
| `test-grid-block-auto-margins.py`, prior Grid baseline/shorthand fixtures | PASS |
| `run-grid-justify-tests.py`, `host-css-rule-model`, `run-js-tests.py` | 48/48, 51/51, 14/14 PASS |
| fixed `run-interop2025-harness.py` ×9 | 7 whole FAIL→PASS; two old PASS sentinels maintained |
| `run-acid-tests.py --out build/ws074-acid-q1107` | Acid3 score100/inventory100/100/pixel diff0 of480000; Acid2 diff1865 of120000 |
| `git diff --check` | PASS |

q1107-i01 partial cleared. p217/p189/p173 whole and all3,805 PASS remain uncleared. `clang-format` is unavailable; near-final WS conformance remains required. Shared projection/GitHub publication pending with Q1; no push.
