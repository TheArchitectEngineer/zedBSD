# q1072-i01 — fixed CSS Grid testharness recensus

Pinned WPT `2d66b9b7998bb58c336138c178323ddee857b586`; fixed Interop 2025 manifest 3,805 unique IDs. The [derived selection](selection-manifest.json) contains all 502 `css/css-grid/` testharness IDs, and every one is eligible for the existing single localhost HTTP physical-HTML runner. [Exact eligible IDs](eligible-ids.json) and [analysis](analysis.json) preserve the selection hash and transitions. Four ≤128-ID shards used the q1068 conditions: host browser, fixed three fonts, localhost, 5 s settle, 20 s process limit, unchanged WPT/harness.

| Fixed Grid testharness | PASS | FAIL | no_output |
| --- | ---: | ---: | ---: |
| q1068 before q1070/q1071 | 41 | 429 | 32 |
| q1072 after q1070/q1071 | **43** | **427** | **32** |

Two IDs changed whole FAIL→PASS: `/css/css-grid/alignment/grid-align-justify-margin-border-padding.html` (4→18 PASS subtests) and `/css/css-grid/alignment/grid-row-axis-alignment-sticky-positioned-items-001.html` (0→2 PASS subtests). No old PASS regressed. Across 502 IDs, completion subtests changed from 1,777 PASS/6,028 FAIL to **1,809 PASS/5,996 FAIL**. All 32 no_output IDs are two sets of positioned Grid descendant tests with `<script type="module">`; this is a shared module-script execution issue candidate, not a geometry assertion result. None is counted PASS.

Each [shard-0000](shard-0000/report.json), [shard-0128](shard-0128/report.json), [shard-0256](shard-0256/report.json), and [shard-0384](shard-0384/report.json) finished with exit0 and a fsync checkpoint. [Analysis](analysis.json) verified all 502 unique selected IDs and their SHA256, all shard selected lists, each checkpoint result/outcome and gzip raw report ID/outcome, and compared every ID against q1068. Runner/browser/WPT/ABI/Master code did not change; no host build or Acid recheck was needed after q1071 final verification. `git diff --check` PASS.

q1072-i01 partial scope cleared; p189 whole (1,783 testharness) and p173 all 3,805 PASS remain uncleared. The latest complete single-origin eligible baseline across all1,455 IDs remains q1068's 223 PASS/1,232 nonPASS until an all-focus recensus; the 502 Grid subset must not be added to that whole-set total without remeasuring or accounting for other changes. Shared Master/Queue/GitHub publication remain pending with Q1; no push.
