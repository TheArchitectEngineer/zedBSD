<!-- awesome-plan project=zedbsd record=ws074-p099 -->

# ws074-p099: Acid2 exact rendering

Status: cleared
Disposition: normal
Parent: [WS074](../ws.md)
Queue: q507-i01

## Purpose

Bring the pinned historical Acid2 diagnostic from the p097 baseline of 90.56%
pixel agreement to an exact match. Use the existing p097 harness, viewport,
fonts, and pinned WPT revision so that the result is directly comparable.

## Scope

- Reproduce and classify the Acid2 test/reference image difference.
- Correct general HTML, CSS, layout, image decoding, or paint behavior
  responsible for the difference. Do not add Acid2-specific coordinate or
  pixel exceptions.
- Add or update focused fixtures for every corrected behavior.
- Keep Acid3, the full WPT CSS2 run, ES modules, shell features, HAL, public
  ABI, vendor sources, and toolchain changes outside this Phase.

## Acceptance criteria

- `run-acid-tests.py` reports Acid2 `pass: true`, 100.00% pixel agreement,
  zero crashes, and zero timeouts with WPT commit
  `2d66b9b7998bb58c336138c178323ddee857b586`.
- Every implementation change is a general behavior correction covered by a
  focused regression, not a test-specific exception.
- The plain and ASan host builds complete with no warnings, and the focused
  regressions pass in both builds.
- `style-check.py` reports no finding in changed C/header files and
  `git diff --check` passes.
- Results, commands, limitations, and any residual work are recorded here.

## Prerequisites and constraints

- ws074-p097 is cleared and supplies the fixed harness and baseline.
- Apply [Guardrail](../../guardrail.md), the full
  [C coding standard](../../coding-style.md), and [design](../design.md) §14.
- Do not run aggregate `make check`. Do not use QEMU console or serial logs as
  evidence. This host rendering Phase does not require a guest or boot test
  unless a source change affects the guest-only path.
- The investigation is bounded to the Acid2 difference clusters and their
  focused regressions. If exact equality requires a product decision, a major
  architecture change, or scope outside the listed browser modules, end the
  attempt uncleared and record the resume condition.

## Execution record

Started 2026-09-30 from commit `83bdae50` by the current user instruction
“Run ws074.” q507-i01 cleared on 2026-09-30.

The pinned p097 baseline reproduced at Acid2 90.56% pixel agreement. DOM,
computed-style, layout, paint-list, decoded-image, and pixel-cluster comparison
identified independent general behavior gaps rather than a single offset. The
implementation now:

- applies `max-height` before `min-height`, treats unresolved percentage height
  as `auto` while laying out descendants, and preserves the CSS rule that the
  minimum wins conflicting constraints;
- validates duplicate `background` components, parses
  `background-attachment`, and positions `fixed` backgrounds against the
  viewport;
- loads successfully decoded `<object data>` images as replaced content while
  retaining fallback children when decoding fails, and reconstructs Adam7 PNG
  passes in `libpng-compat`;
- paints zero-content border boxes as triangular CSS borders, paints in-flow
  block decorations before sibling floats and their inline content afterward,
  and includes background images on inline replaced boxes; and
- matches integer premultiplied compositing for whole image texels while
  retaining nearest rounding for shapes and fractional coverage.

The focused `acid-features.html` fixture covers the corrected height,
background, point-border, failed/successful object, and Adam7 paths. Existing
paint goldens were reviewed and refreshed for the corrected CSS painting order.
The Acid harness now retains DOM/style/layout/paint dumps for future diagnosis;
comparison helpers fall back to the checked-in fonts when the old staged font
directory is absent.

## Verification

Environment: Debian host `cc 14.2.0`, WPT
`2d66b9b7998bb58c336138c178323ddee857b586`, 400x300 Acid2 viewport, checked-in
Inter/JetBrains Mono/Droid Sans Fallback fonts. The source commit at start was
`83bdae506a0e`; the result and this record are carried by the same local `WIP`
commit.

- `make toolchain-cache` and `make toolchain`: PASS. Stale generated build
  caches were preserved under `/tmp`, the verified rev-0 LLVM/Noct toolchain was
  rebuilt, and `plan/tools/toolchain-lock.sh lock` restored read-only state.
- `sh plan/ws074/tests/host-build.sh plain`: PASS, no warnings.
- `sh plan/ws074/tests/host-build.sh asan`: PASS, no warnings.
- `python3 plan/ws074/tests/run-acid-tests.py --out
  build/ws074-acid-p099-final-plain`: Acid2 100.00%, PASS, no crash or timeout.
- `env ASAN_OPTIONS=detect_stack_use_after_return=0 python3
  plan/ws074/tests/run-acid-tests.py --program
  build/ws074-host/asan/browser --out build/ws074-acid-p099-final-asan`: Acid2
  100.00%, PASS, no sanitizer report, crash, or timeout.
- Plain and isolated ASan `golden-dumps.sh ... style layout paint`: 62 checked,
  0 failed in each configuration. A parallel sandboxed ASan invocation once
  produced an empty first dump because of sanitizer startup restrictions; the
  isolated approved rerun passed completely.
- Plain and ASan `host-object`: 100 checks, 0 failed in each configuration.
- `sh plan/tools/files/host-png.sh build/ws074-p099-png-final`: PASS, including
  exact Adam7 RGBA reconstruction and damaged-CRC refusal.
- `gpu-compare.py` on `acid-features.html` and Acid2: agree; largest channel
  differences 0 and 1 respectively, with 0 pixels over the threshold.
- `style-check.py` on all changed C/header files, Python byte compilation of
  the three changed helpers, and `git diff --check`: PASS.

The plain Acid2 test and reference PNGs are byte-identical with SHA-256
`d4bc344146d44577143ff678d2044fdfd8218468391b2c90feee95c3d2c05f25`.
No guest/boot test was run because the changed decoding, layout, and render
paths were exercised by the required plain/ASan host builds and the Phase does
not change a guest-only path. Aggregate `make check` was not run by instruction.

Acid3 remains score 9 and 40.35% pixel agreement with its pre-existing
`TypeError`; it is deliberately residual work for p100 and does not affect this
Phase's Acid2 acceptance. GitHub publication was not requested and remains
pending; the local plan is authoritative for this handoff.
