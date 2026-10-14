# q1135-i01 — empty Grid intrinsic size

After q1132 began honoring fixed `grid-auto-columns`, [q1134](../../../phase189/evidence/20261014-q1134/README.md) found the previously passing `abspos/empty-grid-001.html` failing six intrinsic-width checks: an empty grid with no explicit columns had expected width0 but actual200. `grid_place_row` retains one internal column for placement, and `grid_size_columns` treated that placeholder as a real 200px implicit track even with no in-flow items. An empty implicit grid has no such track. `grid_size_columns` now leaves that placeholder width0 when both item count and explicit column count are zero; existing item and explicit-track sizing paths remain in use.

The fixed [empty-grid WPT report](wpt/empty-grid-001.html.json) is now whole PASS, 9/9 subtests including empty, absolute-child, dynamic add/remove, explicit rows and explicit columns. The former [implicit-track placement PASS](wpt/grid-auto-placement-implicit-tracks-001.html.json) remains 12/12 whole PASS. [auto-columns-valid](wpt/grid-auto-columns-valid.html.json) remains 3/30 whole FAIL, matching its q1132 measured state; additional track grammar is outside this Queue.

| Final-source check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, [log](host-build.log), warning0/error0 |
| `host-css-rule-model`, `run-js-tests.py`, `run-grid-justify-tests.py`, `test-grid-implicit-tracks.py` | 51/51, 14/14, 48/48, PASS ([logs](css-test.log), [JS](js-test.log), [Grid](grid-test.log), [implicit](implicit-test.log)) |
| fixed `run-interop2025-harness.py` ×3, pinned fonts | empty-grid FAIL→PASS; implicit placement PASS maintained |
| `run-acid-tests.py --out build/ws074-acid-q1135` | [report](acid-report.json): Acid3 score100/inventory100/100/pixel diff0 of480000; Acid2 diff1865 of120000 |
| `git diff --check` | PASS |

q1135-i01 partial cleared. p223 whole and p189/p173 all-focus work remain uncleared. `clang-format` unavailable; near-final WS conformance remains required. Shared projection/GitHub publication pending with Q1; no push.
