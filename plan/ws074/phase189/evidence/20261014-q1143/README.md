# q1143-i01 — fixed Grid502 recensus after vertical axes

The same 502 eligible fixed Grid testharness IDs as [q1140](../20261014-q1140/README.md) ran after q1142's bounded vertical-lr Grid implementation. Inputs and conditions were unchanged: WPT `2d66b9b7998bb58c336138c178323ddee857b586`, [q1077 selection manifest](../20261014-q1077/selection-manifest.json), localhost HTTP, three fixed fonts, 5 s settle, and 20 s process bound. Four bounded shards completed exit0.

| Fixed Grid testharness | PASS | FAIL | no_output | PASS subtests | FAIL subtests |
| --- | ---: | ---: | ---: | ---: | ---: |
| q1140 baseline | 126 | 376 | 0 | 5,163 | 5,842 |
| q1143 current | **126** | **376** | **0** | **5,163** | **5,842** |

[Analysis](analysis.json) verifies the exact 502-ID set and SHA256 `5fa65ed37998a0958da75da5dc9a69f85bd26a05c2f64a118072c7527f56af23`, 502 checkpoint rows, and 502 gzip raw reports. No whole or subtest status changed and no former PASS regressed. The first failure of `grid-self-alignment-stretch-vertical-lr-001.html` improved from expected width10/actual100 to expected10/actual60; it remains FAIL because vertical inline text geometry is still missing.

[Shards 0](shard-0000/report.json), [128](shard-0128/report.json), [256](shard-0256/report.json), and [384](shard-0384/report.json) retain every raw callback. q1143-i01 read-only recensus cleared. p189 whole1,783 testharness and p173 all3,805 PASS remain uncleared. Browser, runner, WPT, public ABI, Master, and Acid output were unchanged in this Queue. Shared projection/GitHub publication pending with Q1; no push.
