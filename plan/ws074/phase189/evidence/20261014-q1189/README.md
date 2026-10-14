# q1189-i01 — fixed Grid502 recensus after q1178–q1188

The same 502 eligible fixed Grid testharness IDs as [q1177](../20261014-q1177/README.md) ran after the bounded positioned Grid fixes. Inputs and conditions remained fixed: WPT `2d66b9b7998bb58c336138c178323ddee857b586`, [q1077 selection manifest](../20261014-q1077/selection-manifest.json), localhost HTTP, three fixed fonts, 5 s settle and 20 s process bound. Four bounded shards completed exit0.

| Fixed Grid testharness | PASS | FAIL | no_output | PASS subtests | FAIL subtests |
| --- | ---: | ---: | ---: | ---: | ---: |
| q1177 baseline | 189 | 313 | 0 | 5,272 | 5,733 |
| q1189 current | **192** | **310** | **0** | **5,288** | **5,717** |

[Analysis](analysis.json) verifies the exact 502-ID set/order and SHA256 `5fa65ed37998a0958da75da5dc9a69f85bd26a05c2f64a118072c7527f56af23`, callback names, 502 checkpoint results plus four headers and 502 gzip raw reports. Exactly three whole files changed FAIL→PASS: `grid-self-alignment-non-static-positioned-items-007` and `008`, and `grid-self-alignment-positioned-items-with-margin-border-padding-005`. Sixteen subtests changed FAIL→PASS; no whole PASS or subtest regressed.

[Shards 0](shard-0000/report.json), [128](shard-0128/report.json), [256](shard-0256/report.json) and [384](shard-0384/report.json) retain every raw callback. Largest remaining families: `grid-self-alignment-stretch` 12 FAIL, `grid-self-alignment-positioned-items-with-margin-border-padding` 11 FAIL, `grid-positioned-items-and-autofit-tracks` 7 FAIL; non-static positioned 009–012 have four FAIL. q1189-i01 read-only recensus cleared. Remaining 310 Grid FAIL and runner coverage outside these 502 remain; p189 whole1,783 testharness and p173 all3,805 PASS uncleared. Browser, runner, WPT, public ABI, Master and Acid output were unchanged in this Queue. Shared projection/GitHub publication pending with Q1; no push.
