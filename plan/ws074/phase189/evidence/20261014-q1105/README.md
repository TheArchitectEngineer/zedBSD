# q1105-i01 — fixed Grid502 full recensus

The same 502 eligible fixed Grid testharness IDs as q1093 ran after q1095–q1104. Inputs and conditions were unchanged: WPT `2d66b9b7998bb58c336138c178323ddee857b586`, [q1077 selection manifest](../20261014-q1077/selection-manifest.json), localhost HTTP, three fixed fonts, 5 s settle and 20 s process bound. Four bounded shards completed exit0.

| Fixed Grid testharness | PASS | FAIL | no_output | PASS subtests | FAIL subtests |
| --- | ---: | ---: | ---: | ---: | ---: |
| q1093 baseline | 103 | 399 | 0 | 5,099 | 5,906 |
| q1105 current | **110** | **392** | **0** | **5,225** | **5,780** |

[Analysis](analysis.json) verifies the exact 502-ID set and SHA256 `5fa65ed37998a0958da75da5dc9a69f85bd26a05c2f64a118072c7527f56af23`, 502 checkpoint rows and 502 gzip raw reports. Seven whole files changed FAIL→PASS: `absolute-positioning-definite-sizes-001` and style-changes001–006. No former whole PASS regressed. At subtest level, 127 changed FAIL→PASS and one changed PASS→FAIL. The latter is `.grid 17` in still-FAIL `grid-find-fr-size-gutters-001.html`: its former PASS became width expected200/actual120, while 14 other subtests in the same file improved. The newly parsed `grid` shorthand exposes remaining span/fr intrinsic sizing behavior; this row is residual work, not a claim of non-regression. [Shards 0](shard-0000/report.json), [128](shard-0128/report.json), [256](shard-0256/report.json), and [384](shard-0384/report.json) retain every raw callback.

The remaining whole FAIL files cluster in alignment200, grid-definition42, parsing35, grid-items28, layout-algorithm28 and abspos23. These are directory counts, not root causes. The p220 vertical inline work is prerequisite to the two remaining style-changes files. Grid areas/line names/CSSOM and fr intrinsic sizing are independent further candidates. The runner's `program_sha256` covers the `browser` launcher, which remained byte-identical; it does not hash the dynamically loaded `libbrowser.so`. The source commits and final-source build logs identify the tested code; a future runner improvement should capture the library hash too.

q1105-i01 read-only recensus cleared. p189 whole1,783 testharness and p173 all3,805 PASS remain uncleared. The q1068 all-focus baseline remains the latest uniform all-focus census; this Grid subset must not be added to it. Browser, runner, WPT, public ABI, Master and Acid output were unchanged in this Queue. Shared projection/GitHub publication pending with Q1; no push.
