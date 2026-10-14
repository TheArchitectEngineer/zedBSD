# q1151-i01 — fixed Grid502 recensus after implicit auto tracks

The same 502 eligible fixed Grid testharness IDs as [q1147](../20261014-q1147/README.md) ran after q1150's bounded vertical implicit auto-track implementation. Inputs and conditions were unchanged: WPT `2d66b9b7998bb58c336138c178323ddee857b586`, [q1077 selection manifest](../20261014-q1077/selection-manifest.json), localhost HTTP, three fixed fonts, 5 s settle, and 20 s process bound. Four bounded shards completed exit0.

| Fixed Grid testharness | PASS | FAIL | no_output | PASS subtests | FAIL subtests |
| --- | ---: | ---: | ---: | ---: | ---: |
| q1147 baseline | 134 | 368 | 0 | 5,171 | 5,834 |
| q1151 current | **136** | **366** | **0** | **5,173** | **5,832** |

[Analysis](analysis.json) verifies the exact 502-ID set and SHA256 `5fa65ed37998a0958da75da5dc9a69f85bd26a05c2f64a118072c7527f56af23`, 502 checkpoint rows and 502 gzip raw reports. Two whole files and their one subtest each changed FAIL→PASS: `alignment/grid-self-alignment-stretch-vertical-lr-009.html` and `-013.html`. No whole PASS or subtest regressed. Remaining `-010`/`-014` add margins, `-011`/`-015` borders, and `-012`/`-016` padding to their Grid items. The q1149 source classification was corrected accordingly in its linked evidence.

[Shards 0](shard-0000/report.json), [128](shard-0128/report.json), [256](shard-0256/report.json), and [384](shard-0384/report.json) retain every raw callback. q1151-i01 read-only recensus cleared. p189 whole1,783 testharness and p173 all3,805 PASS remain uncleared. Browser, runner, WPT, public ABI, Master, and Acid output were unchanged in this Queue. Shared projection/GitHub publication pending with Q1; no push.
