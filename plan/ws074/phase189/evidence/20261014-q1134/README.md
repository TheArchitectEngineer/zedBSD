# q1134-i01 — fixed Grid502 full recensus

The same 502 eligible fixed Grid testharness IDs as q1105 ran after q1106–q1133. Inputs and conditions were unchanged: WPT `2d66b9b7998bb58c336138c178323ddee857b586`, [q1077 selection manifest](../20261014-q1077/selection-manifest.json), localhost HTTP, three fixed fonts, 5 s settle and 20 s process bound. Four bounded shards completed exit0.

| Fixed Grid testharness | PASS | FAIL | no_output | PASS subtests | FAIL subtests |
| --- | ---: | ---: | ---: | ---: | ---: |
| q1105 baseline | 110 | 392 | 0 | 5,225 | 5,780 |
| q1134 current | **124** | **378** | **0** | **5,118** | **5,887** |

[Analysis](analysis.json) verifies exact 502-ID set and SHA256 `5fa65ed37998a0958da75da5dc9a69f85bd26a05c2f64a118072c7527f56af23`, 502 checkpoint rows and 502 gzip raw reports. Sixteen whole files changed FAIL→PASS, including eight alignment, two parsing validity, two grid-auto-flow, two placement, one grid-model and one layout-algorithm file. Two former whole PASS regressed: `abspos/empty-grid-001.html` (six min-content width subtests expected0/actual200) and `animation/grid-template-rows-composition.html` (178 animation subtests). At subtest level, 398 changed FAIL→PASS and 505 changed PASS→FAIL. The 505 include 238 in `grid-template-rows-interpolation.html` and 178 in `grid-template-rows-composition.html`; the remaining changes include alignment baseline and empty-grid. These regressions are explicit residual implementation work.

[Shards 0](shard-0000/report.json), [128](shard-0128/report.json), [256](shard-0256/report.json), and [384](shard-0384/report.json) retain every raw callback. Remaining whole FAIL files cluster in alignment192, grid-definition42, parsing31, grid-items28, layout-algorithm27 and abspos24. These are directory counts, not root causes. q1134-i01 read-only recensus cleared. p189 whole1,783 testharness and p173 all3,805 PASS remain uncleared. Browser, runner, WPT, public ABI, Master and Acid output were unchanged in this Queue. Shared projection/GitHub publication pending with Q1; no push.
