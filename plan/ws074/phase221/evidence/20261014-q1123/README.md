# q1123-i01 — fixed Grid parsing triage

The pinned Interop 2025 selection contains **41** `/css/css-grid/parsing/` testharness IDs ([sorted ID list](ids.txt), SHA256 `ba289245efb4f17256da4a94da3db7a50cb5e3ba41e00b1e97df810cf0c2b087`). All 41 were rerun on the q1122 final source with WPT `2d66b9b7998bb58c336138c178323ddee857b586`, localhost HTTP, three fixed fonts, 5 s settle and 20 s process bound. Each ID has a [raw report](test-00.json) through `test-40.json`; [analysis](analysis.json) lists every first failing assertion. IDs/commit/program launcher hash are uniform and all 41 have callbacks.

| Fixed parsing set | Whole PASS | Whole FAIL | PASS subtests | FAIL subtests | no_output/process_error |
| --- | ---: | ---: | ---: | ---: | ---: |
| q1123 | **6** | **35** | 336 | 930 | 0 |

The 35 whole failures cluster as follows; this is a symptom classification, not a claim of one cause per file.

| Cluster | Whole FAIL | Representative first difference | Source observation |
| --- | ---: | --- | --- |
| Missing Grid auto/areas properties | 9 | `grid-auto-columns`/`grid-auto-rows`/`grid-auto-flow`/`grid-template-areas` computed value absent | no matching property in `css/values.c`/`bind/computed.c` |
| Track parse/serialization | 14 | `grid-template-rows` is empty; columns omit `[a]`; `-10px` accepted; `repeat(auto-fit,...)` unsupported | rows parsed into `css_style.rows` but `bind/computed.c` only exposes columns; line names and invalid-track checks are separate |
| Grid area and shorthands | 10 | `grid-area` longhand value empty/canonical form wrong; `grid`/`grid-template` serialization wrong | existing shorthand paths have narrower grammar and CSSOM output |
| Other | 2 | multiple grid columns/rows get/set empty; content-sized columns `15px auto` vs `15px 75px` | needs independent CSSOM/used-track analysis |

The six whole PASS are the invalid suites for grid-auto-columns, grid-auto-flow, grid-auto-rows, grid-shorthand, grid-template-areas and grid-template-shorthand. The latest uniform Grid502 remains q1105 **110 PASS/392 FAIL**; q1123 is a targeted subset and cannot update that total.

First finite implementation candidate: expose `grid-template-rows` CSSOM using the existing parsed `style.rows`/`row_count` and the columns serializer, while preserving `none` and old column output. Verify minimal no-grid/explicit row cases, fixed rows computed-nogrid/withcontent/implicit/computed/valid/invalid files to record each next residual, old parsing PASS/other Grid PASS, host CSS/JS/Grid, warning0 build and Acid3 diff0/Acid2 nonincrease. Line names, implicit used tracks, `auto-*`, areas, shorthand and vertical text remain separate work; no full-suite gain is claimed before rerun.

q1123-i01 read-only partial cleared. p221/p189/p173 whole and all3,805 PASS remain uncleared. Product browser, WPT, runner, ABI, Master and Acid output unchanged. Shared projection/GitHub publication pending with Q1; no push.
