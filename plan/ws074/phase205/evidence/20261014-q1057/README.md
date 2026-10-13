# q1057-i01 — logical margin mapping for vertical-lr Flex references

Fixed WPT checkout `2d66b9b7998bb58c336138c178323ddee857b586`. The five selected `gap-001-lr` through `gap-005-lr` reftests were q1044 local PASS, q1053/q1056 pixel FAIL. Each reference uses a logical margin, while the prior CSS parser aliased logical names to horizontal-LTR physical sides before the element's writing mode was computed. [CSS Logical Properties 1 §4.2](https://www.w3.org/TR/css-logical-1/#margin-properties) defines mapping by computed writing mode and direction.

`internal.h` now gives the four margin longhands their own internal property IDs. `values.c` preserves identity for longhands, two-value shorthands and CSS-wide keywords. `cascade.c` computes the final writing mode and direction first, then maps logical margins to physical margins while preserving source order against physical declarations. No public ABI or WPT source changed.

- `sh plan/ws074/tests/host-build.sh plain`: exit 0, warning 0.
- `build/ws074-host/plain/host-css-rule-model`: 51/51, including vertical-lr LTR/RTL, both shorthands, physical/logical declaration order and horizontal RTL. `host-cssom` and `host-cssom-mutation`: 12/12 each.
- `python3 plan/ws074/tests/run-wpt-reftests.py --root css/css-flexbox --selection-manifest plan/ws074/phase205/evidence/20261014-q1057/selection-manifest.json --render-timeout 5 --limit 0 --out plan/ws074/phase205/evidence/20261014-q1057`: **5/5 local pixel exact PASS**, 0 error/timeout. `report.json` and `progress.jsonl` retain exact outcomes.
- Fixed Flex 10-ID regression: 10/10 local pixel exact PASS, `../20261014-q1057-flex/report.json`.
- `python3 plan/ws074/tests/run-acid-tests.py --out build/ws074-acid-q1057`: Acid3 score/inventory100/100 and pixel difference0/480000; Acid2 difference1865/120000, unchanged. Disposable artifacts under `build/`.
- `git diff --check`: PASS. `clang-format` unavailable; manual C full-standard check completed for changed source and fixture.

p205 whole cleared; q1057-i01 cleared. The other three of q1056's old eight regressions and p190's other pixel/runner failures remain. Static local PASS is not official Interop 2025 100%. Shared Master/Queue/GitHub publication belongs to Q1 and remains pending; no push.
