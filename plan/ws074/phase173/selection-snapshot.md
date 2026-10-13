# Interop 2025 selection snapshot — 2026-10-13

This is a planning inventory, **not a WPT pass rate or the final scoring denominator**.

## Fixed inputs and selection rule

- [Interop 2025 focus areas](https://github.com/web-platform-tests/interop/blob/main/2025/README.md): inspected local revision `2d28de9c1e86182c38a7311854ca59cefde7ac04`.
- [wpt-metadata](https://github.com/web-platform-tests/wpt-metadata): inspected local revision `018c06b2c0fae50400c71585c73b7f238a2d029a`. `META.yml` `links[].label` applies to `results[].test`, relative to that file's directory. Investigation efforts are outside this selection.
- Existing WPT checkout: `2d66b9b7998bb58c336138c178323ddee857b586` (2026-09-27). It is sparse and does not yet contain every selected path.
- Layout is the union of `interop-2021-flexbox`, `interop-2023-flexbox`, `interop-2021-grid`, `interop-2023-grid`, `interop-2022-subgrid`; pointer/mouse uses `interop-2023-events`, as the official 2025 README test links specify. Other 17 areas use their `interop-2025-*` labels.

| Focus area / label suffix | Metadata paths | Present in sparse WPT |
| --- | ---: | ---: |
| anchor-positioning | 234 | 234 |
| backdrop-filter | 69 | 69 |
| core-web-vitals | 117 | 0 |
| details | 46 | 0 |
| layout (five carryover labels) | 2252 | 2252 |
| modules | 30 | 0 |
| navigation | 378 | 0 |
| pointer-and-mouse-events (`interop-2023-events`) | 42 | 0 |
| remove-mutation-events | 1 | 1 |
| scope | 29 | 29 |
| scrollend | 28 | 7 |
| storageaccess | 22 | 0 |
| textdecoration | 16 | 16 |
| urlpattern | 12 | 0 |
| view-transitions | 263 | 257 |
| webassembly | 12 | 0 |
| webcompat | 82 | 49 |
| webrtc | 11 | 0 |
| writingmodes | 167 | 167 |
| **All** | **3811 memberships; 3805 unique paths** | **3075 unique paths** |

The 730 absent unique paths mix sparse-checkout omissions and possible WPT/metadata revision drift. File paths may expand to multiple WPT variants; per-test metadata labels and scoring need comparison with the generated WPT manifest or official dashboard selection before treating these counts as a denominator. No browser WPT tests were run for this snapshot.

## Proposed first Queue: q1012-i01 / p173 partial scope

**Status at selection: proposed; approved and executed as q1012-i01 on 2026-10-13 23:49–23:58 JST. Timebox: at most 90 minutes.**

1. Pin the 19-area label mapping and input revisions in a reproducible manifest generator. Expand the fixed WPT checkout to the required paths, classify missing metadata paths and generated URL variants, and produce a test/area manifest plus license and environment notes. Preserve all selected tests and label overlap.
2. Inventory current WS074 WPT runner capabilities (testharness, reftest, scripted, HTTPS, multi-origin, WebDriver/interaction, workers) against the manifest. Run a bounded representative sample from each supported type and record PASS/FAIL/error/crash/timeout separately from unsupported/unrun; never count unrun as PASS.
3. Save commands, reports, manifest hashes and a coverage matrix; split later code/runner work by failure cluster into finite Phases. Maintain Acid3 fixed score100/RGB diff0 when browser source changes (none proposed here). If complete enumeration or sample execution exceeds 90 minutes, end uncleared with exact residual and a revised next scope.

**Dependencies:** p172 whole cleared; p188 merged-main Acid3 output verified. p100 whole Acid2 exact/target gate remains separate. **Allowed files:** `plan/ws074/phase173/`, `plan/ws074/ws.md`, WS074 test/manifest tooling only if needed for reproducible inventory, ignored `build/ws074-*`/`/tmp` for external checkout and temporary results. No browser engine/API implementation in this attempt. `plan/master.md` is out of scope. No other agent is used.

**Approval:** The user answered “q1012 の範囲を承認する” on 2026-10-13 23:49 JST. The prior user instruction to repeat 90-minute Queues was scoped to Acid3 and ended when its pixel target was met. [q1012 results](evidence/20261013-q1012/README.md) are retained separately from this pre-execution snapshot.
