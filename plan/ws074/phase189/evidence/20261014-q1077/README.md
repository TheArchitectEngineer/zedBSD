# q1077-i01 — CSS Grid testharness recensus after min-content/replaced sizing

Pinned WPT `2d66b9b7998bb58c336138c178323ddee857b586`, fixed Interop 2025 manifest 3,805 unique IDs. The [same 502 eligible Grid testharness IDs](selection-manifest.json) as q1074 ran against p214/p215's final host browser through single localhost HTTP, three fixed fonts, 5 s settle and 20 s process limit. Four ≤128-ID shards finished exit0. [Analysis](analysis.json) verifies the selected ID set/hash `5fa65ed37998a0958da75da5dc9a69f85bd26a05c2f64a118072c7527f56af23`, every shard checkpoint and gzip raw callback against its report row, and each ID against q1074.

| Fixed Grid testharness | PASS | FAIL | no_output | PASS subtests | FAIL subtests |
| --- | ---: | ---: | ---: | ---: | ---: |
| q1074 before p214/p215 | 57 | 413 | 32 | 1,851 | 5,954 |
| q1077 after p214/p215 | **71** | **399** | **32** | **1,877** | **5,928** |

Fourteen whole IDs changed FAIL→PASS, with no old PASS regression. [Analysis](analysis.json) lists all 14 and 17 IDs with changed PASS subtest counts. The 32 no_output IDs remain two positioned Grid descendant series with `<script type="module">`. Among the 399 remaining whole FAIL, 206 are under `alignment/`, 42 `grid-definition/`, 35 `parsing/`, 28 each `grid-items/` and `layout-algorithm/`, and 24 `abspos/`. These are directory counts, not proven root causes. FAIL and no_output remain non-PASS.

[Shards 0](shard-0000/report.json), [128](shard-0128/report.json), [256](shard-0256/report.json), [384](shard-0384/report.json) preserve result rows, fsync checkpoints and 502 compressed raw callbacks. Runner/browser/WPT/ABI/Master code did not change after p215's final build and Acid verification, so q1077 ran measurement only. `git diff --check` passed. This is local runner coverage, not the official Interop score.

q1077-i01 partial cleared; p189 whole 1,783 testharness and p173 all 3,805 PASS remain uncleared. The all-focus eligible baseline is still q1068's 223 PASS/1,232 nonPASS until a uniform all-focus recensus; Grid subset values should not be added to it. Shared Queue/Master/GitHub publication remain pending with Q1; no push.
