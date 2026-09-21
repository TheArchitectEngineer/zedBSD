# tests/display

The display tests of the rebuilt i915 driver.  Two kinds of files live here:

- the **kernel** display tests (`*-ktest.c`, `lcd-*.c` scenarios, `hpd-model.c`, ...), linked only by the
  test build (`I915_TESTS=y`, see `plan/ws031/i915-rebuild-s5.md` §1);
- the **host** display tests (`host-*.c`), built with the host compiler by the `plan/ws031/tests/run-*-host-test.sh`
  scripts and never by the kernel build.

The register models are shared by both: `dp-fake-hw.c` (PPS 0, AUX A and a DP sink with DPCD and EDID, the
power references, locks and delayed work of `struct i915_dp_env`) and `lcd-fake-hw.c` (combo PLL, DDI, DP
transport, transcoder / pipe, plane, DBUF, power domains, vblank and flip event, and the sink's link training
behind the AUX model).  They use no host library, so the kernel test build can link them too.

## Host tests

| Script | Test source | What it links | Checks |
| --- | --- | --- | ---: |
| `run-dp-host-test.sh` | `host-dp-test.c` | eDP first stage (`dp-sink.c`, `panel.c`, `aux.c`, `edid-read.c`) on `dp-fake-hw.c` | 72 |
| `run-lcd-host-test.sh` | `host-lcd-test.c` | state calculation and register words (`state.c` and the modeset writers) | 56 |
| `run-lcd-modeset-host-test.sh` | `host-lcd-modeset-test.c` | one-screen modeset (`modeset.c` and its environment) with the resident eDP on `lcd-fake-hw.c` + `dp-fake-hw.c` | 123 |
| `run-opregion-host-test.sh` | `host-opregion-test.c` | OpRegion VBT locator (`vbt.c`: `drv_i915_opregion_locate_vbt`) on the target's OpRegion dump | 12 |
| `run-native-decide-host-test.sh` | `host-native-decide-test.c` | N0 decision rules (`takeover.c`: `drv_i915_native_decide`) | 14 |

Every script builds through `plan/ws031/tests/display-host-lib.sh`: all of `display/*.c` (each its own
translation unit, because the Linux environments of the display are mutually exclusive) plus `trace.c`,
`host-kernel.c`, `host-test.c` and the test's own sources, under ASan/UBSan, linked with `--gc-sections`.

- `host-kernel.c` stands in for the kernel services the tested paths reach (allocator, locks, log, clock,
  interrupt state) and, so that the production files link, for the services they must not reach (MMIO BAR,
  PCI, GT memory, work queues, worker): a call to one of those ends the test with its name.
- `host-test.c` holds the tally (`<test>: N checks, M failures`, the line the old scripts printed) and reads
  the reference files of `plan/ws031/display-ref/`.

## What changed against the old fixtures

Every check of the old fixtures is kept, with the same expected values (the register words compared with
Linux's dump, the sequences read from the run log, the refusals); the check counts are unchanged and the
printed output is identical to the old run.  What was adapted to the new code's contract:

- **Worlds.**  The old code kept its state in file-scope globals; the new code keeps it in worlds the display
  owns.  The tests create them on a zeroed `struct i915_display` as `drv_i915_display_init` does
  (`drv_i915_dp_world_create`, `drv_i915_lcd_world_create`, `drv_i915_wm_world_create`,
  `drv_i915_takeover_world_create`) and pass the world or the display to every call.  The modeset test needs
  the takeover world as well, because the watermark path asks the N1 registry for the crtc state
  (`drv_i915_n1_crtc_state`), which the old code kept static.
- **Positive errno.**  Refusals are now zedBSD positive errnos: `drv_i915_opregion_locate_vbt` answers `EINVAL`
  (old `-1`), `drv_i915_lcd_compute` `ENOSPC` / `EINVAL` (old `-28` / `-22`), the `drv_i915_lcd_emit_*` writers
  `EINVAL`, `drv_i915_lcd_modeset_prepare` `EINVAL` / `EBUSY` (old `-22` / `-16`),
  `drv_i915_lcd_modeset_discard_model` `EPERM` (old `-1`), `drv_i915_lcd_observer_frames` `ETIMEDOUT` (old `-110`).
  The eDP stage keeps its negative Linux errnos (`-I915_EDP_E*`, integrator decision in the S4 reports), and the
  hooks of `struct i915_lcd_emit` keep theirs (`I915_LCD_ETIMEDOUT`, `I915_LCD_EIO`; the model's event wait
  answers `I915_LCD_FAKE_EINVAL` = -22 without an armed event).
- **Models.**  `drv_i915_dp_fake_bind_env` also takes the DP world, on which the model runs the due delayed
  work (`drv_i915_edp_work_run(world, ...)`); the LCD model reaches the resident eDP through that world.  The
  stuck-busy release time moved from a file-scope variable into the model.  Power domains are the neutral
  `I915_PW_DOMAIN_*` values (equal to Linux's `POWER_DOMAIN_*`).
- **Reporting.**  A failed check prints `FAIL: <what>` (no source line; every message is unique).

No check was dropped.
