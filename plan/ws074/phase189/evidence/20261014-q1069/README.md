# q1069-i01 — audit of 19 prior PASS→FAIL cases

The q1068 uniform fixed-font census found 19 IDs that had been PASS in q1063/q1064 without explicit fonts and became FAIL with fixed fonts: 14 anchor-positioning, four sideways writing-mode `scrollIntoView`, and one Flex radio minimum-size case. The exact IDs are in `analysis.json`.

All 19 were rerun with the **same current browser** and pinned WPT/localhost conditions as q1068, changing only removal of the explicit font arguments. All 19 returned PASS again with completion. Thus the PASS→FAIL transition is caused by the runner environment difference, not a source-code regression between q1064 and q1068. A local 100×20 box fixture reported `getBoundingClientRect()` as **0×0 without font paths** and **100×20 with fixed fonts**. The old PASS values therefore relied on missing geometry; they do not satisfy the WPT behavior under a valid layout environment. Fixed-font q1068 FAILs remain real work.

Verification: `progress.jsonl` has header plus 19 unique ordered rows matching `report.json` and the q1068 transition list. All 19 gzip raw reports have a completion with PASS and no `--font` argument. Geometry outputs match expected 0×0/100×20. `git diff --check` PASS. No browser/WPT/runner source changed, so build and Acid were not repeated; q1067 verified both after the last browser edit.

q1069-i01 partial cleared; p189 whole uncleared. Continue with the fixed-font baseline of 223 PASS/1,232 non-PASS among 1,455 eligible IDs; the 328 environment-excluded testharness IDs remain unrun. p190/p173 all 3,805 PASS remain unmet. Shared Master/Queue/GitHub publication pending with Q1; no push.
