# q1047: negative main-axis free space in Flexbox

Fixed WPT commit `2d66b9b7998bb58c336138c178323ddee857b586`. After q1046, the selected ten `flexbox-justify-content-*.xhtml` reftests had 1 local PASS / 9 FAIL / 0 timeout. Both horizontal cases showed start-aligned boxes where the fixed references required negative overflow for `flex-end` and `center`.

`layout/flex.c:flex_place_line` previously clamped negative remaining main-axis space to zero. It now keeps signed space for `flex-end` and `center`; distributed values still fall back to start when no space can be distributed. This matches [CSS Flexbox §8.2](https://www.w3.org/TR/css-flexbox-1/#justify-content-property) for end/center/space-between/space-around and [CSS Box Alignment §4.3](https://www.w3.org/TR/css-align-3/#distribution-values) for space-evenly fallback.

`python3 plan/ws074/tests/run-flex-negative-justify-tests.py`: 5/5 absolute layout offset cases PASS. End=-5px, center=-2.5px, space-between/around/evenly=0px for a 35px item inside a 30px flex container.

`python3 plan/ws074/tests/run-wpt-reftests.py --root css/css-flexbox --selection-manifest /tmp/ws074-q1046-selection.json --render-timeout 5 --limit 0 --out plan/ws074/phase197/evidence/20261014-q1047`: fixed ten IDs at 800×600/fixed fonts, **3 local PASS / 7 FAIL / 0 timeout / 0 error**. Both horizontal IDs changed FAIL→PASS; prior vertical PASS was preserved. [Report](report.json) and [checkpoint](progress.jsonl) retain results. Seven vertical/writing-mode differences remain.

`sh plan/ws074/tests/host-build.sh plain`: warning 0. `python3 -m py_compile plan/ws074/tests/run-flex-negative-justify-tests.py` and `git diff --check` PASS. `clang-format` unavailable. `python3 plan/ws074/tests/run-acid-tests.py --out build/ws074-acid-q1047`: Acid3 score100/inventory100/100/pixel diff0/480000; Acid2 diff1865/120000, unchanged. Local reftest results do not yet certify official WPT environment equivalence.
