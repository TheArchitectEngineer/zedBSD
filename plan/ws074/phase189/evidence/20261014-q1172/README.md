# q1172-i01 — fixed Grid502 recensus after positioned alignment

The same 502 eligible fixed Grid testharness IDs as [q1160](../20261014-q1160/README.md) ran after q1162–q1171. Inputs and conditions remained fixed: WPT `2d66b9b7998bb58c336138c178323ddee857b586`, [q1077 selection manifest](../20261014-q1077/selection-manifest.json), localhost HTTP, three fixed fonts, 5 s settle and 20 s process bound. Four bounded shards completed exit0.

| Fixed Grid testharness | PASS | FAIL | no_output | PASS subtests | FAIL subtests |
| --- | ---: | ---: | ---: | ---: | ---: |
| q1160 baseline | 158 | 344 | 0 | 5,195 | 5,810 |
| q1172 current | **183** | **319** | **0** | **5,245** | **5,760** |

[Analysis](analysis.json) verifies exact 502-ID set and order, SHA256 `5fa65ed37998a0958da75da5dc9a69f85bd26a05c2f64a118072c7527f56af23`, callback names, 502 checkpoint rows and 502 gzip raw reports. Exactly 25 whole files changed FAIL→PASS with 50 subtests changed FAIL→PASS: column-axis/row-axis positioned-items001–011 (22 files) and positioned-items-with-margin-border-padding001–003 (3 files). No whole PASS or subtest regressed.

[Shards 0](shard-0000/report.json), [128](shard-0128/report.json), [256](shard-0256/report.json) and [384](shard-0384/report.json) retain every raw callback. q1172-i01 read-only recensus cleared. Remaining 319 Grid FAIL and runner coverage outside these 502 remain; p189 whole1,783 testharness and p173 all3,805 PASS uncleared. Browser, runner, WPT, public ABI, Master and Acid output were unchanged in this Queue. Shared projection/GitHub publication pending with Q1; no push.
