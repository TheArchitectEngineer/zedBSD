# q1177-i01 — fixed Grid502 recensus after explicit inset areas

The same 502 eligible fixed Grid testharness IDs as [q1172](../20261014-q1172/README.md) ran after q1174–q1176. Inputs and conditions remained fixed: WPT `2d66b9b7998bb58c336138c178323ddee857b586`, [q1077 selection manifest](../20261014-q1077/selection-manifest.json), localhost HTTP, three fixed fonts, 5 s settle and 20 s process bound. Four bounded shards completed exit0.

| Fixed Grid testharness | PASS | FAIL | no_output | PASS subtests | FAIL subtests |
| --- | ---: | ---: | ---: | ---: | ---: |
| q1172 baseline | 183 | 319 | 0 | 5,245 | 5,760 |
| q1177 current | **189** | **313** | **0** | **5,272** | **5,733** |

[Analysis](analysis.json) verifies exact 502-ID set and order, SHA256 `5fa65ed37998a0958da75da5dc9a69f85bd26a05c2f64a118072c7527f56af23`, callback names, 502 checkpoint results plus four headers and 502 gzip raw reports. Exactly six whole files changed FAIL→PASS: `grid-self-alignment-non-static-positioned-items-001..006`, with 27 subtests changed FAIL→PASS. No whole PASS or subtest regressed.

[Shards 0](shard-0000/report.json), [128](shard-0128/report.json), [256](shard-0256/report.json) and [384](shard-0384/report.json) retain every raw callback. Largest remaining file families: `grid-self-alignment-positioned-items-with-margin-border-padding` 005–016 (12 FAIL), `grid-self-alignment-stretch` 005–016 (12 FAIL), `grid-positioned-items-and-autofit-tracks` 7 FAIL, and non-static positioned 007–012 (6 FAIL). q1177-i01 read-only recensus cleared. Remaining 313 Grid FAIL and runner coverage outside these 502 remain; p189 whole1,783 testharness and p173 all3,805 PASS uncleared. Browser, runner, WPT, public ABI, Master and Acid output were unchanged in this Queue. Shared projection/GitHub publication pending with Q1; no push.
