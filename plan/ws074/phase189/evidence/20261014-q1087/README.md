# q1087-i01 — Grid testharness recensus after inline modules

The same fixed 502 eligible Grid testharness IDs as q1077 ran against the q1086 host browser. Inputs and conditions were unchanged: WPT `2d66b9b7998bb58c336138c178323ddee857b586`, [q1077 selection manifest](../20261014-q1077/selection-manifest.json), localhost HTTP, three fixed fonts from `userland/desktop/fonts`, 5 s settle and 20 s process limit. Four bounded shards finished exit0.

| Fixed Grid testharness | PASS | FAIL | no_output | PASS subtests | FAIL subtests |
| --- | ---: | ---: | ---: | ---: | ---: |
| q1077 before inline modules | 71 | 399 | 32 | 1,877 | 5,928 |
| q1087 after q1086 | **103** | **399** | **0** | **5,077** | **5,928** |

The 32 changed IDs are exactly the former `no_output` module pages, each now PASS with 100 subtests. No former PASS regressed. [Analysis](analysis.json) verifies the exact 502-ID set and hash `5fa65ed37998a0958da75da5dc9a69f85bd26a05c2f64a118072c7527f56af23`, every checkpoint row and compressed raw report against its classification. [Shards 0](shard-0000/report.json), [128](shard-0128/report.json), [256](shard-0256/report.json), [384](shard-0384/report.json) retain all 502 raw callback reports.

The remaining 399 whole FAIL cases cluster under `alignment/` 206, `grid-definition/` 42, `parsing/` 35, `grid-items/` 28, `layout-algorithm/` 28 and `abspos/` 24. These are directory counts, not proven shared causes. q1087-i01 partial cleared; p189 whole 1,783 testharness and p173 all 3,805 PASS remain uncleared. The q1068 all-focus baseline of 223 PASS/1,232 nonPASS remains the latest uniform all-focus result; this Grid subset must not be added to it. Shared Queue/Master/GitHub publication pending with Q1; no push.
