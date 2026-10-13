# q1068-i01 — uniform fixed-font testharness census

Pinned WPT `2d66b9b7998bb58c336138c178323ddee857b586`, fixed Interop 2025 manifest 3,805 unique IDs including 1,783 testharness. The same 1,455 physical-HTML single-localhost-HTTP eligible IDs as q1063/q1064 were rerun in twelve bounded shards with the same host browser, fixed Mahora/JetBrainsMono/DroidSansFallbackFull font directory, 5 s virtual settle and 20 s process limit. The 328 excluded IDs retain explicit unrun reasons; none count as PASS. This is a local WPT callback census, not an official Interop score.

| Outcome | q1063/q1064 prior, without explicit fonts | q1068 uniform fixed fonts |
| --- | ---: | ---: |
| PASS | 160 | **223** |
| FAIL | 910 | **1,135** |
| No completion | 299 | **10** |
| No output | 73 | **73** |
| Harness error | 13 | **14** |
| **Total eligible** | **1,455** | **1,455** |

Transitions: 63 FAIL→PASS and 19 no-completion→PASS; 269 no-completion→FAIL, one no-completion→harness error; 19 previous PASS→FAIL. The latter are 14 anchor-positioning, four sideways writing-mode scrollIntoView, and one Flex radio minimum-size ID. Exact IDs are in `analysis.json`. Prior measurements used different font configuration and earlier browser code, so these are change candidates, not yet proven engine regressions. In the same-font q1066 subset of 280 CSS Grid IDs, 19 PASS and 260 FAIL remained stable; the one old process error became a completion-bearing FAIL after q1067's GC fix.

Verification: all twelve shards exited 0; their header/report fixed-font condition, eligible-list SHA256 and exact 1,455 unique ID set agree. All 1,455 gzip raw reports match checkpoint/report ID and outcome; focus totals and transitions were recomputed by ID. Runner unit 6/6 and Python compile PASS; `git diff --check` PASS. This Queue changed only the census runner's explicit font option and did not alter browser/WPT/ABI/Master, so host build and Acid were not repeated; q1067 verified both after the last browser change.

q1068-i01 partial cleared; p189 whole uncleared. 1,232 eligible IDs are non-PASS, 328 testharness IDs remain unrun, and the 19 prior PASS→FAIL candidates need a focused investigation. p190 reftest and p173 all 3,805 PASS remain unmet. Shared Master/Queue/GitHub publication pending with Q1; no push.
