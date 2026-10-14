# q1129-i01 — fixed Grid parsing recensus

The pinned Interop 2025 selection contains **41** `/css/css-grid/parsing/` testharness IDs ([sorted ID list](ids.txt), SHA256 `ba289245efb4f17256da4a94da3db7a50cb5e3ba41e00b1e97df810cf0c2b087`). All 41 were rerun on q1128 final source with WPT `2d66b9b7998bb58c336138c178323ddee857b586`, localhost HTTP, three fixed fonts, 5 s settle and 20 s process bound. Each ID has a raw `test-00.json` through `test-40.json`; [analysis](analysis.json) records the first failure, and [comparison](comparison.json) compares every callback against q1123. IDs, WPT/metadata/program hashes and bounds are uniform; all 41 have completion and callbacks.

| Fixed parsing set | Whole PASS | Whole FAIL | PASS subtests | FAIL subtests | no_output/process_error |
| --- | ---: | ---: | ---: | ---: | ---: |
| q1123 | 6 | 35 | 336 | 930 | 0 |
| q1129 | **8** | **33** | **399** | **867** | **0** |

Two invalid template suites became whole PASS; 63 subtests improved and none regressed. The 33 remaining whole failures include six `grid-auto-columns/rows/flow` valid/computed files where the property is absent, three `grid-template-areas` files where the property is absent, Grid template computed line names/implicit tracks/content sizing, auto-repeat valid/computed cases, and area/Grid shorthands. These are symptom groups, not proof of one defect per group. The latest uniform Grid502 remains q1105 **110 PASS/392 FAIL**; this targeted41 result cannot update that total.

Finite implementation candidates, in priority order:

1. `grid-auto-flow` parse, initial/inherit and computed CSSOM, with the existing layout behavior checked separately. Fixed valid/computed WPT form a bounded two-file target.
2. `grid-auto-columns/rows` parse, style and computed CSSOM; check whether implicit Grid tracks already use the declared sizes before implementing geometry.
3. `grid-template-areas` parse/style/computed CSSOM, then dependent `grid-area`/shorthands.

q1129-i01 read-only partial cleared. p221/p189/p173 whole and all3,805 PASS remain uncleared. Browser/WPT/runner/ABI/Master/Acid output unchanged. Shared projection/GitHub publication pending with Q1; no push.
