# q1074-i01 — CSS Grid testharness recensus after auto-width fix

Pinned WPT `2d66b9b7998bb58c336138c178323ddee857b586`, fixed Interop 2025 manifest 3,805 unique IDs. The [same selected 502 Grid testharness IDs](selection-manifest.json) as q1072 ran against p213's final host browser through single localhost HTTP, three fixed fonts, 5 s settle and 20 s process limit. Four ≤128-ID shards finished exit0. [Analysis](analysis.json) verifies the full selected ID set/hash `5fa65ed37998a0958da75da5dc9a69f85bd26a05c2f64a118072c7527f56af23`, every shard checkpoint and gzip raw report against its report row, and every result against q1072.

| Fixed Grid testharness | PASS | FAIL | no_output | PASS subtests | FAIL subtests |
| --- | ---: | ---: | ---: | ---: | ---: |
| q1072 before p213 | 43 | 427 | 32 | 1,809 | 5,996 |
| q1074 after p213 | **57** | **413** | **32** | **1,851** | **5,954** |

Fourteen whole IDs changed FAIL→PASS, with no old PASS regression. The [analysis](analysis.json) lists all 14 and 16 IDs with changed PASS subtest counts. The 32 no_output IDs remain the two `<script type="module">` positioned Grid descendant series. Among the 413 remaining whole FAIL, 220 are under `alignment/`, 42 `grid-definition/`, 35 `parsing/`, 28 each `grid-items/` and `layout-algorithm/`, 24 `abspos/`; these directory counts identify investigation areas, not root causes. All no_output and FAIL remain non-PASS.

[Shards 0](shard-0000/report.json), [128](shard-0128/report.json), [256](shard-0256/report.json), [384](shard-0384/report.json) preserve result rows, fsync checkpoints and 502 compressed raw callbacks. Runner/browser/WPT/ABI/Master code did not change after p213's final build and Acid verification, so q1074 ran measurement only. `git diff --check` passed. This is local runner coverage and is not the official Interop score.

q1074-i01 partial cleared; p189 whole 1,783 testharness and p173 all 3,805 PASS remain uncleared. The all-focus eligible baseline is still q1068's 223 PASS/1,232 nonPASS until a uniform all-focus recensus; Grid subset values should not be added to it. Shared Queue/Master/GitHub publication remain pending with Q1; no push.
