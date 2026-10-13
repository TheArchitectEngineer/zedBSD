# q1056-i01 — static reftest census after XHTML loading repair

Fixed WPT checkout `2d66b9b7998bb58c336138c178323ddee857b586`, fixed Interop manifest 3,805 unique IDs. Static eligible 1,561 reftests were run under the same localhost, 800×600, fixed fonts and 5 s per-render bound as q1044 and q1053. The other 413 reftests remain unsupported by this runner (402 scripted tests, 5 scripted references, 6 variants). Results are local pixel comparisons, not official Interop scoring.

| Run | Local PASS | Pixel FAIL | Load error | Timeout |
| --- | ---: | ---: | ---: | ---: |
| q1044 | 500 | 1,051 | 0 | 10 |
| q1053 | 518 | 684 | 359 | 0 |
| q1056 | **640** | **921** | **0** | **0** |

Compared with q1053, all 359 load errors now render: 122 local PASS, 237 pixel FAIL. Its 518 PASS and 684 FAIL retain their status. Compared with q1044, 157 FAIL and 10 timeout cases become PASS; 27 former PASS become pixel FAIL, including q1053's 8 previously identified cases. The other 19 had been q1053 load errors and are now real pixel FAIL, not proof of a newly introduced rendering regression. The old 8 remain FAIL. `analysis.json` records exact IDs, transitions, focus breakdown and former errors.

Verification: `python3 plan/ws074/tests/run-wpt-reftests.py --root . --selection-manifest plan/ws074/phase173/evidence/20261013-q1012/manifest.json --render-timeout 5 --limit 0 --out plan/ws074/phase190/evidence/20261014-q1056` exited 0. The 1,561 selected IDs match q1044/q1053 exactly. `progress.jsonl` has one header and 1,561 unique result lines whose statuses match `report.json`; analysis assertions passed. `git diff --check` passed. This Queue changed no browser/WPT/runner code, so host build and Acid remeasurement were not repeated; q1055 measured both after the last browser change.

q1056-i01 partial cleared; p190 whole uncleared. Next: isolate the 8 earlier PASS→FAIL cases, then address the larger pixel FAIL groups and unsupported runner categories. p173 all 3,805 PASS remains unmet. Shared Master/Queue/GitHub publication pending with Q1; no push.
