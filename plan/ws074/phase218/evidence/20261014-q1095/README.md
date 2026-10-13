# q1095-i01 — explicit `grid: rows / columns`

`css/values.c` recognizes the supported explicit `grid: <track-list> / <track-list>` form and reuses the existing track-list parser for both sides. It publishes row and column longhand declarations only after both values parse. A top-level slash is required; `auto-flow`, named areas, CSS-wide single keywords and malformed values are rejected in this bounded pass. Existing cascade order handles preceding/following longhands, and row/column gaps remain untouched. `layout/grid.c`'s file description now includes q1091's horizontal RTL static origin. No public ABI, WPT or runner code changed.

The [six-container fixture](explicit-cases.html), [host dump](explicit-cases.layout.txt) and black-box `test-grid-explicit-shorthand.py` verify 12 child geometries: two 50px columns with a 5px gap; later longhand overriding the shorthand (30/70px); invalid `auto-flow` retaining earlier 40px rows and 30/70px columns; `none` rows with 50/50px columns; shorthand overriding earlier longhand (50/50px and 80px row); and rejected named-area form retaining earlier 40px rows and 30/70px columns. The [q1094 fixture](../20261014-q1094/grid-shorthand-cases.html) now renders shorthand and equivalent longhands with the same 50px columns and 80px row.

[Fixed WPT comparison](comparison.json) for `grid-alignment-style-changes-001.html`: three of six subtests changed FAIL→PASS; the file remains FAIL because the other three require baseline alignment and expected vertical offsets. The old PASS `positioned-grid-descendants-001.html` remains PASS with 100/100 subtests. The q1093 **103 PASS/399 FAIL** is still the latest uniform Grid502 result; one WPT file was sampled here.

| Check | Result |
| --- | --- |
| `sh plan/ws074/tests/host-build.sh plain` | exit0, [log](host-build.log) warning0/error0 |
| `python3 plan/ws074/tests/test-grid-explicit-shorthand.py` | PASS, 12 child geometries across six containers |
| `host-css-rule-model`, `host-cssom`, `host-cssom-mutation` | 51/51, 12/12, 12/12 PASS |
| `run-grid-justify-tests.py` | 48/48 PASS |
| `run-js-tests.py` | classic JS14/14 PASS |
| fixed `run-interop2025-harness.py` ×2 | style-changes 0→3/6 subtests, old PASS 100/100 maintained |
| `run-acid-tests.py --out build/ws074-acid-q1095` | Acid3 score100/inventory100/100/pixel diff0 of480000; Acid2 diff1865 of120000 |
| `git diff --check` | PASS |

This is a partial shorthand implementation. The engine still lacks the implicit-grid longhands, `grid-template-areas`, `grid-template`, CSSOM initial/serialization and the auto-flow syntax. Full [CSS Grid §7.8](https://drafts.csswg.org/css-grid-1/#grid-shorthand) conformance and p218/p189/p173 whole clearance remain open. `clang-format` is unavailable here; the final WS conformance Phase must manually review the C style and re-run applicable checks after later edits. Shared projection/GitHub publication pending with Q1; no push.
