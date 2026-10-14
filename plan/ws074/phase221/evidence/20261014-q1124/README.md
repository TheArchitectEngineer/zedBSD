# q1124-i01 — grid-template-rows computed CSSOM

The existing `css_style.rows`/`row_count` now use the same specified-track CSSOM serializer as columns. `grid-template-rows` returns `none` for no declared tracks and serializes simple px/fr/auto tracks through both `getPropertyValue()` and camel-case access. The [minimal fixture](grid-template-rows.html) and `test-grid-template-rows-computed.py` verify row values, an old columns control, non-Grid style and DOM mutation.

The [fixed six-WPT comparison](comparison.json) shows rows computed-nogrid 0→2/24, computed-withcontent 0→1/27 and computed 0→3/24 PASS subtests. The other three files are unchanged. **Six subtests improved, zero regressed; all six whole files remain FAIL.** Next first errors concern line names (`1px [a]`→`1px`), implicit used tracks (`10px`→`none`) and negative track rejection (`-10px` accepted). The q1123 parsing subset remains the latest uniform 41-ID count, **6 whole PASS/35 FAIL**. q1105 remains the latest uniform Grid502 count, **110 PASS/392 FAIL**.

| Final-source check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, [log](host-build.log) warning0/error0 |
| `test-grid-template-rows-computed.py` | PASS |
| `host-css-rule-model`, `run-js-tests.py`, `run-grid-justify-tests.py` | 51/51, 14/14, 48/48 PASS |
| fixed `run-interop2025-harness.py` ×9 with pinned fonts | six rows files measured; old whole PASS representatives3 maintained |
| `run-acid-tests.py --out build/ws074-acid-q1124` | Acid3 score100/inventory100/100/pixel diff0 of480000; Acid2 diff1865 of120000 |
| `git diff --check` | PASS |

q1124-i01 partial cleared. p221/p189/p173 whole and all3,805 PASS remain uncleared. `clang-format` is unavailable; near-final WS conformance remains required. Shared projection/GitHub publication pending with Q1; no push.
