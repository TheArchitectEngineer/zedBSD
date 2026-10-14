# q1132-i02 — basic implicit Grid track sizing

Grid sizing now uses the declared single fixed pixel size for implicit columns and rows after the explicit tracks. Unspecified implicit tracks remain `auto`; the current row sparse and row dense placement behavior is preserved. Track selection cycles through the declared implicit list, ready for later multiple-value parsing.

The [minimal fixture](implicit-tracks.html) and `test-grid-implicit-tracks.py` pass for 100px implicit columns, 50px implicit rows, sparse placement and row dense backfill. The [fixed placement WPT comparison](comparison.json) shows `grid-auto-placement-implicit-tracks-001` **0→6/12** PASS subtests: all three row sparse and all three row dense cases now pass. Six column-flow cases remain FAIL on their positions, so whole status stays FAIL. The selected sparse WPT remained 4/8 FAIL with no callback regression; two old whole PASS representatives stayed PASS.

| Final-source check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, [log](host-build-i02.log) warning0/error0 |
| `test-grid-implicit-tracks.py`, `test-grid-row-dense.py`, `test-grid-auto-track-css.py` | PASS |
| `host-css-rule-model`, `run-js-tests.py`, `run-grid-justify-tests.py` | 51/51, 14/14, 48/48 PASS |
| fixed `run-interop2025-harness.py` with pinned fonts | placement6/12, old whole PASS2 maintained; auto-columns/rows valid each3/30 maintained |
| `run-acid-tests.py --out build/ws074-acid-q1132` | Acid3 score100/inventory100/100/pixel diff0 of480000; Acid2 diff1865 of120000 |
| `git diff --check` | PASS |

q1132-i02 partial cleared. p222/p189/p173 whole and all3,805 PASS remain uncleared. `clang-format` is unavailable; near-final WS conformance remains required. Shared projection/GitHub publication pending with Q1; no push.
