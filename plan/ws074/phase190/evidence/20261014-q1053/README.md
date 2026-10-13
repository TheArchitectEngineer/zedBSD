# q1053-i01 evidence — fixed static reftest re-census

Fixed WPT checkout `2d66b9b7998bb58c336138c178323ddee857b586`, Interop manifest 3,805 unique IDs. The [report](report.json) and [fsync checkpoint](progress.jsonl) contain the same 1,561 static eligible reftests at 800×600 with fixed fonts, one localhost origin and a 5 s bound per render. The remaining 413 reftests retain the q1044 unsupported classification (402 scripted test, 5 scripted reference, 6 variant). [ID transitions and focus counts](analysis.json) compare this run with q1044 at the same fixed ID set; no ID changed.

| Census | Local PASS | FAIL | Error | Timeout |
| --- | ---: | ---: | ---: | ---: |
| q1044 | 500 | 1,051 | 0 | 10 |
| q1053 | 518 | 684 | 359 | 0 |

Transitions: 469 PASS→PASS, 39 FAIL→PASS, all 10 timeout→PASS (the fixed Flexbox XHTML set), 676 FAIL→FAIL, 336 FAIL→error, 23 PASS→error, and 8 PASS→FAIL. All 359 errors say `Operation not supported`; every failed source/reference file contains a `<!DOCTYPE` within its first 1,000 bytes. The strict top-level XHTML path introduced in q1046 invokes the XML parser, which deliberately returns `ENOTSUP` for DOCTYPE. This is a source-loading limitation, not a pixel FAIL; the 23 former PASS affected by it must be restored before interpreting the new aggregate rate. The 8 PASS→FAIL IDs are recorded in analysis.json and require separate investigation after the loading repair.

Focus outcomes: layout 449 PASS/576 FAIL/280 error; writingmodes 14 PASS/50 FAIL/74 error; anchor-positioning 0 PASS/16 FAIL/5 error; backdrop-filter 31/20; details 12/11; scope 1/1; view-transitions 2/0; webcompat 9/16. Focus counts can overlap because official selection gives a test several focus labels.

Verification: `python3 plan/ws074/tests/run-wpt-reftests.py --root . --selection-manifest plan/ws074/phase173/evidence/20261013-q1012/manifest.json --render-timeout 5 --limit 0 --out plan/ws074/phase190/evidence/20261014-q1053`; exit0. Selected result IDs are exactly q1044's 1,561, each unique; checkpoint has one header and 1,561 result rows; all q1053 statuses match the final report; q1044/q1053 unsupported summaries match. `git diff --check` PASS. This Queue changed no browser/WPT/runner code, so host build and Acid remeasurement were not part of it.

q1053-i01 partial cleared; p190 whole uncleared. Results are local static-runner evidence, not official WPT certification. Next: a bounded XML DOCTYPE subset that preserves strict entity handling, then remeasure the affected IDs and former PASS→FAIL cases. Shared Master/Queue/GitHub publication pending with Q1; `plan/master.md` unchanged, no push.
