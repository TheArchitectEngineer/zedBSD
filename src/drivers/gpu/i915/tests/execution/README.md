# i915 kernel execution tests

The in-kernel tests of the i915 driver: the unit test suite (`ktest`) and the
GPU execution tests (compute, draw, texture) on the started GT.  They are
linked only into the test build (`I915_TESTS=y` in `platform/amd64/vmunix.mk`);
the production kernel does not contain them.

## How a test runs

`device.c` calls the weak checkpoint `drv_i915_test_after_start(device)` once,
on the start worker, after the device has started (GT, interrupts, display)
and right before the node is published and served.  `runner.c` defines it in
the test build: it selects one scenario by the compile-time name
`I915_TEST_SCENARIO` (only `tests/` reads it), runs it, logs one line
`i915: test <name>: PASS` / `FAIL rc=<errno>` (display scenarios log their own
verdict and the runner logs `end`), and returns, so the node is served as in
production.  The GPU scenarios therefore own the render engine while they run;
they leave it parked on its kernel context, put their fixture pages back to
scratch, invalidate the GT TLBs and free their objects, so the served node
(for example vkdemo in the vkloop image) runs normally afterwards.  After a
hang the engines are reset and the objects are kept.

On the hardware (the machine is shared; always take the lock):

    flock /tmp/i915-hw.lock plan/ws031/tests/vkloop-hw.sh test ktest
    flock /tmp/i915-hw.lock plan/ws031/tests/vkloop-hw.sh test eu

