# q1147-i01 — fixed Grid502 recensus after orthogonal items

The same 502 eligible fixed Grid testharness IDs as [q1145](../20261014-q1145/README.md) ran after q1146's bounded horizontal-tb items in vertical-lr Grid. Inputs and conditions were unchanged: WPT `2d66b9b7998bb58c336138c178323ddee857b586`, [q1077 selection manifest](../20261014-q1077/selection-manifest.json), localhost HTTP, three fixed fonts, 5 s settle, and 20 s process bound. Four bounded shards completed exit0.

| Fixed Grid testharness | PASS | FAIL | no_output | PASS subtests | FAIL subtests |
| --- | ---: | ---: | ---: | ---: | ---: |
| q1145 baseline | 130 | 372 | 0 | 5,167 | 5,838 |
| q1147 current | **134** | **368** | **0** | **5,171** | **5,834** |

[Analysis](analysis.json) verifies the exact 502-ID set and SHA256 `5fa65ed37998a0958da75da5dc9a69f85bd26a05c2f64a118072c7527f56af23`, 502 checkpoint rows, and 502 gzip raw reports. Four whole files and their one subtest each changed FAIL→PASS: `alignment/grid-self-alignment-stretch-vertical-lr-005.html` through `-008.html`. No whole PASS or subtest regressed. The remaining `-009`–`-016` cases use a 250px definite vertical Grid with auto tracks, outside q1146's fixed-track path.

[Shards 0](shard-0000/report.json), [128](shard-0128/report.json), [256](shard-0256/report.json), and [384](shard-0384/report.json) retain every raw callback. q1147-i01 read-only recensus cleared. p189 whole1,783 testharness and p173 all3,805 PASS remain uncleared. Browser, runner, WPT, public ABI, Master, and Acid output were unchanged in this Queue. Shared projection/GitHub publication pending with Q1; no push.
