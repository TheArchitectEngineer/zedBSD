# q1058-i01 — residual three Flex regressions

Fixed WPT checkout `2d66b9b7998bb58c336138c178323ddee857b586`. Selection is the three q1056 old PASS→FAIL IDs remaining after q1057's five gap repairs. Static local pixel comparison is evidence of current rendering, not official Interop scoring.

| Fixed three | Local PASS | Pixel FAIL | Error | Timeout |
| --- | ---: | ---: | ---: | ---: |
| Baseline | 0 | 3 | 0 | 0 |
| After auto-main-axis reverse | 1 | 2 | 0 | 0 |
| After flex min/max redistribution | **2** | **1** | **0** | **0** |

`flex-direction-row-vertical.html` has an indefinite vertical main axis and RTL row. The engine previously skipped reverse placement when the main size was indefinite; reversing within the sum of item outer sizes makes it pixel exact PASS. Both textarea tests revealed min/max freezing: after one item reached a min/max, the remaining free space was not redistributed. A bounded grow loop, matching [Flexbox §9.7](https://www.w3.org/TR/css-flexbox-1/#resolve-flexible-lengths), made the horizontal textarea test pixel exact PASS. The vertical textarea still differs by 198 pixels: text glyphs and anti-aliased dotted border at the first split of the fractional 108.8/87.1 pixel reference. Its other min/max case differences disappeared. The remaining single reftest needs separate fractional form-control rendering analysis, not a speculative Flex rounding change.

Verification: `sh plan/ws074/tests/host-build.sh plain` exit0 warning0. Fixed three `report.json`, `../20261014-q1058-reverse/report.json`, and `../20261014-q1058-clamp/report.json` preserve each step and side-by-side failure images. `python3 plan/ws074/tests/run-physical-justify-tests.py` 21/21, host CSS rule51/51, CSSOM12/12. Fixed Flex10 set 10/10 local pixel exact PASS at `../20261014-q1058-flex/report.json`. `python3 plan/ws074/tests/run-acid-tests.py --out build/ws074-acid-q1058`: Acid3 score/inventory100/100, pixel diff0/480000; Acid2 diff1865/120000 unchanged. `git diff --check` PASS. `clang-format` unavailable; changed source manually checked against C full standard.

q1058-i01 partial cleared, p206 whole uncleared. One old eight-case regression remains, with other p190 pixel/runner failures and p173 all 3,805 PASS still open. Shared Master/Queue/GitHub publication pending with Q1; no push.