| Scenario | Function | What it checks |
| --- | --- | --- |
| `ktest` | `drv_i915_test_execution_ktest` | the unit test suite (below); PASS when no check failed |
| `eu` | `drv_i915_test_execution_eu` | multicast workaround probe, compute (GPGPU_WALKER, one SIMD8 thread, A64 store), then 3 more requests on the same context and 2 on a new one |
| `draw` | `drv_i915_test_execution_draw` | the single-colour RECTLIST draw, 1024 pixels |
| `r1` | `drv_i915_test_execution_r1` | draw repeats and compute/3D switching on one and on new contexts (12 steps) |
| `tex` | `drv_i915_test_execution_tex` | the first textured draw, 1024 pixels, texture untouched |
| `t3` | `drv_i915_test_execution_t3` | texture update, binding switch, redraw on the same and a new context (9 steps) |
| `bl` | `drv_i915_test_execution_bl` | bilinear filtering, exact comparison (4 steps) |
| `lcdb` ... `display_ktest` | `drv_i915_test_display_<name>` | the display scenarios of `tests/display/` (weak in the runner's table until linked) |

## Files

- `runner.c` — the checkpoint and the scenario table.
- `ktest.c`, `ktest.h` — the tally and the order of the unit test parts.
- `ktest-sync.c` — completions, work queues, MSI vectors, write-combining, reset, PCODE, DRAM/bandwidth, runtime PM, sleep-range, preemption, late fuse.
- `ktest-display.c` — DRM/vblank bookkeeping, VBT, VGA and power-domain map, power wells, combo PHY, CDCLK, power-domain init, DMC, display state.
- `ktest-display-probe.c` — PCH, display interrupt reset/postinstall/ack/hooks, the nogem probe (watermarks, DPLLs, CRTCs, outputs, readout, sanitize).
- `ktest-gt.c` — GT fuses and interrupts, workarounds/MOCS/PAT/RC6/RPS, GT memory, execlists, contexts, requests, defaults, PXP, HPD setup, the draw/texture fixtures and the page-table walk.
- `eu-test.c`, `eu-internal.h`, `eu-test.h` — the compute test and the request path all GPU tests share.
- `draw-test.c` — draw, R1, texture, T3 and bilinear tests.
- `fhd-render.c`, `fhd-render.h` — the full-HD textured draw into a caller's buffer (used by the display scenarios).
- `ppgtt-walk.c` — a read-only walk of the GT address space, as the GPU does it.
- `firmware-override.c`, `firmware-override.h` — the test build's answer to the firmware provider's weak request checkpoint.
- `../fixtures/` — the draw/texture fixtures (`draw-fixture.c`, the generated `tex-fixture*-gen.inc`) and `vkref-generated.inc`.

## Dropped tests

Tests of retired code, of test hooks that production no longer has, and of
paths that cannot be exercised while the real device runs.  The unit suite
keeps one check per old check otherwise, so the counts stay comparable.

### Not ported (other owners)

- The display unit tests that lived in their own files (eDP, eDP sync, LCD
  modeset, scanout, LCD show, LCD-G, OpRegion, HPD) are run by the display
  scenario `display_ktest` (`tests/display/`), not by `ktest`.

### ktest-sync.c

| Old test | Reason |
| --- | --- |
| K5 `complete_all` (2 checks) | the new `sync.h` has no complete-all; nothing in the driver uses it |
| Time-source fault handling (4 checks) | the hook that swapped in a fake time source was retired; a fault would also latch the driver-wide time-base fault (`sync.c`) of the live device |
| `skl_pcode_request` time-base anomaly (1) | same |
| PCODE preemption-off region, part (b): counter fault inside the region (1) | same; part (a) runs on the real clock |
| Timer next-event / tick calculation (8) | the calculation (`timer_calc`) is retired with the diagnostic runner |
| IRQ-context completion one-shot (4, skipped at run time) | `kern_diag_oneshot_arm` is built only with `CONFIG_DRIVER_PCI_I915_PARITY`, which the new build does not set |
| Real preemption A/B (1, skipped at run time) | same one-shot hook |

### ktest-display.c

| Old test | Reason |
| --- | --- |
| DRM per-pipe worker-create failure unwind, dev_fini reclaim (2) | the worker-fail hook no longer exists |
| Power well: time-base anomaly during the ACK wait (1) | scripted time source retired; the fault is driver-wide |
| D3 D-FAULT stop and driver-remove (2) | same |
| DMC-FINI(running) and DMC-FINI(MMIO fault) (4) | the loader's pause and fault-at test hooks were dropped from production |
| DS-ENOMEM (1) | the global-state allocation can no longer fail and its failure hook is gone |
| DS-QUIRKS forced-DMI sub-case (part of 1) | the DMI match hook no longer exists; the PCI sub-cases run |
| `intel_bios_init` fallback and VBT-EXPLICIT/CHILDREN/PANEL/DEFAULTS (8, skipped at run time) | the VBT parser's world is a driver-wide pointer bound to the live display (`display/vbt.c` `i915_vbt_bound_world`): a second world gets EBUSY |

### ktest-display-probe.c

| Old test | Reason |
| --- | --- |
| PCH-QEMU and the scripted-bridge parts of PCH-REAL/SKIP/NONE/NOP (1 whole, 1 partial) | production has no scripted ISA-bridge hook any more; `detect_pch` scans the real bus, which the test must not do on the live device |
| IRQ-DRAIN-EIO (1) | time-base fault injection was removed from production |

### Execution tests and fixtures

| Old | Reason |
| --- | --- |
| `selftest.c`: `drv_i915_selftest`, clear, RCS, RT, compute and draw selftests, golden run, power probe, statistics read, engine WA application | legacy-hardware selftests of the retired legacy engine/GGTT/LRC code; the draw/texture fixtures they carried moved to `tests/fixtures/draw-fixture.c` byte-for-byte |
| `probe.c` `PARITY_*_TEST` branches | replaced by the runner's scenarios `eu`, `draw`, `r1`, `tex`, `t3`, `bl` |
| Reference-kernel build (`I915_VK_REFERENCE_KERNELS`, `vkref-generated.inc`) and gfx census | not provided: the render path has only the after-draw checkpoint (`drv_i915_gfx_draw_checkpoint`); substituting reference kernels changes the kernels, the URB entry size and the 3DSTATE_VS/SBE/WM/PS words at several places in `render/`, which needs new production seams.  The generated table is kept in `tests/fixtures/vkref-generated.inc` |

### ktest-gt.c

| Old test | Reason |
| --- | --- |
| P6B-RC6, second check (1) | the `skip_rc6` test switch is gone from production |
| P6C3B-PARSE (1) | `gen12_csb_parse` is static in `submit.c`; the same property is covered by P6C3B-CSB through the public CSB processing |
| P6C4B-SUBMIT, WAIT, PARK x2, DEFAULT (5) | the default-context submit/poll/finish helpers are static in `defaults.c`; INIT, INHERIT (with an image the test builds) and WEDGE remain |
| P7-IPC (1) | `i915_skl_watermark_ipc_init` is static in `display/display.c` |
| SSEU, CLOCK, FAULT, P6C2-OFFSETS/SIZE/RPCS | kept, but observed through the public API (`drv_i915_gt_init_mmio`, the laid-out context image, `drv_i915_lrc_update_regs`) instead of the production statics |
