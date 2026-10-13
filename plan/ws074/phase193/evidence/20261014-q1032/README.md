# q1032 fixed details testharness census

The pinned Interop 2025 manifest selects 46 IDs in the details focus: 18 testharness, 25 reftest and 3 crashtest. All 18 testharness IDs were run with the existing single-HTTP-origin callback runner, pinned WPT commit `2d66b9b7998bb58c336138c178323ddee857b586`, the current plain host browser and `--settle-ms 3000`. [Summary](summary.json) maps every ID to its per-test JSON.

Outcomes: **6 PASS, 10 FAIL, 2 no_output**. The 6 PASS include both hidden IDL tests, two beforematch attribute-removal tests, one element-removal test and hidden-until-found/details order. Notable remaining classes: fragment navigation/scroll event behavior; ancestor reveal recursion or detachment; missing computed `content-visibility`; text-fragment and testdriver capabilities (`fetchResults`, action_sequence); details layout/pseudo-element results. `beforematch-element-removal-002.html` and `hidden-until-found-002.html` emitted no WPT result. The 25 reftest and 3 crashtest are not run in this Queue; p190 needs their runner conditions and evidence. No outcome is inferred for them.

Manifest ID count and per-result `wpt_commit` agree with the fixed input. Browser, WPT and Master were unchanged; `git diff --check` PASS. p193 whole and the 3,805-ID objective remain uncleared. Shared Queue/Master/GitHub publication is Q1 owned; no push.
