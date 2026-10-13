# q1062-i01 — static reftest census after q1061

Fixed WPT commit `2d66b9b7998bb58c336138c178323ddee857b586`; fixed Interop 2025 manifest 3,805 unique IDs. The same 1,561 static eligible reftests as q1060 were measured with localhost, 800×600, fixed fonts, and a 5 s per-render bound. The other 413 reftests remain runner unsupported (402 scripted tests, 5 scripted references, 6 variants). Local pixel comparison is not official Interop scoring.

| Run | Local PASS | Pixel FAIL | Load error | Timeout |
| --- | ---: | ---: | ---: | ---: |
| q1060 | 660 | 901 | 0 | 0 |
| q1062 | **662** | **899** | **0** | **0** |

q1060→q1062: 2 FAIL→PASS, 899 FAIL→FAIL, 660 PASS→PASS, **0 PASS→FAIL**. Newly passing IDs are `css/css-flexbox/flexbox-flex-wrap-flexing.html` and `css/css-flexbox/flexbox-sizing-horiz-001.xhtml`. The eight older regression IDs all remain PASS. `analysis.json` records exact IDs, transitions and focus groups.

Verification: `python3 plan/ws074/tests/run-wpt-reftests.py --root . --selection-manifest plan/ws074/phase173/evidence/20261013-q1012/manifest.json --render-timeout 5 --limit 0 --out plan/ws074/phase190/evidence/20261014-q1062` exited 0. Exact selected ID set is 1,561; `progress.jsonl` contains a header and 1,561 unique results matching `report.json`. Analysis assertions checked all statuses and transitions. No browser/runner/WPT code changed; q1061 already verified host build and Acid after the last browser edit.

q1062-i01 partial cleared; p190 whole uncleared. The 899 pixel FAILs and 413 runner-unsupported reftests remain, as does the p173 goal of all 3,805 PASS. Shared Master/Queue/GitHub publication remain pending with Q1; no push.
