# q1140-i01 — fixed Grid502 full recensus

The same 502 eligible fixed Grid testharness IDs as q1134 ran after q1135–q1139. Inputs and conditions were unchanged: WPT `2d66b9b7998bb58c336138c178323ddee857b586`, [q1077 selection manifest](../20261014-q1077/selection-manifest.json), localhost HTTP, three fixed fonts, 5 s settle and 20 s process bound. Four bounded shards completed exit0.

| Fixed Grid testharness | PASS | FAIL | no_output | PASS subtests | FAIL subtests |
| --- | ---: | ---: | ---: | ---: | ---: |
| q1134 baseline | 124 | 378 | 0 | 5,118 | 5,887 |
| q1140 current | **126** | **376** | **0** | **5,163** | **5,842** |

[Analysis](analysis.json) verifies exact 502-ID set and SHA256 `5fa65ed37998a0958da75da5dc9a69f85bd26a05c2f64a118072c7527f56af23`, 502 checkpoint rows and 502 gzip raw reports. Two whole files changed FAIL→PASS: `abspos/empty-grid-001.html` and `grid-definition/grid-auto-explicit-rows-001.html`. No former whole PASS regressed. At subtest level, 45 changed FAIL→PASS and none changed PASS→FAIL. The gains include 6 empty-grid, 32 auto-track parsing/computed, four grid-layout-properties, two template computed-withcontent and one grid-auto-explicit-rows.

[Shards 0](shard-0000/report.json), [128](shard-0128/report.json), [256](shard-0256/report.json), and [384](shard-0384/report.json) retain every raw callback. Remaining whole FAIL files cluster in alignment192, grid-definition41, parsing31, grid-items28, layout-algorithm27 and abspos23. These are directory counts, not root causes. q1140-i01 read-only recensus cleared. p189 whole1,783 testharness and p173 all3,805 PASS remain uncleared. Browser, runner, WPT, public ABI, Master and Acid output were unchanged in this Queue. Shared projection/GitHub publication pending with Q1; no push.
