# q1093-i01 — fixed Grid testharness recensus after horizontal abspos changes

The same 502 eligible fixed Grid testharness IDs as q1087 ran after q1089 and q1091. Inputs and conditions were unchanged: WPT `2d66b9b7998bb58c336138c178323ddee857b586`, [q1077 selection manifest](../20261014-q1077/selection-manifest.json), localhost HTTP, three fixed fonts in `userland/desktop/fonts`, 5 s settle and 20 s process limit. Four bounded shards completed exit0.

| Fixed Grid testharness | PASS | FAIL | no_output | PASS subtests | FAIL subtests |
| --- | ---: | ---: | ---: | ---: | ---: |
| q1087 baseline | 103 | 399 | 0 | 5,077 | 5,928 |
| q1093 after q1091 | **103** | **399** | **0** | **5,099** | **5,906** |

[Analysis](analysis.json) verifies the exact 502-ID set and SHA256 `5fa65ed37998a0958da75da5dc9a69f85bd26a05c2f64a118072c7527f56af23`, all 502 checkpoint rows and gzip raw reports against their classification. No whole-file outcome changed and no former PASS regressed. In 13 alignment files, 22 subtests changed FAIL→PASS; zero changed PASS→FAIL. All changed IDs and counts are in the analysis. [Shards 0](shard-0000/report.json), [128](shard-0128/report.json), [256](shard-0256/report.json), and [384](shard-0384/report.json) retain raw callback reports.

The remaining 399 whole FAIL files still cluster in alignment206, grid-definition42, parsing35, grid-items28, layout-algorithm28 and abspos24. These are directory counts, not root causes. The q1092 vertical diagnosis explains why the representative positioned files are still FAIL despite their horizontal subtests improving. The missing `grid` shorthand is an independent candidate in the grid-definition/parsing groups.

q1093-i01 partial cleared. p189 whole1,783 testharness and p173 all3,805 PASS remain uncleared. The q1068 all-focus baseline remains the latest uniform all-focus testharness census; this Grid subset must not be added to it. Product browser, runner, WPT, public ABI and Master were unchanged. Shared Queue/Master/GitHub publication pending with Q1; no push.
