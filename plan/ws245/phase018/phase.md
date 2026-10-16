<!-- awesome-plan project=zedbsd record=ws245-p018 -->

# ws245-p018: EDID の decode（xd-edid）と旧 VBT parser の本体の削除（殻 `vbt-shell.c`）

Status: planned（q1218 で未着手。2026-10-16 P1 が下の調べだけをして区切った）
Parent: [WS245](../ws.md)
設計: [design.md](../design.md) §6.2 の p018 の行（改訂 6）

## 再開のための調べ（2026-10-16、main 5f39d5a15 の code で数えた）

- `vbt.c`（5492 行、関数 111）の外からの本番の呼び手: `display.c`（`drv_i915_vbt_world_create/_destroy`、`drv_i915_bios_set_opregion_vbt`、`_init_ex`、`_driver_remove`）、`dp-sink.c`（`drv_i915_sha256`、`drv_i915_vbt_encoder_for_port`、`drv_i915_vbt_init_panel`）、`takeover.c`（`drv_i915_sha256`、`drv_i915_opregion_locate_vbt`、`drv_i915_vbt_validate`）、`modeset.c`（`_encoder_for_port`、`_init_panel`）、`dp-ext-kern.c`・`tc-kern.c`（`_encoder_for_port`）。design の 10 関数と同じ。
- **design に無かった依存**: `dp-internal.h:60` が `vbt.h`（vbt.c の Linux の環境）を include し、DP の環境（`dp-sink.c` 12・`aux.c` 1・`dp-ext-kern.c` 1 箇所）が `vbt.h` の macro（`I915_VBT_LOG_*`、`kzalloc` → `drv_i915_vbt_zalloc` など）と helper（`i915_vbt_intel_port_to_phy` 等）を使う。`vbt.h` が宣言する `drv_i915_vbt_zalloc/_free/_provider_get/_notef/_log_enabled/_drm_mode_set_name/_emit/_fmtcheck` は vbt.c が定義している。p018 では、DP の環境が実際に使う物（定義を含む）を数えて、殻（`vbt-shell.c` と縮めた `vbt.h`）に残す必要がある。
- 試験の組: `I915_TEST_SET=execution` の `tests/execution/ktest-display.c` が `drv_i915_bios_is_valid_vbt_header`・`_process_vbt`・`_init_vbt_missing_defaults`・`drv_i915_sha256` を、`ktest-display-probe.c` が `_init_vbt_missing_defaults` を呼ぶ → この段で execution の組が壊れる。R4（壊れた組の試験はその段で消すか vmunix.mk から外す。path は Q1）。`tests/display/lcd-run.c`・`hdmi-output.c`（display の組）は `_encoder_for_port` だけで、殻に残るので壊れない。
- host の試験: `host-xd-vbt-test.c` の旧 parser と比べる部分（`test_vbt_old`、`test_defaults` の後半、`test_stage_shell`（p017）、`test_opregion` の `drv_i915_opregion_locate_vbt` との比べ）を外して期待値を固定値にする。`drv_i915_opregion_locate_vbt` は N0 の読み（p036 まで旧）が使うので、殻に移しても残る。
- p017 で `vbt_state.parsed` と `drv_i915_vbt_init_panel` の出力は既に xd の値（5330 で旧と一致を確かめた: phase017）なので、殻は xd の parse を旧の型に写すだけでよい（`i915_vbt_xd_output_set`・`_panel_set` を流用できる）。
- `edid.c` の `drv_i915_drm_mode_set_crtcinfo`（`pipe.c`・`modeset.c`）・`_get_hv_timing`（`watermark.c`）は殻に移す（design のとおり）。EDID の decode（`drv_i915_edid_preferred_mode`）の本番の呼び手は `state.c`、hotplug の EDID の要約。
