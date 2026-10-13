# q1060-i01 — static reftest census after Flex and textarea repairs

Fixed WPT checkout `2d66b9b7998bb58c336138c178323ddee857b586`, Interop manifest 3,805 unique IDs. The same static eligible 1,561 reftests as q1044/q1056 were run under localhost, 800×600, fixed fonts and a 5 s per-render bound. The remaining 413 reftests keep the unsupported runner classification (402 scripted tests, 5 scripted references, 6 variants). These are local pixel comparisons, not official Interop scoring.

| Run | Local PASS | Pixel FAIL | Load error | Timeout |
| --- | ---: | ---: | ---: | ---: |
| q1044 | 500 | 1,051 | 0 | 10 |
| q1056 | 640 | 921 | 0 | 0 |
| q1060 | **660** | **901** | **0** | **0** |

From q1056 to q1060, 21 FAIL→PASS, 900 FAIL→FAIL, 639 PASS→PASS and **1 PASS→FAIL**. The new regression is `css/css-flexbox/flexbox-flex-wrap-flexing.html`; it needs separate investigation before claiming broad non-regression. The old eight q1044 PASS→FAIL cases are all local PASS in q1060. `analysis.json` records exact IDs, transitions and focus groups.

Verification: `python3 plan/ws074/tests/run-wpt-reftests.py --root . --selection-manifest plan/ws074/phase173/evidence/20261013-q1012/manifest.json --render-timeout 5 --limit 0 --out plan/ws074/phase190/evidence/20261014-q1060` exited0. All 1,561 selected IDs match q1044/q1056 exactly; `progress.jsonl` has one header plus 1,561 unique result rows and statuses matching `report.json`. Analysis assertions and `git diff --check` passed. This Queue changed no browser/WPT/runner code, so host build and Acid remeasurement were not repeated; q1059 measured both after the last browser change.

q1060-i01 partial cleared; p190 whole uncleared. Next: isolate the new flex-wrap regression and continue the remaining 901 pixel FAILs and 413 unsupported runner cases. p173 all 3,805 PASS remains unmet. Shared Master/Queue/GitHub publication pending with Q1; no push.
