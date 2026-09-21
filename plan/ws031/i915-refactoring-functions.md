# WS031 i915リファクタリング案 — 関数移動台帳

[設計本文](i915-refactoring-design.md)と一緒にレビューする別紙。作成日: 2026-09-21。
基準commit: `7e7ff337c7f145e5fd79e4daaafa05a1cd4a4bcc`。
**実装前の配置案**。この表の作成でソースやヘッダを変更していない。

## A. 抽出範囲・凡例

- `src/drivers/gpu/i915/` 以下のC/H/INC 376ファイルを走査。
- 関数定義 2988件（関数本体のあるヘッダinlineと生成INCを含む）。
- `vk/codec-generated.inc`の147件は生成されたstatic encoder/decoder。
- 現行可視性はソース上のstatic有無。preprocess後の一構成のsymbol数ではない。
- 関数マクロの展開結果、宣言のみ、変数・ops表・構造体は定義数に含めない。
- `.json` manifestとshell runnerを含む全378ファイルは[保全台帳](i915-refactoring-assets.md)に列挙。関数のない178ファイルだけでなく、関数があるファイル内の状態・定数も保全対象。
- [敵対的レビュー](i915-refactoring-review.md)を反映。runnerの本番責務、instance/transport、reset配置、診断と試験の誤分類を訂正した。
- 行は旧ファイルと旧関数で識別する。同名でも別ファイル・条件別定義は別行。
- 移動先は責務の所有先。単純コピーでbuildが通ることや、全関数の意味論をレビュー済みであることを示さない。
- 関数本体の詳細分割が必要な行は、まず主担当ファイルを示す。本編§5の分割表を優先する。

| 区分 | 件数 | 新構成での扱い |
| --- | ---: | --- |
| E | 2 | ドライバ外公開。既存登録とreadiness |
| H | 85 | 本編§3の明示契約に対応付けた既存定義。提案名を記載 |
| I | 1116 | 内部宣言候補。旧名で追跡し、Hへの統合・改名・static化をレビューで確定 |
| O | 36 | static ops callback。関数自体をheaderにexportせずbind入口で登録 |
| S | 1363 | static補助/生成関数。同居させるか移動後の呼出しを確認 |
| T | 386 | 試験側。productionからの依存は移動時に解消 |

I欄のヘッダは**移行時の宣言先候補**であって、旧Linux/parity名を最終APIとして固定するものではない。
現時点で「旧関数を全部static化できる」とは判断していない。
H欄で複数旧関数が同じ提案名へ向かう場合は統合対象であり、同名の定義を複数作らない。
型と署名を統合する関数は、本編§4の所有権・errno契約に合わせて呼出元も更新する。
新設bind関数など旧関数と1対1にならない入口は、本編§3に記載する。

## B. 移動先別の規模

| 移動先 | 旧定義数 | H/Eに対応する提案名 | I候補数 |
| --- | ---: | --- | ---: |
| `command.c` | 9 | `drv_i915_stream_parse` | 0 |
| `compiler/compile.c` | 12 | `drv_i915_shader_compile`, `drv_i915_shader_binary_free` | 0 |
| `compiler/eu.c` | 25 | `drv_i915_eu_init`, `drv_i915_eu_free`, `drv_i915_eu_data`, `drv_i915_eu_grf`, `drv_i915_eu_grf_ud`, `drv_i915_eu_grf_scalar`, `drv_i915_eu_negate`, `drv_i915_eu_imm_f`, `drv_i915_eu_imm_d`, `drv_i915_eu_null`, `drv_i915_eu_mov`, `drv_i915_eu_alu2`, `drv_i915_eu_mad`, `drv_i915_eu_math`, `drv_i915_eu_send`, `drv_i915_eu_nop` | 0 |
| `compiler/spirv.c` | 14 | `drv_i915_shader_parse`, `drv_i915_shader_ir_free` | 0 |
| `context.c` | 52 | `drv_i915_context_create`, `drv_i915_context_destroy` | 26 |
| `data/vulkan-codec.inc` | 147 | — | 0 |
| `device-info.c` | 8 | — | 6 |
| `device.c` | 57 | `drv_i915_device_start`, `drv_i915_device_stop`, `drv_i915_device_schedule_start`, `drv_i915_runtime_ready` | 33 |
| `display/aux.c` | 17 | — | 3 |
| `display/clock.c` | 82 | — | 32 |
| `display/color.c` | 12 | — | 5 |
| `display/ddi.c` | 90 | — | 31 |
| `display/diagnostics.c` | 59 | — | 22 |
| `display/display.c` | 9 | `drv_i915_display_session_close` | 5 |
| `display/dmc.c` | 29 | — | 9 |
| `display/dp-internal.h` | 12 | — | 0 |
| `display/dp.c` | 179 | — | 77 |
| `display/edid.c` | 14 | — | 9 |
| `display/gmbus.c` | 21 | — | 6 |
| `display/hdmi.c` | 18 | — | 11 |
| `display/hotplug-internal.h` | 8 | — | 0 |
| `display/hotplug.c` | 88 | — | 50 |
| `display/internal.h` | 19 | — | 0 |
| `display/modeset-internal.h` | 16 | — | 0 |
| `display/modeset.c` | 44 | — | 29 |
| `display/opregion-internal.h` | 2 | — | 0 |
| `display/opregion.c` | 69 | — | 49 |
| `display/panel.c` | 95 | — | 34 |
| `display/phy.c` | 31 | — | 6 |
| `display/pipe.c` | 72 | — | 35 |
| `display/plane.c` | 44 | — | 13 |
| `display/power.c` | 73 | — | 32 |
| `display/present.c` | 15 | — | 8 |
| `display/scanout.c` | 14 | — | 12 |
| `display/state.c` | 38 | `drv_i915_display_panel_mode`, `drv_i915_display_panel_size_mm` | 25 |
| `display/takeover-internal.h` | 4 | — | 0 |
| `display/takeover.c` | 117 | — | 55 |
| `display/vblank.c` | 26 | — | 15 |
| `display/vbt.c` | 123 | — | 54 |
| `display/vbt.h` | 11 | — | 0 |
| `display/watermark-internal.h` | 1 | — | 0 |
| `display/watermark.c` | 107 | — | 29 |
| `engine.c` | 33 | `drv_i915_engine_for_timeline` | 18 |
| `firmware.c` | 4 | — | 3 |
| `ggtt.c` | 28 | — | 15 |
| `i915.c` | 7 | `drv_i915_pci_driver_register` | 2 |
| `irq.c` | 57 | — | 26 |
| `job.c` | 5 | — | 0 |
| `memory.c` | 36 | — | 32 |
| `memory.h` | 7 | — | 0 |
| `mmio.c` | 60 | — | 33 |
| `power.c` | 25 | — | 19 |
| `ppgtt.c` | 36 | — | 23 |
| `render/batch.c` | 7 | `drv_i915_batch_emit`, `drv_i915_batch_zero`, `drv_i915_batch_words`, `drv_i915_batch_pointer`, `drv_i915_batch_pipe_control` | 2 |
| `render/blit.c` | 8 | `drv_i915_blit_prepare`, `drv_i915_blit_build`, `drv_i915_blit_submit` | 3 |
| `render/codec.c` | 18 | `drv_i915_wire_read_u32`, `drv_i915_wire_read_u64`, `drv_i915_wire_read_array`, `drv_i915_wire_reply_u32`, `drv_i915_wire_reply_u64`, `drv_i915_wire_result`, `drv_i915_wire_create_tail`, `drv_i915_wire_create_reply` | 9 |
| `render/command.c` | 48 | `drv_i915_render_command_execute`, `drv_i915_render_command_dispatch` | 11 |
| `render/descriptor.c` | 11 | — | 11 |
| `render/dispatch.c` | 5 | `drv_i915_render_dispatch` | 2 |
| `render/draw.c` | 4 | `drv_i915_render_draw_session_close`, `drv_i915_render_draw` | 1 |
| `render/image.c` | 15 | — | 12 |
| `render/instance.c` | 15 | `drv_i915_render_instance_dispatch` | 0 |
| `render/math.c` | 5 | `drv_i915_float_half`, `drv_i915_float_add`, `drv_i915_float_sub`, `drv_i915_float_from_u32`, `drv_i915_float_ratio` | 0 |
| `render/memory.c` | 25 | `drv_i915_render_buffer_alloc`, `drv_i915_render_memory_cpu`, `drv_i915_render_memory_va`, `drv_i915_render_memory_blob_attach`, `drv_i915_render_memory_blob_detach` | 17 |
| `render/object.c` | 6 | `drv_i915_object_table_create`, `drv_i915_object_table_destroy`, `drv_i915_object_insert`, `drv_i915_object_lookup`, `drv_i915_object_remove`, `drv_i915_object_destroy_dispatch` | 0 |
| `render/pipeline.c` | 25 | `drv_i915_render_pipeline_prepare`, `drv_i915_render_pipeline_release` | 8 |
| `render/render-pass.c` | 2 | — | 2 |
| `render/state.c` | 18 | `drv_i915_render_sampler_write`, `drv_i915_render_surface_write` | 13 |
| `render/sync.c` | 18 | — | 13 |
| `render/transport.c` | 3 | — | 0 |
| `render/vulkan.c` | 7 | `drv_i915_render_attach`, `drv_i915_render_detach`, `drv_i915_render_open`, `drv_i915_render_close`, `drv_i915_render_execute` | 1 |
| `render/wsi.c` | 9 | — | 9 |
| `request.c` | 40 | — | 24 |
| `reset.c` | 15 | `drv_i915_engine_reset`, `drv_i915_gt_reset` | 6 |
| `resource.c` | 9 | — | 0 |
| `session.c` | 6 | `drv_i915_session_object_lookup` | 0 |
| `sync.c` | 62 | `drv_i915_error_from_reference` | 47 |
| `tests/display/dp-fake-hw.c` | 24 | — | 0 |
| `tests/display/edp-ktest.c` | 4 | — | 0 |
| `tests/display/edp-sync-ktest.c` | 16 | — | 0 |
| `tests/display/hpd-ktest.c` | 5 | — | 0 |
| `tests/display/kernel-scenarios.c` | 45 | — | 0 |
| `tests/display/lcd-fake-hw.c` | 36 | — | 0 |
| `tests/display/lcd-modeset-ktest.c` | 3 | — | 0 |
| `tests/display/lcd-pattern.c` | 5 | — | 0 |
| `tests/display/lcd-show-ktest.c` | 7 | — | 0 |
| `tests/display/lcdg-ktest.c` | 8 | — | 0 |
| `tests/display/modeset.c` | 1 | — | 0 |
| `tests/display/opregion-fwtest.c` | 4 | — | 0 |
| `tests/display/opregion-ktest.c` | 18 | — | 0 |
| `tests/display/parity-hpd-test.c` | 3 | — | 0 |
| `tests/display/scanout-hw-check.c` | 1 | — | 0 |
| `tests/display/scanout-ktest.c` | 5 | — | 0 |
| `tests/execution/eu-test.c` | 45 | — | 0 |
| `tests/execution/ktest.c` | 56 | — | 0 |
| `tests/execution/runner.c` | 1 | — | 0 |
| `tests/execution/selftest.c` | 42 | — | 0 |
| `tests/fixtures/dma-contract-test.c` | 2 | — | 0 |
| `tests/fixtures/mmio-contract-test.c` | 1 | — | 0 |
| `tests/fixtures/mock-dma.c` | 12 | — | 0 |
| `tests/fixtures/mock-mmio.c` | 10 | — | 0 |
| `tests/fixtures/mock-pci.c` | 19 | — | 0 |
| `tests/fixtures/pci-contract-test.c` | 1 | — | 0 |
| `tests/fixtures/pte-contract-test.c` | 1 | — | 0 |
| `tests/fixtures/rpm-contract-test.c` | 3 | — | 0 |
| `tests/fixtures/sync-contract-test.c` | 5 | — | 0 |
| `tests/render/readback.c` | 2 | — | 0 |
| `tests/render/reference-shaders.c` | 1 | — | 0 |
| `trace.c` | 7 | — | 7 |
| `workarounds.c` | 33 | — | 26 |

これは移行後の関数数の予測ではない。重複統合、分割、callback接着の除去で変わる。

## C. 分割によりファイル境界を越える現行static呼出し

同一の旧ファイル内の呼出式を字句照合し、レビュー後の配置で201辺を検出した。
条件付き参照shader・診断も含む。関数ポインタ、マクロ展開、他ファイルの呼出しを完全には追わない。
**現行依存の新配置への投影であって、許容する最終依存ではない。**
transport内178/179/180への分岐移設、runnerの試験枝切出し、shimのrequest/display分離は本文を優先する。
Sのまま別TUから呼ぶことはできない。未解決辺がある単位を、そのまま物理分割してはならない。

| 旧ファイル | static helper | 配置先 | 旧呼出関数 → 配置先 | 解決の基準 |
| --- | --- | --- | --- | --- |
| `engine.c`:387 | `i915_engine_program` | `engine.c` | `drv_i915_engine_reset` → `reset.c` | 所有header内部契約または呼出側と同居 |
| `engine.c`:430 | `i915_engine_stop_cs` | `engine.c` | `drv_i915_engine_reset` → `reset.c` | 所有header内部契約または呼出側と同居 |
| `i915.c`:283 | `i915_start` | `device.c` | `i915_attach` → `i915.c` | `drv_i915_device_start`へ統合 |
| `i915.c`:443 | `i915_stop` | `device.c` | `i915_attach` → `i915.c` | `drv_i915_device_stop`へ統合 |
| `i915.c`:443 | `i915_stop` | `device.c` | `i915_detach` → `i915.c` | `drv_i915_device_stop`へ統合 |
| `i915.c`:1934 | `i915_engine_for_timeline` | `engine.c` | `i915_job_reserve` → `job.c` | `drv_i915_engine_for_timeline`へ統合 |
| `i915.c`:1934 | `i915_engine_for_timeline` | `engine.c` | `i915_job_capacity` → `job.c` | `drv_i915_engine_for_timeline`へ統合 |
| `i915.c`:2144 | `i915_session_object` | `session.c` | `i915_submit_stream` → `command.c` | `drv_i915_session_object_lookup`へ統合 |
| `i915.c`:1934 | `i915_engine_for_timeline` | `engine.c` | `i915_submit_marker` → `command.c` | `drv_i915_engine_for_timeline`へ統合 |
| `parity/display_state.c`:59 | `genmask_low` | `display/state.c` | `parity_icl_qgv_points_mask` → `display/watermark.c` | 所有header内部契約または呼出側と同居 |
| `parity/display_state.c`:70 | `is_power_of_2` | `display/state.c` | `is_sagv_enabled` → `display/watermark.c` | 所有header内部契約または呼出側と同居 |
| `parity/display_state.c`:93 | `parity_atomic_global_obj_init` | `display/state.c` | `parity_intel_bw_init` → `display/watermark.c` | 所有header内部契約または呼出側と同居 |
| `parity/display_state.c`:123 | `alloc_global_state` | `display/state.c` | `parity_intel_bw_init` → `display/watermark.c` | 所有header内部契約または呼出側と同居 |
| `parity/display_state.c`:77 | `rmw` | `display/state.c` | `parity_intel_pmdemand_init` → `display/watermark.c` | 所有header内部契約または呼出側と同居 |
| `parity/display_state.c`:93 | `parity_atomic_global_obj_init` | `display/state.c` | `parity_intel_pmdemand_init` → `display/watermark.c` | 所有header内部契約または呼出側と同居 |
| `parity/display_state.c`:123 | `alloc_global_state` | `display/state.c` | `parity_intel_pmdemand_init` → `display/watermark.c` | 所有header内部契約または呼出側と同居 |
| `parity/driver_probe.c`:26 | `rmw` | `display/display.c` | `gen11_hpd_irq_setup` → `display/hotplug.c` | 所有header内部契約または呼出側と同居 |
| `parity/driver_probe.c`:26 | `rmw` | `display/display.c` | `parity_skl_watermark_ipc_init` → `display/watermark.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:195 | `k_usleep` | `sync.c` | `probe_scanout` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:501 | `log_regs` | `display/diagnostics.c` | `at_stage` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:501 | `log_regs` | `display/diagnostics.c` | `preflight` → `display/modeset.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:457 | `bind_ops` | `display/modeset.c` | `lcd_run_one` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:657 | `log_trace` | `display/diagnostics.c` | `lcd_run_one` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:678 | `log_observer` | `display/diagnostics.c` | `lcd_run_one` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:694 | `log_status` | `display/diagnostics.c` | `lcd_run_one` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:708 | `fill_cfg` | `display/modeset.c` | `lcd_run_one` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:785 | `preflight` | `display/modeset.c` | `lcd_run_one` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:457 | `bind_ops` | `display/modeset.c` | `parity_lcd_kernel_dual_run` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:708 | `fill_cfg` | `display/modeset.c` | `parity_lcd_kernel_dual_run` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:457 | `bind_ops` | `display/modeset.c` | `parity_lcd_kernel_dual_share_run` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:708 | `fill_cfg` | `display/modeset.c` | `parity_lcd_kernel_dual_share_run` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:195 | `k_usleep` | `sync.c` | `step_sleep` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:457 | `bind_ops` | `display/modeset.c` | `parity_lcd_kernel_lcdg_run` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:657 | `log_trace` | `display/diagnostics.c` | `parity_lcd_kernel_lcdg_run` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:678 | `log_observer` | `display/diagnostics.c` | `parity_lcd_kernel_lcdg_run` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:694 | `log_status` | `display/diagnostics.c` | `parity_lcd_kernel_lcdg_run` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:708 | `fill_cfg` | `display/modeset.c` | `parity_lcd_kernel_lcdg_run` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:785 | `preflight` | `display/modeset.c` | `parity_lcd_kernel_lcdg_run` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:457 | `bind_ops` | `display/modeset.c` | `parity_lcd_kernel_lcdc_run` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:657 | `log_trace` | `display/diagnostics.c` | `parity_lcd_kernel_lcdc_run` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:678 | `log_observer` | `display/diagnostics.c` | `parity_lcd_kernel_lcdc_run` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:708 | `fill_cfg` | `display/modeset.c` | `parity_lcd_kernel_lcdc_run` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:785 | `preflight` | `display/modeset.c` | `parity_lcd_kernel_lcdc_run` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:457 | `bind_ops` | `display/modeset.c` | `parity_lcd_kernel_lcdd_run` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:657 | `log_trace` | `display/diagnostics.c` | `parity_lcd_kernel_lcdd_run` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:678 | `log_observer` | `display/diagnostics.c` | `parity_lcd_kernel_lcdd_run` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:708 | `fill_cfg` | `display/modeset.c` | `parity_lcd_kernel_lcdd_run` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:785 | `preflight` | `display/modeset.c` | `parity_lcd_kernel_lcdd_run` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:457 | `bind_ops` | `display/modeset.c` | `parity_lcd_kernel_lcdo_run` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:657 | `log_trace` | `display/diagnostics.c` | `parity_lcd_kernel_lcdo_run` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:678 | `log_observer` | `display/diagnostics.c` | `parity_lcd_kernel_lcdo_run` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:708 | `fill_cfg` | `display/modeset.c` | `parity_lcd_kernel_lcdo_run` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:785 | `preflight` | `display/modeset.c` | `parity_lcd_kernel_lcdo_run` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:195 | `k_usleep` | `sync.c` | `n1_hold` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:2704 | `n1_mirror_console` | `display/takeover.c` | `n1_window_mirror` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:457 | `bind_ops` | `display/modeset.c` | `parity_lcd_kernel_n1_run` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:657 | `log_trace` | `display/diagnostics.c` | `parity_lcd_kernel_n1_run` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:678 | `log_observer` | `display/diagnostics.c` | `parity_lcd_kernel_n1_run` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:708 | `fill_cfg` | `display/modeset.c` | `parity_lcd_kernel_n1_run` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:785 | `preflight` | `display/modeset.c` | `parity_lcd_kernel_n1_run` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:2610 | `n1_read_plane` | `display/takeover.c` | `parity_lcd_kernel_n1_run` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:2655 | `n1_console_fb` | `display/takeover.c` | `parity_lcd_kernel_n1_run` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:2667 | `n1_check_ggtt` | `display/takeover.c` | `parity_lcd_kernel_n1_run` → `tests/display/kernel-scenarios.c` | 所有header内部契約または呼出側と同居 |
| `parity/lcd/parity_lcd_kernel.c`:657 | `log_trace` | `display/diagnostics.c` | `parity_lcd_kernel_resident_run` → `display/modeset.c` | 所有header内部契約または呼出側と同居 |
| `parity/legacy_shim.c`:99 | `to_errno` | `sync.c` | `parity_shim_lrc_create` → `context.c` | `drv_i915_error_from_reference`へ統合 |
| `parity/legacy_shim.c`:99 | `to_errno` | `sync.c` | `shim_run` → `request.c` | `drv_i915_error_from_reference`へ統合 |
| `parity/legacy_shim.c`:105 | `shim_find` | `context.c` | `shim_run` → `request.c` | 所有header内部契約または呼出側と同居 |
| `parity/legacy_shim.c`:344 | `shim_sync_do` | `request.c` | `parity_shim_display_present` → `display/present.c` | 所有header内部契約または呼出側と同居 |
| `parity/legacy_shim.c`:344 | `shim_sync_do` | `request.c` | `parity_shim_display_release` → `display/present.c` | 所有header内部契約または呼出側と同居 |
| `parity/legacy_shim.c`:344 | `shim_sync_do` | `request.c` | `parity_shim_display_present_blob` → `display/present.c` | 所有header内部契約または呼出側と同居 |
| `parity/legacy_shim.c`:242 | `shim_run` | `request.c` | `shim_present_blob` → `display/present.c` | 所有header内部契約または呼出側と同居 |
| `parity/legacy_shim.c`:505 | `shim_map_panel` | `display/scanout.c` | `shim_present_blob` → `display/present.c` | 所有header内部契約または呼出側と同居 |
| `parity/legacy_shim.c`:437 | `shim_present` | `display/present.c` | `shim_serve` → `request.c` | 所有header内部契約または呼出側と同居 |
| `parity/legacy_shim.c`:560 | `shim_present_blob` | `display/present.c` | `shim_serve` → `request.c` | 所有header内部契約または呼出側と同居 |
| `parity/legacy_shim.c`:545 | `shim_unmap_panel` | `display/scanout.c` | `shim_display_window` → `display/present.c` | 所有header内部契約または呼出側と同居 |
| `parity/legacy_shim.c`:632 | `shim_serve` | `request.c` | `shim_display_window` → `display/present.c` | 所有header内部契約または呼出側と同居 |
| `parity/legacy_shim.c`:632 | `shim_serve` | `request.c` | `parity_resident_serve` → `device.c` | 所有header内部契約または呼出側と同居 |
| `parity/probe.c`:68 | `parity_dump_trace` | `trace.c` | `drv_i915_parity_attach` → `device.c` | 所有header内部契約または呼出側と同居 |
| `parity/probe.c`:96 | `devid_is_tigerlake` | `device-info.c` | `drv_i915_parity_attach` → `device.c` | 所有header内部契約または呼出側と同居 |
| `parity/probe.c`:118 | `outcome_name` | `trace.c` | `drv_i915_parity_attach` → `device.c` | 所有header内部契約または呼出側と同居 |
| `parity/probe.c`:138 | `parity_cpu_phys_bits` | `device-info.c` | `drv_i915_parity_attach` → `device.c` | 所有header内部契約または呼出側と同居 |
| `parity/resident_display.c`:62 | `rd_init` | `display/display.c` | `rd_release` → `display/present.c` | 所有header内部契約または呼出側と同居 |
| `parity/resident_display.c`:62 | `rd_init` | `display/display.c` | `rd_present` → `display/present.c` | 所有header内部契約または呼出側と同居 |
| `parity/resident_display.c`:72 | `rd_panel` | `display/display.c` | `rd_present` → `display/present.c` | 所有header内部契約または呼出側と同居 |
| `parity/resident_display.c`:62 | `rd_init` | `display/display.c` | `rd_wait` → `display/present.c` | 所有header内部契約または呼出側と同居 |
| `parity/resident_display.c`:228 | `rd_release_locked` | `display/present.c` | `drv_i915_resident_display_close` → `display/display.c` | 所有header内部契約または呼出側と同居 |
| `parity/runner.c`:68 | `probe_status_name` | `tests/execution/runner.c` | `runner_thread` → `device.c` | 試験専用枝/呼出しを分離 |
| `vk/cmd.c`:413 | `i915_vk_cmd_set_reply` | `render/transport.c` | `i915_vk_cmd_builtin` → `render/dispatch.c` | 所有header内部契約または呼出側と同居 |
| `vk/cmd.c`:434 | `i915_vk_cmd_seek_reply` | `render/transport.c` | `i915_vk_cmd_builtin` → `render/dispatch.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-draw.c`:212 | `gfx_object` | `render/memory.c` | `gfx_session` → `render/draw.c` | `drv_i915_render_buffer_alloc`へ統合 |
| `vk/gfx-draw.c`:292 | `gfx_fnv1a` | `tests/render/reference-shaders.c` | `i915_vk_gfx_pipeline_prepare` → `render/pipeline.c` | 試験専用枝/呼出しを分離 |
| `vk/gfx-draw.c`:148 | `sf_half` | `render/math.c` | `gfx_write_state` → `render/state.c` | `drv_i915_float_half`へ統合 |
| `vk/gfx-draw.c`:157 | `sf_add` | `render/math.c` | `gfx_write_state` → `render/state.c` | `drv_i915_float_add`へ統合 |
| `vk/gfx-draw.c`:201 | `sf_sub` | `render/math.c` | `gfx_write_state` → `render/state.c` | `drv_i915_float_sub`へ統合 |
| `vk/gfx-draw.c`:97 | `emit` | `render/batch.c` | `emit_sba` → `render/state.c` | `drv_i915_batch_emit`へ統合 |
| `vk/gfx-draw.c`:97 | `emit` | `render/batch.c` | `emit_vertex_input` → `render/state.c` | `drv_i915_batch_emit`へ統合 |
| `vk/gfx-draw.c`:107 | `emit_zero` | `render/batch.c` | `emit_vertex_input` → `render/state.c` | `drv_i915_batch_zero`へ統合 |
| `vk/gfx-draw.c`:97 | `emit` | `render/batch.c` | `emit_urb` → `render/state.c` | `drv_i915_batch_emit`へ統合 |
| `vk/gfx-draw.c`:107 | `emit_zero` | `render/batch.c` | `emit_urb` → `render/state.c` | `drv_i915_batch_zero`へ統合 |
| `vk/gfx-draw.c`:97 | `emit` | `render/batch.c` | `emit_constants` → `render/state.c` | `drv_i915_batch_emit`へ統合 |
| `vk/gfx-draw.c`:97 | `emit` | `render/batch.c` | `emit_raster` → `render/state.c` | `drv_i915_batch_emit`へ統合 |
| `vk/gfx-draw.c`:97 | `emit` | `render/batch.c` | `emit_depth` → `render/state.c` | `drv_i915_batch_emit`へ統合 |
| `vk/gfx-draw.c`:107 | `emit_zero` | `render/batch.c` | `emit_depth` → `render/state.c` | `drv_i915_batch_zero`へ統合 |
| `vk/gfx-draw.c`:97 | `emit` | `render/batch.c` | `emit_shader_state` → `render/state.c` | `drv_i915_batch_emit`へ統合 |
| `vk/gfx-draw.c`:97 | `emit` | `render/batch.c` | `gfx_build_batch` → `render/draw.c` | `drv_i915_batch_emit`へ統合 |
| `vk/gfx-draw.c`:107 | `emit_zero` | `render/batch.c` | `gfx_build_batch` → `render/draw.c` | `drv_i915_batch_zero`へ統合 |
| `vk/gfx-draw.c`:117 | `emit_words` | `render/batch.c` | `gfx_build_batch` → `render/draw.c` | `drv_i915_batch_words`へ統合 |
| `vk/gfx-draw.c`:126 | `emit_pointer` | `render/batch.c` | `gfx_build_batch` → `render/draw.c` | `drv_i915_batch_pointer`へ統合 |
| `vk/gfx-draw.c`:134 | `emit_pc` | `render/batch.c` | `gfx_build_batch` → `render/draw.c` | `drv_i915_batch_pipe_control`へ統合 |
| `vk/gfx-draw.c`:599 | `emit_sba` | `render/state.c` | `gfx_build_batch` → `render/draw.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-draw.c`:632 | `emit_vertex_input` | `render/state.c` | `gfx_build_batch` → `render/draw.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-draw.c`:706 | `emit_urb` | `render/state.c` | `gfx_build_batch` → `render/draw.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-draw.c`:729 | `emit_constants` | `render/state.c` | `gfx_build_batch` → `render/draw.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-draw.c`:756 | `emit_raster` | `render/state.c` | `gfx_build_batch` → `render/draw.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-draw.c`:785 | `emit_depth` | `render/state.c` | `gfx_build_batch` → `render/draw.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-draw.c`:839 | `emit_shader_state` | `render/state.c` | `gfx_build_batch` → `render/draw.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-draw.c`:392 | `gfx_kernels` | `render/pipeline.c` | `i915_vk_gfx_draw` → `render/draw.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-draw.c`:507 | `gfx_write_state` | `render/state.c` | `i915_vk_gfx_draw` → `render/draw.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-draw.c`:999 | `gfx_census` | `tests/render/readback.c` | `i915_vk_gfx_draw` → `render/draw.c` | 試験専用枝/呼出しを分離 |
| `vk/gfx-draw.c`:232 | `gfx_session` | `render/draw.c` | `i915_vk_gfx_rect_prepare` → `render/blit.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-draw.c`:97 | `emit` | `render/batch.c` | `i915_vk_gfx_rect_build` → `render/blit.c` | `drv_i915_batch_emit`へ統合 |
| `vk/gfx-draw.c`:107 | `emit_zero` | `render/batch.c` | `i915_vk_gfx_rect_build` → `render/blit.c` | `drv_i915_batch_zero`へ統合 |
| `vk/gfx-draw.c`:126 | `emit_pointer` | `render/batch.c` | `i915_vk_gfx_rect_build` → `render/blit.c` | `drv_i915_batch_pointer`へ統合 |
| `vk/gfx-draw.c`:134 | `emit_pc` | `render/batch.c` | `i915_vk_gfx_rect_build` → `render/blit.c` | `drv_i915_batch_pipe_control`へ統合 |
| `vk/gfx-draw.c`:483 | `gfx_write_sampler` | `render/state.c` | `i915_vk_gfx_rect_build` → `render/blit.c` | `drv_i915_render_sampler_write`へ統合 |
| `vk/gfx-draw.c`:599 | `emit_sba` | `render/state.c` | `i915_vk_gfx_rect_build` → `render/blit.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-draw.c`:706 | `emit_urb` | `render/state.c` | `i915_vk_gfx_rect_build` → `render/blit.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-draw.c`:729 | `emit_constants` | `render/state.c` | `i915_vk_gfx_rect_build` → `render/blit.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-draw.c`:839 | `emit_shader_state` | `render/state.c` | `i915_vk_gfx_rect_build` → `render/blit.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-draw.c`:1220 | `sf_from_u32` | `render/math.c` | `i915_vk_gfx_rect_build` → `render/blit.c` | `drv_i915_float_from_u32`へ統合 |
| `vk/gfx-draw.c`:1236 | `sf_ratio` | `render/math.c` | `i915_vk_gfx_rect_build` → `render/blit.c` | `drv_i915_float_ratio`へ統合 |
| `vk/gfx-draw.c`:1257 | `gfx_write_surface` | `render/state.c` | `i915_vk_gfx_rect_build` → `render/blit.c` | `drv_i915_render_surface_write`へ統合 |
| `vk/gfx-obj.c`:49 | `gfx_create_tail` | `render/codec.c` | `gfx_allocate_memory` → `render/memory.c` | `drv_i915_wire_create_tail`へ統合 |
| `vk/gfx-obj.c`:58 | `gfx_create_reply` | `render/codec.c` | `gfx_allocate_memory` → `render/memory.c` | `drv_i915_wire_create_reply`へ統合 |
| `vk/gfx-obj.c`:36 | `gfx_result` | `render/codec.c` | `gfx_bind` → `render/memory.c` | `drv_i915_wire_result`へ統合 |
| `vk/gfx-obj.c`:49 | `gfx_create_tail` | `render/codec.c` | `gfx_create_buffer` → `render/memory.c` | `drv_i915_wire_create_tail`へ統合 |
| `vk/gfx-obj.c`:58 | `gfx_create_reply` | `render/codec.c` | `gfx_create_buffer` → `render/memory.c` | `drv_i915_wire_create_reply`へ統合 |
| `vk/gfx-obj.c`:49 | `gfx_create_tail` | `render/codec.c` | `gfx_create_image` → `render/image.c` | `drv_i915_wire_create_tail`へ統合 |
| `vk/gfx-obj.c`:58 | `gfx_create_reply` | `render/codec.c` | `gfx_create_image` → `render/image.c` | `drv_i915_wire_create_reply`へ統合 |
| `vk/gfx-obj.c`:49 | `gfx_create_tail` | `render/codec.c` | `gfx_create_image_view` → `render/image.c` | `drv_i915_wire_create_tail`へ統合 |
| `vk/gfx-obj.c`:58 | `gfx_create_reply` | `render/codec.c` | `gfx_create_image_view` → `render/image.c` | `drv_i915_wire_create_reply`へ統合 |
| `vk/gfx-obj.c`:49 | `gfx_create_tail` | `render/codec.c` | `gfx_create_sampler` → `render/image.c` | `drv_i915_wire_create_tail`へ統合 |
| `vk/gfx-obj.c`:58 | `gfx_create_reply` | `render/codec.c` | `gfx_create_sampler` → `render/image.c` | `drv_i915_wire_create_reply`へ統合 |
| `vk/gfx-obj.c`:49 | `gfx_create_tail` | `render/codec.c` | `gfx_create_dsl` → `render/descriptor.c` | `drv_i915_wire_create_tail`へ統合 |
| `vk/gfx-obj.c`:58 | `gfx_create_reply` | `render/codec.c` | `gfx_create_dsl` → `render/descriptor.c` | `drv_i915_wire_create_reply`へ統合 |
| `vk/gfx-obj.c`:49 | `gfx_create_tail` | `render/codec.c` | `gfx_create_dpool` → `render/descriptor.c` | `drv_i915_wire_create_tail`へ統合 |
| `vk/gfx-obj.c`:58 | `gfx_create_reply` | `render/codec.c` | `gfx_create_dpool` → `render/descriptor.c` | `drv_i915_wire_create_reply`へ統合 |
| `vk/gfx-obj.c`:36 | `gfx_result` | `render/codec.c` | `gfx_allocate_dsets` → `render/descriptor.c` | `drv_i915_wire_result`へ統合 |
| `vk/gfx-obj.c`:49 | `gfx_create_tail` | `render/codec.c` | `gfx_create_pipeline_layout` → `render/pipeline.c` | `drv_i915_wire_create_tail`へ統合 |
| `vk/gfx-obj.c`:58 | `gfx_create_reply` | `render/codec.c` | `gfx_create_pipeline_layout` → `render/pipeline.c` | `drv_i915_wire_create_reply`へ統合 |
| `vk/gfx-obj.c`:49 | `gfx_create_tail` | `render/codec.c` | `gfx_create_render_pass` → `render/render-pass.c` | `drv_i915_wire_create_tail`へ統合 |
| `vk/gfx-obj.c`:58 | `gfx_create_reply` | `render/codec.c` | `gfx_create_render_pass` → `render/render-pass.c` | `drv_i915_wire_create_reply`へ統合 |
| `vk/gfx-obj.c`:49 | `gfx_create_tail` | `render/codec.c` | `gfx_create_framebuffer` → `render/render-pass.c` | `drv_i915_wire_create_tail`へ統合 |
| `vk/gfx-obj.c`:58 | `gfx_create_reply` | `render/codec.c` | `gfx_create_framebuffer` → `render/render-pass.c` | `drv_i915_wire_create_reply`へ統合 |
| `vk/gfx-obj.c`:49 | `gfx_create_tail` | `render/codec.c` | `gfx_create_shader` → `render/pipeline.c` | `drv_i915_wire_create_tail`へ統合 |
| `vk/gfx-obj.c`:58 | `gfx_create_reply` | `render/codec.c` | `gfx_create_shader` → `render/pipeline.c` | `drv_i915_wire_create_reply`へ統合 |
| `vk/gfx-obj.c`:36 | `gfx_result` | `render/codec.c` | `gfx_create_pipelines` → `render/pipeline.c` | `drv_i915_wire_result`へ統合 |
| `vk/gfx-obj.c`:49 | `gfx_create_tail` | `render/codec.c` | `gfx_create_semaphore` → `render/sync.c` | `drv_i915_wire_create_tail`へ統合 |
| `vk/gfx-obj.c`:58 | `gfx_create_reply` | `render/codec.c` | `gfx_create_semaphore` → `render/sync.c` | `drv_i915_wire_create_reply`へ統合 |
| `vk/gfx-obj.c`:81 | `gfx_destroy_plain` | `render/object.c` | `i915_vk_gfx_obj_dispatch` → `render/dispatch.c` | `drv_i915_object_destroy_dispatch`へ統合 |
| `vk/gfx-obj.c`:156 | `gfx_allocate_memory` | `render/memory.c` | `i915_vk_gfx_obj_dispatch` → `render/dispatch.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-obj.c`:199 | `gfx_free_memory` | `render/memory.c` | `i915_vk_gfx_obj_dispatch` → `render/dispatch.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-obj.c`:227 | `gfx_bind` | `render/memory.c` | `i915_vk_gfx_obj_dispatch` → `render/dispatch.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-obj.c`:267 | `gfx_requirements` | `render/memory.c` | `i915_vk_gfx_obj_dispatch` → `render/dispatch.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-obj.c`:305 | `gfx_create_buffer` | `render/memory.c` | `i915_vk_gfx_obj_dispatch` → `render/dispatch.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-obj.c`:342 | `gfx_create_image` | `render/image.c` | `i915_vk_gfx_obj_dispatch` → `render/dispatch.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-obj.c`:395 | `gfx_create_image_view` | `render/image.c` | `i915_vk_gfx_obj_dispatch` → `render/dispatch.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-obj.c`:423 | `gfx_create_sampler` | `render/image.c` | `i915_vk_gfx_obj_dispatch` → `render/dispatch.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-obj.c`:452 | `gfx_subresource_layout` | `render/image.c` | `i915_vk_gfx_obj_dispatch` → `render/dispatch.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-obj.c`:480 | `gfx_create_dsl` | `render/descriptor.c` | `i915_vk_gfx_obj_dispatch` → `render/dispatch.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-obj.c`:515 | `gfx_create_dpool` | `render/descriptor.c` | `i915_vk_gfx_obj_dispatch` → `render/dispatch.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-obj.c`:540 | `gfx_allocate_dsets` | `render/descriptor.c` | `i915_vk_gfx_obj_dispatch` → `render/dispatch.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-obj.c`:591 | `gfx_update_dsets` | `render/descriptor.c` | `i915_vk_gfx_obj_dispatch` → `render/dispatch.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-obj.c`:661 | `gfx_create_pipeline_layout` | `render/pipeline.c` | `i915_vk_gfx_obj_dispatch` → `render/dispatch.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-obj.c`:684 | `gfx_create_render_pass` | `render/render-pass.c` | `i915_vk_gfx_obj_dispatch` → `render/dispatch.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-obj.c`:729 | `gfx_create_framebuffer` | `render/render-pass.c` | `i915_vk_gfx_obj_dispatch` → `render/dispatch.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-obj.c`:768 | `gfx_create_shader` | `render/pipeline.c` | `i915_vk_gfx_obj_dispatch` → `render/dispatch.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-obj.c`:930 | `gfx_create_pipelines` | `render/pipeline.c` | `i915_vk_gfx_obj_dispatch` → `render/dispatch.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-obj.c`:994 | `gfx_destroy_pipeline` | `render/pipeline.c` | `i915_vk_gfx_obj_dispatch` → `render/dispatch.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-obj.c`:1022 | `gfx_create_semaphore` | `render/sync.c` | `i915_vk_gfx_obj_dispatch` → `render/dispatch.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-rec.c`:52 | `gfx_result` | `render/codec.c` | `rec_create_pool` → `render/command.c` | `drv_i915_wire_result`へ統合 |
| `vk/gfx-rec.c`:52 | `gfx_result` | `render/codec.c` | `rec_reset_pool` → `render/command.c` | `drv_i915_wire_result`へ統合 |
| `vk/gfx-rec.c`:52 | `gfx_result` | `render/codec.c` | `rec_allocate` → `render/command.c` | `drv_i915_wire_result`へ統合 |
| `vk/gfx-rec.c`:52 | `gfx_result` | `render/codec.c` | `rec_begin` → `render/command.c` | `drv_i915_wire_result`へ統合 |
| `vk/gfx-rec.c`:625 | `exec_clear` | `render/blit.c` | `exec_cmdbuf` → `render/command.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-rec.c`:674 | `exec_copy` | `render/blit.c` | `exec_cmdbuf` → `render/command.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-rec.c`:723 | `exec_image` | `render/blit.c` | `exec_cmdbuf` → `render/command.c` | 所有header内部契約または呼出側と同居 |
| `vk/gfx-rec.c`:52 | `gfx_result` | `render/codec.c` | `rec_submit` → `render/command.c` | `drv_i915_wire_result`へ統合 |
| `vk/inst.c`:536 | `inst_execute_streams` | `render/transport.c` | `i915_vk_inst_dispatch` → `render/instance.c` | 所有header内部契約または呼出側と同居 |
| `vk/pipe.c`:672 | `i915_vk_batch_emit` | `render/batch.c` | `i915_vk_pipeline_emit` → `render/state.c` | 所有header内部契約または呼出側と同居 |
| `vk/pipe.c`:690 | `i915_vk_batch_pad` | `render/batch.c` | `i915_vk_pipeline_emit` → `render/state.c` | 所有header内部契約または呼出側と同居 |
| `vk/pipe.c`:672 | `i915_vk_batch_emit` | `render/batch.c` | `i915_vk_pipe_emit_base` → `render/state.c` | 所有header内部契約または呼出側と同居 |
| `vk/pipe.c`:690 | `i915_vk_batch_pad` | `render/batch.c` | `i915_vk_pipe_emit_base` → `render/state.c` | 所有header内部契約または呼出側と同居 |
| `vk/res.c`:342 | `i915_vk_surface_state` | `render/state.c` | `i915_vk_image_create` → `render/image.c` | 所有header内部契約または呼出側と同居 |
| `vk/res.c`:548 | `i915_vk_result` | `render/memory.c` | `i915_vk_res_create_image` → `render/image.c` | 所有header内部契約または呼出側と同居 |
| `vk/res.c`:593 | `i915_vk_res_skip_extension` | `render/memory.c` | `i915_vk_res_create_image` → `render/image.c` | 所有header内部契約または呼出側と同居 |
| `vk/res.c`:610 | `i915_vk_res_allocate_memory` | `render/memory.c` | `i915_vk_res_dispatch` → `render/dispatch.c` | 所有header内部契約または呼出側と同居 |
| `vk/res.c`:663 | `i915_vk_res_create_buffer` | `render/memory.c` | `i915_vk_res_dispatch` → `render/dispatch.c` | 所有header内部契約または呼出側と同居 |
| `vk/res.c`:722 | `i915_vk_res_create_image` | `render/image.c` | `i915_vk_res_dispatch` → `render/dispatch.c` | 所有header内部契約または呼出側と同居 |
| `vk/res.c`:794 | `i915_vk_res_bind` | `render/memory.c` | `i915_vk_res_dispatch` → `render/dispatch.c` | 所有header内部契約または呼出側と同居 |
| `vk/res.c`:832 | `i915_vk_res_destroy` | `render/memory.c` | `i915_vk_res_dispatch` → `render/dispatch.c` | 所有header内部契約または呼出側と同居 |

## D. 同一配置先に集まる同名定義

6組（DP/modesetのto_i915は別private headerへ分離）。条件分岐による同名定義も含むため、全件がlink衝突とは限らない。
異なる旧ファイルのstatic関数を単純連結すると衝突する。内容を比べて統合するか、機能名で区別する。
別headerへ分けたto_i915も同じTUで両方includeすれば衝突するため、統合/改名とinclude graphの確認は残る。
この6組は「同一配置先」という検査条件の件数であり、全link/include衝突数ではない。

| 移動先 | 旧関数名 | 旧定義の所在 |
| --- | --- | --- |
| `display/clock.c` | `calc_voltage_level` | `parity/cdclk.c`:161<br>`parity/lcd/intel_cdclk_port.c`:136 |
| `display/takeover.c` | `readout_plane_state` | `parity/display_nogem.c`:1175<br>`parity/lcd/intel_modeset_setup_port.c`:675 |
| `display/takeover.c` | `intel_early_display_was` | `parity/display_nogem.c`:1375<br>`parity/lcd/intel_modeset_setup_port.c`:941 |
| `display/takeover.c` | `intel_sanitize_crtc` | `parity/display_nogem.c`:1439<br>`parity/lcd/intel_modeset_setup_port.c`:504 |
| `display/dmc.c` | `is_valid_dmc_id` | `parity/dmc.c`:109<br>`parity/lcd/intel_dmc_port.c`:51 |
| `render/codec.c` | `gfx_result` | `vk/gfx-obj.c`:36<br>`vk/gfx-rec.c`:52 |

## E. 旧ファイル別・関数単位の移動表

各表のヘッダ欄に書かれたパスは**新構成の予定パス**。
旧ファイルのリンクは基準ソースへの相対リンクで、行番号は左列に示す。
S/Oにヘッダ宣言を追加しない。ヘッダ内のstatic inlineは所有headerに残すか通常関数へ統合する。

### engine.c

現行: [engine.c](../../src/drivers/gpu/i915/engine.c)。15定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 62 | `drv_i915_engines_start` | global | `engine.c` | I / 旧名で照合 | `engine.h` |
| 142 | `drv_i915_engines_stop` | global | `engine.c` | I / 旧名で照合 | `engine.h` |
| 162 | `drv_i915_engine_reset` | global | `reset.c` | I / 旧名で照合 | `reset.h` |
| 206 | `drv_i915_engine_recover` | global | `reset.c` | I / 旧名で照合 | `reset.h` |
| 264 | `drv_i915_engine_interrupt` | global | `engine.c` | I / 旧名で照合 | `engine.h` |
| 311 | `drv_i915_engine_idle` | global | `engine.c` | I / 旧名で照合 | `engine.h` |
| 325 | `i915_engine_init` | static | `engine.c` | S | — |
| 364 | `i915_engine_fini` | static | `engine.c` | S | — |
| 387 | `i915_engine_program` | static | `engine.c` | S | — |
| 430 | `i915_engine_stop_cs` | static | `engine.c` | S | — |
| 454 | `i915_engine_reset_prepare` | static | `reset.c` | S | — |
| 492 | `i915_engine_reset_cancel` | static | `reset.c` | S | — |
| 500 | `i915_mocs_init` | static | `engine.c` | S | — |
| 522 | `i915_mocs_control` | static | `engine.c` | S | — |
| 537 | `i915_mocs_l3cc` | static | `engine.c` | S | — |

### gem.c

現行: [gem.c](../../src/drivers/gpu/i915/gem.c)。9定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 32 | `drv_i915_gem_create` | global | `memory.c` | I / 旧名で照合 | `memory.h` |
| 87 | `drv_i915_gem_destroy` | global | `memory.c` | I / 旧名で照合 | `memory.h` |
| 136 | `drv_i915_gem_share_put` | global | `memory.c` | I / 旧名で照合 | `memory.h` |
| 154 | `drv_i915_gem_bind_ggtt` | global | `memory.c` | I / 旧名で照合 | `memory.h` |
| 188 | `drv_i915_gem_unbind_ggtt` | global | `memory.c` | I / 旧名で照合 | `memory.h` |
| 206 | `drv_i915_gem_bind_vm` | global | `memory.c` | I / 旧名で照合 | `memory.h` |
| 238 | `drv_i915_gem_unbind_vm` | global | `memory.c` | I / 旧名で照合 | `memory.h` |
| 255 | `drv_i915_gem_read` | global | `memory.c` | I / 旧名で照合 | `memory.h` |
| 280 | `drv_i915_gem_write` | global | `memory.c` | I / 旧名で照合 | `memory.h` |

### ggtt.c

現行: [ggtt.c](../../src/drivers/gpu/i915/ggtt.c)。13定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 43 | `drv_i915_ggtt_start` | global | `ggtt.c` | I / 旧名で照合 | `ggtt.h` |
| 111 | `drv_i915_ggtt_stop` | global | `ggtt.c` | I / 旧名で照合 | `ggtt.h` |
| 138 | `drv_i915_ggtt_alloc` | global | `ggtt.c` | I / 旧名で照合 | `ggtt.h` |
| 191 | `drv_i915_ggtt_free` | global | `ggtt.c` | I / 旧名で照合 | `ggtt.h` |
| 224 | `drv_i915_ggtt_insert` | global | `ggtt.c` | I / 旧名で照合 | `ggtt.h` |
| 263 | `drv_i915_ggtt_clear` | global | `ggtt.c` | I / 旧名で照合 | `ggtt.h` |
| 287 | `i915_ggtt_probe` | static | `ggtt.c` | S | — |
| 350 | `i915_ggtt_scratch_start` | static | `ggtt.c` | S | — |
| 379 | `i915_ggtt_boot_scanout` | static | `ggtt.c` | S | — |
| 443 | `i915_ggtt_write_pte` | static | `ggtt.c` | S | — |
| 457 | `i915_ggtt_flush` | static | `ggtt.c` | S | — |
| 472 | `i915_ggtt_bit_test` | static | `ggtt.c` | S | — |
| 488 | `i915_ggtt_bit_set` | static | `ggtt.c` | S | — |

### i915.c

現行: [i915.c](../../src/drivers/gpu/i915/i915.c)。44定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 115 | `drv_i915_pci_driver_register` | global | `i915.c` | E / `drv_i915_pci_driver_register` | `include/drivers/i915.h` |
| 143 | `drv_i915_stream_parse` | global | `command.c` | H / `drv_i915_stream_parse` | `command.h` |
| 217 | `i915_attach` | static | `i915.c` | O | —（ops経由） |
| 283 | `i915_start` | static | `device.c` | H / `drv_i915_device_start` | `device.h` |
| 443 | `i915_stop` | static | `device.c` | H / `drv_i915_device_stop` | `device.h` |
| 533 | `i915_detach` | static | `i915.c` | O | —（ops経由） |
| 570 | `i915_publish` | static | `i915.c` | S | — |
| 650 | `i915_unpublish` | static | `i915.c` | S | — |
| 683 | `drv_i915_resident_publish` | global | `i915.c` | I / 旧名で照合 | `i915.h` |
| 713 | `drv_i915_resident_unpublish` | global | `i915.c` | I / 旧名で照合 | `i915.h` |
| 730 | `i915_open` | static | `session.c` | O | —（ops経由） |
| 825 | `i915_close` | static | `session.c` | O | —（ops経由） |
| 880 | `i915_get_info` | static | `session.c` | O | —（ops経由） |
| 902 | `i915_resource_create` | static | `resource.c` | O | —（ops経由） |
| 972 | `i915_resource_destroy` | static | `resource.c` | O | —（ops経由） |
| 1018 | `i915_resource_read` | static | `resource.c` | O | —（ops経由） |
| 1052 | `i915_resource_write` | static | `resource.c` | O | —（ops経由） |
| 1086 | `i915_blob_create` | static | `resource.c` | O | —（ops経由） |
| 1168 | `i915_share_export` | static | `resource.c` | O | —（ops経由） |
| 1183 | `i915_share_release` | static | `resource.c` | O | —（ops経由） |
| 1193 | `i915_share_import` | static | `resource.c` | O | —（ops経由） |
| 1233 | `i915_resource_map` | static | `resource.c` | O | —（ops経由） |
| 1279 | `i915_vk_command_reply` | static | `command.c` | S | — |
| 1316 | `i915_get_capset` | static | `command.c` | O | —（ops経由） |
| 1345 | `i915_command` | static | `command.c` | O | —（ops経由） |
| 1391 | `i915_command_submit` | static | `command.c` | O | —（ops経由） |
| 1454 | `i915_command_drain` | static | `command.c` | O | —（ops経由） |
| 1479 | `i915_job_reserve` | static | `job.c` | O | —（ops経由） |
| 1537 | `i915_job_commit` | static | `job.c` | O | —（ops経由） |
| 1591 | `i915_job_cancel` | static | `job.c` | O | —（ops経由） |
| 1654 | `i915_job_capacity` | static | `job.c` | O | —（ops経由） |
| 1699 | `i915_stop_begin` | static | `reset.c` | O | —（ops経由） |
| 1725 | `i915_stop_poll` | static | `reset.c` | O | —（ops経由） |
| 1758 | `i915_fault` | static | `reset.c` | O | —（ops経由） |
| 1793 | `i915_reset_device` | static | `reset.c` | O | —（ops経由） |
| 1860 | `i915_isolate` | static | `reset.c` | O | —（ops経由） |
| 1895 | `i915_session_contexts_destroy` | static | `session.c` | S | — |
| 1910 | `i915_session_batches_destroy` | static | `session.c` | S | — |
| 1934 | `i915_engine_for_timeline` | static | `engine.c` | H / `drv_i915_engine_for_timeline` | `engine.h` |
| 1965 | `i915_submit_stream` | static | `command.c` | S | — |
| 2046 | `i915_submit_marker` | static | `command.c` | S | — |
| 2085 | `i915_batch_acquire` | static | `command.c` | S | — |
| 2144 | `i915_session_object` | static | `session.c` | H / `drv_i915_session_object_lookup` | `session.h` |
| 2160 | `i915_reservation` | static | `job.c` | S | — |

### irq.c

現行: [irq.c](../../src/drivers/gpu/i915/irq.c)。9定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 44 | `drv_i915_irq_start` | global | `irq.c` | I / 旧名で照合 | `irq.h` |
| 94 | `drv_i915_irq_stop` | global | `irq.c` | I / 旧名で照合 | `irq.h` |
| 128 | `drv_i915_irq_reset` | global | `irq.c` | I / 旧名で照合 | `irq.h` |
| 161 | `drv_i915_irq_handler` | global | `irq.c` | I / 旧名で照合 | `irq.h` |
| 202 | `i915_irq_enable` | static | `irq.c` | S | — |
| 225 | `i915_irq_bank` | static | `irq.c` | S | — |
| 256 | `i915_irq_identity` | static | `irq.c` | S | — |
| 296 | `i915_irq_dispatch` | static | `irq.c` | S | — |
| 325 | `i915_irq_engine` | static | `irq.c` | S | — |

### lrc.c

現行: [lrc.c](../../src/drivers/gpu/i915/lrc.c)。12定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 54 | `drv_i915_lrc_create` | global | `context.c` | H / `drv_i915_context_create` | `context.h` |
| 125 | `drv_i915_lrc_destroy` | global | `context.c` | H / `drv_i915_context_destroy` | `context.h` |
| 155 | `drv_i915_lrc_submit` | global | `context.c` | I / 旧名で照合 | `context.h` |
| 184 | `drv_i915_lrc_reset_csb` | global | `context.c` | I / 旧名で照合 | `context.h` |
| 222 | `drv_i915_lrc_csb_consume` | global | `context.c` | I / 旧名で照合 | `context.h` |
| 276 | `drv_i915_lrc_ring_space` | global | `context.c` | I / 旧名で照合 | `context.h` |
| 312 | `drv_i915_lrc_ring_emit` | global | `context.c` | I / 旧名で照合 | `context.h` |
| 341 | `i915_lrc_set_offsets` | static | `context.c` | S | — |
| 395 | `i915_lrc_init_regs` | static | `context.c` | S | — |
| 445 | `i915_lrc_render_power_state` | static | `context.c` | S | — |
| 466 | `i915_lrc_csb_read` | static | `context.c` | S | — |
| 511 | `i915_lrc_csb_parse` | static | `context.c` | S | — |

### parity/backend_delayed.c

現行: [parity/backend_delayed.c](../../src/drivers/gpu/i915/parity/backend_delayed.c)。11定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 14 | `ktimer_thread` | static | `sync.c` | S | — |
| 64 | `parity_ktimerq_create` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 91 | `parity_ktimerq_destroy` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 105 | `parity_kdelayed_init` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 117 | `wait_not_firing` | static | `sync.c` | S | — |
| 128 | `disarm` | static | `sync.c` | S | — |
| 142 | `parity_kdelayed_queue` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 178 | `parity_kdelayed_cancel` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 196 | `parity_kdelayed_cancel_sync` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 214 | `parity_kdelayed_flush` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 228 | `parity_kdelayed_pending` | global | `sync.c` | I / 旧名で照合 | `sync.h` |

### parity/backend_dma.c

現行: [parity/backend_dma.c](../../src/drivers/gpu/i915/parity/backend_dma.c)。2定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 16 | `b_set_info` | static | `memory.c` | S | — |
| 43 | `parity_dma_backend` | global | `memory.c` | I / 旧名で照合 | `memory.h` |

### parity/backend_mmio.c

現行: [parity/backend_mmio.c](../../src/drivers/gpu/i915/parity/backend_mmio.c)。13定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 42 | `b_raw_read32` | static | `mmio.c` | S | — |
| 52 | `b_raw_write32` | static | `mmio.c` | S | — |
| 62 | `fw_req_reg` | static | `mmio.c` | S | — |
| 74 | `fw_ack_reg` | static | `mmio.c` | S | — |
| 86 | `b_fw_request` | static | `mmio.c` | S | — |
| 96 | `b_fw_ack` | static | `mmio.c` | S | — |
| 113 | `parity_mmio_backend` | global | `mmio.c` | I / 旧名で照合 | `mmio.h` |
| 119 | `rpm_resume` | static | `mmio.c` | S | — |
| 120 | `rpm_suspend` | static | `mmio.c` | S | — |
| 127 | `parity_rpm_backend` | global | `mmio.c` | I / 旧名で照合 | `mmio.h` |
| 134 | `pci_probe_pm_resume` | static | `mmio.c` | S | — |
| 145 | `pci_probe_pm_suspend` | static | `mmio.c` | S | — |
| 155 | `parity_pci_probe_pm_backend` | global | `mmio.c` | I / 旧名で照合 | `mmio.h` |

### parity/backend_pci.c

現行: [parity/backend_pci.c](../../src/drivers/gpu/i915/parity/backend_pci.c)。10定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 14 | `b_read8` | static | `device.c` | S | — |
| 23 | `b_read16` | static | `device.c` | S | — |
| 32 | `b_read32` | static | `device.c` | S | — |
| 41 | `b_write8` | static | `device.c` | S | — |
| 48 | `b_write16` | static | `device.c` | S | — |
| 55 | `b_write32` | static | `device.c` | S | — |
| 67 | `format_msi_source` | static | `device.c` | S | — |
| 88 | `b_alloc_msi_vector` | static | `device.c` | S | — |
| 109 | `b_free_msi_vector` | static | `device.c` | S | — |
| 135 | `parity_pci_backend` | global | `device.c` | I / 旧名で照合 | `device.h` |

### parity/backend_sync.c

現行: [parity/backend_sync.c](../../src/drivers/gpu/i915/parity/backend_sync.c)。15定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 14 | `parity_kcompletion_init` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 23 | `parity_kcomplete` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 33 | `parity_kcomplete_all` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 42 | `parity_kreinit_completion` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 51 | `parity_kwait` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 73 | `parity_kwork_init` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 87 | `kwq_remove_pending` | static | `sync.c` | S | — |
| 111 | `kworker_thread` | static | `sync.c` | S | — |
| 149 | `parity_kworkqueue_create` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 178 | `parity_kqueue_work` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 206 | `parity_kcancel_work` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 218 | `parity_kwork_is_pending` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 230 | `parity_kcancel_work_sync` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 258 | `parity_kflush_work` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 281 | `parity_kworkqueue_destroy` | global | `sync.c` | I / 旧名で照合 | `sync.h` |

### parity/bios.c

現行: [parity/bios.c](../../src/drivers/gpu/i915/parity/bios.c)。17定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 49 | `rd16` | static | `display/vbt.c` | S | — |
| 55 | `rd32` | static | `display/vbt.c` | S | — |
| 62 | `parity_bios_is_valid_vbt` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 93 | `parity_bios_process_vbt` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 132 | `parity_bios_init_vbt_missing_defaults` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 181 | `oprom_get_vbt` | static | `display/vbt.c` | S | — |
| 250 | `sha_rotr` | static | `display/vbt.c` | S | — |
| 253 | `parity_sha256` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 324 | `parity_vbt_emit` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 331 | `parity_vbt_fmtcheck` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 367 | `explicit_blob_get` | static | `display/vbt.c` | S | — |
| 415 | `parity_bios_set_opregion_vbt` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 422 | `parity_intel_bios_init_ex` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 544 | `parity_intel_bios_init` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 551 | `parity_intel_bios_driver_remove` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 563 | `explicit_pin_for` | static | `display/vbt.c` | S | — |
| 573 | `parity_vbt_explicit_pin` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |

### parity/cdclk.c

現行: [parity/cdclk.c](../../src/drivers/gpu/i915/parity/cdclk.c)。29定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 95 | `parity_adlp_cdclk_table` | global | `display/clock.c` | I / 旧名で照合 | `display/clock.h` |
| 99 | `parity_icl_cdclk_table` | global | `display/clock.c` | I / 旧名で照合 | `display/clock.h` |
| 102 | `divrc` | static | `display/clock.c` | S | — |
| 104 | `hweight16` | static | `display/clock.c` | S | — |
| 114 | `parity_adlp_display_step` | global | `display/clock.c` | I / 旧名で照合 | `display/clock.h` |
| 126 | `parity_intel_init_cdclk_hooks` | global | `display/clock.c` | I / 旧名で照合 | `display/clock.h` |
| 161 | `calc_voltage_level` | static | `display/clock.c` | S | — |
| 173 | `parity_tgl_calc_voltage_level` | global | `display/clock.c` | I / 旧名で照合 | `display/clock.h` |
| 181 | `cdclk_calc_voltage_level` | static | `display/clock.c` | S | — |
| 189 | `skl_cdclk_decimal` | static | `display/clock.c` | S | — |
| 193 | `parity_bxt_calc_cdclk` | global | `display/clock.c` | I / 旧名で照合 | `display/clock.h` |
| 208 | `parity_bxt_calc_cdclk_pll_vco` | global | `display/clock.c` | I / 旧名で照合 | `display/clock.h` |
| 225 | `bxt_cdclk_cd2x_div_sel` | static | `display/clock.c` | S | — |
| 241 | `cdclk_squash_waveform` | static | `display/clock.c` | S | — |
| 254 | `cdclk_pll_is_unknown` | static | `display/clock.c` | S | — |
| 259 | `icl_readout_refclk` | static | `display/clock.c` | S | — |
| 274 | `bxt_de_pll_readout` | static | `display/clock.c` | S | — |
| 295 | `parity_bxt_get_cdclk` | global | `display/clock.c` | I / 旧名で照合 | `display/clock.h` |
| 330 | `parity_intel_update_cdclk` | global | `display/clock.c` | I / 旧名で照合 | `display/clock.h` |
| 340 | `icl_cdclk_pll_disable` | static | `display/clock.c` | S | — |
| 351 | `icl_cdclk_pll_enable` | static | `display/clock.c` | S | — |
| 366 | `icl_cdclk_pll_update` | static | `display/clock.c` | S | — |
| 375 | `adlp_cdclk_pll_crawl` | static | `display/clock.c` | S | — |
| 401 | `_bxt_set_cdclk` | static | `display/clock.c` | S | — |
| 439 | `parity_bxt_set_cdclk` | global | `display/clock.c` | I / 旧名で照合 | `display/clock.h` |
| 485 | `parity_bxt_sanitize_cdclk` | global | `display/clock.c` | I / 旧名で照合 | `display/clock.h` |
| 529 | `bxt_cdclk_init_hw` | static | `display/clock.c` | S | — |
| 550 | `parity_intel_cdclk_init_hw` | global | `display/clock.c` | I / 旧名で照合 | `display/clock.h` |
| 558 | `parity_intel_max_cdclk_freq` | global | `display/clock.c` | I / 旧名で照合 | `display/clock.h` |

### parity/combo_phy.c

現行: [parity/combo_phy.c](../../src/drivers/gpu/i915/parity/combo_phy.c)。19定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 47 | `combophy_base` | static | `display/phy.c` | S | — |
| 48 | `comp_dw` | static | `display/phy.c` | S | — |
| 49 | `cl_dw` | static | `display/phy.c` | S | — |
| 50 | `phy_misc` | static | `display/phy.c` | S | — |
| 51 | `tx_dw8_ln0` | static | `display/phy.c` | S | — |
| 52 | `tx_dw8_grp` | static | `display/phy.c` | S | — |
| 53 | `pcs_dw1_ln0` | static | `display/phy.c` | S | — |
| 54 | `pcs_dw1_grp` | static | `display/phy.c` | S | — |
| 57 | `has_phy_misc` | static | `display/phy.c` | S | — |
| 58 | `phy_is_master` | static | `display/phy.c` | S | — |
| 61 | `rmw` | static | `display/phy.c` | S | — |
| 70 | `get_procmon` | static | `display/phy.c` | S | — |
| 87 | `check_phy_reg` | static | `display/phy.c` | S | — |
| 93 | `verify_procmon` | static | `display/phy.c` | S | — |
| 105 | `set_procmon` | static | `display/phy.c` | S | — |
| 116 | `combo_phy_enabled` | static | `display/phy.c` | S | — |
| 125 | `parity_combo_phy_verify_state` | global | `display/phy.c` | I / 旧名で照合 | `display/phy.h` |
| 148 | `parity_combo_phy_init_one` | global | `display/phy.c` | I / 旧名で照合 | `display/phy.h` |
| 180 | `parity_intel_combo_phy_init` | global | `display/phy.c` | I / 旧名で照合 | `display/phy.h` |

### parity/display_core.c

現行: [parity/display_core.c](../../src/drivers/gpu/i915/parity/display_core.c)。17定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 77 | `rmw` | static | `display/power.c` | S | — |
| 86 | `gen9_set_dc_state_disable` | static | `display/power.c` | S | — |
| 99 | `pch_reset_handshake` | static | `display/power.c` | S | — |
| 107 | `parity_enabled_dbuf_slices_mask` | global | `display/power.c` | I / 旧名で照合 | `display/power.h` |
| 120 | `gen9_dbuf_slice_set` | static | `display/power.c` | S | — |
| 135 | `gen9_dbuf_slices_update` | static | `display/power.c` | S | — |
| 148 | `parity_dbuf_ctl_reg` | global | `display/power.c` | I / 旧名で照合 | `display/power.h` |
| 155 | `parity_gen9_dbuf_slices_update` | global | `display/power.c` | I / 旧名で照合 | `display/power.h` |
| 165 | `gen12_dbuf_slices_config` | static | `display/power.c` | S | — |
| 184 | `gen9_dbuf_enable` | static | `display/power.c` | S | — |
| 214 | `icl_mbus_init` | static | `display/power.c` | S | — |
| 239 | `tgl_bw_buddy_init` | static | `display/power.c` | S | — |
| 291 | `fault` | static | `display/power.c` | S | — |
| 301 | `icl_display_core_init` | static | `display/power.c` | S | — |
| 392 | `parity_dc_off_enable` | global | `display/power.c` | I / 旧名で照合 | `display/power.h` |
| 441 | `parity_intel_power_domains_init_hw` | global | `display/power.c` | I / 旧名で照合 | `display/power.h` |
| 492 | `parity_intel_power_domains_driver_remove` | global | `display/power.c` | I / 旧名で照合 | `display/power.h` |

### parity/display_nogem.c

現行: [parity/display_nogem.c](../../src/drivers/gpu/i915/parity/display_nogem.c)。46定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 91 | `zero_mem` | static | `display/takeover.c` | S | — |
| 101 | `popcount4` | static | `display/takeover.c` | S | — |
| 110 | `wr` | static | `display/takeover.c` | S | — |
| 117 | `rmw` | static | `display/takeover.c` | S | — |
| 132 | `parity_adjust_wm_latency` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 165 | `parity_skl_setup_wm_latency` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 209 | `intel_sagv_block_time` | static | `display/takeover.c` | S | — |
| 232 | `intel_sagv_init` | static | `display/takeover.c` | S | — |
| 266 | `parity_intel_shared_dpll_init` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 315 | `parity_intel_crtc_init` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 377 | `parity_intel_display_wa_apply` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 416 | `parity_intel_update_max_cdclk` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 428 | `gmbus_setup` | static | `display/takeover.c` | S | — |
| 479 | `parity_intel_display_nogem_front` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 615 | `icl_dpclka_ddi_clk_off` | static | `display/takeover.c` | S | — |
| 630 | `icl_dpclka_tc_clk_off` | static | `display/takeover.c` | S | — |
| 638 | `parity_intel_ddi_crt_present` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 652 | `parity_dvo_port_to_port` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 684 | `parity_intel_port_to_phy` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 692 | `parity_intel_phy_is_tc` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 700 | `parity_intel_ddi_is_tc` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 711 | `ddi_lanes_domain` | static | `display/takeover.c` | S | — |
| 724 | `port_in_use` | static | `display/takeover.c` | S | — |
| 735 | `ddi_skip` | static | `display/takeover.c` | S | — |
| 751 | `intel_ddi_init` | static | `display/takeover.c` | S | — |
| 861 | `parity_intel_setup_outputs` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 895 | `parity_intel_ddi_get_hw_state` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 966 | `parity_intel_ddi_is_clock_enabled` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 987 | `parity_intel_ddi_disable_clock` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 1029 | `trans_reg` | static | `display/takeover.c` | S | — |
| 1043 | `trans_power_on` | static | `display/takeover.c` | S | — |
| 1058 | `parity_hsw_enabled_transcoders` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 1112 | `get_transcoder_timings` | static | `display/takeover.c` | S | — |
| 1128 | `hsw_get_pipe_config` | static | `display/takeover.c` | S | — |
| 1175 | `readout_plane_state` | static | `display/takeover.c` | S | — |
| 1214 | `parity_intel_dpll_readout` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 1271 | `parity_intel_modeset_readout_hw_state` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 1375 | `intel_early_display_was` | static | `display/takeover.c` | S | — |
| 1387 | `intel_fbc_sanitize` | static | `display/takeover.c` | S | — |
| 1417 | `sanitize_encoder_pll_mapping` | static | `display/takeover.c` | S | — |
| 1439 | `intel_sanitize_crtc` | static | `display/takeover.c` | S | — |
| 1469 | `adlp_cmtg_clock_gating_wa` | static | `display/takeover.c` | S | — |
| 1490 | `parity_intel_dpll_sanitize_state` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 1526 | `intel_power_domains_sanitize_state` | static | `display/takeover.c` | S | — |
| 1546 | `parity_intel_modeset_sanitize_hw_state` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 1634 | `parity_intel_display_nogem_fini` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |

### parity/display_state.c

現行: [parity/display_state.c](../../src/drivers/gpu/i915/parity/display_state.c)。33定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 48 | `zero_mem` | static | `display/state.c` | S | — |
| 59 | `genmask_low` | static | `display/state.c` | I / 旧名で照合 | `display/state.h` |
| 70 | `is_power_of_2` | static | `display/state.c` | I / 旧名で照合 | `display/state.h` |
| 77 | `rmw` | static | `display/state.c` | I / 旧名で照合 | `display/state.h` |
| 93 | `parity_atomic_global_obj_init` | static | `display/state.c` | I / 旧名で照合 | `display/state.h` |
| 123 | `alloc_global_state` | static | `display/state.c` | I / 旧名で照合 | `display/state.h` |
| 152 | `parity_intel_mode_config_init` | global | `display/state.c` | I / 旧名で照合 | `display/state.h` |
| 220 | `parity_intel_cdclk_init` | global | `display/state.c` | I / 旧名で照合 | `display/state.h` |
| 242 | `parity_intel_color_init` | global | `display/state.c` | I / 旧名で照合 | `display/state.h` |
| 263 | `parity_intel_dbuf_init` | global | `display/state.c` | I / 旧名で照合 | `display/state.h` |
| 286 | `parity_intel_has_sagv` | global | `display/watermark.c` | I / 旧名で照合 | `display/watermark.h` |
| 299 | `parity_icl_qgv_points_mask` | global | `display/watermark.c` | I / 旧名で照合 | `display/watermark.h` |
| 326 | `icl_max_bw_index` | static | `display/watermark.c` | S | — |
| 352 | `tgl_max_bw_index` | static | `display/watermark.c` | S | — |
| 378 | `icl_qgv_bw` | static | `display/watermark.c` | S | — |
| 395 | `adl_psf_bw` | static | `display/watermark.c` | S | — |
| 404 | `parity_icl_qgv_bw` | global | `display/watermark.c` | I / 旧名で照合 | `display/watermark.h` |
| 410 | `parity_icl_max_bw_qgv_point_mask` | global | `display/watermark.c` | I / 旧名で照合 | `display/watermark.h` |
| 438 | `parity_icl_max_bw_psf_gv_point_mask` | global | `display/watermark.c` | I / 旧名で照合 | `display/watermark.h` |
| 460 | `icl_prepare_qgv_points_mask` | static | `display/watermark.c` | S | — |
| 469 | `is_sagv_enabled` | static | `display/watermark.c` | S | — |
| 477 | `parity_icl_pcode_restrict_qgv_points` | global | `display/watermark.c` | I / 旧名で照合 | `display/watermark.h` |
| 505 | `icl_force_disable_sagv` | static | `display/watermark.c` | S | — |
| 527 | `parity_intel_bw_init` | global | `display/watermark.c` | I / 旧名で照合 | `display/watermark.h` |
| 558 | `parity_intel_pmdemand_init` | global | `display/watermark.c` | I / 旧名で照合 | `display/watermark.h` |
| 681 | `intel_set_quirk` | static | `display/state.c` | S | — |
| 689 | `parity_intel_init_quirks` | global | `display/state.c` | I / 旧名で照合 | `display/state.h` |
| 724 | `fbc_underrun_work_fn` | static | `display/state.c` | S | — |
| 737 | `need_fbc_vtd_wa` | static | `display/state.c` | S | — |
| 751 | `parity_intel_sanitize_fbc_option` | global | `display/state.c` | I / 旧名で照合 | `display/state.h` |
| 768 | `intel_fbc_create` | static | `display/state.c` | S | — |
| 799 | `parity_intel_fbc_init` | global | `display/state.c` | I / 旧名で照合 | `display/state.h` |
| 831 | `parity_intel_display_state_fini` | global | `display/state.c` | I / 旧名で照合 | `display/state.h` |

### parity/dmc.c

現行: [parity/dmc.c](../../src/drivers/gpu/i915/parity/dmc.c)。26定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 95 | `arena_copy` | static | `display/dmc.c` | S | — |
| 109 | `is_valid_dmc_id` | static | `display/dmc.c` | S | — |
| 113 | `fw_info_matches_stepping` | static | `display/dmc.c` | S | — |
| 125 | `dmc_set_fw_offset` | static | `display/dmc.c` | S | — |
| 146 | `mmio_addr_ok` | static | `display/dmc.c` | S | — |
| 174 | `parse_css` | static | `display/dmc.c` | S | — |
| 192 | `parse_package` | static | `display/dmc.c` | S | — |
| 230 | `parse_header` | static | `display/dmc.c` | S | — |
| 316 | `parity_dmc_prepare` | global | `display/dmc.c` | I / 旧名で照合 | `display/dmc.h` |
| 332 | `parity_parse_dmc_fw` | global | `display/dmc.c` | I / 旧名で照合 | `display/dmc.h` |
| 365 | `parity_dmc_parse_reset` | global | `display/dmc.c` | I / 旧名で照合 | `display/dmc.h` |
| 378 | `parity_dmc_has_payload` | global | `display/dmc.c` | I / 旧名で照合 | `display/dmc.h` |
| 399 | `dmc_reg_base` | static | `display/dmc.c` | S | — |
| 405 | `dmc_reg` | static | `display/dmc.c` | S | — |
| 406 | `dmc_evt_ctl` | static | `display/dmc.c` | S | — |
| 407 | `dmc_evt_htp` | static | `display/dmc.c` | S | — |
| 410 | `is_evt_ctl` | static | `display/dmc.c` | S | — |
| 421 | `dmc_mmiodata` | static | `display/dmc.c` | S | — |
| 429 | `payload_dword` | static | `display/dmc.c` | S | — |
| 438 | `rmw32` | static | `display/dmc.c` | S | — |
| 446 | `parity_intel_dmc_load_program` | global | `display/dmc.c` | I / 旧名で照合 | `display/dmc.h` |
| 538 | `dmc_get_ref` | static | `display/dmc.c` | S | — |
| 546 | `dmc_put_ref` | static | `display/dmc.c` | S | — |
| 556 | `dmc_load_work_fn` | static | `display/dmc.c` | S | — |
| 609 | `parity_intel_dmc_init` | global | `display/dmc.c` | I / 旧名で照合 | `display/dmc.h` |
| 643 | `parity_intel_dmc_fini` | global | `display/dmc.c` | I / 旧名で照合 | `display/dmc.h` |

### parity/dp/dp_compat.h

現行: [parity/dp/dp_compat.h](../../src/drivers/gpu/i915/parity/dp/dp_compat.h)。12定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 58 | `parity_dp_memcpy` | static | `display/dp-internal.h` | S | `display/dp-internal.h` |
| 97 | `i915_mmio_reg_offset` | static | `display/dp-internal.h` | S | `display/dp-internal.h` |
| 98 | `i915_mmio_reg_valid` | static | `display/dp-internal.h` | S | `display/dp-internal.h` |
| 132 | `parity_dp_env_of` | static | `display/dp-internal.h` | S | `display/dp-internal.h` |
| 137 | `intel_de_rmw` | static | `display/dp-internal.h` | S | `display/dp-internal.h` |
| 163 | `wait_remaining_ms_from_jiffies` | static | `display/dp-internal.h` | S | `display/dp-internal.h` |
| 230 | `i2c_transfer` | static | `display/dp-internal.h` | S | `display/dp-internal.h` |
| 289 | `dp_to_dig_port` | static | `display/dp-internal.h` | S | `display/dp-internal.h` |
| 293 | `dp_to_i915` | static | `display/dp-internal.h` | S | `display/dp-internal.h` |
| 295 | `to_i915` | static | `display/dp-internal.h` | S | `display/dp-internal.h` |
| 299 | `intel_dp_is_edp` | static | `display/dp-internal.h` | S | `display/dp-internal.h` |
| 301 | `intel_aux_power_domain` | static | `display/dp-internal.h` | S | `display/dp-internal.h` |

### parity/dp/dp_fake_hw.c

現行: [parity/dp/dp_fake_hw.c](../../src/drivers/gpu/i915/parity/dp/dp_fake_hw.c)。24定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 39 | `bytes_clear` | static | `tests/display/dp-fake-hw.c` | T | — |
| 47 | `dp_fake_init` | global | `tests/display/dp-fake-hw.c` | T | `tests/display/dp-fake-hw.h` |
| 69 | `dp_fake_script` | global | `tests/display/dp-fake-hw.c` | T | `tests/display/dp-fake-hw.h` |
| 86 | `aux_finish` | static | `tests/display/dp-fake-hw.c` | T | — |
| 91 | `aux_reply` | static | `tests/display/dp-fake-hw.c` | T | — |
| 102 | `aux_transaction` | static | `tests/display/dp-fake-hw.c` | T | — |
| 224 | `pp_status` | static | `tests/display/dp-fake-hw.c` | T | — |
| 230 | `fake_read` | static | `tests/display/dp-fake-hw.c` | T | — |
| 258 | `fake_write` | static | `tests/display/dp-fake-hw.c` | T | — |
| 299 | `fake_wait_reg` | static | `tests/display/dp-fake-hw.c` | T | — |
| 320 | `fake_sleep_us` | static | `tests/display/dp-fake-hw.c` | T | — |
| 325 | `fake_now_ms` | static | `tests/display/dp-fake-hw.c` | T | — |
| 330 | `fake_power_get` | static | `tests/display/dp-fake-hw.c` | T | — |
| 349 | `fake_power_put_async` | static | `tests/display/dp-fake-hw.c` | T | — |
| 364 | `fake_lock` | static | `tests/display/dp-fake-hw.c` | T | — |
| 374 | `fake_unlock` | static | `tests/display/dp-fake-hw.c` | T | — |
| 383 | `fake_delayed_queue` | static | `tests/display/dp-fake-hw.c` | T | — |
| 396 | `fake_delayed_cancel` | static | `tests/display/dp-fake-hw.c` | T | — |
| 413 | `fake_delayed_pending` | static | `tests/display/dp-fake-hw.c` | T | — |
| 419 | `release_parked` | static | `tests/display/dp-fake-hw.c` | T | — |
| 429 | `dp_fake_run_due` | global | `tests/display/dp-fake-hw.c` | T | `tests/display/dp-fake-hw.h` |
| 445 | `dp_fake_flush_async` | global | `tests/display/dp-fake-hw.c` | T | `tests/display/dp-fake-hw.h` |
| 454 | `fake_power_put` | static | `tests/display/dp-fake-hw.c` | T | — |
| 464 | `dp_fake_bind_env` | global | `tests/display/dp-fake-hw.c` | T | `tests/display/dp-fake-hw.h` |

### parity/dp/drm_dp_helper_port.c

現行: [parity/dp/drm_dp_helper_port.c](../../src/drivers/gpu/i915/parity/dp/drm_dp_helper_port.c)。17定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 45 | `drm_dp_dump_access` | static | `display/dp.c` | S | — |
| 70 | `drm_dp_dpcd_access` | static | `display/dp.c` | S | — |
| 147 | `drm_dp_dpcd_probe` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 174 | `drm_dp_dpcd_read` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 221 | `drm_dp_dpcd_write` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 236 | `drm_dp_read_extended_dpcd_caps` | static | `display/dp.c` | S | — |
| 290 | `drm_dp_read_dpcd_caps` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 310 | `drm_dp_i2c_functionality` | static | `display/dp.c` | S | — |
| 318 | `drm_dp_i2c_msg_write_status_update` | static | `display/dp.c` | S | — |
| 343 | `drm_dp_aux_req_duration` | static | `display/dp.c` | S | — |
| 354 | `drm_dp_aux_reply_duration` | static | `display/dp.c` | S | — |
| 382 | `drm_dp_i2c_msg_duration` | static | `display/dp.c` | S | — |
| 396 | `drm_dp_i2c_retry_count` | static | `display/dp.c` | S | — |
| 420 | `drm_dp_i2c_do_msg` | static | `display/dp.c` | S | — |
| 529 | `drm_dp_i2c_msg_set_request` | static | `display/dp.c` | S | — |
| 543 | `drm_dp_i2c_drain_msg` | static | `display/dp.c` | S | — |
| 574 | `drm_dp_i2c_xfer` | static | `display/dp.c` | S | — |

### parity/dp/drm_edid_port.c

現行: [parity/dp/drm_edid_port.c](../../src/drivers/gpu/i915/parity/dp/drm_edid_port.c)。4定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 59 | `drm_edid_header_is_valid` | global | `display/edid.c` | I / 旧名で照合 | `display/edid.h` |
| 72 | `edid_block_compute_checksum` | static | `display/edid.c` | S | — |
| 86 | `edid_block_get_checksum` | static | `display/edid.c` | S | — |
| 106 | `drm_do_probe_ddc_edid` | static | `display/edid.c` | S | — |

### parity/dp/edp_ktest.c

現行: [parity/dp/edp_ktest.c](../../src/drivers/gpu/i915/parity/dp/edp_ktest.c)。4定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 21 | `target_cfg` | static | `tests/display/edp-ktest.c` | T | — |
| 32 | `fresh` | static | `tests/display/edp-ktest.c` | T | — |
| 39 | `released` | static | `tests/display/edp-ktest.c` | T | — |
| 49 | `parity_edp_ktest` | global | `tests/display/edp-ktest.c` | T | `tests/display/edp-ktest.h` |

### parity/dp/edp_sync_ktest.c

現行: [parity/dp/edp_sync_ktest.c](../../src/drivers/gpu/i915/parity/dp/edp_sync_ktest.c)。16定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 23 | `ticks_after_ms` | static | `tests/display/edp-sync-ktest.c` | T | — |
| 39 | `body_fn` | static | `tests/display/edp-sync-ktest.c` | T | — |
| 50 | `delayed_work_checks` | static | `tests/display/edp-sync-ktest.c` | T | — |
| 137 | `hy_read32` | static | `tests/display/edp-sync-ktest.c` | T | — |
| 143 | `hy_write32` | static | `tests/display/edp-sync-ktest.c` | T | — |
| 154 | `hy_wait_reg` | static | `tests/display/edp-sync-ktest.c` | T | — |
| 161 | `hy_sleep_us` | static | `tests/display/edp-sync-ktest.c` | T | — |
| 162 | `hy_now_ms` | static | `tests/display/edp-sync-ktest.c` | T | — |
| 164 | `hy_power_get` | static | `tests/display/edp-sync-ktest.c` | T | — |
| 175 | `hy_power_put` | static | `tests/display/edp-sync-ktest.c` | T | — |
| 183 | `hy_power_put_async` | static | `tests/display/edp-sync-ktest.c` | T | — |
| 191 | `hybrid_fresh` | static | `tests/display/edp-sync-ktest.c` | T | — |
| 210 | `hybrid_released` | static | `tests/display/edp-sync-ktest.c` | T | — |
| 220 | `wait_vdd` | static | `tests/display/edp-sync-ktest.c` | T | — |
| 231 | `edp_concurrency_checks` | static | `tests/display/edp-sync-ktest.c` | T | — |
| 307 | `parity_edp_sync_ktest` | global | `tests/display/edp-sync-ktest.c` | T | `tests/display/edp-sync-ktest.h` |

### parity/dp/intel_dp_aux_port.c

現行: [parity/dp/intel_dp_aux_port.c](../../src/drivers/gpu/i915/parity/dp/intel_dp_aux_port.c)。13定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 29 | `intel_dp_aux_pack` | global | `display/aux.c` | I / 旧名で照合 | `display/aux.h` |
| 41 | `intel_dp_aux_unpack` | static | `display/aux.c` | S | — |
| 52 | `intel_dp_aux_wait_done` | static | `display/aux.c` | S | — |
| 75 | `skl_get_aux_clock_divider` | static | `display/aux.c` | S | — |
| 85 | `intel_dp_aux_sync_len` | static | `display/aux.c` | S | — |
| 93 | `intel_dp_aux_fw_sync_len` | global | `display/aux.c` | I / 旧名で照合 | `display/aux.h` |
| 114 | `skl_get_aux_send_ctl` | static | `display/aux.c` | S | — |
| 151 | `intel_dp_aux_xfer` | static | `display/aux.c` | S | — |
| 362 | `intel_dp_aux_header` | static | `display/aux.c` | S | — |
| 371 | `intel_dp_aux_xfer_flags` | static | `display/aux.c` | S | — |
| 386 | `intel_dp_aux_transfer` | static | `display/aux.c` | S | — |
| 466 | `tgl_aux_ctl_reg` | static | `display/aux.c` | S | — |
| 488 | `tgl_aux_data_reg` | static | `display/aux.c` | S | — |

### parity/dp/intel_pps_port.c

現行: [parity/dp/intel_pps_port.c](../../src/drivers/gpu/i915/parity/dp/intel_pps_port.c)。54定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 29 | `pps_name` | static | `display/panel.c` | S | — |
| 63 | `intel_pps_lock` | global | `display/panel.c` | I / 旧名で照合 | `display/panel.h` |
| 77 | `intel_pps_unlock` | global | `display/panel.c` | I / 旧名で照合 | `display/panel.h` |
| 92 | `bxt_power_sequencer_idx` | static | `display/panel.c` | S | — |
| 118 | `pps_has_pp_on` | static | `display/panel.c` | S | — |
| 123 | `pps_has_vdd_on` | static | `display/panel.c` | S | — |
| 128 | `pps_any` | static | `display/panel.c` | S | — |
| 135 | `intel_num_pps` | static | `display/panel.c` | S | — |
| 155 | `intel_pps_is_valid` | static | `display/panel.c` | S | — |
| 168 | `bxt_initial_pps_idx` | static | `display/panel.c` | S | — |
| 181 | `pps_initial_setup` | static | `display/panel.c` | S | — |
| 236 | `intel_pps_get_registers` | static | `display/panel.c` | S | — |
| 265 | `_pp_ctrl_reg` | static | `display/panel.c` | S | — |
| 275 | `_pp_stat_reg` | static | `display/panel.c` | S | — |
| 284 | `edp_have_panel_power` | static | `display/panel.c` | S | — |
| 297 | `edp_have_panel_vdd` | static | `display/panel.c` | S | — |
| 310 | `intel_pps_check_power_unlocked` | global | `display/panel.c` | I / 旧名で照合 | `display/panel.h` |
| 343 | `wait_panel_status` | static | `display/panel.c` | S | — |
| 377 | `wait_panel_on` | static | `display/panel.c` | S | — |
| 388 | `wait_panel_off` | static | `display/panel.c` | S | — |
| 399 | `wait_panel_power_cycle` | static | `display/panel.c` | S | — |
| 424 | `intel_pps_wait_power_cycle` | global | `display/panel.c` | I / 旧名で照合 | `display/panel.h` |
| 435 | `wait_backlight_on` | static | `display/panel.c` | S | — |
| 441 | `edp_wait_backlight_off` | static | `display/panel.c` | S | — |
| 451 | `ilk_get_pp_control` | static | `display/panel.c` | S | — |
| 472 | `intel_pps_vdd_on_unlocked` | global | `display/panel.c` | I / 旧名で照合 | `display/panel.h` |
| 535 | `intel_pps_vdd_on` | global | `display/panel.c` | I / 旧名で照合 | `display/panel.h` |
| 553 | `intel_pps_vdd_off_sync_unlocked` | static | `display/panel.c` | S | — |
| 596 | `intel_pps_vdd_off_sync` | global | `display/panel.c` | I / 旧名で照合 | `display/panel.h` |
| 612 | `edp_panel_vdd_work` | static | `display/panel.c` | S | — |
| 625 | `edp_panel_vdd_schedule_off` | static | `display/panel.c` | S | — |
| 652 | `intel_pps_vdd_off_unlocked` | global | `display/panel.c` | I / 旧名で照合 | `display/panel.h` |
| 675 | `intel_pps_on_unlocked` | global | `display/panel.c` | I / 旧名で照合 | `display/panel.h` |
| 738 | `intel_pps_on` | global | `display/panel.c` | I / 旧名で照合 | `display/panel.h` |
| 749 | `intel_pps_off_unlocked` | global | `display/panel.c` | I / 旧名で照合 | `display/panel.h` |
| 792 | `intel_pps_off` | global | `display/panel.c` | I / 旧名で照合 | `display/panel.h` |
| 804 | `intel_pps_backlight_on` | global | `display/panel.c` | I / 旧名で照合 | `display/panel.h` |
| 830 | `intel_pps_backlight_off` | global | `display/panel.c` | I / 旧名で照合 | `display/panel.h` |
| 857 | `pps_vdd_init` | static | `display/panel.c` | S | — |
| 882 | `intel_pps_have_panel_power_or_vdd` | global | `display/panel.c` | I / 旧名で照合 | `display/panel.h` |
| 895 | `pps_init_timestamps` | static | `display/panel.c` | S | — |
| 909 | `intel_pps_readout_hw_state` | static | `display/panel.c` | S | — |
| 944 | `intel_pps_dump_state` | static | `display/panel.c` | S | — |
| 955 | `intel_pps_verify_state` | static | `display/panel.c` | S | — |
| 971 | `pps_delays_valid` | static | `display/panel.c` | S | — |
| 977 | `pps_init_delays_bios` | static | `display/panel.c` | S | — |
| 992 | `pps_init_delays_vbt` | static | `display/panel.c` | S | — |
| 1024 | `pps_init_delays_spec` | static | `display/panel.c` | S | — |
| 1046 | `pps_init_delays` | static | `display/panel.c` | S | — |
| 1109 | `pps_init_registers` | static | `display/panel.c` | S | — |
| 1200 | `intel_pps_encoder_reset` | global | `display/panel.c` | I / 旧名で照合 | `display/panel.h` |
| 1225 | `intel_pps_init` | global | `display/panel.c` | I / 旧名で照合 | `display/panel.h` |
| 1246 | `pps_init_late` | static | `display/panel.c` | S | — |
| 1268 | `intel_pps_init_late` | global | `display/panel.c` | I / 旧名で照合 | `display/panel.h` |

### parity/dp/parity_dp_aux_glue.inc

現行: [parity/dp/parity_dp_aux_glue.inc](../../src/drivers/gpu/i915/parity/dp/parity_dp_aux_glue.inc)。1定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 12 | `parity_intel_dp_aux_init` | global | `display/aux.c` | I / 旧名で照合 | `display/aux.h` |

### parity/dp/parity_dp_kernel.c

現行: [parity/dp/parity_dp_kernel.c](../../src/drivers/gpu/i915/parity/dp/parity_dp_kernel.c)。34定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 29 | `k_read32` | static | `display/dp.c` | S | — |
| 34 | `k_write32` | static | `display/dp.c` | S | — |
| 39 | `k_wait_reg` | static | `display/dp.c` | S | — |
| 55 | `now_us` | static | `display/dp.c` | S | — |
| 77 | `parity_dp_kernel_sleep_us` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 93 | `k_sleep_us` | static | `display/dp.c` | S | — |
| 98 | `k_now_ms` | static | `display/dp.c` | S | — |
| 108 | `k_power_get` | static | `display/dp.c` | S | — |
| 115 | `k_power_put` | static | `display/dp.c` | S | — |
| 122 | `k_power_put_async` | static | `display/dp.c` | S | — |
| 130 | `k_lock` | static | `display/dp.c` | S | — |
| 135 | `k_unlock` | static | `display/dp.c` | S | — |
| 140 | `sync_deadline` | static | `display/dp.c` | S | — |
| 149 | `k_delayed_queue` | static | `display/dp.c` | S | — |
| 157 | `k_delayed_cancel` | static | `display/dp.c` | S | — |
| 166 | `k_delayed_pending` | static | `display/dp.c` | S | — |
| 174 | `vdd_off_body` | static | `display/dp.c` | S | — |
| 180 | `async_put_body` | static | `display/dp.c` | S | — |
| 185 | `pd_async_queue` | static | `display/dp.c` | S | — |
| 192 | `pd_async_cancel` | static | `display/dp.c` | S | — |
| 202 | `parity_dp_kernel_sync_start` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 222 | `parity_dp_kernel_sync_stop` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 233 | `parity_dp_kernel_bind_sync` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 243 | `parity_dp_kernel_bind` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 258 | `log_bytes` | static | `display/dp.c` | S | — |
| 269 | `log_pps` | static | `display/dp.c` | S | — |
| 275 | `cfg_from_panel` | static | `display/dp.c` | S | — |
| 296 | `parity_edp_device_prepare` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 306 | `well_refs` | static | `display/dp.c` | S | — |
| 315 | `parity_edp_device_init_connector` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 462 | `parity_edp_device_fini` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 493 | `vdd_is_on` | static | `display/dp.c` | S | — |
| 500 | `wait_vdd_off` | static | `display/dp.c` | S | — |
| 513 | `parity_edp_aux_test_run` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |

### parity/dp/parity_drm_dp_glue.inc

現行: [parity/dp/parity_drm_dp_glue.inc](../../src/drivers/gpu/i915/parity/dp/parity_drm_dp_glue.inc)。1定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 8 | `parity_drm_dp_aux_init` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |

### parity/dp/parity_drm_edid_glue.inc

現行: [parity/dp/parity_drm_edid_glue.inc](../../src/drivers/gpu/i915/parity/dp/parity_drm_edid_glue.inc)。1定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 16 | `parity_drm_edid_read` | global | `display/edid.c` | I / 旧名で照合 | `display/edid.h` |

### parity/dp/parity_edp.c

現行: [parity/dp/parity_edp.c](../../src/drivers/gpu/i915/parity/dp/parity_edp.c)。26定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 43 | `parity_dp_log_enabled` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 50 | `parity_dp_note` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 58 | `parity_dp_env_current` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 63 | `parity_dp_sleep_us` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 74 | `parity_dp_now_ms` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 81 | `power_slot` | static | `display/dp.c` | S | — |
| 86 | `parity_dp_power_get` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 100 | `parity_dp_power_put` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 115 | `parity_dp_power_put_async` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 132 | `parity_dp_mutex_lock` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 145 | `parity_dp_mutex_unlock` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 157 | `parity_dp_delayed_queue` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 164 | `parity_dp_delayed_cancel` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 172 | `read_pps_regs` | static | `display/dp.c` | S | — |
| 185 | `snapshot_ownership` | static | `display/dp.c` | S | — |
| 207 | `apply_panel_vbt` | static | `display/dp.c` | S | — |
| 219 | `fail` | static | `display/dp.c` | S | — |
| 229 | `parity_edp_begin` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 326 | `parity_edp_init_late` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 346 | `parity_edp_work_run` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 356 | `parity_edp_snapshot` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 362 | `parity_edp_end` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 388 | `parity_edp_dpcd_read` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 395 | `parity_edp_dpcd_write` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 402 | `parity_edp_read_dpcd_caps` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 410 | `parity_edp_panel_op` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |

### parity/dram_bw.c

現行: [parity/dram_bw.c](../../src/drivers/gpu/i915/parity/dram_bw.c)。6定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 47 | `zero_bytes` | static | `display/watermark.c` | S | — |
| 57 | `parity_dram_decode` | global | `display/watermark.c` | I / 旧名で照合 | `display/watermark.h` |
| 85 | `parity_dram_detect` | global | `display/watermark.c` | I / 旧名で照合 | `display/watermark.h` |
| 108 | `parity_icl_get_qgv_points` | static | `display/watermark.c` | S | — |
| 184 | `parity_sagv_max_dclk` | static | `display/watermark.c` | S | — |
| 195 | `parity_bw_init_hw` | global | `display/watermark.c` | I / 旧名で照合 | `display/watermark.h` |

### parity/driver_probe.c

現行: [parity/driver_probe.c](../../src/drivers/gpu/i915/parity/driver_probe.c)。27定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 26 | `rmw` | static | `display/display.c` | I / 旧名で照合 | `display/display.h` |
| 37 | `parity_intel_ddi_hpd_pin` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 48 | `gen11_tc_hotplug` | static | `display/hotplug.c` | S | — |
| 49 | `gen11_tbt_hotplug` | static | `display/hotplug.c` | S | — |
| 50 | `gen11_hotplug_ctl_enable` | static | `display/hotplug.c` | S | — |
| 51 | `sde_ddi_hotplug_icp` | static | `display/hotplug.c` | S | — |
| 52 | `sde_tc_hotplug_icp` | static | `display/hotplug.c` | S | — |
| 53 | `shotplug_ctl_ddi_hpd_enable` | static | `display/hotplug.c` | S | — |
| 54 | `icp_tc_hpd_enable` | static | `display/hotplug.c` | S | — |
| 56 | `is_tc_pin` | static | `display/hotplug.c` | S | — |
| 57 | `is_ddi_pin` | static | `display/hotplug.c` | S | — |
| 60 | `parity_intel_hpd_init_pins` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 82 | `hpd_irqs` | static | `display/hotplug.c` | S | — |
| 102 | `hotplug_mask` | static | `display/hotplug.c` | S | — |
| 114 | `hotplug_enables` | static | `display/hotplug.c` | S | — |
| 127 | `gen11_hpd_irq_setup` | static | `display/hotplug.c` | S | — |
| 185 | `parity_intel_hpd_irq_setup` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 193 | `parity_intel_hpd_init` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 214 | `parity_intel_hpd_poll_disable` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 232 | `parity_skl_watermark_ipc_init` | global | `display/watermark.c` | I / 旧名で照合 | `display/watermark.h` |
| 247 | `parity_intel_display_driver_probe` | global | `display/display.c` | I / 旧名で照合 | `display/display.h` |
| 297 | `parity_intel_power_domains_verify_state` | global | `display/power.c` | I / 旧名で照合 | `display/power.h` |
| 318 | `wells_on` | static | `display/power.c` | S | — |
| 329 | `parity_intel_power_domains_enable` | global | `display/power.c` | I / 旧名で照合 | `display/power.h` |
| 345 | `parity_intel_power_domains_disable` | global | `display/power.c` | I / 旧名で照合 | `display/power.h` |
| 357 | `parity_i915_driver_register` | global | `device.c` | I / 旧名で照合 | `device.h` |
| 403 | `parity_i915_driver_unregister` | global | `device.c` | I / 旧名で照合 | `device.h` |

### parity/drm_device.c

現行: [parity/drm_device.c](../../src/drivers/gpu/i915/parity/drm_device.c)。7定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 27 | `parity_drm_vblank_test_fail_worker_at` | global | `device.c` | I / 旧名で照合 | `device.h` |
| 33 | `vblank_worker_should_fail` | static | `device.c` | S | — |
| 44 | `parity_drm_dev_init` | global | `device.c` | I / 旧名で照合 | `device.h` |
| 67 | `parity_drmm_add_action_or_reset` | global | `device.c` | I / 旧名で照合 | `device.h` |
| 85 | `parity_drm_dev_fini` | global | `device.c` | I / 旧名で照合 | `device.h` |
| 107 | `parity_drm_vblank_crtc_cleanup` | static | `device.c` | S | — |
| 125 | `parity_drm_vblank_init` | global | `device.c` | I / 旧名で照合 | `device.h` |

### parity/eu_test.c

現行: [parity/eu_test.c](../../src/drivers/gpu/i915/parity/eu_test.c)。45定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 50 | `emit` | static | `tests/execution/eu-test.c` | T | — |
| 61 | `emit_pc` | static | `tests/execution/eu-test.c` | T | — |
| 74 | `emit_marker` | static | `tests/execution/eu-test.c` | T | — |
| 83 | `emit_copy` | static | `tests/execution/eu-test.c` | T | — |
| 93 | `emit_sba` | static | `tests/execution/eu-test.c` | T | — |
| 121 | `parity_eu_test_build_batch` | global | `tests/execution/eu-test.c` | T | `tests/execution/eu-test.h` |
| 218 | `parity_eu_batch_check_pipeline_select` | global | `tests/execution/eu-test.c` | T | `tests/execution/eu-test.h` |
| 243 | `fnv1a64` | static | `tests/execution/eu-test.c` | T | — |
| 256 | `fail` | static | `tests/execution/eu-test.c` | T | — |
| 268 | `retired` | static | `tests/execution/eu-test.c` | T | — |
| 274 | `wait_retired` | static | `tests/execution/eu-test.c` | T | — |
| 299 | `eu_build_request` | static | `tests/execution/eu-test.c` | T | — |
| 343 | `eu_park` | static | `tests/execution/eu-test.c` | T | — |
| 374 | `eu_log_record` | static | `tests/execution/eu-test.c` | T | — |
| 396 | `eu_hang_dump_reset` | static | `tests/execution/eu-test.c` | T | — |
| 420 | `parity_eu_test_run` | global | `tests/execution/eu-test.c` | T | `tests/execution/eu-test.h` |
| 594 | `eu_reset_markers` | static | `tests/execution/eu-test.c` | T | — |
| 610 | `parity_eu_test_repeat` | global | `tests/execution/eu-test.c` | T | `tests/execution/eu-test.h` |
| 742 | `eu_scrub_fixture_ptes` | static | `tests/execution/eu-test.c` | T | — |
| 754 | `parity_eu_test_release` | global | `tests/execution/eu-test.c` | T | `tests/execution/eu-test.h` |
| 788 | `parity_draw_batch_check_pipeline_select` | global | `tests/execution/eu-test.c` | T | `tests/execution/eu-test.h` |
| 800 | `parity_draw_test_run` | global | `tests/execution/eu-test.c` | T | `tests/execution/eu-test.h` |
| 943 | `parity_draw_test_release` | global | `tests/execution/eu-test.c` | T | `tests/execution/eu-test.h` |
| 960 | `parity_tex_test_run` | global | `tests/execution/eu-test.c` | T | `tests/execution/eu-test.h` |
| 1134 | `parity_tex_test_release` | global | `tests/execution/eu-test.c` | T | `tests/execution/eu-test.h` |
| 1163 | `parity_fhd_va_layout` | global | `tests/execution/eu-test.c` | T | `tests/execution/eu-test.h` |
| 1182 | `parity_fhd_render_verify` | global | `tests/execution/eu-test.c` | T | `tests/execution/eu-test.h` |
| 1203 | `parity_fhd_render_run` | global | `tests/execution/eu-test.c` | T | `tests/execution/eu-test.h` |
| 1211 | `parity_fhd_render_run_ex` | global | `tests/execution/eu-test.c` | T | `tests/execution/eu-test.h` |
| 1414 | `fhd_maps` | static | `tests/execution/eu-test.c` | T | — |
| 1427 | `parity_fhd_render_release` | global | `tests/execution/eu-test.c` | T | `tests/execution/eu-test.h` |
| 1473 | `parity_fhd_render_keep` | global | `tests/execution/eu-test.c` | T | `tests/execution/eu-test.h` |
| 1486 | `parity_fhd_rt_map` | global | `tests/execution/eu-test.c` | T | `tests/execution/eu-test.h` |
| 1524 | `parity_fhd_rt_unmap` | global | `tests/execution/eu-test.c` | T | `tests/execution/eu-test.h` |
| 1560 | `t3_upload` | static | `tests/execution/eu-test.c` | T | — |
| 1570 | `t3_tex_diff` | static | `tests/execution/eu-test.c` | T | — |
| 1591 | `t3_run_plan` | static | `tests/execution/eu-test.c` | T | — |
| 1808 | `parity_t3_test_run` | global | `tests/execution/eu-test.c` | T | `tests/execution/eu-test.h` |
| 1833 | `parity_bl_test_run` | global | `tests/execution/eu-test.c` | T | `tests/execution/eu-test.h` |
| 1851 | `parity_t3_test_release` | global | `tests/execution/eu-test.c` | T | `tests/execution/eu-test.h` |
| 1878 | `r1_write_c1_state` | static | `tests/execution/eu-test.c` | T | — |
| 1895 | `parity_r1_test_run` | global | `tests/execution/eu-test.c` | T | `tests/execution/eu-test.h` |
| 2109 | `parity_r1_test_release` | global | `tests/execution/eu-test.c` | T | `tests/execution/eu-test.h` |
| 2154 | `mcr_read_steered` | static | `tests/execution/eu-test.c` | T | — |
| 2171 | `parity_mcr_probe_wa` | global | `tests/execution/eu-test.c` | T | `tests/execution/eu-test.h` |

### parity/gt_defaults.c

現行: [parity/gt_defaults.c](../../src/drivers/gpu/i915/parity/gt_defaults.c)。9定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 17 | `fail` | static | `context.c` | S | — |
| 27 | `parity_engines_record_defaults_submit` | global | `context.c` | I / 旧名で照合 | `context.h` |
| 95 | `retired` | static | `context.c` | S | — |
| 102 | `parity_engines_record_defaults_poll` | global | `context.c` | I / 旧名で照合 | `context.h` |
| 169 | `parity_engines_record_defaults_finish` | global | `context.c` | I / 旧名で照合 | `context.h` |
| 196 | `release_contexts` | static | `context.c` | S | — |
| 210 | `parity_engines_defaults_release` | global | `context.c` | I / 旧名で照合 | `context.h` |
| 227 | `parity_engine_dump` | global | `context.c` | I / 旧名で照合 | `context.h` |
| 265 | `parity_engines_record_defaults` | global | `context.c` | I / 旧名で照合 | `context.h` |

### parity/gt_engine.c

現行: [parity/gt_engine.c](../../src/drivers/gpu/i915/parity/gt_engine.c)。12定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 23 | `parity_engine_setup_common` | global | `engine.c` | I / 旧名で照合 | `engine.h` |
| 69 | `parity_execlists_submission_setup` | global | `engine.c` | I / 旧名で照合 | `engine.h` |
| 103 | `wr` | static | `engine.c` | S | — |
| 111 | `enable_error_interrupt` | static | `engine.c` | S | — |
| 137 | `parity_execlists_enable` | global | `engine.c` | I / 旧名で照合 | `engine.h` |
| 164 | `parity_execlists_reset_csb_pointers` | global | `engine.c` | I / 旧名で照合 | `engine.h` |
| 213 | `parity_engine_release` | global | `engine.c` | I / 旧名で照合 | `engine.h` |
| 231 | `parity_ring_set_paused` | global | `engine.c` | I / 旧名で照合 | `engine.h` |
| 241 | `parity_engine_stop_cs` | global | `engine.c` | I / 旧名で照合 | `engine.h` |
| 286 | `msg_idle_reg` | static | `engine.c` | S | — |
| 299 | `parity_engine_wait_for_pending_mi_fw` | global | `engine.c` | I / 旧名で照合 | `engine.h` |
| 327 | `parity_execlists_reset_prepare` | global | `engine.c` | I / 旧名で照合 | `engine.h` |

### parity/gt_init_base.c

現行: [parity/gt_init_base.c](../../src/drivers/gpu/i915/parity/gt_init_base.c)。24定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 70 | `wa_add_entry` | static | `workarounds.c` | S | — |
| 103 | `parity_wa_write_or` | global | `workarounds.c` | I / 旧名で照合 | `workarounds.h` |
| 111 | `parity_wa_write_clr_set` | global | `workarounds.c` | I / 旧名で照合 | `workarounds.h` |
| 119 | `parity_wa_write` | global | `workarounds.c` | I / 旧名で照合 | `workarounds.h` |
| 133 | `parity_wa_masked_en` | global | `workarounds.c` | I / 旧名で照合 | `workarounds.h` |
| 141 | `parity_wa_masked_dis` | global | `workarounds.c` | I / 旧名で照合 | `workarounds.h` |
| 149 | `parity_wa_masked_field_set` | global | `workarounds.c` | I / 旧名で照合 | `workarounds.h` |
| 158 | `parity_wa_add_no_verify` | global | `workarounds.c` | I / 旧名で照合 | `workarounds.h` |
| 165 | `parity_wa_list_dump` | global | `workarounds.c` | I / 旧名で照合 | `workarounds.h` |
| 188 | `parity_wa_list_apply` | global | `workarounds.c` | I / 旧名で照合 | `workarounds.h` |
| 244 | `parity_engine_apply_whitelist` | global | `workarounds.c` | I / 旧名で照合 | `workarounds.h` |
| 320 | `parity_get_mocs_settings` | global | `workarounds.c` | I / 旧名で照合 | `workarounds.h` |
| 376 | `parity_intel_mocs_init` | global | `workarounds.c` | I / 旧名で照合 | `workarounds.h` |
| 398 | `parity_init_l3cc_table` | global | `workarounds.c` | I / 旧名で照合 | `workarounds.h` |
| 420 | `parity_tgl_setup_private_ppat` | global | `ppgtt.c` | I / 旧名で照合 | `ppgtt.h` |
| 438 | `parity_intel_rc6_init` | global | `power.c` | I / 旧名で照合 | `power.h` |
| 455 | `parity_gen11_rc6_enable` | global | `power.c` | I / 旧名で照合 | `power.h` |
| 523 | `parity_intel_rps_init` | global | `power.c` | I / 旧名で照合 | `power.h` |
| 558 | `parity_intel_rps_enable` | global | `power.c` | I / 旧名で照合 | `power.h` |
| 577 | `parity_gt_init_tables` | global | `device.c` | I / 旧名で照合 | `device.h` |
| 607 | `parity_gt_init_hw_core` | global | `device.c` | I / 旧名で照合 | `device.h` |
| 643 | `parity_engine_apply_resume_wa` | global | `device.c` | I / 旧名で照合 | `device.h` |
| 656 | `parity_intel_rc6_sanitize` | global | `power.c` | I / 旧名で照合 | `power.h` |
| 669 | `parity_intel_rps_sanitize` | global | `power.c` | I / 旧名で照合 | `power.h` |

### parity/gt_lrc.c

現行: [parity/gt_lrc.c](../../src/drivers/gpu/i915/parity/gt_lrc.c)。28定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 21 | `hweight8v` | static | `context.c` | S | — |
| 30 | `parity_gen12_rcs_offsets_ref` | global | `context.c` | I / 旧名で照合 | `context.h` |
| 36 | `parity_gen12_xcs_offsets_ref` | global | `context.c` | I / 旧名で照合 | `context.h` |
| 44 | `parity_lrc_state_size` | global | `context.c` | I / 旧名で照合 | `context.h` |
| 62 | `parity_lrc_set_offsets` | global | `context.c` | I / 旧名で照合 | `context.h` |
| 117 | `parity_sseu_make_rpcs` | global | `context.c` | I / 旧名で照合 | `context.h` |
| 142 | `parity_lrc_alloc` | global | `context.c` | I / 旧名で照合 | `context.h` |
| 202 | `init_common_regs` | static | `context.c` | S | — |
| 223 | `init_ppgtt_regs` | static | `context.c` | S | — |
| 238 | `reset_stop_ring` | static | `context.c` | S | — |
| 248 | `parity_lrc_init_regs` | global | `context.c` | I / 旧名で照合 | `context.h` |
| 275 | `parity_lrc_init_state` | global | `context.c` | I / 旧名で照合 | `context.h` |
| 305 | `parity_lrc_reset` | global | `context.c` | I / 旧名で照合 | `context.h` |
| 322 | `parity_lrc_aux_inv_reg` | global | `context.c` | I / 旧名で照合 | `context.h` |
| 336 | `context_wabb` | static | `context.c` | S | — |
| 347 | `lrc_indirect_bb` | static | `context.c` | S | — |
| 358 | `emit_timestamp_wa` | static | `context.c` | S | — |
| 384 | `emit_cmd_buf_wa` | static | `context.c` | S | — |
| 404 | `emit_restore_scratch` | static | `context.c` | S | — |
| 423 | `emit_aux_table_inv` | static | `context.c` | S | — |
| 434 | `emit_invalidate_state_cache` | static | `context.c` | S | — |
| 450 | `setup_predicate_disable_wa` | static | `context.c` | S | — |
| 469 | `setup_indirect_ctx_bb` | static | `context.c` | S | — |
| 505 | `setup_per_ctx_bb` | static | `context.c` | S | — |
| 527 | `parity_lrc_descriptor` | global | `context.c` | I / 旧名で照合 | `context.h` |
| 541 | `parity_lrc_update_regs` | global | `context.c` | I / 旧名で照合 | `context.h` |
| 584 | `parity_lrc_keep` | global | `context.c` | I / 旧名で照合 | `context.h` |
| 595 | `parity_lrc_release` | global | `context.c` | I / 旧名で照合 | `context.h` |

### parity/gt_mem.c

現行: [parity/gt_mem.c](../../src/drivers/gpu/i915/parity/gt_mem.c)。39定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 31 | `parity_gen12_ppgtt_pte_encode` | global | `ppgtt.c` | I / 旧名で照合 | `ppgtt.h` |
| 47 | `parity_gen8_pde_encode` | global | `ppgtt.c` | I / 旧名で照合 | `ppgtt.h` |
| 54 | `parity_gen8_pde_encode_cached` | global | `ppgtt.c` | I / 旧名で照合 | `ppgtt.h` |
| 63 | `parity_gt_mem_init` | global | `memory.c` | I / 旧名で照合 | `memory.h` |
| 93 | `parity_gt_mem_fini` | global | `memory.c` | I / 旧名で照合 | `memory.h` |
| 118 | `parity_gt_object_create` | global | `memory.c` | I / 旧名で照合 | `memory.h` |
| 196 | `parity_gt_object_destroy` | global | `memory.c` | I / 旧名で照合 | `memory.h` |
| 223 | `parity_gt_object_page_dma` | global | `memory.c` | I / 旧名で照合 | `memory.h` |
| 265 | `window_bit` | static | `ggtt.c` | S | — |
| 271 | `window_set` | static | `ggtt.c` | S | — |
| 281 | `window_alloc` | static | `ggtt.c` | S | — |
| 311 | `ggtt_write_pte` | static | `ggtt.c` | S | — |
| 318 | `parity_gt_ggtt_flush` | global | `ggtt.c` | I / 旧名で照合 | `ggtt.h` |
| 332 | `parity_gt_ggtt_bind` | global | `ggtt.c` | I / 旧名で照合 | `ggtt.h` |
| 381 | `parity_gt_ggtt_unbind` | global | `ggtt.c` | I / 旧名で照合 | `ggtt.h` |
| 407 | `parity_gt_ggtt_read_pte` | global | `ggtt.c` | I / 旧名で照合 | `ggtt.h` |
| 416 | `parity_gt_display_window_init` | global | `ggtt.c` | I / 旧名で照合 | `ggtt.h` |
| 437 | `display_bit` | static | `ggtt.c` | S | — |
| 443 | `display_set` | static | `ggtt.c` | S | — |
| 456 | `parity_gt_display_bind` | global | `ggtt.c` | I / 旧名で照合 | `ggtt.h` |
| 547 | `parity_gt_display_bind_foreign` | global | `ggtt.c` | I / 旧名で照合 | `ggtt.h` |
| 601 | `parity_gt_display_unbind_foreign` | global | `ggtt.c` | I / 旧名で照合 | `ggtt.h` |
| 620 | `parity_gt_display_unbind` | global | `ggtt.c` | I / 旧名で照合 | `ggtt.h` |
| 649 | `parity_gt_init_scratch` | global | `memory.c` | I / 旧名で照合 | `memory.h` |
| 677 | `parity_gt_clflush` | global | `memory.c` | I / 旧名で照合 | `memory.h` |
| 694 | `fill_px` | static | `ppgtt.c` | S | — |
| 705 | `parity_gt_ppgtt_create` | global | `ppgtt.c` | I / 旧名で照合 | `ppgtt.h` |
| 787 | `parity_gt_ppgtt_destroy` | global | `ppgtt.c` | I / 旧名で照合 | `ppgtt.h` |
| 820 | `pd_range` | static | `ppgtt.c` | S | — |
| 833 | `pt_count` | static | `ppgtt.c` | S | — |
| 841 | `child_of` | static | `ppgtt.c` | S | — |
| 853 | `alloc_level` | static | `ppgtt.c` | S | — |
| 904 | `parity_gt_ppgtt_alloc_range` | global | `ppgtt.c` | I / 旧名で照合 | `ppgtt.h` |
| 918 | `foreach_level` | static | `ppgtt.c` | S | — |
| 943 | `parity_gt_ppgtt_foreach_pt` | global | `ppgtt.c` | I / 旧名で照合 | `ppgtt.h` |
| 955 | `parity_gt_ppgtt_insert_page` | global | `ppgtt.c` | I / 旧名で照合 | `ppgtt.h` |
| 981 | `parity_gt_ppgtt_insert_scratch` | global | `ppgtt.c` | I / 旧名で照合 | `ppgtt.h` |
| 1006 | `table_by_dma` | static | `ppgtt.c` | S | — |
| 1017 | `parity_gt_ppgtt_walk` | global | `ppgtt.c` | I / 旧名で照合 | `ppgtt.h` |

### parity/gt_migrate.c

現行: [parity/gt_migrate.c](../../src/drivers/gpu/i915/parity/gt_migrate.c)。5定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 14 | `fail` | static | `engine.c` | S | — |
| 25 | `insert_pte` | static | `engine.c` | S | — |
| 42 | `first_copy_engine` | static | `engine.c` | S | — |
| 56 | `parity_intel_migrate_init` | global | `engine.c` | I / 旧名で照合 | `engine.h` |
| 130 | `parity_intel_migrate_fini` | global | `engine.c` | I / 旧名で照合 | `engine.h` |

### parity/gt_mmio.c

現行: [parity/gt_mmio.c](../../src/drivers/gpu/i915/parity/gt_mmio.c)。13定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 69 | `hweight16v` | static | `mmio.c` | S | — |
| 78 | `hweight32v` | static | `mmio.c` | S | — |
| 89 | `gen11_get_crystal_clock_freq` | static | `mmio.c` | S | — |
| 107 | `read_reference_ts_freq` | static | `mmio.c` | S | — |
| 122 | `parity_gen11_read_clock_frequency` | global | `mmio.c` | I / 旧名で照合 | `mmio.h` |
| 155 | `parity_gen11_compute_sseu_info` | global | `mmio.c` | I / 旧名で照合 | `mmio.h` |
| 183 | `parity_gen12_sseu_info_init` | global | `mmio.c` | I / 旧名で照合 | `mmio.h` |
| 234 | `parity_intel_engine_context_size` | global | `mmio.c` | I / 旧名で照合 | `mmio.h` |
| 256 | `parity_engine_mask_apply_media_fuses` | global | `mmio.c` | I / 旧名で照合 | `mmio.h` |
| 329 | `get_reset_domain` | static | `mmio.c` | S | — |
| 342 | `setup_engine_capabilities` | static | `mmio.c` | S | — |
| 358 | `parity_intel_gt_check_and_clear_faults` | global | `mmio.c` | I / 旧名で照合 | `mmio.h` |
| 412 | `parity_intel_gt_init_mmio` | global | `mmio.c` | I / 旧名で照合 | `mmio.h` |

### parity/gt_request.c

現行: [parity/gt_request.c](../../src/drivers/gpu/i915/parity/gt_request.c)。12定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 19 | `parity_ring_begin` | global | `request.c` | I / 旧名で照合 | `request.h` |
| 54 | `parity_ring_advance` | global | `request.c` | I / 旧名で照合 | `request.h` |
| 66 | `parity_gen12_emit_aux_table_inv` | global | `request.c` | I / 旧名で照合 | `request.h` |
| 88 | `preparser_disable` | static | `request.c` | S | — |
| 95 | `emit_pipe_control` | static | `request.c` | S | — |
| 110 | `emit_flush_rcs` | static | `request.c` | S | — |
| 179 | `emit_flush_xcs` | static | `request.c` | S | — |
| 235 | `parity_emit_flush` | global | `request.c` | I / 旧名で照合 | `request.h` |
| 247 | `parity_emit_ctx_wa` | global | `request.c` | I / 旧名で照合 | `request.h` |
| 297 | `parity_request_create` | global | `request.c` | I / 旧名で照合 | `request.h` |
| 326 | `emit_fini_breadcrumb_tail` | static | `request.c` | S | — |
| 355 | `parity_request_add` | global | `request.c` | I / 旧名で照合 | `request.h` |

### parity/gt_resume.c

現行: [parity/gt_resume.c](../../src/drivers/gpu/i915/parity/gt_resume.c)。4定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 16 | `parity_intel_engines_init` | global | `engine.c` | I / 旧名で照合 | `engine.h` |
| 66 | `parity_intel_engines_release` | global | `engine.c` | I / 旧名で照合 | `engine.h` |
| 82 | `execlists_sanitize` | static | `engine.c` | S | — |
| 100 | `parity_intel_gt_resume` | global | `engine.c` | I / 旧名で照合 | `engine.h` |

### parity/gt_submit.c

現行: [parity/gt_submit.c](../../src/drivers/gpu/i915/parity/gt_submit.c)。10定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 21 | `parity_execlists_init` | global | `request.c` | I / 旧名で照合 | `request.h` |
| 34 | `parity_gen12_csb_parse` | global | `request.c` | I / 旧名で照合 | `request.h` |
| 56 | `parity_request_completed` | global | `request.c` | I / 旧名で照合 | `request.h` |
| 67 | `schedule_in` | static | `request.c` | S | — |
| 90 | `schedule_out` | static | `request.c` | S | — |
| 103 | `update_context` | static | `request.c` | S | — |
| 141 | `write_desc` | static | `request.c` | S | — |
| 150 | `parity_execlists_submit` | global | `request.c` | I / 旧名で照合 | `request.h` |
| 190 | `csb_read` | static | `request.c` | S | — |
| 237 | `parity_execlists_process_csb` | global | `request.c` | I / 旧名で照合 | `request.h` |

### parity/gt_tlb.c

現行: [parity/gt_tlb.c](../../src/drivers/gpu/i915/parity/gt_tlb.c)。2定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 27 | `parity_gt_tlb_engine_reg` | global | `ppgtt.c` | I / 旧名で照合 | `ppgtt.h` |
| 51 | `parity_gt_invalidate_tlb_full` | global | `ppgtt.c` | I / 旧名で照合 | `ppgtt.h` |

### parity/gt_verify_wa.c

現行: [parity/gt_verify_wa.c](../../src/drivers/gpu/i915/parity/gt_verify_wa.c)。11定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 25 | `parity_gen12_mcr_range` | global | `workarounds.c` | I / 旧名で照合 | `workarounds.h` |
| 36 | `fail` | static | `workarounds.c` | S | — |
| 46 | `parity_wa_list_srm` | global | `workarounds.c` | I / 旧名で照合 | `workarounds.h` |
| 81 | `parity_wa_list_check` | global | `workarounds.c` | I / 旧名で照合 | `workarounds.h` |
| 123 | `parity_engine_verify_wa_submit` | global | `workarounds.c` | I / 旧名で照合 | `workarounds.h` |
| 183 | `retired` | static | `workarounds.c` | S | — |
| 190 | `parity_engine_verify_wa_poll` | global | `workarounds.c` | I / 旧名で照合 | `workarounds.h` |
| 219 | `parity_engine_verify_wa_park` | global | `workarounds.c` | I / 旧名で照合 | `workarounds.h` |
| 258 | `wait_engine` | static | `workarounds.c` | S | — |
| 280 | `parity_engines_verify_workarounds` | global | `workarounds.c` | I / 旧名で照合 | `workarounds.h` |
| 352 | `parity_engines_verify_wa_release` | global | `workarounds.c` | I / 旧名で照合 | `workarounds.h` |

### parity/gt_wa_adlp.c

現行: [parity/gt_wa_adlp.c](../../src/drivers/gpu/i915/parity/gt_wa_adlp.c)。8定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 90 | `icl_wa_init_mcr` | static | `workarounds.c` | S | — |
| 106 | `wa_14011060649` | static | `workarounds.c` | S | — |
| 124 | `parity_gt_init_workarounds_adlp` | global | `workarounds.c` | I / 旧名で照合 | `workarounds.h` |
| 153 | `parity_gt_init_workarounds` | global | `workarounds.c` | I / 旧名で照合 | `workarounds.h` |
| 162 | `parity_engine_init_workarounds` | global | `workarounds.c` | I / 旧名で照合 | `workarounds.h` |
| 232 | `parity_engine_init_ctx_wa` | global | `workarounds.c` | I / 旧名で照合 | `workarounds.h` |
| 291 | `whitelist_reg_ext` | static | `workarounds.c` | S | — |
| 306 | `parity_engine_init_whitelist` | global | `workarounds.c` | I / 旧名で照合 | `workarounds.h` |

### parity/irq.c

現行: [parity/irq.c](../../src/drivers/gpu/i915/parity/irq.c)。46定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 163 | `wr` | static | `irq.c` | S | — |
| 171 | `rd` | static | `irq.c` | S | — |
| 178 | `gen3_irq_reset` | static | `irq.c` | S | — |
| 194 | `gen3_assert_iir_is_zero` | static | `irq.c` | S | — |
| 211 | `gen3_irq_init` | static | `irq.c` | S | — |
| 222 | `gen11_master_intr_disable` | static | `irq.c` | S | — |
| 233 | `gen11_master_intr_enable` | static | `irq.c` | S | — |
| 239 | `pipe_power_on` | static | `irq.c` | S | — |
| 246 | `transcoder_power_on` | static | `irq.c` | S | — |
| 255 | `parity_gen8_de_pipe_fault_mask` | global | `irq.c` | I / 旧名で照合 | `irq.h` |
| 269 | `parity_gen8_de_port_aux_mask` | global | `irq.c` | I / 旧名で照合 | `irq.h` |
| 300 | `parity_gen8_de_pipe_underrun_mask` | global | `irq.c` | I / 旧名で照合 | `irq.h` |
| 311 | `parity_gen8_de_pipe_flip_done_mask` | global | `irq.c` | I / 旧名で照合 | `irq.h` |
| 322 | `parity_gen11_gt_irq_reset` | global | `irq.c` | I / 旧名で照合 | `irq.h` |
| 349 | `parity_gen11_gt_irq_postinstall` | global | `irq.c` | I / 旧名で照合 | `irq.h` |
| 398 | `parity_gen11_display_irq_reset` | global | `irq.c` | I / 旧名で照合 | `irq.h` |
| 439 | `icp_irq_postinstall` | static | `irq.c` | S | — |
| 447 | `gen8_de_irq_postinstall` | static | `irq.c` | S | — |
| 527 | `parity_gen11_de_irq_postinstall` | global | `irq.c` | I / 旧名で照合 | `irq.h` |
| 545 | `gen11_gt_engine_identity` | static | `irq.c` | S | — |
| 584 | `gt_engine_irq` | static | `irq.c` | S | — |
| 597 | `gen11_gt_identity_handler` | static | `irq.c` | S | — |
| 640 | `gen11_gt_bank_handler` | static | `irq.c` | S | — |
| 661 | `parity_gen11_gt_irq_handler` | global | `irq.c` | I / 旧名で照合 | `irq.h` |
| 680 | `gen8_de_irq_handler` | static | `irq.c` | S | — |
| 793 | `parity_gen11_display_irq_handler` | global | `irq.c` | I / 旧名で照合 | `irq.h` |
| 807 | `parity_intel_irq_reset` | global | `irq.c` | I / 旧名で照合 | `irq.h` |
| 819 | `parity_intel_irq_postinstall` | global | `irq.c` | I / 旧名で照合 | `irq.h` |
| 842 | `gen11_irq_handler_body` | static | `irq.c` | S | — |
| 906 | `gen11_irq_handler` | static | `irq.c` | S | — |
| 918 | `parity_irq_vblank_init` | global | `irq.c` | I / 旧名で照合 | `irq.h` |
| 932 | `parity_intel_synchronize_irq` | global | `irq.c` | I / 旧名で照合 | `irq.h` |
| 958 | `parity_gen8_irq_power_well_post_enable` | global | `irq.c` | I / 旧名で照合 | `irq.h` |
| 988 | `parity_irq_drain_pipes` | global | `irq.c` | I / 旧名で照合 | `irq.h` |
| 1021 | `parity_gen8_irq_power_well_pre_disable` | global | `irq.c` | I / 旧名で照合 | `irq.h` |
| 1052 | `bdw_update_pipe_irq` | static | `irq.c` | S | — |
| 1081 | `bdw_enable_vblank_locked` | static | `irq.c` | S | — |
| 1088 | `bdw_disable_vblank_locked` | static | `irq.c` | S | — |
| 1100 | `parity_drm_vblank_get` | global | `irq.c` | I / 旧名で照合 | `irq.h` |
| 1121 | `parity_drm_vblank_put` | global | `irq.c` | I / 旧名で照合 | `irq.h` |
| 1138 | `vbl_snapshot` | static | `irq.c` | S | — |
| 1152 | `parity_wait_vblank` | global | `irq.c` | I / 旧名で照合 | `irq.h` |
| 1205 | `pw_irq_post_enable` | static | `irq.c` | S | — |
| 1211 | `pw_irq_pre_disable` | static | `irq.c` | S | — |
| 1219 | `parity_intel_irq_install` | global | `irq.c` | I / 旧名で照合 | `irq.h` |
| 1259 | `parity_intel_irq_uninstall` | global | `irq.c` | I / 旧名で照合 | `irq.h` |

### parity/ktest.c

現行: [parity/ktest.c](../../src/drivers/gpu/i915/parity/ktest.c)。56定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 87 | `edp_ktest_check` | static | `tests/execution/ktest.c` | T | — |
| 100 | `pd_async_test_queue` | static | `tests/execution/ktest.c` | T | — |
| 112 | `pd_async_test_cancel` | static | `tests/execution/ktest.c` | T | — |
| 127 | `deadline_ms` | static | `tests/execution/ktest.c` | T | — |
| 136 | `msi_probe_handler` | static | `tests/execution/ktest.c` | T | — |
| 188 | `ktest_vga_get` | static | `tests/execution/ktest.c` | T | — |
| 189 | `ktest_vga_in8` | static | `tests/execution/ktest.c` | T | — |
| 190 | `ktest_vga_out8` | static | `tests/execution/ktest.c` | T | — |
| 191 | `ktest_vga_put` | static | `tests/execution/ktest.c` | T | — |
| 201 | `pt_oneshot` | static | `tests/execution/ktest.c` | T | — |
| 207 | `pt_thread_b` | static | `tests/execution/ktest.c` | T | — |
| 219 | `pt_spin_until` | static | `tests/execution/ktest.c` | T | — |
| 229 | `pt_thread_a` | static | `tests/execution/ktest.c` | T | — |
| 251 | `sleep_corunner` | static | `tests/execution/ktest.c` | T | — |
| 261 | `pcode_time_ok` | static | `tests/execution/ktest.c` | T | — |
| 271 | `pcode_time_fault` | static | `tests/execution/ktest.c` | T | — |
| 293 | `dmc_bad_request` | static | `tests/execution/ktest.c` | T | — |
| 304 | `dmc_fini_thread` | static | `tests/execution/ktest.c` | T | — |
| 317 | `ktest_bridge_next` | static | `tests/execution/ktest.c` | T | — |
| 330 | `ktest_fail_read` | static | `tests/execution/ktest.c` | T | — |
| 343 | `fake_wt_record` | static | `tests/execution/ktest.c` | T | — |
| 350 | `fake_wt_find` | static | `tests/execution/ktest.c` | T | — |
| 361 | `fake_gen_get` | static | `tests/execution/ktest.c` | T | — |
| 372 | `fake_gen_set` | static | `tests/execution/ktest.c` | T | — |
| 382 | `fake_raw_read32` | static | `tests/execution/ktest.c` | T | — |
| 423 | `fake_raw_write32` | static | `tests/execution/ktest.c` | T | — |
| 508 | `fake_fw_request` | static | `tests/execution/ktest.c` | T | — |
| 510 | `fake_fw_ack` | static | `tests/execution/ktest.c` | T | — |
| 518 | `fake_mmio_open` | static | `tests/execution/ktest.c` | T | — |
| 532 | `drm_test_action` | static | `tests/execution/ktest.c` | T | — |
| 539 | `drm_zero_fixture` | static | `tests/execution/ktest.c` | T | — |
| 555 | `oneshot_timer_cb` | static | `tests/execution/ktest.c` | T | — |
| 568 | `completer_worker` | static | `tests/execution/ktest.c` | T | — |
| 575 | `spawn_detached` | static | `tests/execution/ktest.c` | T | — |
| 589 | `spawn_on_cpu` | static | `tests/execution/ktest.c` | T | — |
| 605 | `wq_requeue_fn` | static | `tests/execution/ktest.c` | T | — |
| 622 | `cancel_worker_fn` | static | `tests/execution/ktest.c` | T | — |
| 632 | `canceller_thread` | static | `tests/execution/ktest.c` | T | — |
| 653 | `xcpu_fn` | static | `tests/execution/ktest.c` | T | — |
| 665 | `bios_fpci_r8` | static | `tests/execution/ktest.c` | T | — |
| 668 | `bios_fpci_r16` | static | `tests/execution/ktest.c` | T | — |
| 673 | `fpci_subsys` | static | `tests/execution/ktest.c` | T | — |
| 679 | `bios_fpci_r32` | static | `tests/execution/ktest.c` | T | — |
| 680 | `bios_fpci_w8` | static | `tests/execution/ktest.c` | T | — |
| 681 | `bios_fpci_w16` | static | `tests/execution/ktest.c` | T | — |
| 682 | `bios_fpci_w32` | static | `tests/execution/ktest.c` | T | — |
| 683 | `bios_fpci_alloc_msi` | static | `tests/execution/ktest.c` | T | — |
| 684 | `bios_fpci_free_msi` | static | `tests/execution/ktest.c` | T | — |
| 696 | `bios_make_vbt` | static | `tests/execution/ktest.c` | T | — |
| 722 | `bios_zero` | static | `tests/execution/ktest.c` | T | — |
| 732 | `rpm_test_resume` | static | `tests/execution/ktest.c` | T | — |
| 733 | `rpm_test_suspend` | static | `tests/execution/ktest.c` | T | — |
| 748 | `time_test_read` | static | `tests/execution/ktest.c` | T | — |
| 762 | `pd_async_checks` | static | `tests/execution/ktest.c` | T | — |
| 839 | `kvb_read_frame` | static | `tests/execution/ktest.c` | T | — |
| 854 | `parity_sync_ktest` | global | `tests/execution/ktest.c` | T | `tests/execution/ktest.h` |

### parity/lcd/drm_connector_status_port.c

現行: [parity/lcd/drm_connector_status_port.c](../../src/drivers/gpu/i915/parity/lcd/drm_connector_status_port.c)。1定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 42 | `drm_get_connector_status_name` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |

### parity/lcd/drm_dp_bw_port.c

現行: [parity/lcd/drm_dp_bw_port.c](../../src/drivers/gpu/i915/parity/lcd/drm_dp_bw_port.c)。2定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 44 | `drm_dp_is_uhbr_rate` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 64 | `drm_dp_bw_channel_coding_efficiency` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |

### parity/lcd/drm_dp_link_port.c

現行: [parity/lcd/drm_dp_link_port.c](../../src/drivers/gpu/i915/parity/lcd/drm_dp_link_port.c)。24定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 56 | `dp_lttpr_common_cap` | static | `display/dp.c` | S | — |
| 61 | `dp_lttpr_phy_cap` | static | `display/dp.c` | S | — |
| 66 | `drm_dp_read_lttpr_regs` | static | `display/dp.c` | S | — |
| 93 | `dp_link_status` | static | `display/dp.c` | S | — |
| 98 | `dp_get_lane_status` | static | `display/dp.c` | S | — |
| 108 | `drm_dp_channel_eq_ok` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 127 | `drm_dp_clock_recovery_ok` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 141 | `drm_dp_get_adjust_request_voltage` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 153 | `drm_dp_get_adjust_request_pre_emphasis` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 166 | `drm_dp_get_adjust_tx_ffe_preset` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 178 | `__8b10b_clock_recovery_delay_us` | static | `display/dp.c` | S | — |
| 190 | `__8b10b_channel_eq_delay_us` | static | `display/dp.c` | S | — |
| 202 | `__128b132b_channel_eq_delay_us` | static | `display/dp.c` | S | — |
| 236 | `__read_delay` | static | `display/dp.c` | S | — |
| 291 | `drm_dp_read_clock_recovery_delay` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 297 | `drm_dp_read_channel_eq_delay` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 316 | `drm_dp_dpcd_read_phy_link_status` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 366 | `drm_dp_lttpr_count` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 390 | `drm_dp_lttpr_voltage_swing_level_3_supported` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 405 | `drm_dp_lttpr_pre_emphasis_level_3_supported` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 422 | `drm_dp_read_lttpr_common_caps` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 442 | `drm_dp_read_lttpr_phy_caps` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 462 | `drm_dp_phy_name` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 483 | `drm_dp_link_rate_to_bw_code` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |

### parity/lcd/drm_edid_mode_port.c

現行: [parity/lcd/drm_edid_mode_port.c](../../src/drivers/gpu/i915/parity/lcd/drm_edid_mode_port.c)。2定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 66 | `drm_mode_do_interlace_quirk` | static | `display/edid.c` | S | — |
| 104 | `drm_mode_detailed` | static | `display/edid.c` | S | — |

### parity/lcd/drm_modes_hv_port.c

現行: [parity/lcd/drm_modes_hv_port.c](../../src/drivers/gpu/i915/parity/lcd/drm_modes_hv_port.c)。3定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 51 | `drm_mode_copy` | global | `display/edid.c` | I / 旧名で照合 | `display/edid.h` |
| 69 | `drm_mode_init` | global | `display/edid.c` | I / 旧名で照合 | `display/edid.h` |
| 84 | `drm_mode_get_hv_timing` | global | `display/edid.c` | I / 旧名で照合 | `display/edid.h` |

### parity/lcd/drm_modes_port.c

現行: [parity/lcd/drm_modes_port.c](../../src/drivers/gpu/i915/parity/lcd/drm_modes_port.c)。1定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 58 | `drm_mode_set_crtcinfo` | global | `display/edid.c` | I / 旧名で照合 | `display/edid.h` |

### parity/lcd/drm_probe_detect_port.c

現行: [parity/lcd/drm_probe_detect_port.c](../../src/drivers/gpu/i915/parity/lcd/drm_probe_detect_port.c)。2定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 47 | `drm_helper_probe_detect_ctx` | static | `display/hotplug.c` | S | — |
| 94 | `drm_helper_probe_detect` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |

### parity/lcd/hpd_compat.h

現行: [parity/lcd/hpd_compat.h](../../src/drivers/gpu/i915/parity/lcd/hpd_compat.h)。8定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 141 | `parity_hpd_queue_work` | static | `display/hotplug-internal.h` | S | `display/hotplug-internal.h` |
| 146 | `parity_hpd_queue_delayed_work` | static | `display/hotplug-internal.h` | S | `display/hotplug-internal.h` |
| 152 | `parity_hpd_mod_delayed_work` | static | `display/hotplug-internal.h` | S | `display/hotplug-internal.h` |
| 159 | `parity_hpd_cancel_work_sync` | static | `display/hotplug-internal.h` | S | `display/hotplug-internal.h` |
| 163 | `parity_hpd_cancel_delayed_work_sync` | static | `display/hotplug-internal.h` | S | `display/hotplug-internal.h` |
| 209 | `bxt_gmbus_clock_gating` | static | `display/hotplug-internal.h` | S | `display/hotplug-internal.h` |
| 210 | `pch_gmbus_clock_gating` | static | `display/hotplug-internal.h` | S | `display/hotplug-internal.h` |
| 288 | `intel_encoder_is_dig_port` | static | `display/hotplug-internal.h` | S | `display/hotplug-internal.h` |

### parity/lcd/hpd_ktest.c

現行: [parity/lcd/hpd_ktest.c](../../src/drivers/gpu/i915/parity/lcd/hpd_ktest.c)。5定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 34 | `make_edid` | static | `tests/display/hpd-ktest.c` | T | — |
| 54 | `ksleep_ticks` | static | `tests/display/hpd-ktest.c` | T | — |
| 60 | `wait_records` | static | `tests/display/hpd-ktest.c` | T | — |
| 76 | `set_encoder` | static | `tests/display/hpd-ktest.c` | T | — |
| 86 | `parity_hpd_ktest` | global | `tests/display/hpd-ktest.c` | T | `tests/display/hpd-ktest.h` |

### parity/lcd/intel_acpi_port.c

現行: [parity/lcd/intel_acpi_port.c](../../src/drivers/gpu/i915/parity/lcd/intel_acpi_port.c)。2定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 41 | `acpi_display_type` | static | `display/panel.c` | S | — |
| 82 | `intel_acpi_device_id_update` | global | `display/panel.c` | I / 旧名で照合 | `display/panel.h` |

### parity/lcd/intel_atomic_plane_port.c

現行: [parity/lcd/intel_atomic_plane_port.c](../../src/drivers/gpu/i915/parity/lcd/intel_atomic_plane_port.c)。5定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 49 | `intel_adjusted_rate` | global | `display/plane.c` | I / 旧名で照合 | `display/plane.h` |
| 68 | `intel_plane_pixel_rate` | global | `display/plane.c` | I / 旧名で照合 | `display/plane.h` |
| 89 | `use_min_ddb` | static | `display/plane.c` | S | — |
| 100 | `intel_plane_relative_data_rate` | static | `display/plane.c` | S | — |
| 144 | `intel_plane_data_rate` | global | `display/plane.c` | I / 旧名で照合 | `display/plane.h` |

### parity/lcd/intel_backlight_port.c

現行: [parity/lcd/intel_backlight_port.c](../../src/drivers/gpu/i915/parity/lcd/intel_backlight_port.c)。30定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 74 | `scale` | static | `display/panel.c` | S | — |
| 100 | `clamp_user_to_hw` | static | `display/panel.c` | S | — |
| 113 | `scale_hw_to_user` | static | `display/panel.c` | S | — |
| 122 | `intel_backlight_invert_pwm_level` | global | `display/panel.c` | I / 旧名で照合 | `display/panel.h` |
| 140 | `intel_backlight_set_pwm_level` | global | `display/panel.c` | I / 旧名で照合 | `display/panel.h` |
| 151 | `intel_backlight_level_to_pwm` | global | `display/panel.c` | I / 旧名で照合 | `display/panel.h` |
| 165 | `intel_backlight_level_from_pwm` | global | `display/panel.c` | I / 旧名で照合 | `display/panel.h` |
| 182 | `bxt_get_backlight` | static | `display/panel.c` | S | — |
| 190 | `bxt_set_backlight` | static | `display/panel.c` | S | — |
| 199 | `cnp_disable_backlight` | static | `display/panel.c` | S | — |
| 211 | `cnp_enable_backlight` | static | `display/panel.c` | S | — |
| 242 | `cnp_num_backlight_controllers` | static | `display/panel.c` | S | — |
| 256 | `cnp_backlight_controller_is_valid` | static | `display/panel.c` | S | — |
| 273 | `cnp_hz_to_pwm` | static | `display/panel.c` | S | — |
| 281 | `get_vbt_pwm_freq` | static | `display/panel.c` | S | — |
| 300 | `get_backlight_max_vbt` | static | `display/panel.c` | S | — |
| 326 | `get_backlight_min_vbt` | static | `display/panel.c` | S | — |
| 353 | `cnp_setup_backlight` | static | `display/panel.c` | S | — |
| 396 | `intel_pwm_get_backlight` | static | `display/panel.c` | S | — |
| 404 | `intel_pwm_set_backlight` | static | `display/panel.c` | S | — |
| 413 | `intel_pwm_enable_backlight` | static | `display/panel.c` | S | — |
| 423 | `intel_pwm_disable_backlight` | static | `display/panel.c` | S | — |
| 432 | `intel_pwm_setup_backlight` | static | `display/panel.c` | S | — |
| 449 | `__intel_backlight_enable` | static | `display/panel.c` | S | — |
| 472 | `intel_backlight_enable` | global | `display/panel.c` | I / 旧名で照合 | `display/panel.h` |
| 492 | `intel_backlight_disable` | global | `display/panel.c` | I / 旧名で照合 | `display/panel.h` |
| 524 | `intel_panel_actually_set_backlight` | static | `display/panel.c` | S | — |
| 537 | `scale_user_to_hw` | static | `display/panel.c` | S | — |
| 547 | `intel_panel_set_backlight` | static | `display/panel.c` | S | — |
| 574 | `intel_backlight_set_acpi` | global | `display/panel.c` | I / 旧名で照合 | `display/panel.h` |

### parity/lcd/intel_bw_port.c

現行: [parity/lcd/intel_bw_port.c](../../src/drivers/gpu/i915/parity/lcd/intel_bw_port.c)。4定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 26 | `intel_bw_crtc_data_rate` | static | `display/watermark.c` | S | — |
| 51 | `intel_bw_crtc_min_cdclk` | static | `display/watermark.c` | S | — |
| 62 | `intel_bw_crtc_num_active_planes` | static | `display/watermark.c` | S | — |
| 71 | `intel_bw_crtc_update` | global | `display/watermark.c` | I / 旧名で照合 | `display/watermark.h` |

### parity/lcd/intel_cdclk_port.c

現行: [parity/lcd/intel_cdclk_port.c](../../src/drivers/gpu/i915/parity/lcd/intel_cdclk_port.c)。7定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 102 | `bxt_calc_cdclk` | static | `display/clock.c` | S | — |
| 118 | `bxt_calc_cdclk_pll_vco` | static | `display/clock.c` | S | — |
| 136 | `calc_voltage_level` | static | `display/clock.c` | S | — |
| 150 | `tgl_calc_voltage_level` | static | `display/clock.c` | S | — |
| 164 | `intel_pixel_rate_to_cdclk` | static | `display/clock.c` | S | — |
| 182 | `intel_planes_min_cdclk` | static | `display/clock.c` | S | — |
| 195 | `intel_crtc_compute_min_cdclk` | global | `display/clock.c` | I / 旧名で照合 | `display/clock.h` |

### parity/lcd/intel_color_port.c

現行: [parity/lcd/intel_color_port.c](../../src/drivers/gpu/i915/parity/lcd/intel_color_port.c)。10定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 50 | `lut_is_legacy` | static | `display/color.c` | S | — |
| 55 | `icl_gamma_mode` | static | `display/color.c` | S | — |
| 84 | `icl_csc_mode` | static | `display/color.c` | S | — |
| 98 | `icl_load_csc_matrix` | static | `display/color.c` | S | — |
| 109 | `icl_load_luts` | static | `display/color.c` | S | — |
| 138 | `icl_color_commit_noarm` | static | `display/color.c` | S | — |
| 151 | `icl_color_commit_arm` | static | `display/color.c` | S | — |
| 170 | `intel_color_load_luts` | global | `display/color.c` | I / 旧名で照合 | `display/color.h` |
| 180 | `intel_color_commit_noarm` | global | `display/color.c` | I / 旧名で照合 | `display/color.h` |
| 188 | `intel_color_commit_arm` | global | `display/color.c` | I / 旧名で照合 | `display/color.h` |

### parity/lcd/intel_combo_phy_port.c

現行: [parity/lcd/intel_combo_phy_port.c](../../src/drivers/gpu/i915/parity/lcd/intel_combo_phy_port.c)。1定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 17 | `intel_combo_phy_power_up_lanes` | global | `display/phy.c` | I / 旧名で照合 | `display/phy.h` |

### parity/lcd/intel_crtc_port.c

現行: [parity/lcd/intel_crtc_port.c](../../src/drivers/gpu/i915/parity/lcd/intel_crtc_port.c)。9定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 29 | `intel_crtc_state_reset` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |
| 44 | `intel_usecs_to_scanlines` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |
| 55 | `intel_crtc_get_vblank_counter` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |
| 69 | `intel_crtc_needs_vblank_work` | static | `display/pipe.c` | S | — |
| 78 | `intel_mode_vblank_start` | static | `display/pipe.c` | S | — |
| 88 | `intel_crtc_vblank_evade_scanlines` | static | `display/pipe.c` | S | — |
| 155 | `intel_pipe_update_start` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |
| 271 | `intel_pipe_update_end` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |
| 354 | `intel_crtc_wait_for_next_vblank` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |

### parity/lcd/intel_ddi_buf_trans_port.c

現行: [parity/lcd/intel_ddi_buf_trans_port.c](../../src/drivers/gpu/i915/parity/lcd/intel_ddi_buf_trans_port.c)。10定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 256 | `is_hobl_buf_trans` | global | `display/phy.c` | I / 旧名で照合 | `display/phy.h` |
| 261 | `use_edp_hobl` | static | `display/phy.c` | S | — |
| 269 | `use_edp_low_vswing` | static | `display/phy.c` | S | — |
| 278 | `intel_get_buf_trans` | static | `display/phy.c` | S | — |
| 285 | `tgl_get_combo_buf_trans_dp` | static | `display/phy.c` | S | — |
| 306 | `tgl_get_combo_buf_trans_edp` | static | `display/phy.c` | S | — |
| 325 | `tgl_get_combo_buf_trans` | static | `display/phy.c` | S | — |
| 338 | `adlp_get_combo_buf_trans_dp` | static | `display/phy.c` | S | — |
| 349 | `adlp_get_combo_buf_trans_edp` | static | `display/phy.c` | S | — |
| 368 | `adlp_get_combo_buf_trans` | static | `display/phy.c` | S | — |

### parity/lcd/intel_ddi_hotplug_port.c

現行: [parity/lcd/intel_ddi_hotplug_port.c](../../src/drivers/gpu/i915/parity/lcd/intel_ddi_hotplug_port.c)。2定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 46 | `intel_ddi_hotplug` | static | `display/hotplug.c` | S | — |
| 108 | `lpt_digital_port_connected` | static | `display/hotplug.c` | S | — |

### parity/lcd/intel_ddi_port.c

現行: [parity/lcd/intel_ddi_port.c](../../src/drivers/gpu/i915/parity/lcd/intel_ddi_port.c)。77定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 200 | `ddi_buf_phy_link_rate` | static | `display/ddi.c` | S | — |
| 225 | `intel_ddi_init_dp_buf_reg` | static | `display/ddi.c` | S | — |
| 252 | `intel_ddi_set_dp_msa` | global | `display/ddi.c` | I / 旧名で照合 | `display/ddi.h` |
| 312 | `bdw_trans_port_sync_master_select` | static | `display/ddi.c` | S | — |
| 327 | `intel_ddi_transcoder_func_reg_val_get` | static | `display/ddi.c` | S | — |
| 438 | `intel_ddi_enable_transcoder_func` | global | `display/ddi.c` | I / 旧名で照合 | `display/ddi.h` |
| 471 | `intel_ddi_config_transcoder_func` | static | `display/ddi.c` | S | — |
| 485 | `hsw_chicken_trans_reg` | global | `display/ddi.c` | I / 旧名で照合 | `display/ddi.h` |
| 493 | `tgl_ddi_pre_enable_dp` | static | `display/ddi.c` | S | — |
| 635 | `intel_ddi_pre_enable_dp` | static | `display/ddi.c` | S | — |
| 665 | `intel_ddi_pre_enable` | static | `display/ddi.c` | S | — |
| 709 | `intel_enable_ddi_dp` | static | `display/ddi.c` | S | — |
| 731 | `intel_enable_ddi` | static | `display/ddi.c` | S | — |
| 760 | `intel_ddi_pre_pll_enable` | static | `display/ddi.c` | S | — |
| 804 | `intel_ddi_dp_voltage_max` | static | `display/ddi.c` | S | — |
| 828 | `intel_ddi_dp_preemph_max` | static | `display/ddi.c` | S | — |
| 833 | `intel_ddi_enable_clock` | global | `display/ddi.c` | I / 旧名で照合 | `display/ddi.h` |
| 840 | `intel_ddi_disable_clock` | global | `display/ddi.c` | I / 旧名で照合 | `display/ddi.h` |
| 846 | `_icl_ddi_enable_clock` | static | `display/ddi.c` | S | — |
| 862 | `_icl_ddi_disable_clock` | static | `display/ddi.c` | S | — |
| 872 | `icl_ddi_combo_enable_clock` | static | `display/ddi.c` | S | — |
| 888 | `icl_ddi_combo_disable_clock` | static | `display/ddi.c` | S | — |
| 898 | `intel_ddi_main_link_aux_domain` | static | `display/ddi.c` | S | — |
| 928 | `main_link_aux_power_domain_get` | static | `display/ddi.c` | S | — |
| 944 | `main_link_aux_power_domain_put` | static | `display/ddi.c` | S | — |
| 959 | `intel_ddi_enable_transcoder_clock` | global | `display/ddi.c` | I / 旧名で照合 | `display/ddi.h` |
| 981 | `intel_ddi_disable_transcoder_clock` | global | `display/ddi.c` | I / 旧名で照合 | `display/ddi.h` |
| 998 | `intel_ddi_dp_level` | static | `display/ddi.c` | S | — |
| 1014 | `intel_ddi_level` | global | `display/ddi.c` | I / 旧名で照合 | `display/ddi.h` |
| 1038 | `icl_combo_phy_loadgen_select` | static | `display/ddi.c` | S | — |
| 1050 | `icl_ddi_combo_vswing_program` | static | `display/ddi.c` | S | — |
| 1114 | `icl_combo_phy_set_signal_levels` | static | `display/ddi.c` | S | — |
| 1165 | `translate_signal_level` | static | `display/ddi.c` | S | — |
| 1183 | `intel_ddi_power_up_lanes` | static | `display/ddi.c` | S | — |
| 1200 | `intel_ddi_mso_configure` | static | `display/ddi.c` | S | — |
| 1225 | `tgl_dp_tp_transcoder` | static | `display/ddi.c` | S | — |
| 1233 | `dp_tp_ctl_reg` | global | `display/ddi.c` | I / 旧名で照合 | `display/ddi.h` |
| 1244 | `dp_tp_status_reg` | global | `display/ddi.c` | I / 旧名で照合 | `display/ddi.h` |
| 1255 | `intel_wait_ddi_buf_idle` | global | `display/ddi.c` | I / 旧名で照合 | `display/ddi.h` |
| 1269 | `intel_wait_ddi_buf_active` | static | `display/ddi.c` | S | — |
| 1307 | `intel_ddi_prepare_link_retrain` | static | `display/ddi.c` | S | — |
| 1357 | `intel_ddi_set_link_train` | static | `display/ddi.c` | S | — |
| 1389 | `intel_ddi_set_idle_link_train` | static | `display/ddi.c` | S | — |
| 1416 | `intel_ddi_disable_fec` | static | `display/ddi.c` | S | — |
| 1429 | `disable_ddi_buf` | static | `display/ddi.c` | S | — |
| 1454 | `intel_disable_ddi_buf` | static | `display/ddi.c` | S | — |
| 1471 | `intel_ddi_disable_transcoder_func` | global | `display/ddi.c` | I / 旧名で照合 | `display/ddi.h` |
| 1512 | `intel_dp_sink_set_msa_timing_par_ignore_state` | static | `display/ddi.c` | S | — |
| 1528 | `intel_disable_ddi_dp` | static | `display/ddi.c` | S | — |
| 1549 | `intel_disable_ddi` | static | `display/ddi.c` | S | — |
| 1566 | `intel_ddi_post_disable_dp` | static | `display/ddi.c` | S | — |
| 1631 | `intel_ddi_post_disable` | static | `display/ddi.c` | S | — |
| 1686 | `intel_ddi_post_pll_disable` | static | `display/ddi.c` | S | — |
| 1702 | `icl_ddi_min_voltage_level` | static | `display/ddi.c` | S | — |
| 1710 | `jsl_ddi_min_voltage_level` | static | `display/ddi.c` | S | — |
| 1718 | `tgl_ddi_min_voltage_level` | static | `display/ddi.c` | S | — |
| 1726 | `intel_ddi_compute_min_voltage_level` | global | `display/ddi.c` | I / 旧名で照合 | `display/ddi.h` |
| 1740 | `intel_ddi_hdmi_level` | static | `display/ddi.c` | S | — |
| 1752 | `intel_ddi_pre_enable_hdmi` | static | `display/ddi.c` | S | — |
| 1777 | `intel_enable_ddi_hdmi` | static | `display/ddi.c` | S | — |
| 1880 | `intel_disable_ddi_hdmi` | static | `display/ddi.c` | S | — |
| 1895 | `intel_ddi_post_disable_hdmi` | static | `display/ddi.c` | S | — |
| 1927 | `intel_ddi_get_encoder_pipes` | static | `display/ddi.c` | S | — |
| 2043 | `intel_ddi_get_hw_state` | global | `display/ddi.c` | I / 旧名で照合 | `display/ddi.h` |
| 2059 | `intel_ddi_read_func_ctl` | static | `display/ddi.c` | S | — |
| 2184 | `ddi_dotclock_get` | static | `display/ddi.c` | S | — |
| 2194 | `intel_ddi_get_config` | static | `display/ddi.c` | S | — |
| 2248 | `intel_ddi_get_clock` | global | `display/ddi.c` | I / 旧名で照合 | `display/ddi.h` |
| 2271 | `_icl_ddi_get_pll` | static | `display/ddi.c` | S | — |
| 2281 | `icl_ddi_combo_get_pll` | global | `display/ddi.c` | I / 旧名で照合 | `display/ddi.h` |
| 2291 | `icl_ddi_combo_get_config` | static | `display/ddi.c` | S | — |
| 2298 | `intel_ddi_sync_state` | static | `display/ddi.c` | S | — |
| 2312 | `intel_ddi_get_power_domains` | static | `display/ddi.c` | S | — |
| 2338 | `_icl_ddi_is_clock_enabled` | static | `display/ddi.c` | S | — |
| 2344 | `icl_ddi_combo_is_clock_enabled` | static | `display/ddi.c` | S | — |
| 2353 | `intel_ddi_connector_get_hw_state` | global | `display/ddi.c` | I / 旧名で照合 | `display/ddi.h` |
| 2420 | `intel_ddi_sanitize_encoder_pll_mapping` | global | `display/ddi.c` | I / 旧名で照合 | `display/ddi.h` |

### parity/lcd/intel_display_port.c

現行: [parity/lcd/intel_display_port.c](../../src/drivers/gpu/i915/parity/lcd/intel_display_port.c)。56定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 100 | `intel_phy_is_tc` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |
| 121 | `intel_port_to_phy` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |
| 139 | `intel_reduce_m_n_ratio` | static | `display/pipe.c` | S | — |
| 148 | `compute_m_n` | static | `display/pipe.c` | S | — |
| 161 | `intel_link_compute_m_n` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |
| 188 | `intel_set_m_n` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |
| 203 | `intel_cpu_transcoder_has_m2_n2` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |
| 212 | `intel_cpu_transcoder_set_m1_n1` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |
| 229 | `intel_cpu_transcoder_set_m2_n2` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |
| 243 | `intel_set_transcoder_timings` | static | `display/pipe.c` | S | — |
| 324 | `intel_set_pipe_src_size` | static | `display/pipe.c` | S | — |
| 339 | `hsw_set_frame_start_delay` | static | `display/pipe.c` | S | — |
| 349 | `hsw_set_transconf` | static | `display/pipe.c` | S | — |
| 379 | `hsw_configure_cpu_transcoder` | static | `display/pipe.c` | S | — |
| 408 | `hsw_crtc_enable` | static | `display/pipe.c` | S | — |
| 501 | `intel_dotclock_calculate` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |
| 524 | `ilk_pipe_pixel_rate` | static | `display/pipe.c` | S | — |
| 545 | `intel_get_m_n` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |
| 557 | `has_dsi_transcoders` | static | `display/pipe.c` | S | — |
| 563 | `has_pipe_transcoders` | static | `display/pipe.c` | S | — |
| 570 | `has_edp_transcoders` | static | `display/pipe.c` | S | — |
| 575 | `is_hdr_mode` | static | `display/pipe.c` | S | — |
| 582 | `intel_phy_is_combo` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |
| 604 | `intel_aux_power_domain` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |
| 615 | `intel_wait_for_pipe_off` | static | `display/pipe.c` | S | — |
| 632 | `intel_enable_transcoder` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |
| 692 | `intel_disable_transcoder` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |
| 733 | `bdw_set_pipe_misc` | static | `display/pipe.c` | S | — |
| 783 | `icl_set_pipe_chicken` | static | `display/pipe.c` | S | — |
| 822 | `hsw_set_linetime_wm` | static | `display/pipe.c` | S | — |
| 832 | `hsw_crtc_disable` | static | `display/pipe.c` | S | — |
| 863 | `get_crtc_power_domains` | static | `display/pipe.c` | S | — |
| 900 | `intel_modeset_get_crtc_power_domains` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |
| 925 | `intel_modeset_put_crtc_power_domains` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |
| 933 | `hsw_panel_transcoders` | static | `display/pipe.c` | S | — |
| 943 | `hsw_enabled_transcoders` | static | `display/pipe.c` | S | — |
| 1012 | `hsw_get_transcoder_state` | static | `display/pipe.c` | S | — |
| 1050 | `intel_get_transcoder_timings` | static | `display/pipe.c` | S | — |
| 1099 | `intel_get_pipe_src_size` | static | `display/pipe.c` | S | — |
| 1116 | `bdw_get_pipe_misc_output_format` | static | `display/pipe.c` | S | — |
| 1136 | `hsw_get_pipe_config` | static | `display/pipe.c` | S | — |
| 1229 | `intel_crtc_get_pipe_config` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |
| 1244 | `intel_crtc_readout_derived_state` | static | `display/pipe.c` | S | — |
| 1280 | `intel_encoder_get_config` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |
| 1288 | `intel_set_plane_visible` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |
| 1302 | `intel_plane_fixup_bitmasks` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |
| 1322 | `intel_plane_disable_noatomic` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |
| 1374 | `transcoder_ddi_func_is_enabled` | static | `display/pipe.c` | S | — |
| 1389 | `intel_crtc_dotclock` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |
| 1412 | `assert_enabled_transcoders` | static | `display/pipe.c` | S | — |
| 1427 | `intel_pipe_is_interlaced` | static | `display/pipe.c` | S | — |
| 1442 | `intel_cpu_transcoder_get_m1_n1` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |
| 1459 | `intel_cpu_transcoder_get_m2_n2` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |
| 1473 | `intel_mode_from_crtc_timings` | static | `display/pipe.c` | S | — |
| 1494 | `intel_splitter_adjust_timings` | static | `display/pipe.c` | S | — |
| 1518 | `intel_crtc_compute_pixel_rate` | static | `display/pipe.c` | S | — |

### parity/lcd/intel_display_power_set_port.c

現行: [parity/lcd/intel_display_power_set_port.c](../../src/drivers/gpu/i915/parity/lcd/intel_display_power_set_port.c)。2定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 19 | `intel_display_power_get_in_set` | global | `display/power.c` | I / 旧名で照合 | `display/power.h` |
| 35 | `intel_display_power_put_mask_in_set` | global | `display/power.c` | I / 旧名で照合 | `display/power.h` |

### parity/lcd/intel_dmc_port.c

現行: [parity/lcd/intel_dmc_port.c](../../src/drivers/gpu/i915/parity/lcd/intel_dmc_port.c)。3定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 51 | `is_valid_dmc_id` | static | `display/dmc.c` | S | — |
| 56 | `intel_dmc_enable_pipe` | global | `display/dmc.c` | I / 旧名で照合 | `display/dmc.h` |
| 69 | `intel_dmc_disable_pipe` | global | `display/dmc.c` | I / 旧名で照合 | `display/dmc.h` |

### parity/lcd/intel_dp_connected_port.c

現行: [parity/lcd/intel_dp_connected_port.c](../../src/drivers/gpu/i915/parity/lcd/intel_dp_connected_port.c)。1定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 49 | `intel_digital_port_connected` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |

### parity/lcd/intel_dp_link_training_port.c

現行: [parity/lcd/intel_dp_link_training_port.c](../../src/drivers/gpu/i915/parity/lcd/intel_dp_link_training_port.c)。45定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 151 | `intel_dp_reset_lttpr_common_caps` | static | `display/dp.c` | S | — |
| 156 | `intel_dp_reset_lttpr_count` | static | `display/dp.c` | S | — |
| 162 | `intel_dp_lttpr_phy_caps` | static | `display/dp.c` | S | — |
| 168 | `intel_dp_read_lttpr_phy_caps` | static | `display/dp.c` | S | — |
| 184 | `intel_dp_read_lttpr_common_caps` | static | `display/dp.c` | S | — |
| 210 | `intel_dp_set_lttpr_transparent_mode` | static | `display/dp.c` | S | — |
| 218 | `intel_dp_lttpr_transparent_mode_enabled` | static | `display/dp.c` | S | — |
| 233 | `intel_dp_init_lttpr_phys` | static | `display/dp.c` | S | — |
| 294 | `intel_dp_init_lttpr` | static | `display/dp.c` | S | — |
| 325 | `intel_dp_init_lttpr_and_dprx_caps` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 359 | `dp_voltage_max` | static | `display/dp.c` | S | — |
| 374 | `intel_dp_lttpr_voltage_max` | static | `display/dp.c` | S | — |
| 385 | `intel_dp_lttpr_preemph_max` | static | `display/dp.c` | S | — |
| 397 | `intel_dp_phy_is_downstream_of_source` | static | `display/dp.c` | S | — |
| 408 | `intel_dp_phy_voltage_max` | static | `display/dp.c` | S | — |
| 431 | `intel_dp_phy_preemph_max` | static | `display/dp.c` | S | — |
| 453 | `has_per_lane_signal_levels` | static | `display/dp.c` | S | — |
| 463 | `intel_dp_get_lane_adjust_tx_ffe_preset` | static | `display/dp.c` | S | — |
| 483 | `intel_dp_get_lane_adjust_vswing_preemph` | static | `display/dp.c` | S | — |
| 519 | `intel_dp_get_lane_adjust_train` | static | `display/dp.c` | S | — |
| 534 | `intel_dp_get_adjust_train` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 563 | `intel_dp_training_pattern_set_reg` | static | `display/dp.c` | S | — |
| 572 | `intel_dp_set_link_train` | static | `display/dp.c` | S | — |
| 592 | `dp_training_pattern_name` | static | `display/dp.c` | S | — |
| 608 | `intel_dp_program_link_training_pattern` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 622 | `intel_dp_set_signal_levels` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 649 | `intel_dp_reset_link_train` | static | `display/dp.c` | S | — |
| 660 | `intel_dp_update_link_train` | static | `display/dp.c` | S | — |
| 678 | `intel_dp_lane_max_tx_ffe_reached` | static | `display/dp.c` | S | — |
| 694 | `intel_dp_lane_max_vswing_reached` | static | `display/dp.c` | S | — |
| 710 | `intel_dp_link_max_vswing_reached` | static | `display/dp.c` | S | — |
| 731 | `intel_dp_update_downspread_ctrl` | static | `display/dp.c` | S | — |
| 743 | `intel_dp_update_link_bw_set` | static | `display/dp.c` | S | — |
| 778 | `intel_dp_prepare_link_train` | static | `display/dp.c` | S | — |
| 827 | `intel_dp_adjust_request_changed` | static | `display/dp.c` | S | — |
| 854 | `intel_dp_dump_link_status` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 868 | `intel_dp_link_training_clock_recovery` | static | `display/dp.c` | S | — |
| 961 | `intel_dp_training_pattern` | static | `display/dp.c` | S | — |
| 1019 | `intel_dp_link_training_channel_equalization` | static | `display/dp.c` | S | — |
| 1088 | `intel_dp_disable_dpcd_training_pattern` | static | `display/dp.c` | S | — |
| 1113 | `intel_dp_stop_link_train` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 1129 | `intel_dp_link_train_phy` | static | `display/dp.c` | S | — |
| 1152 | `intel_dp_schedule_fallback_link_training` | static | `display/dp.c` | S | — |
| 1179 | `intel_dp_link_train_all_phys` | static | `display/dp.c` | S | — |
| 1215 | `intel_dp_start_link_train` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |

### parity/lcd/intel_dpll_port.c

現行: [parity/lcd/intel_dpll_port.c](../../src/drivers/gpu/i915/parity/lcd/intel_dpll_port.c)。35定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 127 | `ehl_combo_pll_div_frac_wa_needed` | static | `display/clock.c` | S | — |
| 200 | `icl_calc_dp_combo_pll` | static | `display/clock.c` | S | — |
| 222 | `icl_calc_dpll_state` | static | `display/clock.c` | S | — |
| 249 | `intel_combo_pll_enable_reg` | static | `display/clock.c` | S | — |
| 261 | `icl_pll_power_enable` | static | `display/clock.c` | S | — |
| 276 | `icl_dpll_write` | static | `display/clock.c` | S | — |
| 318 | `icl_pll_enable` | static | `display/clock.c` | S | — |
| 329 | `adlp_cmtg_clock_gating_wa` | static | `display/clock.c` | S | — |
| 353 | `combo_pll_enable` | static | `display/clock.c` | S | — |
| 375 | `icl_pll_disable` | static | `display/clock.c` | S | — |
| 406 | `combo_pll_disable` | static | `display/clock.c` | S | — |
| 414 | `_intel_enable_shared_dpll` | static | `display/clock.c` | S | — |
| 430 | `intel_enable_shared_dpll` | global | `display/clock.c` | I / 旧名で照合 | `display/clock.h` |
| 470 | `_intel_disable_shared_dpll` | static | `display/clock.c` | S | — |
| 486 | `intel_disable_shared_dpll` | global | `display/clock.c` | I / 旧名で照合 | `display/clock.h` |
| 526 | `icl_wrpll_ref_clock` | static | `display/clock.c` | S | — |
| 540 | `icl_wrpll_get_multipliers` | static | `display/clock.c` | S | — |
| 579 | `icl_wrpll_params_populate` | static | `display/clock.c` | S | — |
| 628 | `icl_calc_wrpll` | static | `display/clock.c` | S | — |
| 680 | `intel_get_shared_dpll_by_id` | global | `display/clock.c` | I / 旧名で照合 | `display/clock.h` |
| 696 | `intel_dpll_mask_all` | static | `display/clock.c` | S | — |
| 712 | `intel_find_shared_dpll` | static | `display/clock.c` | S | — |
| 774 | `intel_reference_shared_dpll_crtc` | static | `display/clock.c` | S | — |
| 789 | `intel_reference_shared_dpll` | static | `display/clock.c` | S | — |
| 813 | `intel_unreference_shared_dpll_crtc` | global | `display/clock.c` | I / 旧名で照合 | `display/clock.h` |
| 827 | `intel_unreference_shared_dpll` | static | `display/clock.c` | S | — |
| 838 | `icl_ddi_combo_pll_get_freq` | static | `display/clock.c` | S | — |
| 907 | `intel_dpll_get_freq` | global | `display/clock.c` | I / 旧名で照合 | `display/clock.h` |
| 925 | `intel_dpll_get_hw_state` | global | `display/clock.c` | I / 旧名で照合 | `display/clock.h` |
| 932 | `combo_pll_get_hw_state` | static | `display/clock.c` | S | — |
| 941 | `icl_pll_get_hw_state` | static | `display/clock.c` | S | — |
| 1001 | `readout_dpll_hw_state` | static | `display/clock.c` | S | — |
| 1026 | `intel_dpll_readout_hw_state` | global | `display/clock.c` | I / 旧名で照合 | `display/clock.h` |
| 1035 | `sanitize_dpll_state` | static | `display/clock.c` | S | — |
| 1053 | `intel_dpll_sanitize_state` | global | `display/clock.c` | I / 旧名で照合 | `display/clock.h` |

### parity/lcd/intel_gmbus_port.c

現行: [parity/lcd/intel_gmbus_port.c](../../src/drivers/gpu/i915/parity/lcd/intel_gmbus_port.c)。17定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 89 | `to_intel_gmbus` | static | `display/gmbus.c` | S | — |
| 95 | `intel_gmbus_reset` | global | `display/gmbus.c` | I / 旧名で照合 | `display/gmbus.h` |
| 101 | `has_gmbus_irq` | static | `display/gmbus.c` | S | — |
| 110 | `gmbus_wait` | static | `display/gmbus.c` | S | — |
| 143 | `gmbus_wait_idle` | static | `display/gmbus.c` | S | — |
| 165 | `gmbus_max_xfer_size` | static | `display/gmbus.c` | S | — |
| 172 | `gmbus_xfer_read_chunk` | static | `display/gmbus.c` | S | — |
| 224 | `gmbus_xfer_read` | static | `display/gmbus.c` | S | — |
| 251 | `gmbus_xfer_write_chunk` | static | `display/gmbus.c` | S | — |
| 286 | `gmbus_xfer_write` | static | `display/gmbus.c` | S | — |
| 314 | `gmbus_is_index_xfer` | static | `display/gmbus.c` | S | — |
| 324 | `gmbus_index_xfer` | static | `display/gmbus.c` | S | — |
| 356 | `do_gmbus_xfer` | static | `display/gmbus.c` | S | — |
| 487 | `gmbus_xfer` | static | `display/gmbus.c` | S | — |
| 511 | `intel_gmbus_force_bit` | global | `display/gmbus.c` | I / 旧名で照合 | `display/gmbus.h` |
| 527 | `intel_gmbus_is_forced_bit` | global | `display/gmbus.c` | I / 旧名で照合 | `display/gmbus.h` |
| 534 | `intel_gmbus_irq_handler` | global | `display/gmbus.c` | I / 旧名で照合 | `display/gmbus.h` |

### parity/lcd/intel_hdmi_detect_port.c

現行: [parity/lcd/intel_hdmi_detect_port.c](../../src/drivers/gpu/i915/parity/lcd/intel_hdmi_detect_port.c)。3定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 49 | `intel_hdmi_unset_edid` | static | `display/hdmi.c` | S | — |
| 61 | `intel_hdmi_set_edid` | static | `display/hdmi.c` | S | — |
| 102 | `intel_hdmi_detect` | static | `display/hdmi.c` | S | — |

### parity/lcd/intel_hdmi_mode_port.c

現行: [parity/lcd/intel_hdmi_mode_port.c](../../src/drivers/gpu/i915/parity/lcd/intel_hdmi_mode_port.c)。4定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 54 | `assert_hdmi_transcoder_func_disabled` | static | `display/hdmi.c` | S | — |
| 63 | `hsw_set_infoframes` | static | `display/hdmi.c` | S | — |
| 106 | `intel_dp_dual_mode_set_tmds_output` | global | `display/hdmi.c` | I / 旧名で照合 | `display/hdmi.h` |
| 139 | `intel_hdmi_handle_sink_scrambling` | global | `display/hdmi.c` | I / 旧名で照合 | `display/hdmi.h` |

### parity/lcd/intel_hotplug_irq_port.c

現行: [parity/lcd/intel_hotplug_irq_port.c](../../src/drivers/gpu/i915/parity/lcd/intel_hotplug_irq_port.c)。4定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 25 | `icp_ddi_port_hotplug_long_detect` | static | `display/hotplug.c` | S | — |
| 38 | `icp_tc_port_hotplug_long_detect` | static | `display/hotplug.c` | S | — |
| 60 | `intel_get_hpd_pins` | static | `display/hotplug.c` | S | — |
| 85 | `icp_irq_handler` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |

### parity/lcd/intel_hotplug_port.c

現行: [parity/lcd/intel_hotplug_port.c](../../src/drivers/gpu/i915/parity/lcd/intel_hotplug_port.c)。12定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 58 | `intel_connector_hpd_pin` | static | `display/hotplug.c` | S | — |
| 100 | `intel_hpd_irq_storm_detect` | static | `display/hotplug.c` | S | — |
| 136 | `intel_hpd_irq_storm_switch_to_polling` | static | `display/hotplug.c` | S | — |
| 177 | `intel_hpd_irq_storm_reenable_work` | static | `display/hotplug.c` | S | — |
| 219 | `intel_hotplug_detect_connector` | static | `display/hotplug.c` | S | — |
| 252 | `intel_encoder_hotplug` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 258 | `intel_encoder_has_hpd_pulse` | static | `display/hotplug.c` | S | — |
| 264 | `i915_digport_work_func` | static | `display/hotplug.c` | S | — |
| 315 | `i915_hotplug_work_func` | static | `display/hotplug.c` | S | — |
| 431 | `intel_hpd_irq_handler` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 544 | `intel_hpd_init_early` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 563 | `intel_hpd_cancel_work` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |

### parity/lcd/intel_link_port.c

現行: [parity/lcd/intel_link_port.c](../../src/drivers/gpu/i915/parity/lcd/intel_link_port.c)。20定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 49 | `intel_dp_is_uhbr` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 61 | `intel_dp_link_symbol_size` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 73 | `intel_dp_link_symbol_clock` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 86 | `intel_dp_link_required` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 101 | `intel_dp_effective_data_rate` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 129 | `intel_dp_max_data_rate` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 155 | `intel_dp_needs_vsc_sdp` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 181 | `intel_dp_rate_index` | static | `display/dp.c` | S | — |
| 201 | `intel_dp_is_edp` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 208 | `intel_dp_source_supports_tps3` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 213 | `intel_dp_source_supports_tps4` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 218 | `intel_dp_rate_select` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 230 | `intel_dp_compute_rate` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 250 | `intel_dp_set_link_params` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 259 | `downstream_hpd_needs_d0` | static | `display/dp.c` | S | — |
| 275 | `intel_edp_init_source_oui` | static | `display/dp.c` | S | — |
| 303 | `intel_dp_set_power` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 349 | `intel_edp_backlight_on` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 365 | `intel_edp_backlight_off` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |
| 379 | `intel_dp_set_infoframes` | global | `display/dp.c` | I / 旧名で照合 | `display/dp.h` |

### parity/lcd/intel_modeset_setup_port.c

現行: [parity/lcd/intel_modeset_setup_port.c](../../src/drivers/gpu/i915/parity/lcd/intel_modeset_setup_port.c)。25定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 68 | `intel_crtc_needs_link_reset` | static | `display/takeover.c` | S | — |
| 83 | `get_bigjoiner_slave_pipes` | static | `display/takeover.c` | S | — |
| 102 | `get_portsync_pipes` | static | `display/takeover.c` | S | — |
| 136 | `get_transcoder_pipes` | static | `display/takeover.c` | S | — |
| 159 | `intel_crtc_disable_noatomic_begin` | static | `display/takeover.c` | S | — |
| 222 | `intel_crtc_disable_noatomic_complete` | static | `display/takeover.c` | S | — |
| 260 | `intel_crtc_disable_noatomic` | static | `display/takeover.c` | S | — |
| 296 | `set_encoder_for_connector` | static | `display/takeover.c` | S | — |
| 314 | `reset_encoder_connector_state` | static | `display/takeover.c` | S | — |
| 339 | `reset_crtc_encoder_state` | static | `display/takeover.c` | S | — |
| 350 | `intel_modeset_update_connector_atomic_state` | static | `display/takeover.c` | S | — |
| 375 | `intel_crtc_copy_hw_to_uapi_state` | static | `display/takeover.c` | S | — |
| 403 | `intel_sanitize_plane_mapping` | static | `display/takeover.c` | S | — |
| 431 | `intel_crtc_has_encoders` | static | `display/takeover.c` | S | — |
| 442 | `intel_encoder_find_connector` | static | `display/takeover.c` | S | — |
| 461 | `intel_sanitize_fifo_underrun_reporting` | static | `display/takeover.c` | S | — |
| 484 | `has_bogus_dpll_config` | static | `display/takeover.c` | S | — |
| 504 | `intel_sanitize_crtc` | static | `display/takeover.c` | S | — |
| 556 | `intel_sanitize_all_crtcs` | static | `display/takeover.c` | S | — |
| 592 | `intel_sanitize_encoder` | static | `display/takeover.c` | S | — |
| 675 | `readout_plane_state` | static | `display/takeover.c` | S | — |
| 708 | `intel_modeset_readout_hw_state` | static | `display/takeover.c` | S | — |
| 919 | `get_encoder_power_domains` | static | `display/takeover.c` | S | — |
| 941 | `intel_early_display_was` | static | `display/takeover.c` | S | — |
| 967 | `intel_modeset_setup_hw_state` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |

### parity/lcd/intel_opregion_port.c

現行: [parity/lcd/intel_opregion_port.c](../../src/drivers/gpu/i915/parity/lcd/intel_opregion_port.c)。28定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 307 | `asle_set_als_illum` | static | `display/opregion.c` | S | — |
| 315 | `asle_set_backlight` | static | `display/opregion.c` | S | — |
| 356 | `asle_set_pwm_freq` | static | `display/opregion.c` | S | — |
| 362 | `asle_set_pfit` | static | `display/opregion.c` | S | — |
| 370 | `asle_set_supported_rotation_angles` | static | `display/opregion.c` | S | — |
| 376 | `asle_set_button_array` | static | `display/opregion.c` | S | — |
| 400 | `asle_set_convertible` | static | `display/opregion.c` | S | — |
| 412 | `asle_set_docking` | static | `display/opregion.c` | S | — |
| 423 | `asle_isct_state` | static | `display/opregion.c` | S | — |
| 429 | `asle_work` | static | `display/opregion.c` | S | — |
| 481 | `intel_opregion_asle_intr` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 493 | `intel_opregion_video_event` | static | `display/opregion.c` | S | — |
| 515 | `check_swsci_function` | static | `display/opregion.c` | S | — |
| 542 | `swsci` | static | `display/opregion.c` | S | — |
| 616 | `intel_opregion_notify_adapter` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 639 | `set_did` | static | `display/opregion.c` | S | — |
| 653 | `intel_didl_outputs` | static | `display/opregion.c` | S | — |
| 692 | `intel_setup_cadls` | static | `display/opregion.c` | S | — |
| 722 | `intel_no_opregion_vbt_callback` | static | `display/opregion.c` | S | — |
| 729 | `intel_load_vbt_firmware` | static | `display/opregion.c` | S | — |
| 769 | `intel_opregion_setup` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 929 | `intel_opregion_register` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 945 | `intel_opregion_resume_display` | static | `display/opregion.c` | S | — |
| 971 | `intel_opregion_resume` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 984 | `intel_opregion_suspend_display` | static | `display/opregion.c` | S | — |
| 997 | `intel_opregion_suspend` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 1010 | `intel_opregion_unregister` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 1025 | `intel_opregion_cleanup` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |

### parity/lcd/intel_vblank_port.c

現行: [parity/lcd/intel_vblank_port.c](../../src/drivers/gpu/i915/parity/lcd/intel_vblank_port.c)。9定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 27 | `pipe_scanline_is_moving` | static | `display/vblank.c` | S | — |
| 40 | `wait_for_pipe_scanline_moving` | static | `display/vblank.c` | S | — |
| 52 | `intel_wait_for_pipe_scanline_stopped` | global | `display/vblank.c` | I / 旧名で照合 | `display/vblank.h` |
| 57 | `intel_wait_for_pipe_scanline_moving` | global | `display/vblank.c` | I / 旧名で照合 | `display/vblank.h` |
| 62 | `g4x_get_vblank_counter` | global | `display/vblank.c` | I / 旧名で照合 | `display/vblank.h` |
| 78 | `__intel_get_crtc_scanline` | static | `display/vblank.c` | S | — |
| 134 | `intel_get_crtc_scanline` | global | `display/vblank.c` | I / 旧名で照合 | `display/vblank.h` |
| 151 | `intel_crtc_scanline_offset` | static | `display/vblank.c` | S | — |
| 198 | `intel_crtc_update_active_timings` | global | `display/vblank.c` | I / 旧名で照合 | `display/vblank.h` |

### parity/lcd/intel_vrr_port.c

現行: [parity/lcd/intel_vrr_port.c](../../src/drivers/gpu/i915/parity/lcd/intel_vrr_port.c)。2定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 19 | `trans_vrr_ctl` | static | `display/pipe.c` | S | — |
| 32 | `intel_vrr_set_transcoder_timings` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |

### parity/lcd/intel_wm_port.c

現行: [parity/lcd/intel_wm_port.c](../../src/drivers/gpu/i915/parity/lcd/intel_wm_port.c)。1定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 20 | `intel_wm_plane_visible` | global | `display/watermark.c` | I / 旧名で照合 | `display/watermark.h` |

### parity/lcd/lcd_compat.h

現行: [parity/lcd/lcd_compat.h](../../src/drivers/gpu/i915/parity/lcd/lcd_compat.h)。14定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 52 | `mul_u32_u32` | static | `display/modeset-internal.h` | S | `display/modeset-internal.h` |
| 53 | `div_u64` | static | `display/modeset-internal.h` | S | `display/modeset-internal.h` |
| 54 | `roundup_pow_of_two` | static | `display/modeset-internal.h` | S | `display/modeset-internal.h` |
| 220 | `drm_atomic_crtc_needs_modeset` | static | `display/modeset-internal.h` | S | `display/modeset-internal.h` |
| 228 | `set_bit` | static | `display/modeset-internal.h` | S | `display/modeset-internal.h` |
| 229 | `clear_bit` | static | `display/modeset-internal.h` | S | `display/modeset-internal.h` |
| 230 | `test_bit` | static | `display/modeset-internal.h` | S | `display/modeset-internal.h` |
| 231 | `bitmap_zero` | static | `display/modeset-internal.h` | S | `display/modeset-internal.h` |
| 233 | `bitmap_andnot` | static | `display/modeset-internal.h` | S | `display/modeset-internal.h` |
| 235 | `bitmap_subset` | static | `display/modeset-internal.h` | S | `display/modeset-internal.h` |
| 254 | `drm_rect_width` | static | `display/modeset-internal.h` | S | `display/modeset-internal.h` |
| 255 | `drm_rect_height` | static | `display/modeset-internal.h` | S | `display/modeset-internal.h` |
| 441 | `parity_lcd_trans_offset` | static | `display/modeset-internal.h` | S | `display/modeset-internal.h` |
| 459 | `to_i915` | static | `display/modeset-internal.h` | S | `display/modeset-internal.h` |

### parity/lcd/lcd_dp_compat.h

現行: [parity/lcd/lcd_dp_compat.h](../../src/drivers/gpu/i915/parity/lcd/lcd_dp_compat.h)。5定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 31 | `parity_lcd_dpcd_read` | static | `display/dp.c` | S | — |
| 36 | `parity_lcd_dpcd_write` | static | `display/dp.c` | S | — |
| 43 | `parity_lcd_dpcd_readb` | static | `display/dp.c` | S | — |
| 44 | `parity_lcd_dpcd_writeb` | static | `display/dp.c` | S | — |
| 48 | `parity_lcd_dpcd_probe` | static | `display/dp.c` | S | — |

### parity/lcd/lcd_dp_helper_inlines.h

現行: [parity/lcd/lcd_dp_helper_inlines.h](../../src/drivers/gpu/i915/parity/lcd/lcd_dp_helper_inlines.h)。3定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 33 | `drm_dp_tps3_supported` | static | `display/dp.c` | S | — |
| 40 | `drm_dp_tps4_supported` | static | `display/dp.c` | S | — |
| 47 | `drm_dp_is_branch` | static | `display/dp.c` | S | — |

### parity/lcd/lcd_drm_fourcc.h

現行: [parity/lcd/lcd_drm_fourcc.h](../../src/drivers/gpu/i915/parity/lcd/lcd_drm_fourcc.h)。1定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 940 | `drm_fourcc_canonicalize_nvidia_format_mod` | static | `display/state.c` | S | — |

### parity/lcd/lcd_drm_plane_defs.h

現行: [parity/lcd/lcd_drm_plane_defs.h](../../src/drivers/gpu/i915/parity/lcd/lcd_drm_plane_defs.h)。1定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 32 | `drm_rotation_90_or_270` | static | `display/plane.c` | S | — |

### parity/lcd/lcd_fake_hw.c

現行: [parity/lcd/lcd_fake_hw.c](../../src/drivers/gpu/i915/parity/lcd/lcd_fake_hw.c)。36定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 49 | `slices_of_range` | static | `tests/display/lcd-fake-hw.c` | T | — |
| 63 | `dc_off_held` | static | `tests/display/lcd-fake-hw.c` | T | — |
| 68 | `slot` | static | `tests/display/lcd-fake-hw.c` | T | — |
| 86 | `lcd_fake_reg` | global | `tests/display/lcd-fake-hw.c` | T | `tests/display/lcd-fake-hw.h` |
| 96 | `pll_locked` | static | `tests/display/lcd-fake-hw.c` | T | — |
| 101 | `ddi_clock_on` | static | `tests/display/lcd-fake-hw.c` | T | — |
| 107 | `frame_of` | static | `tests/display/lcd-fake-hw.c` | T | — |
| 113 | `to_next_frame` | static | `tests/display/lcd-fake-hw.c` | T | — |
| 121 | `f_read32` | static | `tests/display/lcd-fake-hw.c` | T | — |
| 156 | `f_write32` | static | `tests/display/lcd-fake-hw.c` | T | — |
| 260 | `f_rmw32` | static | `tests/display/lcd-fake-hw.c` | T | — |
| 268 | `f_wait_reg` | static | `tests/display/lcd-fake-hw.c` | T | — |
| 283 | `f_sleep` | static | `tests/display/lcd-fake-hw.c` | T | — |
| 290 | `f_dpcd_read` | static | `tests/display/lcd-fake-hw.c` | T | — |
| 296 | `f_dpcd_write` | static | `tests/display/lcd-fake-hw.c` | T | — |
| 302 | `f_read_dpcd_caps` | static | `tests/display/lcd-fake-hw.c` | T | — |
| 308 | `f_panel` | static | `tests/display/lcd-fake-hw.c` | T | — |
| 314 | `f_power_get` | static | `tests/display/lcd-fake-hw.c` | T | — |
| 325 | `f_power_put` | static | `tests/display/lcd-fake-hw.c` | T | — |
| 345 | `f_power_put_async` | static | `tests/display/lcd-fake-hw.c` | T | — |
| 355 | `f_dbuf_slices_update` | static | `tests/display/lcd-fake-hw.c` | T | — |
| 375 | `f_vblank_get` | static | `tests/display/lcd-fake-hw.c` | T | — |
| 384 | `f_vblank_put` | static | `tests/display/lcd-fake-hw.c` | T | — |
| 395 | `f_vblank_sleep` | static | `tests/display/lcd-fake-hw.c` | T | — |
| 413 | `f_irq_off` | static | `tests/display/lcd-fake-hw.c` | T | — |
| 423 | `f_irq_on` | static | `tests/display/lcd-fake-hw.c` | T | — |
| 432 | `f_arm_event` | static | `tests/display/lcd-fake-hw.c` | T | — |
| 441 | `f_wait_event` | static | `tests/display/lcd-fake-hw.c` | T | — |
| 461 | `f_cancel_event` | static | `tests/display/lcd-fake-hw.c` | T | — |
| 471 | `f_observe` | static | `tests/display/lcd-fake-hw.c` | T | — |
| 481 | `f_lock` | static | `tests/display/lcd-fake-hw.c` | T | — |
| 492 | `f_step` | static | `tests/display/lcd-fake-hw.c` | T | — |
| 495 | `sink_on_dpcd_write` | static | `tests/display/lcd-fake-hw.c` | T | — |
| 545 | `lcd_fake_init` | global | `tests/display/lcd-fake-hw.c` | T | `tests/display/lcd-fake-hw.h` |
| 594 | `lcd_fake_power_refs_total` | global | `tests/display/lcd-fake-hw.c` | T | `tests/display/lcd-fake-hw.h` |
| 603 | `lcd_fake_violations` | global | `tests/display/lcd-fake-hw.c` | T | `tests/display/lcd-fake-hw.h` |

### parity/lcd/lcd_hw_check.c

現行: [parity/lcd/lcd_hw_check.c](../../src/drivers/gpu/i915/parity/lcd/lcd_hw_check.c)。1定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 21 | `parity_lcd_scanout_hw_check` | global | `tests/display/scanout-hw-check.c` | T | `tests/display/scanout-hw-check.h` |

### parity/lcd/lcd_i915_fixed.h

現行: [parity/lcd/lcd_i915_fixed.h](../../src/drivers/gpu/i915/parity/lcd/lcd_i915_fixed.h)。15定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 21 | `is_fixed16_zero` | static | `display/internal.h` | S | `display/internal.h` |
| 26 | `u32_to_fixed16` | static | `display/internal.h` | S | `display/internal.h` |
| 35 | `fixed16_to_u32_round_up` | static | `display/internal.h` | S | `display/internal.h` |
| 40 | `fixed16_to_u32` | static | `display/internal.h` | S | `display/internal.h` |
| 45 | `min_fixed16` | static | `display/internal.h` | S | `display/internal.h` |
| 53 | `max_fixed16` | static | `display/internal.h` | S | `display/internal.h` |
| 61 | `clamp_u64_to_fixed16` | static | `display/internal.h` | S | `display/internal.h` |
| 70 | `div_round_up_fixed16` | static | `display/internal.h` | S | `display/internal.h` |
| 76 | `mul_round_up_u32_fixed16` | static | `display/internal.h` | S | `display/internal.h` |
| 87 | `mul_fixed16` | static | `display/internal.h` | S | `display/internal.h` |
| 98 | `div_fixed16` | static | `display/internal.h` | S | `display/internal.h` |
| 108 | `div_round_up_u32_fixed16` | static | `display/internal.h` | S | `display/internal.h` |
| 119 | `mul_u32_fixed16` | static | `display/internal.h` | S | `display/internal.h` |
| 128 | `add_fixed16` | static | `display/internal.h` | S | `display/internal.h` |
| 138 | `add_fixed16_u32` | static | `display/internal.h` | S | `display/internal.h` |

### parity/lcd/lcd_link_training_inlines.h

現行: [parity/lcd/lcd_link_training_inlines.h](../../src/drivers/gpu/i915/parity/lcd/lcd_link_training_inlines.h)。1定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 15 | `intel_dp_training_pattern_symbol` | static | `display/dp.c` | S | — |

### parity/lcd/lcd_modeset_compat.h

現行: [parity/lcd/lcd_modeset_compat.h](../../src/drivers/gpu/i915/parity/lcd/lcd_modeset_compat.h)。2定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 87 | `str_on_off` | static | `display/modeset-internal.h` | S | `display/modeset-internal.h` |
| 88 | `str_enable_disable` | static | `display/modeset-internal.h` | S | `display/modeset-internal.h` |

### parity/lcd/lcd_modeset_ktest.c

現行: [parity/lcd/lcd_modeset_ktest.c](../../src/drivers/gpu/i915/parity/lcd/lcd_modeset_ktest.c)。3定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 30 | `bring_up` | static | `tests/display/lcd-modeset-ktest.c` | T | — |
| 73 | `released` | static | `tests/display/lcd-modeset-ktest.c` | T | — |
| 82 | `parity_lcd_modeset_ktest` | global | `tests/display/lcd-modeset-ktest.c` | T | `tests/display/lcd-modeset-ktest.h` |

### parity/lcd/lcd_pattern.c

現行: [parity/lcd/lcd_pattern.c](../../src/drivers/gpu/i915/parity/lcd/lcd_pattern.c)。5定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 15 | `in_rect` | static | `tests/display/lcd-pattern.c` | T | — |
| 24 | `in_digit` | static | `tests/display/lcd-pattern.c` | T | — |
| 41 | `parity_lcd_pattern_pixel` | global | `tests/display/lcd-pattern.c` | T | `tests/display/lcd-pattern.h` |
| 99 | `parity_lcd_pattern_fill` | global | `tests/display/lcd-pattern.c` | T | `tests/display/lcd-pattern.h` |
| 121 | `parity_lcd_pattern_verify` | global | `tests/display/lcd-pattern.c` | T | `tests/display/lcd-pattern.h` |

### parity/lcd/lcd_ref_inlines.h

現行: [parity/lcd/lcd_ref_inlines.h](../../src/drivers/gpu/i915/parity/lcd/lcd_ref_inlines.h)。4定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 10 | `transcoder_is_dsi` | static | `display/internal.h` | S | `display/internal.h` |
| 17 | `intel_crtc_has_type` | static | `display/internal.h` | S | `display/internal.h` |
| 24 | `intel_crtc_has_dp_encoder` | static | `display/internal.h` | S | `display/internal.h` |
| 33 | `intel_crtc_needs_modeset` | static | `display/internal.h` | S | `display/internal.h` |

### parity/lcd/lcd_show_ktest.c

現行: [parity/lcd/lcd_show_ktest.c](../../src/drivers/gpu/i915/parity/lcd/lcd_show_ktest.c)。7定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 47 | `locked` | static | `tests/display/lcd-show-ktest.c` | T | — |
| 59 | `bring_up` | static | `tests/display/lcd-show-ktest.c` | T | — |
| 112 | `edp_released` | static | `tests/display/lcd-show-ktest.c` | T | — |
| 121 | `live_ptes` | static | `tests/display/lcd-show-ktest.c` | T | — |
| 131 | `fail_in_window` | static | `tests/display/lcd-show-ktest.c` | T | — |
| 138 | `stick_the_pipe` | static | `tests/display/lcd-show-ktest.c` | T | — |
| 145 | `parity_lcd_show_ktest` | global | `tests/display/lcd-show-ktest.c` | T | `tests/display/lcd-show-ktest.h` |

### parity/lcd/lcd_wm_compat.h

現行: [parity/lcd/lcd_wm_compat.h](../../src/drivers/gpu/i915/parity/lcd/lcd_wm_compat.h)。1定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 34 | `drm_format_info` | static | `display/watermark-internal.h` | S | `display/watermark-internal.h` |

### parity/lcd/lcd_wm_ddb_types.h

現行: [parity/lcd/lcd_wm_ddb_types.h](../../src/drivers/gpu/i915/parity/lcd/lcd_wm_ddb_types.h)。2定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 19 | `skl_ddb_entry_size` | static | `display/watermark.c` | S | — |
| 24 | `skl_ddb_entry_equal` | static | `display/watermark.c` | S | — |

### parity/lcd/lcdg_ktest.c

現行: [parity/lcd/lcdg_ktest.c](../../src/drivers/gpu/i915/parity/lcd/lcdg_ktest.c)。8定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 36 | `tf_read` | static | `tests/display/lcdg-ktest.c` | T | — |
| 58 | `tf_write` | static | `tests/display/lcdg-ktest.c` | T | — |
| 71 | `tf_fw_request` | static | `tests/display/lcdg-ktest.c` | T | — |
| 72 | `tf_fw_ack` | static | `tests/display/lcdg-ktest.c` | T | — |
| 88 | `map_draw` | static | `tests/display/lcdg-ktest.c` | T | — |
| 119 | `present_ptes` | static | `tests/display/lcdg-ktest.c` | T | — |
| 134 | `present_at` | static | `tests/display/lcdg-ktest.c` | T | — |
| 147 | `parity_lcdg_ktest` | global | `tests/display/lcdg-ktest.c` | T | `tests/display/lcdg-ktest.h` |

### parity/lcd/n1_compat.h

現行: [parity/lcd/n1_compat.h](../../src/drivers/gpu/i915/parity/lcd/n1_compat.h)。4定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 61 | `parity_n1_power_put` | static | `display/takeover-internal.h` | S | `display/takeover-internal.h` |
| 90 | `drm_rect_init` | static | `display/takeover-internal.h` | S | `display/takeover-internal.h` |
| 286 | `drm_atomic_set_mode_for_crtc` | static | `display/takeover-internal.h` | S | `display/takeover-internal.h` |
| 344 | `parity_n1_bitmap_empty` | static | `display/takeover-internal.h` | S | `display/takeover-internal.h` |

### parity/lcd/opregion_compat.h

現行: [parity/lcd/opregion_compat.h](../../src/drivers/gpu/i915/parity/lcd/opregion_compat.h)。2定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 43 | `parity_opregion_work_trampoline` | static | `display/opregion-internal.h` | S | `display/opregion-internal.h` |
| 52 | `parity_opregion_queue_work` | static | `display/opregion-internal.h` | S | `display/opregion-internal.h` |

### parity/lcd/opregion_fwtest.c

現行: [parity/lcd/opregion_fwtest.c](../../src/drivers/gpu/i915/parity/lcd/opregion_fwtest.c)。4定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 35 | `fw_backlight` | static | `tests/display/opregion-fwtest.c` | T | — |
| 42 | `log_mbox` | static | `tests/display/opregion-fwtest.c` | T | — |
| 54 | `parity_opregion_fw_test` | global | `tests/display/opregion-fwtest.c` | T | `tests/display/opregion-fwtest.h` |
| 151 | `parity_opregion_fw_log_again` | global | `tests/display/opregion-fwtest.c` | T | `tests/display/opregion-fwtest.h` |

### parity/lcd/opregion_ktest.c

現行: [parity/lcd/opregion_ktest.c](../../src/drivers/gpu/i915/parity/lcd/opregion_ktest.c)。18定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 23 | `rd32` | static | `tests/display/opregion-ktest.c` | T | — |
| 24 | `wr32` | static | `tests/display/opregion-ktest.c` | T | — |
| 25 | `wr16v` | static | `tests/display/opregion-ktest.c` | T | — |
| 43 | `shadow_init` | static | `tests/display/opregion-ktest.c` | T | — |
| 56 | `shadow_vbt_init` | static | `tests/display/opregion-ktest.c` | T | — |
| 75 | `other_cb` | static | `tests/display/opregion-ktest.c` | T | — |
| 82 | `log_dispatch` | static | `tests/display/opregion-ktest.c` | T | — |
| 105 | `fake_backlight` | static | `tests/display/opregion-ktest.c` | T | — |
| 114 | `asle_request` | static | `tests/display/opregion-ktest.c` | T | — |
| 122 | `log_asle` | static | `tests/display/opregion-ktest.c` | T | — |
| 134 | `asle_tests` | static | `tests/display/opregion-ktest.c` | T | — |
| 185 | `slow_cb` | static | `tests/display/opregion-ktest.c` | T | — |
| 195 | `dispatch_fn` | static | `tests/display/opregion-ktest.c` | T | — |
| 203 | `blocking_backlight` | static | `tests/display/opregion-ktest.c` | T | — |
| 216 | `blocker_fn` | static | `tests/display/opregion-ktest.c` | T | — |
| 223 | `start_service` | static | `tests/display/opregion-ktest.c` | T | — |
| 233 | `lifecycle_tests` | static | `tests/display/opregion-ktest.c` | T | — |
| 323 | `parity_opregion_ktest` | global | `tests/display/opregion-ktest.c` | T | `tests/display/opregion-ktest.h` |

### parity/lcd/parity_atomic_plane_glue.inc

現行: [parity/lcd/parity_atomic_plane_glue.inc](../../src/drivers/gpu/i915/parity/lcd/parity_atomic_plane_glue.inc)。1定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 9 | `parity_lcd_ms_plane_data_rates` | global | `display/plane.c` | I / 旧名で照合 | `display/plane.h` |

### parity/lcd/parity_backlight_glue.inc

現行: [parity/lcd/parity_backlight_glue.inc](../../src/drivers/gpu/i915/parity/lcd/parity_backlight_glue.inc)。5定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 31 | `parity_lcd_ms_backlight_setup` | global | `display/panel.c` | I / 旧名で照合 | `display/panel.h` |
| 55 | `parity_lcd_ms_set_brightness` | global | `display/panel.c` | I / 旧名で照合 | `display/panel.h` |
| 62 | `parity_lcd_ms_set_acpi` | global | `display/panel.c` | I / 旧名で照合 | `display/panel.h` |
| 69 | `parity_lcd_ms_backlight_power` | global | `display/panel.c` | I / 旧名で照合 | `display/panel.h` |
| 79 | `parity_lcd_ms_user_level` | global | `display/panel.c` | I / 旧名で照合 | `display/panel.h` |

### parity/lcd/parity_buf_trans_glue.inc

現行: [parity/lcd/parity_buf_trans_glue.inc](../../src/drivers/gpu/i915/parity/lcd/parity_buf_trans_glue.inc)。1定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 9 | `parity_lcd_ms_bind_buf_trans` | global | `display/phy.c` | I / 旧名で照合 | `display/phy.h` |

### parity/lcd/parity_bw_glue.inc

現行: [parity/lcd/parity_bw_glue.inc](../../src/drivers/gpu/i915/parity/lcd/parity_bw_glue.inc)。2定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 8 | `parity_lcd_ms_bw_min_cdclk` | global | `display/watermark.c` | I / 旧名で照合 | `display/watermark.h` |
| 13 | `parity_lcd_ms_bw_data_rate` | global | `display/watermark.c` | I / 旧名で照合 | `display/watermark.h` |

### parity/lcd/parity_cdclk_glue.inc

現行: [parity/lcd/parity_cdclk_glue.inc](../../src/drivers/gpu/i915/parity/lcd/parity_cdclk_glue.inc)。1定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 12 | `parity_lcd_ms_cdclk_check` | global | `display/clock.c` | I / 旧名で照合 | `display/clock.h` |

### parity/lcd/parity_color_glue.inc

現行: [parity/lcd/parity_color_glue.inc](../../src/drivers/gpu/i915/parity/lcd/parity_color_glue.inc)。2定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 19 | `parity_lcd_ms_color_funcs` | global | `display/color.c` | I / 旧名で照合 | `display/color.h` |
| 24 | `parity_lcd_ms_color_check` | global | `display/color.c` | I / 旧名で照合 | `display/color.h` |

### parity/lcd/parity_ddi_emit_glue.inc

現行: [parity/lcd/parity_ddi_emit_glue.inc](../../src/drivers/gpu/i915/parity/lcd/parity_ddi_emit_glue.inc)。13定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 11 | `parity_ddi_emit` | global | `display/ddi.c` | I / 旧名で照合 | `display/ddi.h` |
| 69 | `parity_lcd_hdmi_level_shift` | global | `display/ddi.c` | I / 旧名で照合 | `display/ddi.h` |
| 75 | `parity_lcd_ms_bound_port` | global | `display/ddi.c` | I / 旧名で照合 | `display/ddi.h` |
| 86 | `parity_lcd_ms_bind_readout` | global | `display/ddi.c` | I / 旧名で照合 | `display/ddi.h` |
| 94 | `parity_lcd_ms_bound_encoder` | global | `display/ddi.c` | I / 旧名で照合 | `display/ddi.h` |
| 99 | `parity_lcd_ms_bound_connector` | global | `display/ddi.c` | I / 旧名で照合 | `display/ddi.h` |
| 104 | `parity_lcd_ms_bind_encoder` | global | `display/ddi.c` | I / 旧名で照合 | `display/ddi.h` |
| 143 | `intel_encoders_pre_pll_enable` | global | `display/ddi.c` | I / 旧名で照合 | `display/ddi.h` |
| 148 | `intel_encoders_pre_enable` | global | `display/ddi.c` | I / 旧名で照合 | `display/ddi.h` |
| 153 | `intel_encoders_enable` | global | `display/ddi.c` | I / 旧名で照合 | `display/ddi.h` |
| 158 | `intel_encoders_disable` | global | `display/ddi.c` | I / 旧名で照合 | `display/ddi.h` |
| 163 | `intel_encoders_post_disable` | global | `display/ddi.c` | I / 旧名で照合 | `display/ddi.h` |
| 168 | `intel_encoders_post_pll_disable` | global | `display/ddi.c` | I / 旧名で照合 | `display/ddi.h` |

### parity/lcd/parity_ddi_hotplug_glue.inc

現行: [parity/lcd/parity_ddi_hotplug_glue.inc](../../src/drivers/gpu/i915/parity/lcd/parity_ddi_hotplug_glue.inc)。6定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 17 | `intel_port_to_phy` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 24 | `intel_phy_is_tc` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 30 | `intel_tc_port_link_reset` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 38 | `intel_dp_phy_test` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 43 | `intel_dp_retrain_link` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 54 | `intel_hdmi_reset_link` | static | `display/hotplug.c` | S | — |

### parity/lcd/parity_display_emit_glue.inc

現行: [parity/lcd/parity_display_emit_glue.inc](../../src/drivers/gpu/i915/parity/lcd/parity_display_emit_glue.inc)。5定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 12 | `parity_display_emit_transcoder` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |
| 47 | `parity_display_emit_cpu_transcoder` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |
| 105 | `parity_lcd_ms_display_funcs` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |
| 110 | `parity_lcd_ms_crtc_enable` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |
| 119 | `parity_lcd_ms_crtc_disable` | global | `display/pipe.c` | I / 旧名で照合 | `display/pipe.h` |

### parity/lcd/parity_dpll_glue.inc

現行: [parity/lcd/parity_dpll_glue.inc](../../src/drivers/gpu/i915/parity/lcd/parity_dpll_glue.inc)。10定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 12 | `parity_icl_hdmi_wrpll` | global | `display/clock.c` | I / 旧名で照合 | `display/clock.h` |
| 38 | `parity_icl_dp_combo_pll` | global | `display/clock.c` | I / 旧名で照合 | `display/clock.h` |
| 96 | `parity_lcd_shared_dpll_state` | global | `display/clock.c` | I / 旧名で照合 | `display/clock.h` |
| 102 | `parity_lcd_dpll_pool_bind` | global | `display/clock.c` | I / 旧名で照合 | `display/clock.h` |
| 123 | `parity_lcd_dpll_pool_init` | static | `display/clock.c` | S | — |
| 134 | `parity_lcd_ms_release_pipe` | global | `display/clock.c` | I / 旧名で照合 | `display/clock.h` |
| 145 | `parity_lcd_dplls_reset` | global | `display/clock.c` | I / 旧名で照合 | `display/clock.h` |
| 156 | `parity_lcd_ms_alloc_pll` | global | `display/clock.c` | I / 旧名で照合 | `display/clock.h` |
| 173 | `parity_lcd_ms_release_pll` | global | `display/clock.c` | I / 旧名で照合 | `display/clock.h` |
| 183 | `parity_lcd_ms_bind_pll` | global | `display/clock.c` | I / 旧名で照合 | `display/clock.h` |

### parity/lcd/parity_edid_mode_glue.inc

現行: [parity/lcd/parity_edid_mode_glue.inc](../../src/drivers/gpu/i915/parity/lcd/parity_edid_mode_glue.inc)。3定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 15 | `drm_mode_create` | global | `display/edid.c` | I / 旧名で照合 | `display/edid.h` |
| 25 | `drm_mode_set_name` | global | `display/edid.c` | I / 旧名で照合 | `display/edid.h` |
| 30 | `parity_edid_preferred_mode` | global | `display/edid.c` | I / 旧名で照合 | `display/edid.h` |

### parity/lcd/parity_flip_glue.inc

現行: [parity/lcd/parity_flip_glue.inc](../../src/drivers/gpu/i915/parity/lcd/parity_flip_glue.inc)。8定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 16 | `parity_lcd_ms_crtc_funcs` | global | `display/vblank.c` | I / 旧名で照合 | `display/vblank.h` |
| 23 | `parity_lcd_ms_active_timings` | global | `display/vblank.c` | I / 旧名で照合 | `display/vblank.h` |
| 35 | `parity_lcd_irq_disable` | global | `display/vblank.c` | I / 旧名で照合 | `display/vblank.h` |
| 42 | `parity_lcd_irq_enable` | global | `display/vblank.c` | I / 旧名で照合 | `display/vblank.h` |
| 49 | `parity_lcd_irq_save` | global | `display/vblank.c` | I / 旧名で照合 | `display/vblank.h` |
| 57 | `parity_lcd_irq_restore` | global | `display/vblank.c` | I / 旧名で照合 | `display/vblank.h` |
| 64 | `parity_lcd_ms_evade_window` | global | `display/vblank.c` | I / 旧名で照合 | `display/vblank.h` |
| 75 | `parity_crtc_state_size_crtc_unit` | global | `display/vblank.c` | I / 旧名で照合 | `display/vblank.h` |

### parity/lcd/parity_gmbus_glue.inc

現行: [parity/lcd/parity_gmbus_glue.inc](../../src/drivers/gpu/i915/parity/lcd/parity_gmbus_glue.inc)。4定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 16 | `hpd_gmbus_xfer_locked` | static | `display/gmbus.c` | S | — |
| 29 | `hpd_bit_xfer_step` | static | `display/gmbus.c` | S | — |
| 36 | `parity_hpd_gmbus_adapter` | global | `display/gmbus.c` | I / 旧名で照合 | `display/gmbus.h` |
| 68 | `parity_hpd_gmbus_forget` | global | `display/gmbus.c` | I / 旧名で照合 | `display/gmbus.h` |

### parity/lcd/parity_hdmi_detect_glue.inc

現行: [parity/lcd/parity_hdmi_detect_glue.inc](../../src/drivers/gpu/i915/parity/lcd/parity_hdmi_detect_glue.inc)。10定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 28 | `hpd_conn_index` | static | `display/hdmi.c` | S | — |
| 33 | `drm_edid_read_ddc` | global | `display/hdmi.c` | I / 旧名で照合 | `display/hdmi.h` |
| 76 | `drm_edid_connector_update` | global | `display/hdmi.c` | I / 旧名で照合 | `display/hdmi.h` |
| 94 | `drm_edid_is_digital` | global | `display/hdmi.c` | I / 旧名で照合 | `display/hdmi.h` |
| 99 | `drm_edid_free` | global | `display/hdmi.c` | I / 旧名で照合 | `display/hdmi.h` |
| 104 | `intel_hdmi_dp_dual_mode_detect` | static | `display/hdmi.c` | S | — |
| 112 | `parity_hpd_hdmi_connector_funcs` | global | `display/hdmi.c` | I / 旧名で照合 | `display/hdmi.h` |
| 117 | `parity_hpd_edid_info` | global | `display/hdmi.c` | I / 旧名で照合 | `display/hdmi.h` |
| 124 | `parity_hpd_edid_bytes` | global | `display/hdmi.c` | I / 旧名で照合 | `display/hdmi.h` |
| 134 | `parity_hpd_edid_forget` | global | `display/hdmi.c` | I / 旧名で照合 | `display/hdmi.h` |

### parity/lcd/parity_hdmi_mode_glue.inc

現行: [parity/lcd/parity_hdmi_mode_glue.inc](../../src/drivers/gpu/i915/parity/lcd/parity_hdmi_mode_glue.inc)。1定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 11 | `parity_lcd_hdmi_tmds_output` | global | `display/hdmi.c` | I / 旧名で照合 | `display/hdmi.h` |

### parity/lcd/parity_hotplug_glue.inc

現行: [parity/lcd/parity_hotplug_glue.inc](../../src/drivers/gpu/i915/parity/lcd/parity_hotplug_glue.inc)。42定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 25 | `parity_hpd_model_allowed` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 62 | `hpd_status_name` | static | `display/hotplug.c` | S | — |
| 67 | `parity_hpd_warn` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 76 | `parity_hpd_sync_deadline` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 85 | `parity_hpd_read` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 115 | `parity_hpd_rmw` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 136 | `hpd_fake_gmbus_write` | static | `display/hotplug.c` | S | — |
| 170 | `parity_hpd_write` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 181 | `parity_hpd_encoder_at` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 186 | `parity_hpd_connector_next` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 192 | `intel_display_power_get` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 200 | `intel_display_power_put` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 209 | `intel_hpd_irq_setup` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 222 | `parity_hpd_gmbus_woken` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 227 | `intel_irqs_enabled` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 232 | `drm_kms_helper_poll_reschedule` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 238 | `drm_kms_helper_connector_hotplug_event` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 243 | `drm_kms_helper_hotplug_event` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 248 | `i915_hpd_poll_init_work` | static | `display/hotplug.c` | S | — |
| 253 | `hpd_dp_pulse_step` | static | `display/hotplug.c` | S | — |
| 262 | `hpd_dp_detect_step` | static | `display/hotplug.c` | S | — |
| 272 | `hpd_tc_connected_step` | static | `display/hotplug.c` | S | — |
| 279 | `hpd_hotplug_recorded` | static | `display/hotplug.c` | S | — |
| 315 | `parity_hpd_work_trampoline` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 329 | `hpd_make_objects` | static | `display/hotplug.c` | S | — |
| 403 | `parity_hpd_start` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 470 | `parity_hpd_pch_irq` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 484 | `parity_hpd_model_irq` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 499 | `hpd_icp_entry` | static | `display/hotplug.c` | S | — |
| 525 | `parity_hpd_stop` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 547 | `parity_hpd_probe_connector` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 563 | `parity_hpd_connector_name` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 568 | `parity_hpd_summary` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 584 | `parity_hpd_irq_record` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 589 | `parity_hpd_hotplug_record` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 595 | `parity_hpd_flush_reenable` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 602 | `parity_hpd_event_bits` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 603 | `parity_hpd_retry_bits` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 604 | `parity_hpd_pin_state` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 605 | `parity_hpd_pin_count` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 606 | `parity_hpd_connector_polled` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |
| 607 | `parity_hpd_connector_status` | global | `display/hotplug.c` | I / 旧名で照合 | `display/hotplug.h` |

### parity/lcd/parity_hpd_test.c

現行: [parity/lcd/parity_hpd_test.c](../../src/drivers/gpu/i915/parity/lcd/parity_hpd_test.c)。3定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 23 | `st_name` | static | `tests/display/parity-hpd-test.c` | T | — |
| 28 | `parity_hpd_test_run` | global | `tests/display/parity-hpd-test.c` | T | `tests/display/parity-hpd-test.h` |
| 102 | `parity_hdmi_edid_test_run` | global | `tests/display/parity-hpd-test.c` | T | `tests/display/parity-hpd-test.h` |

### parity/lcd/parity_lcd_calc.c

現行: [parity/lcd/parity_lcd_calc.c](../../src/drivers/gpu/i915/parity/lcd/parity_lcd_calc.c)。17定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 29 | `parity_lcd_error` | global | `display/state.c` | I / 旧名で照合 | `display/state.h` |
| 36 | `parity_lcd_errors` | global | `display/state.c` | I / 旧名で照合 | `display/state.h` |
| 41 | `parity_lcd_error_bind` | global | `display/state.c` | I / 旧名で照合 | `display/state.h` |
| 52 | `parity_lcd_note` | global | `display/state.c` | I / 旧名で照合 | `display/state.h` |
| 60 | `rate_from_bw_code` | static | `display/state.c` | S | — |
| 71 | `parity_lcd_compute` | global | `display/state.c` | I / 旧名で照合 | `display/state.h` |
| 148 | `parity_lcd_compute_hdmi` | global | `display/state.c` | I / 旧名で照合 | `display/state.h` |
| 166 | `record_write` | static | `display/state.c` | S | — |
| 182 | `record_rmw` | static | `display/state.c` | S | — |
| 199 | `record_step` | static | `display/state.c` | S | — |
| 215 | `parity_lcd_emit_plane` | global | `display/state.c` | I / 旧名で照合 | `display/state.h` |
| 235 | `parity_lcd_words_step` | global | `display/state.c` | I / 旧名で照合 | `display/state.h` |
| 245 | `parity_lcd_words_find` | global | `display/state.c` | I / 旧名で照合 | `display/state.h` |
| 264 | `state_to_mode` | static | `display/state.c` | S | — |
| 280 | `parity_lcd_emit_cpu_transcoder` | global | `display/state.c` | I / 旧名で照合 | `display/state.h` |
| 304 | `parity_lcd_emit_ddi` | global | `display/state.c` | I / 旧名で照合 | `display/state.h` |
| 331 | `parity_lcd_emit_transcoder` | global | `display/state.c` | I / 旧名で照合 | `display/state.h` |

### parity/lcd/parity_lcd_kernel.c

現行: [parity/lcd/parity_lcd_kernel.c](../../src/drivers/gpu/i915/parity/lcd/parity_lcd_kernel.c)。98定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 134 | `k_note_sink` | static | `display/diagnostics.c` | S | — |
| 139 | `k_read32` | static | `mmio.c` | S | — |
| 155 | `k_write32` | static | `mmio.c` | S | — |
| 162 | `k_rmw32` | static | `mmio.c` | S | — |
| 171 | `k_posting_read` | static | `mmio.c` | S | — |
| 177 | `k_wait_reg` | static | `mmio.c` | S | — |
| 195 | `k_usleep` | static | `sync.c` | I / 旧名で照合 | `sync.h` |
| 207 | `k_udelay` | static | `sync.c` | S | — |
| 217 | `k_dpcd_read` | static | `display/aux.c` | S | — |
| 223 | `k_dpcd_write` | static | `display/aux.c` | S | — |
| 229 | `k_read_dpcd_caps` | static | `display/aux.c` | S | — |
| 235 | `k_panel` | static | `display/panel.c` | S | — |
| 241 | `k_power_get` | static | `display/power.c` | S | — |
| 264 | `k_power_get_if_enabled` | static | `display/power.c` | S | — |
| 275 | `k_power_put` | static | `display/power.c` | S | — |
| 296 | `k_power_put_async` | static | `display/power.c` | S | — |
| 310 | `k_dbuf_slices_update` | static | `display/watermark.c` | S | — |
| 317 | `k_lock` | static | `display/modeset.c` | S | — |
| 331 | `k_step` | static | `display/diagnostics.c` | S | — |
| 345 | `k_error` | static | `display/diagnostics.c` | S | — |
| 357 | `k_debug` | static | `display/diagnostics.c` | S | — |
| 364 | `k_frame` | static | `display/vblank.c` | S | — |
| 371 | `k_vblank_get` | static | `display/vblank.c` | S | — |
| 378 | `k_vblank_put` | static | `display/vblank.c` | S | — |
| 386 | `k_vblank_sleep` | static | `display/vblank.c` | S | — |
| 404 | `k_irq_off` | static | `irq.c` | S | — |
| 411 | `k_irq_on` | static | `irq.c` | S | — |
| 420 | `k_arm_event` | static | `display/vblank.c` | S | — |
| 429 | `k_wait_event` | static | `display/vblank.c` | S | — |
| 447 | `k_cancel_event` | static | `display/vblank.c` | S | — |
| 457 | `bind_ops` | static | `display/modeset.c` | I / 旧名で照合 | `display/modeset.h` |
| 493 | `stage_name` | static | `tests/display/kernel-scenarios.c` | T | — |
| 501 | `log_regs` | static | `display/diagnostics.c` | I / 旧名で照合 | `display/diagnostics.h` |
| 534 | `probe_scanout` | static | `tests/display/kernel-scenarios.c` | T | — |
| 628 | `at_stage` | static | `tests/display/kernel-scenarios.c` | T | — |
| 649 | `kind_name` | static | `display/diagnostics.c` | S | — |
| 657 | `log_trace` | static | `display/diagnostics.c` | I / 旧名で照合 | `display/diagnostics.h` |
| 678 | `log_observer` | static | `display/diagnostics.c` | I / 旧名で照合 | `display/diagnostics.h` |
| 694 | `log_status` | static | `display/diagnostics.c` | I / 旧名で照合 | `display/diagnostics.h` |
| 708 | `fill_cfg` | static | `display/modeset.c` | I / 旧名で照合 | `display/modeset.h` |
| 785 | `preflight` | static | `display/modeset.c` | I / 旧名で照合 | `display/modeset.h` |
| 869 | `lcd_run_one` | static | `tests/display/kernel-scenarios.c` | T | — |
| 1013 | `parity_lcd_kernel_lcdb_run` | global | `tests/display/kernel-scenarios.c` | T | `tests/display/kernel-scenarios.h` |
| 1032 | `hdmib_window` | static | `tests/display/kernel-scenarios.c` | T | — |
| 1052 | `parity_lcd_kernel_hdmib_run` | global | `tests/display/kernel-scenarios.c` | T | `tests/display/kernel-scenarios.h` |
| 1083 | `dual_frame` | static | `tests/display/kernel-scenarios.c` | T | — |
| 1102 | `dual_bring_up` | static | `tests/display/kernel-scenarios.c` | T | — |
| 1146 | `dual_log_state` | static | `tests/display/kernel-scenarios.c` | T | — |
| 1159 | `dual_stop` | static | `tests/display/kernel-scenarios.c` | T | — |
| 1186 | `dual_release` | static | `tests/display/kernel-scenarios.c` | T | — |
| 1200 | `parity_lcd_kernel_dual_run` | global | `tests/display/kernel-scenarios.c` | T | `tests/display/kernel-scenarios.h` |
| 1351 | `parity_lcd_kernel_dual_share_run` | global | `tests/display/kernel-scenarios.c` | T | `tests/display/kernel-scenarios.h` |
| 1515 | `read_frame` | static | `tests/display/kernel-scenarios.c` | T | — |
| 1522 | `step_sleep` | static | `tests/display/kernel-scenarios.c` | T | — |
| 1535 | `irq_check` | static | `tests/display/kernel-scenarios.c` | T | — |
| 1585 | `expected_duty` | static | `tests/display/kernel-scenarios.c` | T | — |
| 1592 | `brightness_step` | static | `tests/display/kernel-scenarios.c` | T | — |
| 1627 | `d_vbt_min` | static | `tests/display/kernel-scenarios.c` | T | — |
| 1632 | `window_first` | static | `tests/display/kernel-scenarios.c` | T | — |
| 1663 | `window_again` | static | `tests/display/kernel-scenarios.c` | T | — |
| 1669 | `parity_lcd_kernel_lcdr_run` | global | `tests/display/kernel-scenarios.c` | T | `tests/display/kernel-scenarios.h` |
| 1697 | `parity_lcd_kernel_abandoned` | global | `display/diagnostics.c` | I / 旧名で照合 | `display/diagnostics.h` |
| 1702 | `parity_lcd_kernel_gpu_retained` | global | `display/diagnostics.c` | I / 旧名で照合 | `display/diagnostics.h` |
| 1707 | `parity_lcd_kernel_summary` | global | `display/diagnostics.c` | I / 旧名で照合 | `display/diagnostics.h` |
| 1725 | `parity_lcdg_finish` | global | `tests/display/kernel-scenarios.c` | T | `tests/display/kernel-scenarios.h` |
| 1757 | `lcdg_verify` | static | `tests/display/kernel-scenarios.c` | T | — |
| 1764 | `parity_lcd_kernel_lcdg_run` | global | `tests/display/kernel-scenarios.c` | T | `tests/display/kernel-scenarios.h` |
| 1916 | `lcdc_verify_a` | static | `tests/display/kernel-scenarios.c` | T | — |
| 1923 | `lcdc_flips` | static | `tests/display/kernel-scenarios.c` | T | — |
| 1958 | `parity_lcd_kernel_lcdc_run` | global | `tests/display/kernel-scenarios.c` | T | `tests/display/kernel-scenarios.h` |
| 2073 | `lcdd_verify` | static | `tests/display/kernel-scenarios.c` | T | — |
| 2087 | `lcdd_draw` | static | `tests/display/kernel-scenarios.c` | T | — |
| 2135 | `lcdd_evasion_probe` | static | `tests/display/kernel-scenarios.c` | T | — |
| 2205 | `lcdd_rounds` | static | `tests/display/kernel-scenarios.c` | T | — |
| 2242 | `parity_lcd_kernel_lcdd_run` | global | `tests/display/kernel-scenarios.c` | T | `tests/display/kernel-scenarios.h` |
| 2402 | `lcdo_rd` | static | `tests/display/kernel-scenarios.c` | T | — |
| 2403 | `lcdo_wr` | static | `tests/display/kernel-scenarios.c` | T | — |
| 2405 | `lcdo_backlight` | static | `tests/display/kernel-scenarios.c` | T | — |
| 2411 | `lcdo_verify` | static | `tests/display/kernel-scenarios.c` | T | — |
| 2419 | `lcdo_expected_duty` | static | `tests/display/kernel-scenarios.c` | T | — |
| 2426 | `lcdo_steps` | static | `tests/display/kernel-scenarios.c` | T | — |
| 2500 | `parity_lcd_kernel_lcdo_run` | global | `tests/display/kernel-scenarios.c` | T | `tests/display/kernel-scenarios.h` |
| 2596 | `n1_hold` | static | `tests/display/kernel-scenarios.c` | T | — |
| 2610 | `n1_read_plane` | static | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 2655 | `n1_console_fb` | static | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 2667 | `n1_check_ggtt` | static | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 2704 | `n1_mirror_console` | static | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 2735 | `n1_window_mirror` | static | `tests/display/kernel-scenarios.c` | T | — |
| 2811 | `n1_window` | static | `tests/display/kernel-scenarios.c` | T | — |
| 2854 | `parity_lcd_kernel_n1_run` | global | `tests/display/kernel-scenarios.c` | T | `tests/display/kernel-scenarios.h` |
| 3158 | `resident_window` | static | `display/modeset.c` | S | — |
| 3175 | `resident_verify` | static | `display/modeset.c` | S | — |
| 3182 | `parity_lcd_kernel_panel_mode` | global | `display/state.c` | H / `drv_i915_display_panel_mode` | `display/state.h` |
| 3202 | `parity_lcd_resident_buffer` | global | `display/scanout.c` | I / 旧名で照合 | `display/scanout.h` |
| 3207 | `parity_lcd_resident_back` | global | `display/scanout.c` | I / 旧名で照合 | `display/scanout.h` |
| 3212 | `parity_lcd_resident_flip` | global | `display/present.c` | I / 旧名で照合 | `display/present.h` |
| 3238 | `parity_lcd_kernel_resident_run` | global | `display/modeset.c` | I / 旧名で照合 | `display/modeset.h` |
| 3342 | `parity_lcd_kernel_panel_size_mm` | global | `display/state.c` | H / `drv_i915_display_panel_size_mm` | `display/state.h` |

### parity/lcd/parity_lcd_modeset.c

現行: [parity/lcd/parity_lcd_modeset.c](../../src/drivers/gpu/i915/parity/lcd/parity_lcd_modeset.c)。31定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 46 | `bind_current` | static | `display/modeset.c` | S | — |
| 61 | `parity_lcd_set_display_ver` | global | `display/modeset.c` | I / 旧名で照合 | `display/modeset.h` |
| 62 | `parity_lcd_display_ver` | global | `display/modeset.c` | I / 旧名で照合 | `display/modeset.h` |
| 65 | `parity_lcd_modeset_select` | global | `display/modeset.c` | I / 旧名で照合 | `display/modeset.h` |
| 74 | `parity_lcd_modeset_selected` | global | `display/modeset.c` | I / 旧名で照合 | `display/modeset.h` |
| 80 | `on_error` | static | `display/modeset.c` | S | — |
| 89 | `parity_lcd_debug` | global | `display/modeset.c` | I / 旧名で照合 | `display/modeset.h` |
| 95 | `parity_lcd_modeset_prepare` | global | `display/modeset.c` | I / 旧名で照合 | `display/modeset.h` |
| 302 | `read_link_status` | static | `display/modeset.c` | S | — |
| 313 | `parity_lcd_modeset_status` | global | `display/modeset.c` | I / 旧名で照合 | `display/modeset.h` |
| 383 | `parity_lcd_modeset_enable` | global | `display/modeset.c` | I / 旧名で照合 | `display/modeset.h` |
| 415 | `parity_lcd_modeset_plane_update` | global | `display/modeset.c` | I / 旧名で照合 | `display/modeset.h` |
| 425 | `parity_lcd_modeset_plane_disable` | global | `display/modeset.c` | I / 旧名で照合 | `display/modeset.h` |
| 437 | `parity_lcd_modeset_disable` | global | `display/modeset.c` | I / 旧名で照合 | `display/modeset.h` |
| 459 | `parity_lcd_ms_vblank_off` | global | `display/vblank.c` | I / 旧名で照合 | `display/vblank.h` |
| 470 | `parity_lcd_modeset_evade_window` | global | `display/vblank.c` | I / 旧名で照合 | `display/vblank.h` |
| 479 | `parity_lcd_modeset_plane_released` | global | `display/modeset.c` | I / 旧名で照合 | `display/modeset.h` |
| 487 | `parity_lcd_modeset_link_status` | global | `display/modeset.c` | I / 旧名で照合 | `display/modeset.h` |
| 496 | `observe` | static | `display/modeset.c` | S | — |
| 502 | `parity_lcd_modeset_commit_enable` | global | `display/modeset.c` | I / 旧名で照合 | `display/modeset.h` |
| 551 | `parity_lcd_modeset_commit_disable` | global | `display/modeset.c` | I / 旧名で照合 | `display/modeset.h` |
| 615 | `parity_lcd_modeset_abandoned` | global | `display/modeset.c` | I / 旧名で照合 | `display/modeset.h` |
| 622 | `parity_lcd_modeset_retained` | global | `display/modeset.c` | I / 旧名で照合 | `display/modeset.h` |
| 627 | `parity_lcd_modeset_discard_model` | global | `tests/display/modeset.c` | T | `tests/display/modeset.h` |
| 639 | `parity_lcd_backend_fault` | global | `display/modeset.c` | I / 旧名で照合 | `display/modeset.h` |
| 644 | `parity_lcd_modeset_brightness` | global | `display/panel.c` | I / 旧名で照合 | `display/panel.h` |
| 659 | `parity_lcd_modeset_backlight_acpi` | global | `display/panel.c` | I / 旧名で照合 | `display/panel.h` |
| 670 | `parity_lcd_modeset_backlight` | global | `display/panel.c` | I / 旧名で照合 | `display/panel.h` |
| 687 | `live_surf` | static | `display/present.c` | S | — |
| 692 | `frame_now` | static | `display/present.c` | S | — |
| 697 | `parity_lcd_modeset_flip` | global | `display/present.c` | I / 旧名で照合 | `display/present.h` |

### parity/lcd/parity_lcd_observe.c

現行: [parity/lcd/parity_lcd_observe.c](../../src/drivers/gpu/i915/parity/lcd/parity_lcd_observe.c)。9定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 11 | `parity_lcd_observer_init` | global | `display/diagnostics.c` | I / 旧名で照合 | `display/diagnostics.h` |
| 25 | `sample` | static | `display/diagnostics.c` | S | — |
| 74 | `parity_lcd_observer_point` | global | `display/diagnostics.c` | I / 旧名で照合 | `display/diagnostics.h` |
| 83 | `parity_lcd_observer_steady_begin` | global | `display/diagnostics.c` | I / 旧名で照合 | `display/diagnostics.h` |
| 89 | `parity_lcd_observer_steady_sample` | global | `display/diagnostics.c` | I / 旧名で照合 | `display/diagnostics.h` |
| 94 | `parity_lcd_observer_steady_end` | global | `display/diagnostics.c` | I / 旧名で照合 | `display/diagnostics.h` |
| 102 | `parity_lcd_observer_frames` | global | `display/diagnostics.c` | I / 旧名で照合 | `display/diagnostics.h` |
| 118 | `parity_lcd_observer_stopped` | global | `display/diagnostics.c` | I / 旧名で照合 | `display/diagnostics.h` |
| 130 | `parity_lcd_observer_pipe_active` | global | `display/diagnostics.c` | I / 旧名で照合 | `display/diagnostics.h` |

### parity/lcd/parity_lcd_regs.c

現行: [parity/lcd/parity_lcd_regs.c](../../src/drivers/gpu/i915/parity/lcd/parity_lcd_regs.c)。4定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 26 | `parity_lcd_reg_table` | global | `display/diagnostics.c` | I / 旧名で照合 | `display/diagnostics.h` |
| 68 | `parity_lcd_reg_by_name` | global | `display/diagnostics.c` | I / 旧名で照合 | `display/diagnostics.h` |
| 82 | `parity_lcd_ref_dbuf_ctl` | global | `display/diagnostics.c` | I / 旧名で照合 | `display/diagnostics.h` |
| 93 | `parity_lcd_last_resort_stop` | global | `display/diagnostics.c` | I / 旧名で照合 | `display/diagnostics.h` |

### parity/lcd/parity_lcd_show.c

現行: [parity/lcd/parity_lcd_show.c](../../src/drivers/gpu/i915/parity/lcd/parity_lcd_show.c)。15定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 21 | `parity_lcd_show_retained` | global | `display/modeset.c` | I / 旧名で照合 | `display/modeset.h` |
| 26 | `parity_lcd_show_retain_gpu` | global | `display/modeset.c` | I / 旧名で照合 | `display/modeset.h` |
| 35 | `parity_lcd_show_gpu_retained` | global | `display/modeset.c` | I / 旧名で照合 | `display/modeset.h` |
| 40 | `parity_lcd_show_discard_gpu_model` | global | `display/modeset.c` | I / 旧名で照合 | `display/modeset.h` |
| 52 | `parity_lcd_show_discard_model` | global | `display/modeset.c` | I / 旧名で照合 | `display/modeset.h` |
| 65 | `anomaly` | static | `display/modeset.c` | S | — |
| 74 | `reached` | static | `display/modeset.c` | S | — |
| 81 | `observe_tap` | static | `display/modeset.c` | S | — |
| 86 | `first_error_after` | static | `display/modeset.c` | S | — |
| 101 | `show_display` | static | `display/modeset.c` | S | — |
| 219 | `show_passed` | static | `display/modeset.c` | S | — |
| 227 | `show_begin` | static | `display/modeset.c` | S | — |
| 240 | `parity_lcd_show_prepared` | global | `display/modeset.c` | I / 旧名で照合 | `display/modeset.h` |
| 258 | `pattern_verify` | static | `display/modeset.c` | S | — |
| 266 | `parity_lcd_show_run` | global | `display/modeset.c` | I / 旧名で照合 | `display/modeset.h` |

### parity/lcd/parity_lcd_trace.c

現行: [parity/lcd/parity_lcd_trace.c](../../src/drivers/gpu/i915/parity/lcd/parity_lcd_trace.c)。34定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 7 | `add` | static | `display/diagnostics.c` | S | — |
| 22 | `t_write32` | static | `display/diagnostics.c` | S | — |
| 32 | `t_rmw32` | static | `display/diagnostics.c` | S | — |
| 43 | `t_posting_read` | static | `display/diagnostics.c` | S | — |
| 51 | `t_read32` | static | `display/diagnostics.c` | S | — |
| 68 | `t_wait_reg` | static | `display/diagnostics.c` | S | — |
| 81 | `t_usleep` | static | `display/diagnostics.c` | S | — |
| 90 | `t_udelay` | static | `display/diagnostics.c` | S | — |
| 98 | `first_bytes` | static | `display/diagnostics.c` | S | — |
| 108 | `t_dpcd_read` | static | `display/diagnostics.c` | S | — |
| 118 | `t_dpcd_write` | static | `display/diagnostics.c` | S | — |
| 128 | `t_read_dpcd_caps` | static | `display/diagnostics.c` | S | — |
| 138 | `t_panel` | static | `display/diagnostics.c` | S | — |
| 148 | `t_power_get` | static | `display/diagnostics.c` | S | — |
| 158 | `t_power_put` | static | `display/diagnostics.c` | S | — |
| 167 | `t_power_put_async` | static | `display/diagnostics.c` | S | — |
| 176 | `t_dbuf_slices_update` | static | `display/diagnostics.c` | S | — |
| 185 | `t_observe` | static | `display/diagnostics.c` | S | — |
| 198 | `t_vblank_get` | static | `display/diagnostics.c` | S | — |
| 199 | `t_vblank_put` | static | `display/diagnostics.c` | S | — |
| 200 | `t_vblank_sleep` | static | `display/diagnostics.c` | S | — |
| 201 | `t_irq_off` | static | `display/diagnostics.c` | S | — |
| 202 | `t_irq_on` | static | `display/diagnostics.c` | S | — |
| 203 | `t_arm_event` | static | `display/diagnostics.c` | S | — |
| 204 | `t_wait_event` | static | `display/diagnostics.c` | S | — |
| 205 | `t_cancel_event` | static | `display/diagnostics.c` | S | — |
| 207 | `t_lock` | static | `display/diagnostics.c` | S | — |
| 214 | `named` | static | `display/diagnostics.c` | S | — |
| 222 | `t_step` | static | `display/diagnostics.c` | S | — |
| 237 | `t_error` | static | `display/diagnostics.c` | S | — |
| 248 | `t_debug` | static | `display/diagnostics.c` | S | — |
| 256 | `parity_lcd_trace_init` | global | `display/diagnostics.c` | I / 旧名で照合 | `display/diagnostics.h` |
| 296 | `parity_lcd_trace_phase` | global | `display/diagnostics.c` | I / 旧名で照合 | `display/diagnostics.h` |
| 301 | `parity_lcd_trace_find` | global | `display/diagnostics.c` | I / 旧名で照合 | `display/diagnostics.h` |

### parity/lcd/parity_modeset_setup_glue.inc

現行: [parity/lcd/parity_modeset_setup_glue.inc](../../src/drivers/gpu/i915/parity/lcd/parity_modeset_setup_glue.inc)。18定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 50 | `parity_n1_crtc_at` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 59 | `parity_n1_crtc_for_pipe` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 66 | `parity_n1_plane_at` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 76 | `parity_n1_primary_plane` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 81 | `parity_n1_encoder_at` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 89 | `parity_n1_connector_at` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 101 | `parity_n1_crtc_state` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 113 | `n1_plane_get_hw_state` | static | `display/takeover.c` | S | — |
| 129 | `parity_n1_power_get_if_enabled` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 143 | `parity_n1_power_get_in_set_if_enabled` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 152 | `parity_n1_power_put_all_in_set` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 166 | `parity_n1_atomic_crtc_state` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 181 | `parity_n1_atomic_state` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 198 | `n1_build_device` | static | `display/takeover.c` | S | — |
| 266 | `n1_fill_report` | static | `display/takeover.c` | S | — |
| 308 | `parity_n1_readout` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 346 | `parity_n1_takeover` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 375 | `parity_n1_release` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |

### parity/lcd/parity_opregion_glue.inc

現行: [parity/lcd/parity_opregion_glue.inc](../../src/drivers/gpu/i915/parity/lcd/parity_opregion_glue.inc)。36定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 25 | `parity_opregion_warn_on` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 32 | `parity_opregion_pci_read32` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 41 | `parity_opregion_pci_access_unported` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 49 | `parity_opregion_memremap` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 62 | `parity_opregion_memunmap` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 68 | `parity_opregion_dmi_check_system` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 74 | `parity_opregion_boundary` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 80 | `parity_opregion_cancel_work_sync` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 88 | `parity_opregion_mailbox_backend` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 89 | `parity_opregion_service_epoch` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 91 | `parity_opregion_shadow_map` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 103 | `parity_opregion_shadow_setup` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 121 | `parity_opregion_register` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 129 | `parity_opregion_unregister` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 136 | `parity_opregion_cleanup` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 162 | `parity_opregion_firmware_setup` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 212 | `parity_opregion_mbox_read` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 221 | `parity_opregion_mbox_write` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 226 | `parity_opregion_notify_adapter` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 227 | `parity_opregion_vbt` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 232 | `parity_opregion_notifier_registered` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 233 | `parity_opregion_counters` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 253 | `parity_opregion_backlight_policy` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 254 | `parity_opregion_connection_lock` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 255 | `parity_opregion_connection_unlock` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 257 | `parity_opregion_connector_next` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 264 | `parity_opregion_backlight_set_acpi` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 271 | `parity_opregion_service_start` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 284 | `parity_opregion_set_policy` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 286 | `parity_opregion_add_connector` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 302 | `parity_opregion_add_backlight` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 308 | `parity_opregion_gse_entry` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 317 | `parity_opregion_gate_counters` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 323 | `parity_opregion_asle_flush` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 330 | `parity_opregion_worker_stats_get` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 350 | `parity_opregion_notify_encoder` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |

### parity/lcd/parity_plane_emit_glue.inc

現行: [parity/lcd/parity_plane_emit_glue.inc](../../src/drivers/gpu/i915/parity/lcd/parity_plane_emit_glue.inc)。7定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 16 | `parity_plane_emit` | global | `display/plane.c` | I / 旧名で照合 | `display/plane.h` |
| 88 | `parity_lcd_ms_plane_prepare` | global | `display/plane.c` | I / 旧名で照合 | `display/plane.h` |
| 129 | `parity_lcd_ms_plane_update` | global | `display/plane.c` | I / 旧名で照合 | `display/plane.h` |
| 136 | `parity_lcd_ms_plane_disable` | global | `display/plane.c` | I / 旧名で照合 | `display/plane.h` |
| 142 | `parity_lcd_ms_plane_min_cdclk` | global | `display/plane.c` | I / 旧名で照合 | `display/plane.h` |
| 154 | `parity_lcd_ms_plane_update_flip` | global | `display/plane.c` | I / 旧名で照合 | `display/plane.h` |
| 181 | `parity_lcd_plane_disable_arm` | global | `display/plane.c` | I / 旧名で照合 | `display/plane.h` |

### parity/lcd/parity_wm_glue.inc

現行: [parity/lcd/parity_wm_glue.inc](../../src/drivers/gpu/i915/parity/lcd/parity_wm_glue.inc)。6定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 20 | `parity_lcd_dbuf_current` | global | `display/watermark.c` | I / 旧名で照合 | `display/watermark.h` |
| 28 | `parity_lcd_dbuf_publish` | global | `display/watermark.c` | I / 旧名で照合 | `display/watermark.h` |
| 35 | `parity_lcd_dbuf_forget` | global | `display/watermark.c` | I / 旧名で照合 | `display/watermark.h` |
| 41 | `parity_lcd_ms_wm_compute` | global | `display/watermark.c` | I / 旧名で照合 | `display/watermark.h` |
| 83 | `parity_lcd_ms_wm_compute_off` | global | `display/watermark.c` | I / 旧名で照合 | `display/watermark.h` |
| 105 | `parity_lcd_wm_get_hw_state` | global | `display/watermark.c` | I / 旧名で照合 | `display/watermark.h` |

### parity/lcd/scanout_ktest.c

現行: [parity/lcd/scanout_ktest.c](../../src/drivers/gpu/i915/parity/lcd/scanout_ktest.c)。5定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 23 | `table_fill` | static | `tests/display/scanout-ktest.c` | T | — |
| 32 | `stray_writes` | static | `tests/display/scanout-ktest.c` | T | — |
| 45 | `ptes_match` | static | `tests/display/scanout-ktest.c` | T | — |
| 61 | `guards_are_scratch` | static | `tests/display/scanout-ktest.c` | T | — |
| 73 | `parity_scanout_ktest` | global | `tests/display/scanout-ktest.c` | T | `tests/display/scanout-ktest.h` |

### parity/lcd/scanout.c

現行: [parity/lcd/scanout.c](../../src/drivers/gpu/i915/parity/lcd/scanout.c)。8定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 34 | `parity_scanout_create` | global | `display/scanout.c` | I / 旧名で照合 | `display/scanout.h` |
| 73 | `parity_scanout_pin` | global | `display/scanout.c` | I / 旧名で照合 | `display/scanout.h` |
| 95 | `parity_scanout_publish` | global | `display/scanout.c` | I / 旧名で照合 | `display/scanout.h` |
| 104 | `parity_scanout_begin` | global | `display/scanout.c` | I / 旧名で照合 | `display/scanout.h` |
| 115 | `parity_scanout_end` | global | `display/scanout.c` | I / 旧名で照合 | `display/scanout.h` |
| 123 | `parity_scanout_unpin` | global | `display/scanout.c` | I / 旧名で照合 | `display/scanout.h` |
| 141 | `parity_scanout_destroy` | global | `display/scanout.c` | I / 旧名で照合 | `display/scanout.h` |
| 157 | `parity_scanout_abandon` | global | `display/scanout.c` | I / 旧名で照合 | `display/scanout.h` |

### parity/lcd/skl_plane_port.c

現行: [parity/lcd/skl_plane_port.c](../../src/drivers/gpu/i915/parity/lcd/skl_plane_port.c)。30定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 27 | `icl_hdr_plane_mask` | global | `display/plane.c` | I / 旧名で照合 | `display/plane.h` |
| 32 | `icl_is_hdr_plane` | global | `display/plane.c` | I / 旧名で照合 | `display/plane.h` |
| 38 | `skl_plane_stride_mult` | static | `display/plane.c` | S | — |
| 53 | `skl_plane_stride` | static | `display/plane.c` | S | — |
| 66 | `skl_plane_ctl_format` | static | `display/plane.c` | S | — |
| 128 | `skl_plane_ctl_alpha` | static | `display/plane.c` | S | — |
| 146 | `glk_plane_color_ctl_alpha` | static | `display/plane.c` | S | — |
| 164 | `skl_plane_ctl_tiling` | static | `display/plane.c` | S | — |
| 213 | `skl_plane_ctl_rotate` | static | `display/plane.c` | S | — |
| 235 | `icl_plane_ctl_flip` | static | `display/plane.c` | S | — |
| 250 | `adlp_plane_ctl_arb_slots` | static | `display/plane.c` | S | — |
| 273 | `skl_plane_ctl_crtc` | static | `display/plane.c` | S | — |
| 290 | `skl_plane_ctl` | static | `display/plane.c` | S | — |
| 333 | `glk_plane_color_ctl_crtc` | static | `display/plane.c` | S | — |
| 350 | `glk_plane_color_ctl` | static | `display/plane.c` | S | — |
| 389 | `skl_surf_address` | static | `display/plane.c` | S | — |
| 411 | `skl_plane_surf` | static | `display/plane.c` | S | — |
| 425 | `skl_plane_aux_dist` | static | `display/plane.c` | S | — |
| 445 | `skl_plane_keyval` | static | `display/plane.c` | S | — |
| 452 | `skl_plane_keymsk` | static | `display/plane.c` | S | — |
| 465 | `skl_plane_keymax` | static | `display/plane.c` | S | — |
| 473 | `icl_plane_color_plane` | static | `display/plane.c` | S | — |
| 482 | `icl_plane_update_sel_fetch_noarm` | static | `display/plane.c` | S | — |
| 525 | `icl_plane_update_noarm` | static | `display/plane.c` | S | — |
| 599 | `icl_plane_disable_sel_fetch_arm` | static | `display/plane.c` | S | — |
| 611 | `icl_plane_update_sel_fetch_arm` | static | `display/plane.c` | S | — |
| 629 | `icl_plane_update_arm` | static | `display/plane.c` | S | — |
| 665 | `icl_plane_disable_arm` | static | `display/plane.c` | S | — |
| 682 | `icl_plane_min_cdclk` | static | `display/plane.c` | S | — |
| 692 | `skl_plane_get_hw_state` | static | `display/plane.c` | S | — |

### parity/lcd/skl_watermark_port.c

現行: [parity/lcd/skl_watermark_port.c](../../src/drivers/gpu/i915/parity/lcd/skl_watermark_port.c)。69定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 459 | `intel_dbuf_enabled_slices` | static | `display/watermark.c` | S | — |
| 477 | `check_mbus_joined` | static | `display/watermark.c` | S | — |
| 489 | `adlp_check_mbus_joined` | static | `display/watermark.c` | S | — |
| 494 | `compute_dbuf_slices` | static | `display/watermark.c` | S | — |
| 507 | `tgl_compute_dbuf_slices` | static | `display/watermark.c` | S | — |
| 513 | `adlp_compute_dbuf_slices` | static | `display/watermark.c` | S | — |
| 519 | `skl_compute_dbuf_slices` | static | `display/watermark.c` | S | — |
| 539 | `intel_dbuf_slice_size` | static | `display/watermark.c` | S | — |
| 545 | `skl_ddb_entry_init` | static | `display/watermark.c` | S | — |
| 554 | `mbus_ddb_offset` | static | `display/watermark.c` | S | — |
| 568 | `skl_watermark_ipc_enabled` | global | `display/watermark.c` | I / 旧名で照合 | `display/watermark.h` |
| 577 | `skl_needs_memory_bw_wa` | static | `display/watermark.c` | S | — |
| 583 | `intel_get_linetime_us` | static | `display/watermark.c` | S | — |
| 605 | `skl_cursor_allocation` | static | `display/watermark.c` | S | — |
| 636 | `skl_total_relative_data_rate` | static | `display/watermark.c` | S | — |
| 656 | `intel_crtc_dbuf_weights` | static | `display/watermark.c` | S | — |
| 693 | `skl_wm_check_vblank` | static | `display/watermark.c` | S | — |
| 748 | `skl_wm_latency` | static | `display/watermark.c` | S | — |
| 777 | `skl_wm_method1` | static | `display/watermark.c` | S | — |
| 796 | `skl_wm_method2` | static | `display/watermark.c` | S | — |
| 813 | `skl_compute_wm_params` | static | `display/watermark.c` | S | — |
| 902 | `skl_compute_plane_wm_params` | static | `display/watermark.c` | S | — |
| 923 | `skl_wm_has_lines` | static | `display/watermark.c` | S | — |
| 932 | `skl_wm_max_lines` | static | `display/watermark.c` | S | — |
| 940 | `skl_compute_plane_wm` | static | `display/watermark.c` | S | — |
| 1075 | `skl_compute_wm_levels` | static | `display/watermark.c` | S | — |
| 1095 | `tgl_compute_sagv_wm` | static | `display/watermark.c` | S | — |
| 1114 | `skl_compute_transition_wm` | static | `display/watermark.c` | S | — |
| 1177 | `skl_build_plane_wm_single` | static | `display/watermark.c` | S | — |
| 1207 | `icl_build_plane_wm` | static | `display/watermark.c` | S | — |
| 1249 | `skl_build_plane_wm_uv` | static | `display/watermark.c` | S | — |
| 1270 | `skl_build_plane_wm` | static | `display/watermark.c` | S | — |
| 1299 | `skl_max_wm0_lines` | static | `display/watermark.c` | S | — |
| 1315 | `skl_max_wm_level_for_vblank` | static | `display/watermark.c` | S | — |
| 1342 | `skl_is_vblank_too_short` | static | `display/watermark.c` | S | — |
| 1355 | `skl_build_pipe_wm` | static | `display/watermark.c` | S | — |
| 1400 | `skl_check_wm_level` | static | `display/watermark.c` | S | — |
| 1407 | `skl_check_nv12_wm_level` | static | `display/watermark.c` | S | — |
| 1417 | `skl_need_wm_copy_wa` | static | `display/watermark.c` | S | — |
| 1436 | `use_minimal_wm0_only` | static | `display/watermark.c` | S | — |
| 1447 | `skl_allocate_plane_ddb` | static | `display/watermark.c` | S | — |
| 1474 | `skl_crtc_allocate_plane_ddb` | static | `display/watermark.c` | S | — |
| 1646 | `skl_ddb_entry_for_slices` | static | `display/watermark.c` | S | — |
| 1664 | `intel_crtc_ddb_weight` | static | `display/watermark.c` | S | — |
| 1683 | `skl_crtc_allocate_ddb` | static | `display/watermark.c` | S | — |
| 1754 | `skl_plane_wm_level` | static | `display/watermark.c` | S | — |
| 1767 | `skl_plane_trans_wm` | static | `display/watermark.c` | S | — |
| 1778 | `skl_write_wm_level` | static | `display/watermark.c` | S | — |
| 1794 | `skl_ddb_entry_write` | static | `display/watermark.c` | S | — |
| 1806 | `skl_write_plane_wm` | global | `display/watermark.c` | I / 旧名で照合 | `display/watermark.h` |
| 1843 | `intel_dbuf_mdclk_cdclk_ratio_update` | static | `display/watermark.c` | S | — |
| 1862 | `update_mbus_pre_enable` | static | `display/watermark.c` | S | — |
| 1890 | `intel_dbuf_pre_plane_update` | global | `display/watermark.c` | I / 旧名で照合 | `display/watermark.h` |
| 1911 | `intel_dbuf_post_plane_update` | global | `display/watermark.c` | I / 旧名で照合 | `display/watermark.h` |
| 1930 | `xelpdp_is_only_pipe_per_dbuf_bank` | static | `display/watermark.c` | S | — |
| 1949 | `intel_mbus_dbox_update` | global | `display/watermark.c` | I / 旧名で照合 | `display/watermark.h` |
| 2013 | `skl_wm_level_from_reg_val` | static | `display/watermark.c` | S | — |
| 2021 | `skl_pipe_wm_get_hw_state` | static | `display/watermark.c` | S | — |
| 2070 | `skl_pipe_ddb_get_hw_state` | static | `display/watermark.c` | S | — |
| 2095 | `skl_ddb_get_hw_plane_state` | static | `display/watermark.c` | S | — |
| 2120 | `skl_ddb_entry_union` | static | `display/watermark.c` | S | — |
| 2132 | `skl_wm_get_hw_state` | static | `display/watermark.c` | S | — |
| 2200 | `skl_dbuf_is_misconfigured` | static | `display/watermark.c` | S | — |
| 2232 | `skl_wm_sanitize` | static | `display/watermark.c` | S | — |
| 2268 | `skl_wm_get_hw_state_and_sanitize` | static | `display/watermark.c` | S | — |
| 2274 | `skl_ddb_entry_init_from_hw` | static | `display/watermark.c` | S | — |
| 2283 | `skl_ddb_dbuf_slice_mask` | global | `display/watermark.c` | I / 旧名で照合 | `display/watermark.h` |
| 2308 | `skl_ddb_entries_overlap` | static | `display/watermark.c` | S | — |
| 2314 | `skl_ddb_allocation_overlaps` | global | `display/watermark.c` | I / 旧名で照合 | `display/watermark.h` |

### parity/legacy_shim.c

現行: [parity/legacy_shim.c](../../src/drivers/gpu/i915/parity/legacy_shim.c)。24定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 99 | `to_errno` | static | `sync.c` | H / `drv_i915_error_from_reference` | `sync.h` |
| 105 | `shim_find` | static | `context.c` | I / 旧名で照合 | `context.h` |
| 118 | `parity_shim_lrc_create` | global | `context.c` | H / `drv_i915_context_create` | `context.h` |
| 191 | `parity_shim_lrc_destroy` | global | `context.c` | H / `drv_i915_context_destroy` | `context.h` |
| 218 | `parity_shim_request_kick` | global | `request.c` | I / 旧名で照合 | `request.h` |
| 242 | `shim_run` | static | `request.c` | I / 旧名で照合 | `request.h` |
| 333 | `shim_execute` | static | `request.c` | S | — |
| 344 | `shim_sync_do` | static | `request.c` | I / 旧名で照合 | `request.h` |
| 373 | `parity_shim_run_sync` | global | `request.c` | I / 旧名で照合 | `request.h` |
| 386 | `parity_shim_display_present` | global | `display/present.c` | I / 旧名で照合 | `display/present.h` |
| 402 | `parity_shim_display_release` | global | `display/present.c` | I / 旧名で照合 | `display/present.h` |
| 412 | `parity_shim_display_present_blob` | global | `display/present.c` | I / 旧名で照合 | `display/present.h` |
| 427 | `parity_shim_display_deps` | global | `display/display.c` | I / 旧名で照合 | `display/display.h` |
| 437 | `shim_present` | static | `display/present.c` | I / 旧名で照合 | `display/present.h` |
| 473 | `parity_shim_engine_reset` | global | `reset.c` | H / `drv_i915_engine_reset` | `reset.h` |
| 481 | `parity_shim_engine_recover` | global | `reset.c` | I / 旧名で照合 | `reset.h` |
| 491 | `parity_shim_gt_reset` | global | `reset.c` | H / `drv_i915_gt_reset` | `reset.h` |
| 505 | `shim_map_panel` | static | `display/scanout.c` | I / 旧名で照合 | `display/scanout.h` |
| 545 | `shim_unmap_panel` | static | `display/scanout.c` | I / 旧名で照合 | `display/scanout.h` |
| 560 | `shim_present_blob` | static | `display/present.c` | I / 旧名で照合 | `display/present.h` |
| 619 | `shim_finish` | static | `request.c` | S | — |
| 632 | `shim_serve` | static | `request.c` | I / 旧名で照合 | `request.h` |
| 732 | `shim_display_window` | static | `display/present.c` | S | — |
| 744 | `parity_resident_serve` | global | `device.c` | I / 旧名で照合 | `device.h` |

### parity/native_decide.c

現行: [parity/native_decide.c](../../src/drivers/gpu/i915/parity/native_decide.c)。1定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 8 | `parity_native_decide` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |

### parity/native_precheck.c

現行: [parity/native_precheck.c](../../src/drivers/gpu/i915/parity/native_precheck.c)。10定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 20 | `cpu_hypervisor` | static | `display/takeover.c` | S | — |
| 29 | `read_opregion` | static | `display/takeover.c` | S | — |
| 76 | `parity_opregion_read_data` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 123 | `parity_opregion_log` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 138 | `read_vtd` | static | `display/takeover.c` | S | — |
| 159 | `read_fb` | static | `display/takeover.c` | S | — |
| 175 | `read_display` | static | `display/takeover.c` | S | — |
| 228 | `parity_native_precheck` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 260 | `parity_native_log_again` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 269 | `parity_native_log` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |

### parity/opregion_service.c

現行: [parity/opregion_service.c](../../src/drivers/gpu/i915/parity/opregion_service.c)。5定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 16 | `parity_acpi_notifier_init` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 25 | `parity_register_acpi_notifier` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 52 | `parity_unregister_acpi_notifier` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 73 | `parity_acpi_notifier_call_chain` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |
| 107 | `parity_acpi_notifier_count` | global | `display/opregion.c` | I / 旧名で照合 | `display/opregion.h` |

### parity/opregion_vbt.c

現行: [parity/opregion_vbt.c](../../src/drivers/gpu/i915/parity/opregion_vbt.c)。3定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 6 | `le32` | static | `display/vbt.c` | S | — |
| 7 | `le64` | static | `display/vbt.c` | S | — |
| 10 | `parity_opregion_locate_vbt` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |

### parity/osdep/address_types.h

現行: [parity/osdep/address_types.h](../../src/drivers/gpu/i915/parity/osdep/address_types.h)。7定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 33 | `osdep_cpu_phys` | static | `memory.h` | S | `memory.h` |
| 34 | `osdep_dma_addr` | static | `memory.h` | S | `memory.h` |
| 35 | `osdep_gpu_vaddr` | static | `memory.h` | S | `memory.h` |
| 37 | `osdep_cpu_phys_raw` | static | `memory.h` | S | `memory.h` |
| 38 | `osdep_dma_addr_raw` | static | `memory.h` | S | `memory.h` |
| 39 | `osdep_gpu_vaddr_raw` | static | `memory.h` | S | `memory.h` |
| 47 | `osdep_dma_mapping_failed` | static | `memory.h` | S | `memory.h` |

### parity/osdep/dma.c

現行: [parity/osdep/dma.c](../../src/drivers/gpu/i915/parity/osdep/dma.c)。18定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 14 | `tr` | static | `memory.c` | S | — |
| 21 | `osdep_dma_device_init` | global | `memory.c` | I / 旧名で照合 | `memory.h` |
| 44 | `osdep_dma_set_info` | global | `memory.c` | I / 旧名で照合 | `memory.h` |
| 74 | `osdep_dma_address_bits` | global | `memory.c` | I / 旧名で照合 | `memory.h` |
| 80 | `osdep_dma_max_segment` | global | `memory.c` | I / 旧名で照合 | `memory.h` |
| 86 | `osdep_dma_is_coherent` | global | `memory.c` | I / 旧名で照合 | `memory.h` |
| 92 | `alloc_mapping` | static | `memory.c` | S | — |
| 112 | `map_sg_core` | static | `memory.c` | S | — |
| 155 | `osdep_dma_map_sg` | global | `memory.c` | I / 旧名で照合 | `memory.h` |
| 164 | `osdep_dma_map_sgtable` | global | `memory.c` | I / 旧名で照合 | `memory.h` |
| 175 | `osdep_dma_unmap_sg` | global | `memory.c` | I / 旧名で照合 | `memory.h` |
| 195 | `osdep_dma_map_page` | global | `memory.c` | I / 旧名で照合 | `memory.h` |
| 215 | `osdep_dma_unmap_page` | global | `memory.c` | I / 旧名で照合 | `memory.h` |
| 224 | `osdep_dma_pin` | global | `memory.c` | I / 旧名で照合 | `memory.h` |
| 235 | `osdep_dma_unpin` | global | `memory.c` | I / 旧名で照合 | `memory.h` |
| 244 | `osdep_dma_sync_for_device` | global | `memory.c` | I / 旧名で照合 | `memory.h` |
| 253 | `osdep_dma_sync_for_cpu` | global | `memory.c` | I / 旧名で照合 | `memory.h` |
| 262 | `osdep_dma_live_mappings` | global | `memory.c` | I / 旧名で照合 | `memory.h` |

### parity/osdep/firmware.c

現行: [parity/osdep/firmware.c](../../src/drivers/gpu/i915/parity/osdep/firmware.c)。4定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 36 | `osdep_firmware_test_set` | global | `firmware.c` | I / 旧名で照合 | `firmware.h` |
| 42 | `name_eq` | static | `firmware.c` | S | — |
| 49 | `osdep_request_firmware` | global | `firmware.c` | I / 旧名で照合 | `firmware.h` |
| 69 | `osdep_release_firmware` | global | `firmware.c` | I / 旧名で照合 | `firmware.h` |

### parity/osdep/mmio.c

現行: [parity/osdep/mmio.c](../../src/drivers/gpu/i915/parity/osdep/mmio.c)。18定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 11 | `tr` | static | `mmio.c` | S | — |
| 18 | `osdep_mmio_init` | global | `mmio.c` | I / 旧名で照合 | `mmio.h` |
| 40 | `osdep_mmio_domain_of` | global | `mmio.c` | I / 旧名で照合 | `mmio.h` |
| 51 | `osdep_fw_get` | global | `mmio.c` | I / 旧名で照合 | `mmio.h` |
| 79 | `osdep_fw_put` | global | `mmio.c` | I / 旧名で照合 | `mmio.h` |
| 101 | `osdep_fw_is_held` | global | `mmio.c` | I / 旧名で照合 | `mmio.h` |
| 109 | `osdep_mmio_read32_auto` | global | `mmio.c` | I / 旧名で照合 | `mmio.h` |
| 124 | `osdep_mmio_write32_auto` | global | `mmio.c` | I / 旧名で照合 | `mmio.h` |
| 136 | `osdep_mmio_read32` | global | `mmio.c` | I / 旧名で照合 | `mmio.h` |
| 149 | `osdep_mmio_write32` | global | `mmio.c` | I / 旧名で照合 | `mmio.h` |
| 161 | `osdep_mmio_raw_read32` | global | `mmio.c` | I / 旧名で照合 | `mmio.h` |
| 167 | `osdep_mmio_raw_write32` | global | `mmio.c` | I / 旧名で照合 | `mmio.h` |
| 173 | `osdep_mmio_posting_read32` | global | `mmio.c` | I / 旧名で照合 | `mmio.h` |
| 181 | `osdep_mmio_write32_masked` | global | `mmio.c` | I / 旧名で照合 | `mmio.h` |
| 197 | `osdep_mmio_write32_mask_enable` | global | `mmio.c` | I / 旧名で照合 | `mmio.h` |
| 210 | `osdep_mcr_lock` | global | `mmio.c` | I / 旧名で照合 | `mmio.h` |
| 224 | `osdep_mcr_unlock` | global | `mmio.c` | I / 旧名で照合 | `mmio.h` |
| 233 | `osdep_mcr_is_locked` | global | `mmio.c` | I / 旧名で照合 | `mmio.h` |

### parity/osdep/pci.c

現行: [parity/osdep/pci.c](../../src/drivers/gpu/i915/parity/osdep/pci.c)。22定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 14 | `tr` | static | `device.c` | S | — |
| 21 | `osdep_pci_init` | global | `device.c` | I / 旧名で照合 | `device.h` |
| 35 | `osdep_pci_read8` | global | `device.c` | I / 旧名で照合 | `device.h` |
| 36 | `osdep_pci_read16` | global | `device.c` | I / 旧名で照合 | `device.h` |
| 37 | `osdep_pci_read32` | global | `device.c` | I / 旧名で照合 | `device.h` |
| 38 | `osdep_pci_write8` | global | `device.c` | I / 旧名で照合 | `device.h` |
| 39 | `osdep_pci_write16` | global | `device.c` | I / 旧名で照合 | `device.h` |
| 40 | `osdep_pci_write32` | global | `device.c` | I / 旧名で照合 | `device.h` |
| 43 | `osdep_pci_find_capability` | global | `device.c` | I / 旧名で照合 | `device.h` |
| 64 | `osdep_pci_bar_kind` | global | `device.c` | I / 旧名で照合 | `device.h` |
| 86 | `osdep_pci_set_power_state` | global | `device.c` | I / 旧名で照合 | `device.h` |
| 104 | `wanted_decode` | static | `device.c` | S | — |
| 121 | `osdep_pci_enable_device` | global | `device.c` | I / 旧名で照合 | `device.h` |
| 150 | `osdep_pci_disable_device` | global | `device.c` | I / 旧名で照合 | `device.h` |
| 168 | `osdep_pci_is_enabled` | global | `device.c` | I / 旧名で照合 | `device.h` |
| 174 | `osdep_pci_restore` | global | `device.c` | I / 旧名で照合 | `device.h` |
| 185 | `osdep_pci_set_bus_master` | global | `device.c` | I / 旧名で照合 | `device.h` |
| 201 | `msi_write_message` | static | `device.c` | S | — |
| 212 | `msi_set_enable` | static | `device.c` | S | — |
| 224 | `osdep_pci_setup_msi` | global | `device.c` | I / 旧名で照合 | `device.h` |
| 266 | `osdep_pci_teardown_msi` | global | `device.c` | I / 旧名で照合 | `device.h` |
| 283 | `osdep_pci_msi_enabled` | global | `device.c` | I / 旧名で照合 | `device.h` |

### parity/osdep/runtime_pm.c

現行: [parity/osdep/runtime_pm.c](../../src/drivers/gpu/i915/parity/osdep/runtime_pm.c)。11定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 5 | `tr` | static | `power.c` | S | — |
| 12 | `osdep_rpm_init_early` | global | `power.c` | I / 旧名で照合 | `power.h` |
| 29 | `osdep_rpm_enable` | global | `power.c` | I / 旧名で照合 | `power.h` |
| 36 | `osdep_rpm_is_enabled` | global | `power.c` | I / 旧名で照合 | `power.h` |
| 42 | `osdep_rpm_get_noresume` | global | `power.c` | I / 旧名で照合 | `power.h` |
| 49 | `do_resume` | static | `power.c` | S | — |
| 64 | `osdep_rpm_get_sync` | global | `power.c` | I / 旧名で照合 | `power.h` |
| 80 | `osdep_rpm_resume_and_get` | global | `power.c` | I / 旧名で照合 | `power.h` |
| 97 | `osdep_rpm_put` | global | `power.c` | I / 旧名で照合 | `power.h` |
| 113 | `osdep_rpm_usage` | global | `power.c` | I / 旧名で照合 | `power.h` |
| 119 | `osdep_rpm_active` | global | `power.c` | I / 旧名で照合 | `power.h` |

### parity/osdep/sync.c

現行: [parity/osdep/sync.c](../../src/drivers/gpu/i915/parity/osdep/sync.c)。17定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 5 | `tr` | static | `sync.c` | S | — |
| 14 | `osdep_completion_init` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 22 | `osdep_reinit_completion` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 30 | `osdep_complete` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 38 | `osdep_complete_all` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 45 | `osdep_completion_done` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 51 | `try_consume` | static | `sync.c` | S | — |
| 63 | `osdep_wait_for_completion_timeout` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 87 | `osdep_work_init` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 99 | `osdep_workqueue_init` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 113 | `osdep_queue_work` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 137 | `remove_pending` | static | `sync.c` | S | — |
| 161 | `osdep_cancel_work` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 172 | `osdep_cancel_work_sync` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 189 | `osdep_flush_workqueue` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 217 | `osdep_workqueue_pending` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 223 | `osdep_work_is_running` | global | `sync.c` | I / 旧名で照合 | `sync.h` |

### parity/osdep/trace.c

現行: [parity/osdep/trace.c](../../src/drivers/gpu/i915/parity/osdep/trace.c)。5定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 5 | `osdep_trace_init` | global | `trace.c` | I / 旧名で照合 | `trace.h` |
| 24 | `osdep_trace_emit` | global | `trace.c` | I / 旧名で照合 | `trace.h` |
| 50 | `osdep_trace_count` | global | `trace.c` | I / 旧名で照合 | `trace.h` |
| 58 | `osdep_trace_snapshot` | global | `trace.c` | I / 旧名で照合 | `trace.h` |
| 77 | `osdep_trace_op_name` | global | `trace.c` | I / 旧名で照合 | `trace.h` |

### parity/pch.c

現行: [parity/pch.c](../../src/drivers/gpu/i915/parity/pch.c)。6定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 50 | `parity_pch_test_set_bridges` | global | `device-info.c` | I / 旧名で照合 | `device-info.h` |
| 56 | `parity_intel_pch_type` | global | `device-info.c` | I / 旧名で照合 | `device-info.h` |
| 128 | `parity_intel_is_virt_pch` | global | `device-info.c` | I / 旧名で照合 | `device-info.h` |
| 144 | `intel_virt_detect_pch` | static | `device-info.c` | S | — |
| 162 | `next_isa_bridge` | static | `device-info.c` | S | — |
| 195 | `parity_intel_detect_pch` | global | `device-info.c` | I / 旧名で照合 | `device-info.h` |

### parity/pcode.c

現行: [parity/pcode.c](../../src/drivers/gpu/i915/parity/pcode.c)。8定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 31 | `pcode_check_status` | static | `power.c` | S | — |
| 48 | `parity_snb_pcode_rw` | static | `power.c` | S | — |
| 77 | `parity_pcode_read` | global | `power.c` | I / 旧名で照合 | `power.h` |
| 92 | `parity_snb_pcode_write_timeout` | global | `power.c` | I / 旧名で照合 | `power.h` |
| 108 | `parity_snb_pcode_write` | global | `power.c` | I / 旧名で照合 | `power.h` |
| 120 | `parity_skl_pcode_try_request` | static | `power.c` | S | — |
| 132 | `parity_pcode_poll` | static | `power.c` | S | — |
| 157 | `parity_skl_pcode_request` | global | `power.c` | I / 旧名で照合 | `power.h` |

### parity/power_domains.c

現行: [parity/power_domains.c](../../src/drivers/gpu/i915/parity/power_domains.c)。46定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 75 | `pw_dom` | static | `display/power.c` | S | — |
| 82 | `get_allowed_dc_mask` | static | `display/power.c` | S | — |
| 95 | `sanitize_target_dc_state` | static | `display/power.c` | S | — |
| 112 | `sanitize_disable_power_well` | static | `display/power.c` | S | — |
| 120 | `pw_add` | static | `display/power.c` | S | — |
| 155 | `power_map_init` | static | `display/power.c` | S | — |
| 293 | `power_map_init_tgl` | static | `display/power.c` | S | — |
| 417 | `parity_intel_power_domains_init` | global | `display/power.c` | I / 旧名で照合 | `display/power.h` |
| 478 | `parity_intel_power_domains_cleanup` | global | `display/power.c` | I / 旧名で照合 | `display/power.h` |
| 490 | `parity_power_domain_wells` | global | `display/power.c` | I / 旧名で照合 | `display/power.h` |
| 499 | `parity_power_well_by_id` | global | `display/power.c` | I / 旧名で照合 | `display/power.h` |
| 516 | `pw_driver_reg` | static | `display/power.c` | S | — |
| 525 | `pw_req` | static | `display/power.c` | S | — |
| 526 | `pw_state` | static | `display/power.c` | S | — |
| 538 | `pw_wait_fuse` | static | `display/power.c` | S | — |
| 554 | `pw_post_enable` | static | `display/power.c` | S | — |
| 576 | `parity_power_well_enable` | global | `display/power.c` | I / 旧名で照合 | `display/power.h` |
| 650 | `parity_power_well_disable` | global | `display/power.c` | I / 旧名で照合 | `display/power.h` |
| 731 | `parity_power_well_is_enabled` | global | `display/power.c` | I / 旧名で照合 | `display/power.h` |
| 753 | `parity_power_well_sync_hw` | global | `display/power.c` | I / 旧名で照合 | `display/power.h` |
| 785 | `parity_power_well_get` | global | `display/power.c` | I / 旧名で照合 | `display/power.h` |
| 799 | `parity_power_well_put` | global | `display/power.c` | I / 旧名で照合 | `display/power.h` |
| 818 | `parity_display_power_is_enabled` | global | `display/power.c` | I / 旧名で照合 | `display/power.h` |
| 851 | `parity_power_domain_hw_state_on` | global | `display/power.c` | I / 旧名で照合 | `display/power.h` |
| 872 | `mask_test` | static | `display/power.c` | S | — |
| 878 | `mask_set` | static | `display/power.c` | S | — |
| 884 | `mask_clear` | static | `display/power.c` | S | — |
| 890 | `mask_empty` | static | `display/power.c` | S | — |
| 898 | `verify_async_put_domains_state` | static | `display/power.c` | S | — |
| 926 | `cancel_async_put_work` | static | `display/power.c` | S | — |
| 935 | `grab_async_put_ref` | static | `display/power.c` | S | — |
| 956 | `get_domain_locked` | static | `display/power.c` | S | — |
| 984 | `put_domain_locked` | static | `display/power.c` | S | — |
| 1007 | `parity_display_power_get` | global | `display/power.c` | I / 旧名で照合 | `display/power.h` |
| 1019 | `parity_display_power_put` | global | `display/power.c` | I / 旧名で照合 | `display/power.h` |
| 1028 | `parity_display_power_async_bind` | global | `display/power.c` | I / 旧名で照合 | `display/power.h` |
| 1040 | `queue_async_put_domains_work` | static | `display/power.c` | S | — |
| 1053 | `release_async_put_domains` | static | `display/power.c` | S | — |
| 1071 | `parity_display_power_put_async` | global | `display/power.c` | I / 旧名で照合 | `display/power.h` |
| 1103 | `parity_display_power_async_work` | global | `display/power.c` | I / 旧名で照合 | `display/power.h` |
| 1135 | `parity_display_power_flush_work` | global | `display/power.c` | I / 旧名で照合 | `display/power.h` |
| 1155 | `parity_display_power_flush_work_sync` | global | `display/power.c` | I / 旧名で照合 | `display/power.h` |
| 1170 | `parity_intel_pmdemand_init_early` | global | `display/power.c` | I / 旧名で照合 | `display/power.h` |
| 1179 | `gen9_dc_mask` | static | `display/power.c` | S | — |
| 1195 | `gen9_write_dc_state` | static | `display/power.c` | S | — |
| 1218 | `parity_gen9_set_dc_state` | global | `display/power.c` | I / 旧名で照合 | `display/power.h` |

### parity/probe.c

現行: [parity/probe.c](../../src/drivers/gpu/i915/parity/probe.c)。7定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 68 | `parity_dump_trace` | static | `trace.c` | I / 旧名で照合 | `trace.h` |
| 96 | `devid_is_tigerlake` | static | `device-info.c` | I / 旧名で照合 | `device-info.h` |
| 108 | `n0_pipe_powered` | static | `display/takeover.c` | S | — |
| 118 | `outcome_name` | static | `trace.c` | I / 旧名で照合 | `trace.h` |
| 129 | `popcount32` | static | `device.c` | S | — |
| 138 | `parity_cpu_phys_bits` | static | `device-info.c` | I / 旧名で照合 | `device-info.h` |
| 153 | `drv_i915_parity_attach` | global | `device.c` | I / 旧名で照合 | `device.h` |

### parity/pte.c

現行: [parity/pte.c](../../src/drivers/gpu/i915/parity/pte.c)。4定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 5 | `parity_dma_in_range` | global | `ppgtt.c` | I / 旧名で照合 | `ppgtt.h` |
| 18 | `encode_common` | static | `ppgtt.c` | S | — |
| 32 | `parity_ggtt_pte_encode` | global | `ppgtt.c` | I / 旧名で照合 | `ppgtt.h` |
| 39 | `parity_ppgtt_pte_encode` | global | `ppgtt.c` | I / 旧名で照合 | `ppgtt.h` |

### parity/pxp.c

現行: [parity/pxp.c](../../src/drivers/gpu/i915/parity/pxp.c)。3定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 14 | `fail` | static | `device.c` | S | — |
| 24 | `parity_intel_pxp_init` | global | `device.c` | I / 旧名で照合 | `device.h` |
| 77 | `parity_intel_pxp_fini` | global | `device.c` | I / 旧名で照合 | `device.h` |

### parity/reset.c

現行: [parity/reset.c](../../src/drivers/gpu/i915/parity/reset.c)。1定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 23 | `parity_gt_reset_all` | global | `reset.c` | I / 旧名で照合 | `reset.h` |

### parity/resident_display.c

現行: [parity/resident_display.c](../../src/drivers/gpu/i915/parity/resident_display.c)。15定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 62 | `rd_init` | static | `display/display.c` | I / 旧名で照合 | `display/display.h` |
| 72 | `rd_panel` | static | `display/display.c` | I / 旧名で照合 | `display/display.h` |
| 84 | `rd_device_query` | static | `display/scanout.c` | O | —（ops経由） |
| 94 | `rd_constraints` | static | `display/scanout.c` | O | —（ops経由） |
| 114 | `rd_query` | static | `display/display.c` | O | —（ops経由） |
| 157 | `rd_mode` | static | `display/display.c` | O | —（ops経由） |
| 202 | `rd_claim` | static | `display/display.c` | O | —（ops経由） |
| 228 | `rd_release_locked` | static | `display/present.c` | I / 旧名で照合 | `display/present.h` |
| 245 | `rd_release` | static | `display/present.c` | O | —（ops経由） |
| 268 | `parity_shim_blit_source` | global | `tests/render/readback.c` | T | `tests/render/readback.h` |
| 279 | `rd_blit_build` | static | `display/present.c` | S | — |
| 306 | `rd_present` | static | `display/present.c` | O | —（ops経由） |
| 373 | `rd_wait` | static | `display/present.c` | O | —（ops経由） |
| 395 | `rd_events` | static | `display/hotplug.c` | O | —（ops経由） |
| 413 | `drv_i915_resident_display_close` | global | `display/display.c` | H / `drv_i915_display_session_close` | `display/display.h` |

### parity/runner.c

現行: [parity/runner.c](../../src/drivers/gpu/i915/parity/runner.c)。6定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 59 | `ensure_lock` | static | `device.c` | S | — |
| 68 | `probe_status_name` | static | `tests/execution/runner.c` | T | — |
| 79 | `runner_thread` | static | `device.c` | S | — |
| 160 | `try_launch` | static | `device.c` | S | — |
| 191 | `drv_i915_parity_runner_register` | global | `device.c` | H / `drv_i915_device_schedule_start` | `device.h` |
| 205 | `drv_i915_parity_runner_start` | global | `device.c` | E / `drv_i915_runtime_ready` | `include/drivers/i915.h` |

### parity/tests/dma_contract_test.c

現行: [parity/tests/dma_contract_test.c](../../src/drivers/gpu/i915/parity/tests/dma_contract_test.c)。2定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 22 | `trace_has_order` | static | `tests/fixtures/dma-contract-test.c` | T | — |
| 39 | `main` | global | `tests/fixtures/dma-contract-test.c` | T | `tests/fixtures/dma-contract-test.h` |

### parity/tests/mmio_contract_test.c

現行: [parity/tests/mmio_contract_test.c](../../src/drivers/gpu/i915/parity/tests/mmio_contract_test.c)。1定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 17 | `main` | global | `tests/fixtures/mmio-contract-test.c` | T | `tests/fixtures/mmio-contract-test.h` |

### parity/tests/mock_dma.c

現行: [parity/tests/mock_dma.c](../../src/drivers/gpu/i915/parity/tests/mock_dma.c)。12定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 19 | `reverse24` | static | `tests/fixtures/mock-dma.c` | T | — |
| 32 | `mock_dma_translate` | global | `tests/fixtures/mock-dma.c` | T | `tests/fixtures/mock-dma.h` |
| 40 | `mock_dma_untranslate` | global | `tests/fixtures/mock-dma.c` | T | `tests/fixtures/mock-dma.h` |
| 49 | `mock_set_info` | static | `tests/fixtures/mock-dma.c` | T | — |
| 60 | `mock_map_sg` | static | `tests/fixtures/mock-dma.c` | T | — |
| 97 | `mock_unmap_sg` | static | `tests/fixtures/mock-dma.c` | T | — |
| 110 | `mock_map_page` | static | `tests/fixtures/mock-dma.c` | T | — |
| 124 | `mock_unmap_page` | static | `tests/fixtures/mock-dma.c` | T | — |
| 135 | `mock_sync_for_device` | static | `tests/fixtures/mock-dma.c` | T | — |
| 146 | `mock_sync_for_cpu` | static | `tests/fixtures/mock-dma.c` | T | — |
| 157 | `mock_dma_backend` | global | `tests/fixtures/mock-dma.c` | T | `tests/fixtures/mock-dma.h` |
| 176 | `mock_dma_reset` | global | `tests/fixtures/mock-dma.c` | T | `tests/fixtures/mock-dma.h` |

### parity/tests/mock_mmio.c

現行: [parity/tests/mock_mmio.c](../../src/drivers/gpu/i915/parity/tests/mock_mmio.c)。10定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 16 | `find` | static | `tests/fixtures/mock-mmio.c` | T | — |
| 27 | `slot` | static | `tests/fixtures/mock-mmio.c` | T | — |
| 43 | `m_raw_read32` | static | `tests/fixtures/mock-mmio.c` | T | — |
| 54 | `m_raw_write32` | static | `tests/fixtures/mock-mmio.c` | T | — |
| 66 | `m_fw_request` | static | `tests/fixtures/mock-mmio.c` | T | — |
| 75 | `m_fw_ack` | static | `tests/fixtures/mock-mmio.c` | T | — |
| 85 | `mock_mmio_backend` | global | `tests/fixtures/mock-mmio.c` | T | `tests/fixtures/mock-mmio.h` |
| 98 | `mock_mmio_reset` | global | `tests/fixtures/mock-mmio.c` | T | `tests/fixtures/mock-mmio.h` |
| 118 | `mock_mmio_preset` | global | `tests/fixtures/mock-mmio.c` | T | `tests/fixtures/mock-mmio.h` |
| 126 | `mock_mmio_peek` | global | `tests/fixtures/mock-mmio.c` | T | `tests/fixtures/mock-mmio.h` |

### parity/tests/mock_pci.c

現行: [parity/tests/mock_pci.c](../../src/drivers/gpu/i915/parity/tests/mock_pci.c)。19定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 5 | `m_read8` | static | `tests/fixtures/mock-pci.c` | T | — |
| 12 | `m_read16` | static | `tests/fixtures/mock-pci.c` | T | — |
| 21 | `m_read32` | static | `tests/fixtures/mock-pci.c` | T | — |
| 31 | `m_write8` | static | `tests/fixtures/mock-pci.c` | T | — |
| 40 | `m_write16` | static | `tests/fixtures/mock-pci.c` | T | — |
| 51 | `m_write32` | static | `tests/fixtures/mock-pci.c` | T | — |
| 64 | `m_alloc_msi_vector` | static | `tests/fixtures/mock-pci.c` | T | — |
| 80 | `m_free_msi_vector` | static | `tests/fixtures/mock-pci.c` | T | — |
| 89 | `mock_pci_backend` | global | `tests/fixtures/mock-pci.c` | T | `tests/fixtures/mock-pci.h` |
| 101 | `base` | static | `tests/fixtures/mock-pci.c` | T | — |
| 121 | `mem_bar` | static | `tests/fixtures/mock-pci.c` | T | — |
| 132 | `io_bar` | static | `tests/fixtures/mock-pci.c` | T | — |
| 143 | `pm_cap` | static | `tests/fixtures/mock-pci.c` | T | — |
| 151 | `msi_cap` | static | `tests/fixtures/mock-pci.c` | T | — |
| 160 | `pcie_cap` | static | `tests/fixtures/mock-pci.c` | T | — |
| 167 | `mock_pci_setup_full` | global | `tests/fixtures/mock-pci.c` | T | `tests/fixtures/mock-pci.h` |
| 178 | `mock_pci_setup_no_msi` | global | `tests/fixtures/mock-pci.c` | T | `tests/fixtures/mock-pci.h` |
| 188 | `mock_pci_setup_no_pm` | global | `tests/fixtures/mock-pci.c` | T | `tests/fixtures/mock-pci.h` |
| 198 | `mock_pci_setup_io_and_mem` | global | `tests/fixtures/mock-pci.c` | T | `tests/fixtures/mock-pci.h` |

### parity/tests/pci_contract_test.c

現行: [parity/tests/pci_contract_test.c](../../src/drivers/gpu/i915/parity/tests/pci_contract_test.c)。1定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 19 | `main` | global | `tests/fixtures/pci-contract-test.c` | T | `tests/fixtures/pci-contract-test.h` |

### parity/tests/pte_contract_test.c

現行: [parity/tests/pte_contract_test.c](../../src/drivers/gpu/i915/parity/tests/pte_contract_test.c)。1定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 21 | `main` | global | `tests/fixtures/pte-contract-test.c` | T | `tests/fixtures/pte-contract-test.h` |

### parity/tests/rpm_contract_test.c

現行: [parity/tests/rpm_contract_test.c](../../src/drivers/gpu/i915/parity/tests/rpm_contract_test.c)。3定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 18 | `m_resume` | static | `tests/fixtures/rpm-contract-test.c` | T | — |
| 19 | `m_suspend` | static | `tests/fixtures/rpm-contract-test.c` | T | — |
| 22 | `main` | global | `tests/fixtures/rpm-contract-test.c` | T | `tests/fixtures/rpm-contract-test.h` |

### parity/tests/sync_contract_test.c

現行: [parity/tests/sync_contract_test.c](../../src/drivers/gpu/i915/parity/tests/sync_contract_test.c)。5定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 20 | `tick_complete` | static | `tests/fixtures/sync-contract-test.c` | T | — |
| 29 | `work_fn` | static | `tests/fixtures/sync-contract-test.c` | T | — |
| 34 | `work_cancel_other` | static | `tests/fixtures/sync-contract-test.c` | T | — |
| 43 | `work_requeue_self` | static | `tests/fixtures/sync-contract-test.c` | T | — |
| 51 | `main` | global | `tests/fixtures/sync-contract-test.c` | T | `tests/fixtures/sync-contract-test.h` |

### parity/timer_calc.c

現行: [parity/timer_calc.c](../../src/drivers/gpu/i915/parity/timer_calc.c)。5定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 9 | `parity_timer_calc_init` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 19 | `parity_timer_set_sleep` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 28 | `parity_timer_clear_sleep` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 35 | `parity_timer_next_event` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 43 | `parity_timer_on_fire` | global | `sync.c` | I / 旧名で照合 | `sync.h` |

### parity/vbt/intel_bios_port.c

現行: [parity/vbt/intel_bios_port.c](../../src/drivers/gpu/i915/parity/vbt/intel_bios_port.c)。91定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 87 | `_get_blocksize` | static | `display/vbt.c` | S | — |
| 97 | `get_blocksize` | static | `display/vbt.c` | S | — |
| 103 | `find_raw_section` | static | `display/vbt.c` | S | — |
| 137 | `raw_block_offset` | static | `display/vbt.c` | S | — |
| 155 | `bdb_find_section` | static | `display/vbt.c` | S | — |
| 210 | `lfp_data_min_size` | static | `display/vbt.c` | S | — |
| 227 | `validate_lfp_data_ptrs` | static | `display/vbt.c` | S | — |
| 321 | `fixup_lfp_data_ptrs` | static | `display/vbt.c` | S | — |
| 350 | `make_lfp_data_ptr` | static | `display/vbt.c` | S | — |
| 362 | `next_lfp_data_ptr` | static | `display/vbt.c` | S | — |
| 370 | `generate_lfp_data_ptrs` | static | `display/vbt.c` | S | — |
| 461 | `init_bdb_block` | static | `display/vbt.c` | S | — |
| 518 | `init_bdb_blocks` | static | `display/vbt.c` | S | — |
| 535 | `fill_detail_timing_data` | static | `display/vbt.c` | S | — |
| 592 | `get_lvds_dvo_timing` | static | `display/vbt.c` | S | — |
| 600 | `get_lvds_fp_timing` | static | `display/vbt.c` | S | — |
| 608 | `get_lvds_pnp_id` | static | `display/vbt.c` | S | — |
| 616 | `get_lfp_data_tail` | static | `display/vbt.c` | S | — |
| 625 | `dump_pnp_id` | static | `display/vbt.c` | S | — |
| 638 | `opregion_get_panel_type` | static | `display/vbt.c` | S | — |
| 645 | `vbt_get_panel_type` | static | `display/vbt.c` | S | — |
| 670 | `pnpid_get_panel_type` | static | `display/vbt.c` | S | — |
| 720 | `fallback_get_panel_type` | static | `display/vbt.c` | S | — |
| 734 | `get_panel_type` | static | `display/vbt.c` | S | — |
| 793 | `panel_bits` | static | `display/vbt.c` | S | — |
| 798 | `panel_bool` | static | `display/vbt.c` | S | — |
| 805 | `parse_panel_options` | static | `display/vbt.c` | S | — |
| 852 | `parse_lfp_panel_dtd` | static | `display/vbt.c` | S | — |
| 893 | `parse_lfp_data` | static | `display/vbt.c` | S | — |
| 934 | `parse_generic_dtd` | static | `display/vbt.c` | S | — |
| 1024 | `parse_lfp_backlight` | static | `display/vbt.c` | S | — |
| 1111 | `intel_bios_ssc_frequency` | static | `display/vbt.c` | S | — |
| 1126 | `parse_general_features` | static | `display/vbt.c` | S | — |
| 1168 | `child_device_ptr` | static | `display/vbt.c` | S | — |
| 1175 | `parse_driver_features` | static | `display/vbt.c` | S | — |
| 1211 | `parse_panel_driver_features` | static | `display/vbt.c` | S | — |
| 1245 | `parse_power_conservation_features` | static | `display/vbt.c` | S | — |
| 1288 | `parse_edp` | static | `display/vbt.c` | S | — |
| 1430 | `translate_iboost` | static | `display/vbt.c` | S | — |
| 1491 | `map_ddc_pin` | static | `display/vbt.c` | S | — |
| 1533 | `dvo_port_type` | static | `display/vbt.c` | S | — |
| 1566 | `__dvo_port_to_port` | static | `display/vbt.c` | S | — |
| 1585 | `dvo_port_to_port` | static | `display/vbt.c` | S | — |
| 1662 | `dsi_dvo_port_to_port` | static | `display/vbt.c` | S | — |
| 1677 | `intel_bios_encoder_port` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 1690 | `parse_bdb_230_dp_max_link_rate` | static | `display/vbt.c` | S | — |
| 1713 | `parse_bdb_216_dp_max_link_rate` | static | `display/vbt.c` | S | — |
| 1728 | `intel_bios_dp_max_link_rate` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 1739 | `intel_bios_dp_max_lane_count` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 1747 | `sanitize_device_type` | static | `display/vbt.c` | S | — |
| 1768 | `sanitize_hdmi_level_shift` | static | `display/vbt.c` | S | — |
| 1790 | `intel_bios_encoder_supports_crt` | static | `display/vbt.c` | S | — |
| 1796 | `intel_bios_encoder_supports_dvi` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 1802 | `intel_bios_encoder_supports_hdmi` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 1809 | `intel_bios_encoder_supports_dp` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 1815 | `intel_bios_encoder_supports_edp` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 1822 | `intel_bios_encoder_supports_dsi` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 1828 | `intel_bios_encoder_is_lspcon` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 1834 | `intel_bios_hdmi_level_shift` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 1843 | `intel_bios_hdmi_max_tmds_clock` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 1867 | `is_port_valid` | static | `display/vbt.c` | S | — |
| 1880 | `print_ddi_port` | static | `display/vbt.c` | S | — |
| 1951 | `parse_ddi_port` | static | `display/vbt.c` | S | — |
| 1971 | `has_ddi_port_info` | static | `display/vbt.c` | S | — |
| 1976 | `parse_ddi_ports` | static | `display/vbt.c` | S | — |
| 1991 | `parse_general_definitions` | static | `display/vbt.c` | S | — |
| 2091 | `init_vbt_defaults` | static | `display/vbt.c` | S | — |
| 2116 | `init_vbt_panel_defaults` | static | `display/vbt.c` | S | — |
| 2127 | `init_vbt_missing_defaults` | static | `display/vbt.c` | S | — |
| 2183 | `get_bdb_header` | static | `display/vbt.c` | S | — |
| 2197 | `intel_bios_is_valid_vbt` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 2250 | `intel_bios_init` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 2299 | `intel_bios_init_panel` | static | `display/vbt.c` | S | — |
| 2333 | `intel_bios_init_panel_early` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 2340 | `intel_bios_init_panel_late` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 2352 | `intel_bios_driver_remove` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 2369 | `intel_bios_fini_panel` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 2394 | `intel_bios_is_port_present` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 2414 | `intel_bios_encoder_supports_dp_dual_mode` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 2486 | `map_aux_ch` | static | `display/vbt.c` | S | — |
| 2517 | `intel_bios_dp_aux_ch` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 2525 | `intel_bios_dp_has_shared_aux_ch` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 2546 | `intel_bios_dp_boost_level` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 2554 | `intel_bios_hdmi_boost_level` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 2562 | `intel_bios_hdmi_ddc_pin` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 2570 | `intel_bios_encoder_supports_typec_usb` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 2575 | `intel_bios_encoder_supports_tbt` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 2580 | `intel_bios_encoder_lane_reversal` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 2585 | `intel_bios_encoder_hpd_invert` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 2591 | `intel_bios_encoder_data_lookup` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 2603 | `intel_bios_for_each_encoder` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |

### parity/vbt/parity_vbt_glue.inc

現行: [parity/vbt/parity_vbt_glue.inc](../../src/drivers/gpu/i915/parity/vbt/parity_vbt_glue.inc)。12定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 23 | `parity_vbt_zalloc` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 41 | `parity_vbt_free` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 48 | `parity_vbt_provider_get` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 55 | `parity_vbt_set_log_level` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 61 | `parity_vbt_note` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 70 | `parity_vbt_log_enabled` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 78 | `drm_mode_set_name` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 98 | `parity_vbt_validate` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 104 | `parity_vbt_init` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 184 | `parity_vbt_encoder_for_port` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 197 | `parity_vbt_init_panel` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |
| 264 | `parity_vbt_fini` | global | `display/vbt.c` | I / 旧名で照合 | `display/vbt.h` |

### parity/vbt/vbt_compat.h

現行: [parity/vbt/vbt_compat.h](../../src/drivers/gpu/i915/parity/vbt/vbt_compat.h)。11定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 68 | `INIT_LIST_HEAD` | static | `display/vbt.h` | S | `display/vbt.h` |
| 69 | `list_empty` | static | `display/vbt.h` | S | `display/vbt.h` |
| 70 | `list_add_tail` | static | `display/vbt.h` | S | `display/vbt.h` |
| 74 | `list_del` | static | `display/vbt.h` | S | `display/vbt.h` |
| 95 | `kmemdup` | static | `display/vbt.h` | S | `display/vbt.h` |
| 243 | `drm_edid_raw` | static | `display/vbt.h` | S | `display/vbt.h` |
| 245 | `drm_edid_decode_mfg_id` | static | `display/vbt.h` | S | `display/vbt.h` |
| 288 | `intel_port_to_phy` | static | `display/vbt.h` | S | `display/vbt.h` |
| 295 | `intel_phy_is_tc` | static | `display/vbt.h` | S | `display/vbt.h` |
| 301 | `intel_gmbus_is_valid_pin` | static | `display/vbt.h` | S | `display/vbt.h` |
| 307 | `intel_opregion_get_panel_type` | static | `display/vbt.h` | S | `display/vbt.h` |

### parity/vga.c

現行: [parity/vga.c](../../src/drivers/gpu/i915/parity/vga.c)。12定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 33 | `parity_intel_gmch_vga_set_state` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 63 | `parity_intel_gmch_vga_set_decode` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 80 | `parity_vga_client_register` | static | `display/takeover.c` | S | — |
| 92 | `parity_intel_vga_register` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 126 | `parity_intel_vga_unregister` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 149 | `parity_vga_io_test_set` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 155 | `vga_get_legacy_io` | static | `display/takeover.c` | S | — |
| 161 | `vga_in8` | static | `display/takeover.c` | S | — |
| 166 | `vga_out8` | static | `display/takeover.c` | S | — |
| 172 | `vga_put_legacy_io` | static | `display/takeover.c` | S | — |
| 178 | `parity_intel_vga_reset_io_mem` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |
| 200 | `parity_intel_vga_disable` | global | `display/takeover.c` | I / 旧名で照合 | `display/takeover.h` |

### parity/wait.c

現行: [parity/wait.c](../../src/drivers/gpu/i915/parity/wait.c)。11定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 46 | `parity_time_base_set_fault` | static | `sync.c` | S | — |
| 55 | `parity_wait_time_base_faulted` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 61 | `parity_wait_time_base_ok` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 72 | `parity_wait_test_set` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 79 | `parity_wait_test_reset_fault` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 85 | `time_read` | static | `sync.c` | S | — |
| 93 | `time_sleep` | static | `sync.c` | S | — |
| 105 | `us_to_ticks` | static | `sync.c` | S | — |
| 126 | `read_counter_consistent` | static | `sync.c` | S | — |
| 138 | `parity_udelay` | global | `sync.c` | I / 旧名で照合 | `sync.h` |
| 158 | `parity_wait_reg` | global | `sync.c` | I / 旧名で照合 | `sync.h` |

### ppgtt.c

現行: [ppgtt.c](../../src/drivers/gpu/i915/ppgtt.c)。12定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 52 | `drv_i915_ppgtt_create` | global | `ppgtt.c` | I / 旧名で照合 | `ppgtt.h` |
| 104 | `drv_i915_ppgtt_destroy` | global | `ppgtt.c` | I / 旧名で照合 | `ppgtt.h` |
| 141 | `drv_i915_ppgtt_va_alloc` | global | `ppgtt.c` | I / 旧名で照合 | `ppgtt.h` |
| 174 | `drv_i915_ppgtt_insert` | global | `ppgtt.c` | I / 旧名で照合 | `ppgtt.h` |
| 188 | `drv_i915_ppgtt_insert_uncached` | global | `ppgtt.c` | I / 旧名で照合 | `ppgtt.h` |
| 199 | `i915_ppgtt_insert_bits` | static | `ppgtt.c` | S | — |
| 245 | `drv_i915_ppgtt_clear` | global | `ppgtt.c` | I / 旧名で照合 | `ppgtt.h` |
| 273 | `drv_i915_ppgtt_lookup` | global | `ppgtt.c` | I / 旧名で照合 | `ppgtt.h` |
| 289 | `i915_ppgtt_page_alloc` | static | `ppgtt.c` | S | — |
| 320 | `i915_ppgtt_table` | static | `ppgtt.c` | S | — |
| 335 | `i915_ppgtt_walk` | static | `ppgtt.c` | S | — |
| 392 | `i915_ppgtt_fill` | static | `ppgtt.c` | S | — |

### request.c

現行: [request.c](../../src/drivers/gpu/i915/request.c)。11定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 64 | `drv_i915_request_alloc` | global | `request.c` | I / 旧名で照合 | `request.h` |
| 105 | `drv_i915_request_release` | global | `request.c` | I / 旧名で照合 | `request.h` |
| 118 | `drv_i915_request_queue` | global | `request.c` | I / 旧名で照合 | `request.h` |
| 139 | `drv_i915_request_kick` | global | `request.c` | I / 旧名で照合 | `request.h` |
| 193 | `drv_i915_request_retire` | global | `request.c` | I / 旧名で照合 | `request.h` |
| 227 | `drv_i915_request_fail` | global | `request.c` | I / 旧名で照合 | `request.h` |
| 283 | `drv_i915_request_complete_list` | global | `request.c` | I / 旧名で照合 | `request.h` |
| 337 | `i915_request_emit` | static | `request.c` | S | — |
| 371 | `i915_request_emit_prologue` | static | `request.c` | S | — |
| 424 | `i915_request_emit_breadcrumb` | static | `request.c` | S | — |
| 476 | `i915_request_unlink` | static | `request.c` | S | — |

### selftest.c

現行: [selftest.c](../../src/drivers/gpu/i915/selftest.c)。42定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 48 | `drv_i915_selftest` | global | `tests/execution/selftest.c` | T | `tests/execution/selftest.h` |
| 140 | `drv_i915_clear_selftest` | global | `tests/execution/selftest.c` | T | `tests/execution/selftest.h` |
| 249 | `drv_i915_rcs_selftest` | global | `tests/execution/selftest.c` | T | `tests/execution/selftest.h` |
| 320 | `drv_i915_rt_selftest` | global | `tests/execution/selftest.c` | T | `tests/execution/selftest.h` |
| 425 | `i915_selftest_clflush` | static | `tests/execution/selftest.c` | T | — |
| 551 | `i915_draw_apply_engine_workarounds` | static | `tests/execution/selftest.c` | T | — |
| 595 | `i915_draw_fill_eot` | static | `tests/execution/selftest.c` | T | — |
| 614 | `i915_draw_emit` | static | `tests/execution/selftest.c` | T | — |
| 628 | `i915_draw_emit_disabled` | static | `tests/execution/selftest.c` | T | — |
| 642 | `i915_draw_emit_marker` | static | `tests/execution/selftest.c` | T | — |
| 655 | `i915_draw_emit_pipe_control` | static | `tests/execution/selftest.c` | T | — |
| 676 | `i915_draw_write_surface_state` | static | `tests/execution/selftest.c` | T | — |
| 713 | `i915_draw_write_dynamic_state` | static | `tests/execution/selftest.c` | T | — |
| 744 | `i915_draw_write_vertices` | static | `tests/execution/selftest.c` | T | — |
| 761 | `i915_draw_emit_vertex_state` | static | `tests/execution/selftest.c` | T | — |
| 829 | `i915_draw_emit_urb` | static | `tests/execution/selftest.c` | T | — |
| 876 | `i915_draw_emit_raster_state` | static | `tests/execution/selftest.c` | T | — |
| 959 | `i915_draw_emit_depth_state` | static | `tests/execution/selftest.c` | T | — |
| 990 | `i915_draw_build_batch` | static | `tests/execution/selftest.c` | T | — |
| 1200 | `drv_i915_draw_fixture_write_state` | global | `tests/execution/selftest.c` | T | `tests/execution/selftest.h` |
| 1214 | `drv_i915_draw_fixture_build_batch` | global | `tests/execution/selftest.c` | T | `tests/execution/selftest.h` |
| 1253 | `drv_i915_tex_fixture_write_state` | global | `tests/execution/selftest.c` | T | `tests/execution/selftest.h` |
| 1283 | `drv_i915_tex_fixture_write_state_ab_filter` | global | `tests/execution/selftest.c` | T | `tests/execution/selftest.h` |
| 1295 | `drv_i915_tex_fixture_expected_pixel_linear` | global | `tests/execution/selftest.c` | T | `tests/execution/selftest.h` |
| 1328 | `drv_i915_tex_fixture_write_state_ab` | global | `tests/execution/selftest.c` | T | `tests/execution/selftest.h` |
| 1343 | `drv_i915_tex_fixture_build_batch` | global | `tests/execution/selftest.c` | T | `tests/execution/selftest.h` |
| 1373 | `drv_i915_tex_fixture_fhd_write_state` | global | `tests/execution/selftest.c` | T | `tests/execution/selftest.h` |
| 1401 | `drv_i915_tex_fixture_fhd_build_batch` | global | `tests/execution/selftest.c` | T | `tests/execution/selftest.h` |
| 1415 | `drv_i915_tex_fixture_fhd_rt_rss` | global | `tests/execution/selftest.c` | T | `tests/execution/selftest.h` |
| 1421 | `drv_i915_tex_fixture_fhd_ps_bytes` | global | `tests/execution/selftest.c` | T | `tests/execution/selftest.h` |
| 1428 | `drv_i915_tex_fixture_fhd_same_texture_state` | global | `tests/execution/selftest.c` | T | `tests/execution/selftest.h` |
| 1438 | `drv_i915_tex_fixture_pattern` | global | `tests/execution/selftest.c` | T | `tests/execution/selftest.h` |
| 1469 | `drv_i915_tex_fixture_expected_pixel` | global | `tests/execution/selftest.c` | T | `tests/execution/selftest.h` |
| 1479 | `drv_i915_draw_fixture_mocs` | global | `tests/execution/selftest.c` | T | `tests/execution/selftest.h` |
| 1492 | `i915_draw_read_statistics` | static | `tests/execution/selftest.c` | T | — |
| 1604 | `i915_compute_emit_sba` | static | `tests/execution/selftest.c` | T | — |
| 1635 | `i915_compute_emit_copy` | static | `tests/execution/selftest.c` | T | — |
| 1645 | `i915_compute_build_batch` | static | `tests/execution/selftest.c` | T | — |
| 1757 | `i915_compute_power_probe` | static | `tests/execution/selftest.c` | T | — |
| 1777 | `i915_golden_run` | static | `tests/execution/selftest.c` | T | — |
| 1826 | `drv_i915_compute_selftest` | global | `tests/execution/selftest.c` | T | `tests/execution/selftest.h` |
| 2276 | `drv_i915_draw_selftest` | global | `tests/execution/selftest.c` | T | `tests/execution/selftest.h` |

### uncore.c

現行: [uncore.c](../../src/drivers/gpu/i915/uncore.c)。13定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 44 | `drv_i915_read32` | global | `mmio.c` | I / 旧名で照合 | `mmio.h` |
| 69 | `drv_i915_write32` | global | `mmio.c` | I / 旧名で照合 | `mmio.h` |
| 94 | `drv_i915_wait32` | global | `mmio.c` | I / 旧名で照合 | `mmio.h` |
| 136 | `drv_i915_forcewake_get` | global | `mmio.c` | I / 旧名で照合 | `mmio.h` |
| 169 | `drv_i915_forcewake_put` | global | `mmio.c` | I / 旧名で照合 | `mmio.h` |
| 206 | `drv_i915_uncore_init` | global | `mmio.c` | I / 旧名で照合 | `mmio.h` |
| 241 | `drv_i915_gt_reset` | global | `reset.c` | I / 旧名で照合 | `reset.h` |
| 263 | `drv_i915_domain_reset` | global | `reset.c` | I / 旧名で照合 | `reset.h` |
| 290 | `i915_forcewake_domain_get` | static | `mmio.c` | S | — |
| 324 | `i915_forcewake_domain_put` | static | `mmio.c` | S | — |
| 360 | `i915_forcewake_request_register` | static | `mmio.c` | S | — |
| 372 | `i915_forcewake_ack_register` | static | `mmio.c` | S | — |
| 384 | `i915_timeout_ticks` | static | `mmio.c` | S | — |

### vk/cmd.c

現行: [vk/cmd.c](../../src/drivers/gpu/i915/vk/cmd.c)。17定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 47 | `i915_vk_object_table_create` | global | `render/object.c` | H / `drv_i915_object_table_create` | `render/object.h` |
| 71 | `i915_vk_object_table_destroy` | global | `render/object.c` | H / `drv_i915_object_table_destroy` | `render/object.h` |
| 87 | `i915_vk_obj_insert` | global | `render/object.c` | H / `drv_i915_object_insert` | `render/object.h` |
| 139 | `i915_vk_obj_lookup` | global | `render/object.c` | H / `drv_i915_object_lookup` | `render/object.h` |
| 163 | `i915_vk_obj_remove` | global | `render/object.c` | H / `drv_i915_object_remove` | `render/object.h` |
| 186 | `i915_vk_read_u32` | global | `render/codec.c` | H / `drv_i915_wire_read_u32` | `render/codec.h` |
| 215 | `i915_vk_read_u64` | global | `render/codec.c` | H / `drv_i915_wire_read_u64` | `render/codec.h` |
| 230 | `i915_vk_read_handle` | global | `render/codec.c` | I / 旧名で照合 | `render/codec.h` |
| 238 | `i915_vk_read_array` | global | `render/codec.c` | H / `drv_i915_wire_read_array` | `render/codec.h` |
| 266 | `i915_vk_reply_u32` | global | `render/codec.c` | H / `drv_i915_wire_reply_u32` | `render/codec.h` |
| 291 | `i915_vk_reply_u64` | global | `render/codec.c` | H / `drv_i915_wire_reply_u64` | `render/codec.h` |
| 302 | `i915_vk_reply_blob` | global | `render/codec.c` | I / 旧名で照合 | `render/codec.h` |
| 321 | `i915_vk_cmd_dispatch` | global | `render/dispatch.c` | H / `drv_i915_render_dispatch` | `render/dispatch.h` |
| 376 | `i915_vk_route` | static | `render/dispatch.c` | S | — |
| 413 | `i915_vk_cmd_set_reply` | static | `render/transport.c` | S | — |
| 434 | `i915_vk_cmd_seek_reply` | static | `render/transport.c` | S | — |
| 452 | `i915_vk_cmd_builtin` | static | `render/dispatch.c` | S | — |

### vk/cmdbuf.c

現行: [vk/cmdbuf.c](../../src/drivers/gpu/i915/vk/cmdbuf.c)。27定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 65 | `i915_vk_cmdbuf_result` | static | `render/command.c` | S | — |
| 77 | `i915_vk_cmdbuf_object_create` | static | `render/command.c` | S | — |
| 122 | `i915_vk_cmdbuf_object_destroy` | static | `render/command.c` | S | — |
| 142 | `i915_vk_cmdbuf_create_pool` | static | `render/command.c` | S | — |
| 191 | `i915_vk_cmdbuf_destroy_pool` | static | `render/command.c` | S | — |
| 220 | `i915_vk_cmdbuf_reset_pool` | static | `render/command.c` | S | — |
| 243 | `i915_vk_cmdbuf_allocate` | static | `render/command.c` | S | — |
| 310 | `i915_vk_cmdbuf_free` | static | `render/command.c` | S | — |
| 347 | `i915_vk_cmdbuf_begin_command` | static | `render/command.c` | S | — |
| 384 | `i915_vk_cmdbuf_end_command` | static | `render/command.c` | S | — |
| 412 | `i915_vk_cmdbuf_record_bind_pipeline` | static | `render/command.c` | S | — |
| 443 | `i915_vk_cmdbuf_record_bind_vertex` | static | `render/command.c` | S | — |
| 482 | `i915_vk_cmdbuf_record_draw` | static | `render/command.c` | S | — |
| 514 | `i915_vk_cmdbuf_record_end_render_pass` | static | `render/command.c` | S | — |
| 540 | `i915_vk_cmdbuf_queue_submit` | static | `render/command.c` | S | — |
| 610 | `i915_vk_cmdbuf_dispatch` | global | `render/command.c` | I / 旧名で照合 | `render/command.h` |
| 657 | `i915_vk_cmdbuf_begin` | global | `render/command.c` | I / 旧名で照合 | `render/command.h` |
| 668 | `i915_vk_cmdbuf_end` | global | `render/command.c` | I / 旧名で照合 | `render/command.h` |
| 679 | `i915_vk_cmd_bind_pipeline` | global | `render/command.c` | I / 旧名で照合 | `render/command.h` |
| 689 | `i915_vk_cmd_bind_vertex_buffers` | global | `render/command.c` | I / 旧名で照合 | `render/command.h` |
| 706 | `i915_vk_cmd_bind_descriptor_sets` | global | `render/command.c` | I / 旧名で照合 | `render/command.h` |
| 721 | `i915_vk_cmd_push_constants` | global | `render/command.c` | I / 旧名で照合 | `render/command.h` |
| 736 | `i915_vk_cmd_begin_render_pass` | global | `render/command.c` | I / 旧名で照合 | `render/command.h` |
| 751 | `i915_vk_cmd_end_render_pass` | global | `render/command.c` | I / 旧名で照合 | `render/command.h` |
| 760 | `i915_vk_cmd_draw` | global | `render/command.c` | I / 旧名で照合 | `render/command.h` |
| 791 | `i915_vk_queue_submit` | global | `render/command.c` | I / 旧名で照合 | `render/command.h` |
| 849 | `i915_vk_cmdbuf_put` | static | `render/command.c` | S | — |

### vk/codec-generated.inc

現行: [vk/codec-generated.inc](../../src/drivers/gpu/i915/vk/codec-generated.inc)。147定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 157 | `i915_vkc_dec_VkExtent2D` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 168 | `i915_vkc_enc_VkExtent2D` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 176 | `i915_vkc_dec_VkExtent3D` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 188 | `i915_vkc_enc_VkExtent3D` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 197 | `i915_vkc_dec_VkOffset2D` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 208 | `i915_vkc_enc_VkOffset2D` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 216 | `i915_vkc_dec_VkOffset3D` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 228 | `i915_vkc_enc_VkOffset3D` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 237 | `i915_vkc_dec_VkRect2D` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 248 | `i915_vkc_enc_VkRect2D` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 256 | `i915_vkc_dec_VkBufferMemoryBarrier` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 274 | `i915_vkc_dec_VkDispatchIndirectCommand` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 286 | `i915_vkc_enc_VkDispatchIndirectCommand` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 295 | `i915_vkc_dec_VkDrawIndexedIndirectCommand` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 309 | `i915_vkc_enc_VkDrawIndexedIndirectCommand` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 320 | `i915_vkc_dec_VkDrawIndirectCommand` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 333 | `i915_vkc_enc_VkDrawIndirectCommand` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 343 | `i915_vkc_dec_VkImageSubresourceRange` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 357 | `i915_vkc_enc_VkImageSubresourceRange` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 368 | `i915_vkc_dec_VkImageMemoryBarrier` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 387 | `i915_vkc_dec_VkMemoryBarrier` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 400 | `i915_vkc_dec_VkPipelineCacheHeaderVersionOne` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 416 | `i915_vkc_enc_VkPipelineCacheHeaderVersionOne` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 428 | `i915_vkc_dec_VkApplicationInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 444 | `i915_vkc_dec_VkFormatProperties` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 456 | `i915_vkc_enc_VkFormatProperties` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 465 | `i915_vkc_dec_VkImageFormatProperties` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 479 | `i915_vkc_enc_VkImageFormatProperties` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 490 | `i915_vkc_dec_VkInstanceCreateInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 528 | `i915_vkc_dec_VkMemoryHeap` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 539 | `i915_vkc_enc_VkMemoryHeap` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 547 | `i915_vkc_dec_VkMemoryType` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 558 | `i915_vkc_enc_VkMemoryType` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 566 | `i915_vkc_dec_VkPhysicalDeviceFeatures` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 630 | `i915_vkc_enc_VkPhysicalDeviceFeatures` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 691 | `i915_vkc_dec_VkPhysicalDeviceLimits` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 824 | `i915_vkc_enc_VkPhysicalDeviceLimits` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 948 | `i915_vkc_dec_VkPhysicalDeviceMemoryProperties` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 967 | `i915_vkc_enc_VkPhysicalDeviceMemoryProperties` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 981 | `i915_vkc_dec_VkPhysicalDeviceSparseProperties` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 995 | `i915_vkc_enc_VkPhysicalDeviceSparseProperties` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1006 | `i915_vkc_dec_VkPhysicalDeviceProperties` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1028 | `i915_vkc_enc_VkPhysicalDeviceProperties` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1045 | `i915_vkc_dec_VkQueueFamilyProperties` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1058 | `i915_vkc_enc_VkQueueFamilyProperties` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1068 | `i915_vkc_dec_VkDeviceQueueCreateInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1090 | `i915_vkc_dec_VkDeviceCreateInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1137 | `i915_vkc_dec_VkExtensionProperties` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1150 | `i915_vkc_enc_VkExtensionProperties` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1159 | `i915_vkc_dec_VkLayerProperties` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1176 | `i915_vkc_enc_VkLayerProperties` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1188 | `i915_vkc_dec_VkSubmitInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1234 | `i915_vkc_dec_VkMappedMemoryRange` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1248 | `i915_vkc_dec_VkMemoryAllocateInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1261 | `i915_vkc_dec_VkMemoryRequirements` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1273 | `i915_vkc_enc_VkMemoryRequirements` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1282 | `i915_vkc_dec_VkSparseMemoryBind` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1296 | `i915_vkc_dec_VkSparseBufferMemoryBindInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1315 | `i915_vkc_dec_VkSparseImageOpaqueMemoryBindInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1334 | `i915_vkc_dec_VkImageSubresource` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1346 | `i915_vkc_enc_VkImageSubresource` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1355 | `i915_vkc_dec_VkSparseImageMemoryBind` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1370 | `i915_vkc_dec_VkSparseImageMemoryBindInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1389 | `i915_vkc_dec_VkBindSparseInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1445 | `i915_vkc_dec_VkSparseImageFormatProperties` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1457 | `i915_vkc_enc_VkSparseImageFormatProperties` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1466 | `i915_vkc_dec_VkSparseImageMemoryRequirements` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1480 | `i915_vkc_enc_VkSparseImageMemoryRequirements` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1491 | `i915_vkc_dec_VkFenceCreateInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1503 | `i915_vkc_dec_VkSemaphoreCreateInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1515 | `i915_vkc_dec_VkEventCreateInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1527 | `i915_vkc_dec_VkQueryPoolCreateInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1542 | `i915_vkc_dec_VkBufferCreateInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1566 | `i915_vkc_dec_VkBufferViewCreateInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1582 | `i915_vkc_dec_VkImageCreateInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1613 | `i915_vkc_dec_VkSubresourceLayout` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1627 | `i915_vkc_enc_VkSubresourceLayout` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1638 | `i915_vkc_dec_VkComponentMapping` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1651 | `i915_vkc_enc_VkComponentMapping` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1661 | `i915_vkc_dec_VkImageViewCreateInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1678 | `i915_vkc_dec_VkShaderModuleCreateInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1699 | `i915_vkc_dec_VkPipelineCacheCreateInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1720 | `i915_vkc_dec_VkSpecializationMapEntry` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1732 | `i915_vkc_enc_VkSpecializationMapEntry` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1741 | `i915_vkc_dec_VkSpecializationInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1768 | `i915_vkc_dec_VkPipelineShaderStageCreateInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1791 | `i915_vkc_dec_VkComputePipelineCreateInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1807 | `i915_vkc_dec_VkVertexInputBindingDescription` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1819 | `i915_vkc_enc_VkVertexInputBindingDescription` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1828 | `i915_vkc_dec_VkVertexInputAttributeDescription` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1841 | `i915_vkc_enc_VkVertexInputAttributeDescription` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1851 | `i915_vkc_dec_VkPipelineVertexInputStateCreateInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1881 | `i915_vkc_dec_VkPipelineInputAssemblyStateCreateInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1895 | `i915_vkc_dec_VkPipelineTessellationStateCreateInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1908 | `i915_vkc_dec_VkViewport` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1923 | `i915_vkc_enc_VkViewport` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1935 | `i915_vkc_dec_VkPipelineViewportStateCreateInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1965 | `i915_vkc_dec_VkPipelineRasterizationStateCreateInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 1987 | `i915_vkc_dec_VkPipelineMultisampleStateCreateInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2012 | `i915_vkc_dec_VkStencilOpState` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2028 | `i915_vkc_enc_VkStencilOpState` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2041 | `i915_vkc_dec_VkPipelineDepthStencilStateCreateInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2062 | `i915_vkc_dec_VkPipelineColorBlendAttachmentState` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2079 | `i915_vkc_enc_VkPipelineColorBlendAttachmentState` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2093 | `i915_vkc_dec_VkPipelineColorBlendStateCreateInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2120 | `i915_vkc_dec_VkPipelineDynamicStateCreateInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2141 | `i915_vkc_dec_VkPushConstantRange` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2153 | `i915_vkc_enc_VkPushConstantRange` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2162 | `i915_vkc_dec_VkPipelineLayoutCreateInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2192 | `i915_vkc_dec_VkSamplerCreateInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2219 | `i915_vkc_dec_VkCopyDescriptorSet` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2237 | `i915_vkc_dec_VkDescriptorBufferInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2249 | `i915_vkc_dec_VkDescriptorPoolSize` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2260 | `i915_vkc_enc_VkDescriptorPoolSize` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2268 | `i915_vkc_dec_VkDescriptorPoolCreateInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2290 | `i915_vkc_dec_VkDescriptorSetAllocateInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2311 | `i915_vkc_dec_VkDescriptorSetLayoutBinding` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2332 | `i915_vkc_dec_VkDescriptorSetLayoutCreateInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2353 | `i915_vkc_dec_VkAttachmentDescription` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2371 | `i915_vkc_enc_VkAttachmentDescription` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2386 | `i915_vkc_dec_VkAttachmentReference` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2397 | `i915_vkc_enc_VkAttachmentReference` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2405 | `i915_vkc_dec_VkFramebufferCreateInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2430 | `i915_vkc_dec_VkSubpassDescription` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2484 | `i915_vkc_dec_VkSubpassDependency` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2500 | `i915_vkc_enc_VkSubpassDependency` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2513 | `i915_vkc_dec_VkRenderPassCreateInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2552 | `i915_vkc_dec_VkCommandPoolCreateInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2565 | `i915_vkc_dec_VkCommandBufferAllocateInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2579 | `i915_vkc_dec_VkCommandBufferInheritanceInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2596 | `i915_vkc_dec_VkCommandBufferBeginInfo` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2616 | `i915_vkc_dec_VkBufferCopy` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2628 | `i915_vkc_enc_VkBufferCopy` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2637 | `i915_vkc_dec_VkImageSubresourceLayers` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2650 | `i915_vkc_enc_VkImageSubresourceLayers` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2660 | `i915_vkc_dec_VkBufferImageCopy` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2675 | `i915_vkc_enc_VkBufferImageCopy` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2687 | `i915_vkc_dec_VkClearDepthStencilValue` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2698 | `i915_vkc_enc_VkClearDepthStencilValue` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2706 | `i915_vkc_dec_VkClearRect` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2718 | `i915_vkc_enc_VkClearRect` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2727 | `i915_vkc_dec_VkImageBlit` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2746 | `i915_vkc_enc_VkImageBlit` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2760 | `i915_vkc_dec_VkImageCopy` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2774 | `i915_vkc_enc_VkImageCopy` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2785 | `i915_vkc_dec_VkImageResolve` | static | `data/vulkan-codec.inc` | S | —（生成static） |
| 2799 | `i915_vkc_enc_VkImageResolve` | static | `data/vulkan-codec.inc` | S | —（生成static） |

### vk/compile.c

現行: [vk/compile.c](../../src/drivers/gpu/i915/vk/compile.c)。12定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 103 | `i915_vk_compile` | global | `compiler/compile.c` | H / `drv_i915_shader_compile` | `compiler/compiler.h` |
| 205 | `i915_vk_shader_binary_free` | global | `compiler/compile.c` | H / `drv_i915_shader_binary_free` | `compiler/compiler.h` |
| 217 | `compile_sources` | static | `compiler/compile.c` | S | — |
| 233 | `compile_grf` | static | `compiler/compile.c` | S | — |
| 249 | `compile_define` | static | `compiler/compile.c` | S | — |
| 288 | `compile_release` | static | `compiler/compile.c` | S | — |
| 313 | `compile_instruction` | static | `compiler/compile.c` | S | — |
| 447 | `compile_rank` | static | `compiler/compile.c` | S | — |
| 465 | `compile_note` | static | `compiler/compile.c` | S | — |
| 491 | `compile_interface` | static | `compiler/compile.c` | S | — |
| 516 | `compile_prologue` | static | `compiler/compile.c` | S | — |
| 543 | `compile_terminate` | static | `compiler/compile.c` | S | — |

### vk/display.c

現行: [vk/display.c](../../src/drivers/gpu/i915/vk/display.c)。4定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 25 | `i915_vk_display_init` | global | `render/wsi.c` | I / 旧名で照合 | `render/wsi.h` |
| 37 | `i915_vk_display_mode` | global | `render/wsi.c` | I / 旧名で照合 | `render/wsi.h` |
| 51 | `i915_vk_display_flip` | global | `render/wsi.c` | I / 旧名で照合 | `render/wsi.h` |
| 62 | `i915_vk_display_fini` | global | `render/wsi.c` | I / 旧名で照合 | `render/wsi.h` |

### vk/eu.c

現行: [vk/eu.c](../../src/drivers/gpu/i915/vk/eu.c)。25定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 53 | `i915_vk_eu_init` | global | `compiler/eu.c` | H / `drv_i915_eu_init` | `compiler/eu.h` |
| 65 | `i915_vk_eu_free` | global | `compiler/eu.c` | H / `drv_i915_eu_free` | `compiler/eu.h` |
| 77 | `i915_vk_eu_data` | global | `compiler/eu.c` | H / `drv_i915_eu_data` | `compiler/eu.h` |
| 87 | `i915_vk_eu_grf_typed` | static | `compiler/eu.c` | S | — |
| 105 | `i915_vk_eu_grf` | global | `compiler/eu.c` | H / `drv_i915_eu_grf` | `compiler/eu.h` |
| 113 | `i915_vk_eu_grf_ud` | global | `compiler/eu.c` | H / `drv_i915_eu_grf_ud` | `compiler/eu.h` |
| 121 | `i915_vk_eu_grf_scalar` | global | `compiler/eu.c` | H / `drv_i915_eu_grf_scalar` | `compiler/eu.h` |
| 140 | `i915_vk_eu_negate` | global | `compiler/eu.c` | H / `drv_i915_eu_negate` | `compiler/eu.h` |
| 149 | `i915_vk_eu_imm_f` | global | `compiler/eu.c` | H / `drv_i915_eu_imm_f` | `compiler/eu.h` |
| 163 | `i915_vk_eu_imm_d` | global | `compiler/eu.c` | H / `drv_i915_eu_imm_d` | `compiler/eu.h` |
| 177 | `i915_vk_eu_null` | global | `compiler/eu.c` | H / `drv_i915_eu_null` | `compiler/eu.h` |
| 189 | `i915_vk_eu_mov` | global | `compiler/eu.c` | H / `drv_i915_eu_mov` | `compiler/eu.h` |
| 207 | `i915_vk_eu_alu2` | global | `compiler/eu.c` | H / `drv_i915_eu_alu2` | `compiler/eu.h` |
| 245 | `i915_vk_eu_mad` | global | `compiler/eu.c` | H / `drv_i915_eu_mad` | `compiler/eu.h` |
| 265 | `i915_vk_eu_math` | global | `compiler/eu.c` | H / `drv_i915_eu_math` | `compiler/eu.h` |
| 312 | `i915_vk_eu_send` | global | `compiler/eu.c` | H / `drv_i915_eu_send` | `compiler/eu.h` |
| 365 | `i915_vk_eu_nop` | global | `compiler/eu.c` | H / `drv_i915_eu_nop` | `compiler/eu.h` |
| 379 | `i915_vk_eu_reserve` | static | `compiler/eu.c` | S | — |
| 420 | `i915_vk_eu_common` | static | `compiler/eu.c` | S | — |
| 444 | `i915_vk_eu_sync` | static | `compiler/eu.c` | S | — |
| 462 | `i915_vk_eu_dst` | static | `compiler/eu.c` | S | — |
| 475 | `i915_vk_eu_src0` | static | `compiler/eu.c` | S | — |
| 499 | `i915_vk_eu_src1` | static | `compiler/eu.c` | S | — |
| 522 | `i915_vk_eu_set` | static | `compiler/eu.c` | S | — |
| 537 | `i915_vk_eu_bit` | static | `compiler/eu.c` | S | — |

### vk/gfx-draw.c

現行: [vk/gfx-draw.c](../../src/drivers/gpu/i915/vk/gfx-draw.c)。38定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 97 | `emit` | static | `render/batch.c` | H / `drv_i915_batch_emit` | `render/batch.h` |
| 107 | `emit_zero` | static | `render/batch.c` | H / `drv_i915_batch_zero` | `render/batch.h` |
| 117 | `emit_words` | static | `render/batch.c` | H / `drv_i915_batch_words` | `render/batch.h` |
| 126 | `emit_pointer` | static | `render/batch.c` | H / `drv_i915_batch_pointer` | `render/batch.h` |
| 134 | `emit_pc` | static | `render/batch.c` | H / `drv_i915_batch_pipe_control` | `render/batch.h` |
| 148 | `sf_half` | static | `render/math.c` | H / `drv_i915_float_half` | `render/math.h` |
| 157 | `sf_add` | static | `render/math.c` | H / `drv_i915_float_add` | `render/math.h` |
| 201 | `sf_sub` | static | `render/math.c` | H / `drv_i915_float_sub` | `render/math.h` |
| 212 | `gfx_object` | static | `render/memory.c` | H / `drv_i915_render_buffer_alloc` | `render/memory.h` |
| 232 | `gfx_session` | static | `render/draw.c` | I / 旧名で照合 | `render/draw.h` |
| 252 | `i915_vk_gfx_session_close` | global | `render/draw.c` | H / `drv_i915_render_draw_session_close` | `render/draw.h` |
| 292 | `gfx_fnv1a` | static | `tests/render/reference-shaders.c` | T | `tests/render/reference-shaders.h` |
| 306 | `gfx_compile_stage` | static | `render/pipeline.c` | S | — |
| 329 | `i915_vk_gfx_pipeline_prepare` | global | `render/pipeline.c` | H / `drv_i915_render_pipeline_prepare` | `render/pipeline.h` |
| 382 | `i915_vk_gfx_pipeline_release` | global | `render/pipeline.c` | H / `drv_i915_render_pipeline_release` | `render/pipeline.h` |
| 392 | `gfx_kernels` | static | `render/pipeline.c` | I / 旧名で照合 | `render/pipeline.h` |
| 429 | `gfx_surface_format` | static | `render/state.c` | S | — |
| 444 | `gfx_format_components` | static | `render/state.c` | S | — |
| 460 | `gfx_write_rss` | static | `render/state.c` | S | — |
| 483 | `gfx_write_sampler` | static | `render/state.c` | H / `drv_i915_render_sampler_write` | `render/state.h` |
| 507 | `gfx_write_state` | static | `render/state.c` | I / 旧名で照合 | `render/state.h` |
| 599 | `emit_sba` | static | `render/state.c` | I / 旧名で照合 | `render/state.h` |
| 632 | `emit_vertex_input` | static | `render/state.c` | I / 旧名で照合 | `render/state.h` |
| 706 | `emit_urb` | static | `render/state.c` | I / 旧名で照合 | `render/state.h` |
| 729 | `emit_constants` | static | `render/state.c` | I / 旧名で照合 | `render/state.h` |
| 756 | `emit_raster` | static | `render/state.c` | I / 旧名で照合 | `render/state.h` |
| 785 | `emit_depth` | static | `render/state.c` | I / 旧名で照合 | `render/state.h` |
| 839 | `emit_shader_state` | static | `render/state.c` | I / 旧名で照合 | `render/state.h` |
| 890 | `gfx_build_batch` | static | `render/draw.c` | S | — |
| 999 | `gfx_census` | static | `tests/render/readback.c` | T | `tests/render/readback.h` |
| 1063 | `i915_vk_gfx_draw` | global | `render/draw.c` | H / `drv_i915_render_draw` | `render/draw.h` |
| 1143 | `gfx_rect_compile` | static | `render/blit.c` | S | — |
| 1195 | `i915_vk_gfx_rect_prepare` | global | `render/blit.c` | H / `drv_i915_blit_prepare` | `render/blit.h` |
| 1220 | `sf_from_u32` | static | `render/math.c` | H / `drv_i915_float_from_u32` | `render/math.h` |
| 1236 | `sf_ratio` | static | `render/math.c` | H / `drv_i915_float_ratio` | `render/math.h` |
| 1257 | `gfx_write_surface` | static | `render/state.c` | H / `drv_i915_render_surface_write` | `render/state.h` |
| 1279 | `i915_vk_gfx_rect_build` | global | `render/blit.c` | H / `drv_i915_blit_build` | `render/blit.h` |
| 1497 | `i915_vk_gfx_rect` | global | `render/blit.c` | H / `drv_i915_blit_submit` | `render/blit.h` |

### vk/gfx-obj.c

現行: [vk/gfx-obj.c](../../src/drivers/gpu/i915/vk/gfx-obj.c)。32定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 36 | `gfx_result` | static | `render/codec.c` | H / `drv_i915_wire_result` | `render/codec.h` |
| 49 | `gfx_create_tail` | static | `render/codec.c` | H / `drv_i915_wire_create_tail` | `render/codec.h` |
| 58 | `gfx_create_reply` | static | `render/codec.c` | H / `drv_i915_wire_create_reply` | `render/codec.h` |
| 81 | `gfx_destroy_plain` | static | `render/object.c` | H / `drv_i915_object_destroy_dispatch` | `render/object.h` |
| 104 | `i915_vk_gfx_memory_cpu` | global | `render/memory.c` | H / `drv_i915_render_memory_cpu` | `render/memory.h` |
| 113 | `i915_vk_gfx_memory_va` | global | `render/memory.c` | H / `drv_i915_render_memory_va` | `render/memory.h` |
| 125 | `drv_i915_vk_blob_attach` | global | `render/memory.c` | H / `drv_i915_render_memory_blob_attach` | `render/memory.h` |
| 142 | `drv_i915_vk_blob_detach` | global | `render/memory.c` | H / `drv_i915_render_memory_blob_detach` | `render/memory.h` |
| 156 | `gfx_allocate_memory` | static | `render/memory.c` | I / 旧名で照合 | `render/memory.h` |
| 199 | `gfx_free_memory` | static | `render/memory.c` | I / 旧名で照合 | `render/memory.h` |
| 227 | `gfx_bind` | static | `render/memory.c` | I / 旧名で照合 | `render/memory.h` |
| 267 | `gfx_requirements` | static | `render/memory.c` | I / 旧名で照合 | `render/memory.h` |
| 305 | `gfx_create_buffer` | static | `render/memory.c` | I / 旧名で照合 | `render/memory.h` |
| 329 | `gfx_format_bytes` | static | `render/image.c` | S | — |
| 342 | `gfx_create_image` | static | `render/image.c` | I / 旧名で照合 | `render/image.h` |
| 395 | `gfx_create_image_view` | static | `render/image.c` | I / 旧名で照合 | `render/image.h` |
| 423 | `gfx_create_sampler` | static | `render/image.c` | I / 旧名で照合 | `render/image.h` |
| 452 | `gfx_subresource_layout` | static | `render/image.c` | I / 旧名で照合 | `render/image.h` |
| 480 | `gfx_create_dsl` | static | `render/descriptor.c` | I / 旧名で照合 | `render/descriptor.h` |
| 515 | `gfx_create_dpool` | static | `render/descriptor.c` | I / 旧名で照合 | `render/descriptor.h` |
| 540 | `gfx_allocate_dsets` | static | `render/descriptor.c` | I / 旧名で照合 | `render/descriptor.h` |
| 591 | `gfx_update_dsets` | static | `render/descriptor.c` | I / 旧名で照合 | `render/descriptor.h` |
| 661 | `gfx_create_pipeline_layout` | static | `render/pipeline.c` | I / 旧名で照合 | `render/pipeline.h` |
| 684 | `gfx_create_render_pass` | static | `render/render-pass.c` | I / 旧名で照合 | `render/render-pass.h` |
| 729 | `gfx_create_framebuffer` | static | `render/render-pass.c` | I / 旧名で照合 | `render/render-pass.h` |
| 768 | `gfx_create_shader` | static | `render/pipeline.c` | I / 旧名で照合 | `render/pipeline.h` |
| 794 | `gfx_float_bits` | static | `render/pipeline.c` | S | — |
| 805 | `gfx_decode_pipeline` | static | `render/pipeline.c` | S | — |
| 930 | `gfx_create_pipelines` | static | `render/pipeline.c` | I / 旧名で照合 | `render/pipeline.h` |
| 994 | `gfx_destroy_pipeline` | static | `render/pipeline.c` | I / 旧名で照合 | `render/pipeline.h` |
| 1022 | `gfx_create_semaphore` | static | `render/sync.c` | I / 旧名で照合 | `render/sync.h` |
| 1043 | `i915_vk_gfx_obj_dispatch` | global | `render/dispatch.c` | I / 旧名で照合 | `render/dispatch.h` |

### vk/gfx-rec.c

現行: [vk/gfx-rec.c](../../src/drivers/gpu/i915/vk/gfx-rec.c)。26定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 52 | `gfx_result` | static | `render/codec.c` | H / `drv_i915_wire_result` | `render/codec.h` |
| 66 | `rec_create_pool` | static | `render/command.c` | S | — |
| 96 | `rec_free_buffer` | static | `render/command.c` | S | — |
| 111 | `rec_destroy_pool` | static | `render/command.c` | S | — |
| 134 | `rec_reset_pool` | static | `render/command.c` | S | — |
| 157 | `rec_allocate` | static | `render/command.c` | S | — |
| 214 | `rec_free` | static | `render/command.c` | S | — |
| 236 | `rec_begin` | static | `render/command.c` | S | — |
| 258 | `rec_end` | static | `render/command.c` | S | — |
| 277 | `rec_op` | static | `render/command.c` | S | — |
| 298 | `rec_image_copy` | static | `render/command.c` | S | — |
| 338 | `rec_clear_image` | static | `render/command.c` | S | — |
| 366 | `rec_barrier` | static | `render/command.c` | S | — |
| 395 | `rec_copy` | static | `render/command.c` | S | — |
| 436 | `rec_begin_pass` | static | `render/command.c` | S | — |
| 485 | `rec_bind_vertex` | static | `render/command.c` | S | — |
| 510 | `rec_bind_dsets` | static | `render/command.c` | S | — |
| 539 | `rec_push` | static | `render/command.c` | S | — |
| 558 | `rec_command` | static | `render/command.c` | S | — |
| 613 | `image_surface` | static | `render/blit.c` | S | — |
| 625 | `exec_clear` | static | `render/blit.c` | I / 旧名で照合 | `render/blit.h` |
| 674 | `exec_copy` | static | `render/blit.c` | I / 旧名で照合 | `render/blit.h` |
| 723 | `exec_image` | static | `render/blit.c` | I / 旧名で照合 | `render/blit.h` |
| 780 | `exec_cmdbuf` | static | `render/command.c` | H / `drv_i915_render_command_execute` | `render/command.h` |
| 846 | `rec_submit` | static | `render/command.c` | S | — |
| 913 | `i915_vk_gfx_rec_dispatch` | global | `render/command.c` | H / `drv_i915_render_command_dispatch` | `render/command.h` |

### vk/inst.c

現行: [vk/inst.c](../../src/drivers/gpu/i915/vk/inst.c)。16定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 36 | `set_float` | static | `render/instance.c` | S | — |
| 51 | `inst_create_instance` | static | `render/instance.c` | S | — |
| 83 | `inst_enumerate_physical_devices` | static | `render/instance.c` | S | — |
| 117 | `inst_properties` | static | `render/instance.c` | S | — |
| 246 | `inst_features` | static | `render/instance.c` | S | — |
| 269 | `inst_memory_properties` | static | `render/instance.c` | S | — |
| 304 | `inst_queue_families` | static | `render/instance.c` | S | — |
| 338 | `inst_format_features` | static | `render/instance.c` | S | — |
| 364 | `inst_format_properties` | static | `render/instance.c` | S | — |
| 385 | `inst_image_format_properties` | static | `render/instance.c` | S | — |
| 428 | `inst_create_device` | static | `render/instance.c` | S | — |
| 465 | `inst_get_device_queue2` | static | `render/instance.c` | S | — |
| 495 | `inst_destroy` | static | `render/instance.c` | S | — |
| 513 | `inst_wait_idle` | static | `render/instance.c` | S | — |
| 536 | `inst_execute_streams` | static | `render/transport.c` | S | — |
| 590 | `i915_vk_inst_dispatch` | global | `render/instance.c` | H / `drv_i915_render_instance_dispatch` | `render/instance.h` |

### vk/pipe.c

現行: [vk/pipe.c](../../src/drivers/gpu/i915/vk/pipe.c)。19定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 65 | `i915_vk_pipe_result` | static | `render/pipeline.c` | S | — |
| 84 | `i915_vk_pipe_create_shader_module` | static | `render/pipeline.c` | S | — |
| 157 | `i915_vk_pipe_destroy_shader_module` | static | `render/pipeline.c` | S | — |
| 187 | `i915_vk_pipe_skip_string` | static | `render/pipeline.c` | S | — |
| 205 | `i915_vk_pipe_decode_stage` | static | `render/pipeline.c` | S | — |
| 223 | `i915_vk_pipe_skip_words` | static | `render/pipeline.c` | S | — |
| 235 | `i915_vk_pipe_skip_array` | static | `render/pipeline.c` | S | — |
| 249 | `i915_vk_pipe_place_shader` | static | `render/pipeline.c` | S | — |
| 302 | `i915_vk_pipe_release_code` | static | `render/pipeline.c` | S | — |
| 320 | `i915_vk_pipe_decode_graphics` | static | `render/pipeline.c` | S | — |
| 460 | `i915_vk_pipe_create_graphics` | static | `render/pipeline.c` | S | — |
| 537 | `i915_vk_pipe_destroy_pipeline` | static | `render/pipeline.c` | S | — |
| 563 | `i915_vk_pipe_dispatch` | global | `render/pipeline.c` | I / 旧名で照合 | `render/pipeline.h` |
| 592 | `i915_vk_pipeline_create` | global | `render/pipeline.c` | I / 旧名で照合 | `render/pipeline.h` |
| 621 | `i915_vk_pipeline_destroy` | global | `render/pipeline.c` | I / 旧名で照合 | `render/pipeline.h` |
| 635 | `i915_vk_pipeline_emit` | global | `render/state.c` | I / 旧名で照合 | `render/state.h` |
| 656 | `i915_vk_pipe_emit_base` | global | `render/state.c` | I / 旧名で照合 | `render/state.h` |
| 672 | `i915_vk_batch_emit` | static | `render/batch.c` | I / 旧名で照合 | `render/batch.h` |
| 690 | `i915_vk_batch_pad` | static | `render/batch.c` | I / 旧名で照合 | `render/batch.h` |

### vk/res.c

現行: [vk/res.c](../../src/drivers/gpu/i915/vk/res.c)。36定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 101 | `i915_vk_memory_alloc` | global | `render/memory.c` | I / 旧名で照合 | `render/memory.h` |
| 152 | `i915_vk_memory_free` | global | `render/memory.c` | I / 旧名で照合 | `render/memory.h` |
| 172 | `i915_vk_memory_map` | global | `render/memory.c` | I / 旧名で照合 | `render/memory.h` |
| 186 | `i915_vk_buffer_create` | global | `render/memory.c` | I / 旧名で照合 | `render/memory.h` |
| 211 | `i915_vk_buffer_bind` | global | `render/memory.c` | I / 旧名で照合 | `render/memory.h` |
| 223 | `i915_vk_buffer_destroy` | global | `render/memory.c` | I / 旧名で照合 | `render/memory.h` |
| 232 | `i915_vk_image_create` | global | `render/image.c` | I / 旧名で照合 | `render/image.h` |
| 261 | `i915_vk_image_bind` | global | `render/image.c` | I / 旧名で照合 | `render/image.h` |
| 280 | `i915_vk_image_destroy` | global | `render/image.c` | I / 旧名で照合 | `render/image.h` |
| 289 | `i915_vk_image_view_create` | global | `render/image.c` | I / 旧名で照合 | `render/image.h` |
| 319 | `i915_vk_image_view_destroy` | global | `render/image.c` | I / 旧名で照合 | `render/image.h` |
| 328 | `i915_vk_image_surface_state` | global | `render/state.c` | I / 旧名で照合 | `render/state.h` |
| 342 | `i915_vk_surface_state` | static | `render/state.c` | I / 旧名で照合 | `render/state.h` |
| 370 | `i915_vk_sampler_create` | global | `render/image.c` | I / 旧名で照合 | `render/image.h` |
| 398 | `i915_vk_sampler_destroy` | global | `render/image.c` | I / 旧名で照合 | `render/image.h` |
| 407 | `i915_vk_sampler_state` | global | `render/state.c` | I / 旧名で照合 | `render/state.h` |
| 415 | `i915_vk_dsl_create` | global | `render/descriptor.c` | I / 旧名で照合 | `render/descriptor.h` |
| 449 | `i915_vk_dsl_destroy` | global | `render/descriptor.c` | I / 旧名で照合 | `render/descriptor.h` |
| 458 | `i915_vk_dpool_create` | global | `render/descriptor.c` | I / 旧名で照合 | `render/descriptor.h` |
| 479 | `i915_vk_dpool_destroy` | global | `render/descriptor.c` | I / 旧名で照合 | `render/descriptor.h` |
| 488 | `i915_vk_dset_alloc` | global | `render/descriptor.c` | I / 旧名で照合 | `render/descriptor.h` |
| 513 | `i915_vk_dset_free` | global | `render/descriptor.c` | I / 旧名で照合 | `render/descriptor.h` |
| 526 | `i915_vk_dset_update` | global | `render/descriptor.c` | I / 旧名で照合 | `render/descriptor.h` |
| 548 | `i915_vk_result` | static | `render/memory.c` | I / 旧名で照合 | `render/memory.h` |
| 561 | `i915_vk_res_free_memory_obj` | static | `render/memory.c` | S | — |
| 567 | `i915_vk_res_destroy_buffer_obj` | static | `render/memory.c` | S | — |
| 573 | `i915_vk_res_destroy_image_obj` | static | `render/image.c` | S | — |
| 580 | `i915_vk_res_bind_buffer_obj` | static | `render/memory.c` | S | — |
| 586 | `i915_vk_res_bind_image_obj` | static | `render/image.c` | S | — |
| 593 | `i915_vk_res_skip_extension` | static | `render/memory.c` | I / 旧名で照合 | `render/memory.h` |
| 610 | `i915_vk_res_allocate_memory` | static | `render/memory.c` | I / 旧名で照合 | `render/memory.h` |
| 663 | `i915_vk_res_create_buffer` | static | `render/memory.c` | I / 旧名で照合 | `render/memory.h` |
| 722 | `i915_vk_res_create_image` | static | `render/image.c` | I / 旧名で照合 | `render/image.h` |
| 794 | `i915_vk_res_bind` | static | `render/memory.c` | I / 旧名で照合 | `render/memory.h` |
| 832 | `i915_vk_res_destroy` | static | `render/memory.c` | I / 旧名で照合 | `render/memory.h` |
| 862 | `i915_vk_res_dispatch` | global | `render/dispatch.c` | I / 旧名で照合 | `render/dispatch.h` |

### vk/spirv.c

現行: [vk/spirv.c](../../src/drivers/gpu/i915/vk/spirv.c)。14定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 199 | `i915_vk_spirv_parse` | global | `compiler/spirv.c` | H / `drv_i915_shader_parse` | `compiler/compiler.h` |
| 209 | `i915_vk_spirv_parse_diag` | global | `compiler/spirv.c` | H / `drv_i915_shader_parse` | `compiler/compiler.h` |
| 292 | `i915_vk_spirv_free` | global | `compiler/spirv.c` | H / `drv_i915_shader_ir_free` | `compiler/compiler.h` |
| 311 | `spirv_refuse` | static | `compiler/spirv.c` | S | — |
| 325 | `spirv_id` | static | `compiler/spirv.c` | S | — |
| 334 | `spirv_float_components` | static | `compiler/spirv.c` | S | — |
| 355 | `spirv_pass_declarations` | static | `compiler/spirv.c` | S | — |
| 565 | `spirv_add_io` | static | `compiler/spirv.c` | S | — |
| 588 | `spirv_add_uniform` | static | `compiler/spirv.c` | S | — |
| 605 | `spirv_emit` | static | `compiler/spirv.c` | S | — |
| 630 | `spirv_new_value` | static | `compiler/spirv.c` | S | — |
| 642 | `spirv_operand` | static | `compiler/spirv.c` | S | — |
| 675 | `spirv_result` | static | `compiler/spirv.c` | S | — |
| 697 | `spirv_pass_body` | static | `compiler/spirv.c` | S | — |

### vk/sync.c

現行: [vk/sync.c](../../src/drivers/gpu/i915/vk/sync.c)。17定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 52 | `i915_vk_fence_create` | global | `render/sync.c` | I / 旧名で照合 | `render/sync.h` |
| 73 | `i915_vk_fence_destroy` | global | `render/sync.c` | I / 旧名で照合 | `render/sync.h` |
| 82 | `i915_vk_fence_reset` | global | `render/sync.c` | I / 旧名で照合 | `render/sync.h` |
| 92 | `i915_vk_fence_signal` | global | `render/sync.c` | I / 旧名で照合 | `render/sync.h` |
| 101 | `i915_vk_fence_arm` | global | `render/sync.c` | I / 旧名で照合 | `render/sync.h` |
| 116 | `i915_vk_fence_status` | global | `render/sync.c` | I / 旧名で照合 | `render/sync.h` |
| 138 | `i915_vk_fence_wait` | global | `render/sync.c` | I / 旧名で照合 | `render/sync.h` |
| 170 | `i915_vk_fence_ready_locked` | static | `render/sync.c` | S | — |
| 201 | `i915_vk_sync_create_fence` | static | `render/sync.c` | S | — |
| 248 | `i915_vk_sync_destroy_fence` | static | `render/sync.c` | S | — |
| 277 | `i915_vk_sync_reset_fences` | static | `render/sync.c` | S | — |
| 311 | `i915_vk_sync_fence_status` | static | `render/sync.c` | S | — |
| 337 | `i915_vk_sync_dispatch` | global | `render/sync.c` | I / 旧名で照合 | `render/sync.h` |
| 367 | `i915_vk_semaphore_create` | global | `render/sync.c` | I / 旧名で照合 | `render/sync.h` |
| 386 | `i915_vk_semaphore_destroy` | global | `render/sync.c` | I / 旧名で照合 | `render/sync.h` |
| 395 | `i915_vk_query_pool_create` | global | `render/sync.c` | I / 旧名で照合 | `render/sync.h` |
| 418 | `i915_vk_query_pool_destroy` | global | `render/sync.c` | I / 旧名で照合 | `render/sync.h` |

### vk/vk.c

現行: [vk/vk.c](../../src/drivers/gpu/i915/vk/vk.c)。7定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 34 | `drv_i915_vk_attach` | global | `render/vulkan.c` | H / `drv_i915_render_attach` | `render/render.h` |
| 68 | `drv_i915_vk_detach` | global | `render/vulkan.c` | H / `drv_i915_render_detach` | `render/render.h` |
| 81 | `drv_i915_vk_open` | global | `render/vulkan.c` | H / `drv_i915_render_open` | `render/render.h` |
| 114 | `drv_i915_vk_close` | global | `render/vulkan.c` | H / `drv_i915_render_close` | `render/render.h` |
| 128 | `drv_i915_vk_command` | global | `render/vulkan.c` | H / `drv_i915_render_execute` | `render/render.h` |
| 179 | `i915_vk_capset_fill` | static | `render/vulkan.c` | S | — |
| 210 | `i915_vk_errno` | global | `render/vulkan.c` | I / 旧名で照合 | `render/render.h` |

### vk/vkc.c

現行: [vk/vkc.c](../../src/drivers/gpu/i915/vk/vkc.c)。7定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 16 | `i915_vkc_array` | global | `render/codec.c` | I / 旧名で照合 | `render/codec.h` |
| 52 | `i915_vkc_read_string` | global | `render/codec.c` | I / 旧名で照合 | `render/codec.h` |
| 78 | `i915_vkc_read_bytes` | global | `render/codec.c` | I / 旧名で照合 | `render/codec.h` |
| 106 | `i915_vkc_read_float` | global | `render/codec.c` | I / 旧名で照合 | `render/codec.h` |
| 118 | `i915_vkc_skip_external_chain` | global | `render/codec.c` | I / 旧名で照合 | `render/codec.h` |
| 135 | `i915_vkc_reply_bytes` | global | `render/codec.c` | I / 旧名で照合 | `render/codec.h` |
| 151 | `i915_vkc_reply_float` | global | `render/codec.c` | I / 旧名で照合 | `render/codec.h` |

### vk/wsi.c

現行: [vk/wsi.c](../../src/drivers/gpu/i915/vk/wsi.c)。5定義。

| 行 | 現行関数 | 現行 | 移動先 | 区分・提案名 | 宣言ヘッダ（予定） |
| ---: | --- | --- | --- | --- | --- |
| 47 | `i915_vk_wsi_dispatch` | global | `render/wsi.c` | I / 旧名で照合 | `render/wsi.h` |
| 63 | `i915_vk_swapchain_create` | global | `render/wsi.c` | I / 旧名で照合 | `render/wsi.h` |
| 116 | `i915_vk_swapchain_destroy` | global | `render/wsi.c` | I / 旧名で照合 | `render/wsi.h` |
| 138 | `i915_vk_swapchain_acquire` | global | `render/wsi.c` | I / 旧名で照合 | `render/wsi.h` |
| 154 | `i915_vk_swapchain_present` | global | `render/wsi.c` | I / 旧名で照合 | `render/wsi.h` |

## F. 関数定義を抽出しなかったファイル

178ファイル。宣言、型、定数、データの移動先を別に扱う。
関数定義がないことは不要であることを意味しない。定数・型・ops外部宣言の参照先も移行対象。
列挙したheaderの吸収先は配置案で、個々の型layoutの統合承認ではない。

| 現行ファイル | 移動/吸収先候補 |
| --- | --- |
| [draw_fixture.h](../../src/drivers/gpu/i915/draw_fixture.h) | `tests/fixtures/draw-fixture.h` |
| [internal.h](../../src/drivers/gpu/i915/internal.h) | `i915.h` / `device.h` / `session.h` / `memory.h` / `ggtt.h` / `ppgtt.h` / `context.h` / `request.h` |
| [linux/i915-commands.inc](../../src/drivers/gpu/i915/linux/i915-commands.inc) | `data/i915-commands.inc` |
| [linux/i915-ids.inc](../../src/drivers/gpu/i915/linux/i915-ids.inc) | `data/i915-ids.inc` |
| [linux/i915-lrc-offsets.inc](../../src/drivers/gpu/i915/linux/i915-lrc-offsets.inc) | `data/i915-lrc-offsets.inc` |
| [linux/i915-mocs.inc](../../src/drivers/gpu/i915/linux/i915-mocs.inc) | `data/i915-mocs.inc` |
| [linux/i915-regs.inc](../../src/drivers/gpu/i915/linux/i915-regs.inc) | `data/i915-regs.inc` |
| [linux/i915-workarounds.inc](../../src/drivers/gpu/i915/linux/i915-workarounds.inc) | `data/i915-workarounds.inc` |
| [parity/backend.h](../../src/drivers/gpu/i915/parity/backend.h) | `device.h` / `mmio.h` / `memory.h` / `power.h` |
| [parity/backend_delayed.h](../../src/drivers/gpu/i915/parity/backend_delayed.h) | `sync.h` |
| [parity/backend_sync.h](../../src/drivers/gpu/i915/parity/backend_sync.h) | `sync.h` |
| [parity/bios.h](../../src/drivers/gpu/i915/parity/bios.h) | `display/vbt.h` |
| [parity/cdclk.h](../../src/drivers/gpu/i915/parity/cdclk.h) | `display/clock.h` |
| [parity/combo_phy.h](../../src/drivers/gpu/i915/parity/combo_phy.h) | `display/phy.h` |
| [parity/display_core.h](../../src/drivers/gpu/i915/parity/display_core.h) | `display/power.h` |
| [parity/display_nogem.h](../../src/drivers/gpu/i915/parity/display_nogem.h) | `display/takeover.h` |
| [parity/display_state.h](../../src/drivers/gpu/i915/parity/display_state.h) | `display/state.h` / `display/watermark.h` |
| [parity/dmc.h](../../src/drivers/gpu/i915/parity/dmc.h) | `display/dmc.h` |
| [parity/dp/dp_fake_hw.h](../../src/drivers/gpu/i915/parity/dp/dp_fake_hw.h) | `tests/display/dp-fake-hw.h` |
| [parity/dp/dp_fixture_latitude5330.h](../../src/drivers/gpu/i915/parity/dp/dp_fixture_latitude5330.h) | `tests/display/dp-fixture-latitude5330.h` |
| [parity/dp/dp_ref_types.h](../../src/drivers/gpu/i915/parity/dp/dp_ref_types.h) | `display/internal.h（型/inlineを所有headerへ再配分）` |
| [parity/dp/drm_dp.h](../../src/drivers/gpu/i915/parity/dp/drm_dp.h) | `data/display-drm-dp.inc` |
| [parity/dp/edp_ktest.h](../../src/drivers/gpu/i915/parity/dp/edp_ktest.h) | `tests/display/edp-ktest.h` |
| [parity/dp/intel_dp_aux.h](../../src/drivers/gpu/i915/parity/dp/intel_dp_aux.h) | `display/aux.h` |
| [parity/dp/intel_dp_aux_regs.h](../../src/drivers/gpu/i915/parity/dp/intel_dp_aux_regs.h) | `data/display-intel-dp-aux-regs.inc` |
| [parity/dp/intel_pps.h](../../src/drivers/gpu/i915/parity/dp/intel_pps.h) | `display/panel.h` |
| [parity/dp/intel_pps_regs.h](../../src/drivers/gpu/i915/parity/dp/intel_pps_regs.h) | `data/display-intel-pps-regs.inc` |
| [parity/dp/parity_dp_kernel.h](../../src/drivers/gpu/i915/parity/dp/parity_dp_kernel.h) | `display/dp.h` |
| [parity/dp/parity_edp.h](../../src/drivers/gpu/i915/parity/dp/parity_edp.h) | `display/dp.h` |
| [parity/dram_bw.h](../../src/drivers/gpu/i915/parity/dram_bw.h) | `display/watermark.h` |
| [parity/driver_probe.h](../../src/drivers/gpu/i915/parity/driver_probe.h) | `device.h` / `display/display.h` / `display/hotplug.h` / `display/power.h` / `display/watermark.h` |
| [parity/drm_device.h](../../src/drivers/gpu/i915/parity/drm_device.h) | `device.h` |
| [parity/eu_test.h](../../src/drivers/gpu/i915/parity/eu_test.h) | `tests/fixtures/eu-test.h` |
| [parity/firmware_adlp_dmc.c](../../src/drivers/gpu/i915/parity/firmware_adlp_dmc.c) | `data/firmware/firmware-adlp-dmc.c` |
| [parity/firmware_tgl_dmc.c](../../src/drivers/gpu/i915/parity/firmware_tgl_dmc.c) | `data/firmware/firmware-tgl-dmc.c` |
| [parity/firmware_vbt_dell_latitude_5320.c](../../src/drivers/gpu/i915/parity/firmware_vbt_dell_latitude_5320.c) | `data/firmware/firmware-vbt-dell-latitude-5320.c` |
| [parity/firmware_vbt_dell_latitude_5330.c](../../src/drivers/gpu/i915/parity/firmware_vbt_dell_latitude_5330.c) | `data/firmware/firmware-vbt-dell-latitude-5330.c` |
| [parity/gt_defaults.h](../../src/drivers/gpu/i915/parity/gt_defaults.h) | `context.h` |
| [parity/gt_engine.h](../../src/drivers/gpu/i915/parity/gt_engine.h) | `engine.h` |
| [parity/gt_fw_ranges.inc](../../src/drivers/gpu/i915/parity/gt_fw_ranges.inc) | `data/gt-fw-ranges.inc` |
| [parity/gt_init.h](../../src/drivers/gpu/i915/parity/gt_init.h) | `device.h` / `power.h` / `workarounds.h` / `ppgtt.h` |
| [parity/gt_lrc.h](../../src/drivers/gpu/i915/parity/gt_lrc.h) | `context.h` |
| [parity/gt_lrc_offsets.inc](../../src/drivers/gpu/i915/parity/gt_lrc_offsets.inc) | `data/gt-lrc-offsets.inc` |
| [parity/gt_mem.h](../../src/drivers/gpu/i915/parity/gt_mem.h) | `ggtt.h` / `memory.h` / `ppgtt.h` |
| [parity/gt_migrate.h](../../src/drivers/gpu/i915/parity/gt_migrate.h) | `engine.h` |
| [parity/gt_mmio.h](../../src/drivers/gpu/i915/parity/gt_mmio.h) | `mmio.h` |
| [parity/gt_request.h](../../src/drivers/gpu/i915/parity/gt_request.h) | `request.h` |
| [parity/gt_resume.h](../../src/drivers/gpu/i915/parity/gt_resume.h) | `engine.h` |
| [parity/gt_submit.h](../../src/drivers/gpu/i915/parity/gt_submit.h) | `request.h` |
| [parity/gt_tlb.h](../../src/drivers/gpu/i915/parity/gt_tlb.h) | `ppgtt.h` |
| [parity/gt_verify_wa.h](../../src/drivers/gpu/i915/parity/gt_verify_wa.h) | `workarounds.h` |
| [parity/irq.h](../../src/drivers/gpu/i915/parity/irq.h) | `irq.h` |
| [parity/ktest.h](../../src/drivers/gpu/i915/parity/ktest.h) | `tests/fixtures/ktest.h` |
| [parity/lcd/edid_ref_types.h](../../src/drivers/gpu/i915/parity/lcd/edid_ref_types.h) | `display/internal.h（型/inlineを所有headerへ再配分）` |
| [parity/lcd/hpd_drm_connector_status.h](../../src/drivers/gpu/i915/parity/lcd/hpd_drm_connector_status.h) | `display/internal.h（型/inlineを所有headerへ再配分）` |
| [parity/lcd/hpd_for_each_pin.h](../../src/drivers/gpu/i915/parity/lcd/hpd_for_each_pin.h) | `data/display-hpd-for-each-pin.inc` |
| [parity/lcd/hpd_hotplug_state.h](../../src/drivers/gpu/i915/parity/lcd/hpd_hotplug_state.h) | `display/internal.h（型/inlineを所有headerへ再配分）` |
| [parity/lcd/hpd_hotplug_types.h](../../src/drivers/gpu/i915/parity/lcd/hpd_hotplug_types.h) | `display/internal.h（型/inlineを所有headerへ再配分）` |
| [parity/lcd/hpd_mreg_drm_dp.h](../../src/drivers/gpu/i915/parity/lcd/hpd_mreg_drm_dp.h) | `data/display-hpd-mreg-drm-dp.inc` |
| [parity/lcd/hpd_mreg_gmbus.h](../../src/drivers/gpu/i915/parity/lcd/hpd_mreg_gmbus.h) | `data/display-hpd-mreg-gmbus.inc` |
| [parity/lcd/hpd_mreg_gmbus_pins.h](../../src/drivers/gpu/i915/parity/lcd/hpd_mreg_gmbus_pins.h) | `data/display-hpd-mreg-gmbus-pins.inc` |
| [parity/lcd/hpd_mreg_i915_reg.h](../../src/drivers/gpu/i915/parity/lcd/hpd_mreg_i915_reg.h) | `data/display-hpd-mreg-i915-reg.inc` |
| [parity/lcd/hpd_pin_enum.h](../../src/drivers/gpu/i915/parity/lcd/hpd_pin_enum.h) | `data/display-hpd-pin-enum.inc` |
| [parity/lcd/lcd_buf_trans_types.h](../../src/drivers/gpu/i915/parity/lcd/lcd_buf_trans_types.h) | `display/internal.h（型/inlineを所有headerへ再配分）` |
| [parity/lcd/lcd_dbuf_slice_enum.h](../../src/drivers/gpu/i915/parity/lcd/lcd_dbuf_slice_enum.h) | `data/display-lcd-dbuf-slice-enum.inc` |
| [parity/lcd/lcd_dbuf_types.h](../../src/drivers/gpu/i915/parity/lcd/lcd_dbuf_types.h) | `display/internal.h（型/inlineを所有headerへ再配分）` |
| [parity/lcd/lcd_ddi_regs.h](../../src/drivers/gpu/i915/parity/lcd/lcd_ddi_regs.h) | `data/display-lcd-ddi-regs.inc` |
| [parity/lcd/lcd_ddi_types.h](../../src/drivers/gpu/i915/parity/lcd/lcd_ddi_types.h) | `display/internal.h（型/inlineを所有headerへ再配分）` |
| [parity/lcd/lcd_dp_msa.h](../../src/drivers/gpu/i915/parity/lcd/lcd_dp_msa.h) | `data/display-lcd-dp-msa.inc` |
| [parity/lcd/lcd_dp_phy_enum.h](../../src/drivers/gpu/i915/parity/lcd/lcd_dp_phy_enum.h) | `data/display-lcd-dp-phy-enum.inc` |
| [parity/lcd/lcd_dpll_id_enum.h](../../src/drivers/gpu/i915/parity/lcd/lcd_dpll_id_enum.h) | `data/display-lcd-dpll-id-enum.inc` |
| [parity/lcd/lcd_drm_colorspace.h](../../src/drivers/gpu/i915/parity/lcd/lcd_drm_colorspace.h) | `display/internal.h（型/inlineを所有headerへ再配分）` |
| [parity/lcd/lcd_fake_hw.h](../../src/drivers/gpu/i915/parity/lcd/lcd_fake_hw.h) | `tests/display/lcd-fake-hw.h` |
| [parity/lcd/lcd_flip_compat.h](../../src/drivers/gpu/i915/parity/lcd/lcd_flip_compat.h) | `display/internal.h（型/inlineを所有headerへ再配分）` |
| [parity/lcd/lcd_hw_check.h](../../src/drivers/gpu/i915/parity/lcd/lcd_hw_check.h) | `tests/display/scanout-hw-check.h` |
| [parity/lcd/lcd_i915_colorkey.h](../../src/drivers/gpu/i915/parity/lcd/lcd_i915_colorkey.h) | `display/internal.h（型/inlineを所有headerへ再配分）` |
| [parity/lcd/lcd_modeset_ktest.h](../../src/drivers/gpu/i915/parity/lcd/lcd_modeset_ktest.h) | `tests/display/lcd-modeset-ktest.h` |
| [parity/lcd/lcd_mreg_backlight.h](../../src/drivers/gpu/i915/parity/lcd/lcd_mreg_backlight.h) | `data/display-lcd-mreg-backlight.inc` |
| [parity/lcd/lcd_mreg_color.h](../../src/drivers/gpu/i915/parity/lcd/lcd_mreg_color.h) | `data/display-lcd-mreg-color.inc` |
| [parity/lcd/lcd_mreg_combo_phy.h](../../src/drivers/gpu/i915/parity/lcd/lcd_mreg_combo_phy.h) | `data/display-lcd-mreg-combo-phy.inc` |
| [parity/lcd/lcd_mreg_cx0.h](../../src/drivers/gpu/i915/parity/lcd/lcd_mreg_cx0.h) | `data/display-lcd-mreg-cx0.inc` |
| [parity/lcd/lcd_mreg_display.h](../../src/drivers/gpu/i915/parity/lcd/lcd_mreg_display.h) | `data/display-lcd-mreg-display.inc` |
| [parity/lcd/lcd_mreg_display_device.h](../../src/drivers/gpu/i915/parity/lcd/lcd_mreg_display_device.h) | `data/display-lcd-mreg-display-device.inc` |
| [parity/lcd/lcd_mreg_display_reg_defs.h](../../src/drivers/gpu/i915/parity/lcd/lcd_mreg_display_reg_defs.h) | `data/display-lcd-mreg-display-reg-defs.inc` |
| [parity/lcd/lcd_mreg_display_types.h](../../src/drivers/gpu/i915/parity/lcd/lcd_mreg_display_types.h) | `data/display-lcd-mreg-display-types.inc` |
| [parity/lcd/lcd_mreg_dmc.h](../../src/drivers/gpu/i915/parity/lcd/lcd_mreg_dmc.h) | `data/display-lcd-mreg-dmc.inc` |
| [parity/lcd/lcd_mreg_dmc_c.h](../../src/drivers/gpu/i915/parity/lcd/lcd_mreg_dmc_c.h) | `data/display-lcd-mreg-dmc-c.inc` |
| [parity/lcd/lcd_mreg_drm_dp.h](../../src/drivers/gpu/i915/parity/lcd/lcd_mreg_drm_dp.h) | `data/display-lcd-mreg-drm-dp.inc` |
| [parity/lcd/lcd_mreg_hdmi_dip.h](../../src/drivers/gpu/i915/parity/lcd/lcd_mreg_hdmi_dip.h) | `data/display-lcd-mreg-hdmi-dip.inc` |
| [parity/lcd/lcd_mreg_i915_reg.h](../../src/drivers/gpu/i915/parity/lcd/lcd_mreg_i915_reg.h) | `data/display-lcd-mreg-i915-reg.inc` |
| [parity/lcd/lcd_mreg_link_training.h](../../src/drivers/gpu/i915/parity/lcd/lcd_mreg_link_training.h) | `data/display-lcd-mreg-link-training.inc` |
| [parity/lcd/lcd_mreg_power.h](../../src/drivers/gpu/i915/parity/lcd/lcd_mreg_power.h) | `data/display-lcd-mreg-power.inc` |
| [parity/lcd/lcd_mreg_reg_defs.h](../../src/drivers/gpu/i915/parity/lcd/lcd_mreg_reg_defs.h) | `data/display-lcd-mreg-reg-defs.inc` |
| [parity/lcd/lcd_mreg_vdsc.h](../../src/drivers/gpu/i915/parity/lcd/lcd_mreg_vdsc.h) | `data/display-lcd-mreg-vdsc.inc` |
| [parity/lcd/lcd_mreg_wm.h](../../src/drivers/gpu/i915/parity/lcd/lcd_mreg_wm.h) | `data/display-lcd-mreg-wm.inc` |
| [parity/lcd/lcd_pattern.h](../../src/drivers/gpu/i915/parity/lcd/lcd_pattern.h) | `tests/display/lcd-pattern.h` |
| [parity/lcd/lcd_pch_enum.h](../../src/drivers/gpu/i915/parity/lcd/lcd_pch_enum.h) | `data/display-lcd-pch-enum.inc` |
| [parity/lcd/lcd_plane_compat.h](../../src/drivers/gpu/i915/parity/lcd/lcd_plane_compat.h) | `display/internal.h（型/inlineを所有headerへ再配分）` |
| [parity/lcd/lcd_plane_regs.h](../../src/drivers/gpu/i915/parity/lcd/lcd_plane_regs.h) | `data/display-lcd-plane-regs.inc` |
| [parity/lcd/lcd_plane_types.h](../../src/drivers/gpu/i915/parity/lcd/lcd_plane_types.h) | `display/internal.h（型/inlineを所有headerへ再配分）` |
| [parity/lcd/lcd_power_domain_enum.h](../../src/drivers/gpu/i915/parity/lcd/lcd_power_domain_enum.h) | `data/display-lcd-power-domain-enum.inc` |
| [parity/lcd/lcd_power_domain_set_types.h](../../src/drivers/gpu/i915/parity/lcd/lcd_power_domain_set_types.h) | `display/internal.h（型/inlineを所有headerへ再配分）` |
| [parity/lcd/lcd_psr_selfetch_regs.h](../../src/drivers/gpu/i915/parity/lcd/lcd_psr_selfetch_regs.h) | `data/display-lcd-psr-selfetch-regs.inc` |
| [parity/lcd/lcd_ref_types.h](../../src/drivers/gpu/i915/parity/lcd/lcd_ref_types.h) | `display/internal.h（型/inlineを所有headerへ再配分）` |
| [parity/lcd/lcd_seq_compat.h](../../src/drivers/gpu/i915/parity/lcd/lcd_seq_compat.h) | `display/internal.h（型/inlineを所有headerへ再配分）` |
| [parity/lcd/lcd_show_ktest.h](../../src/drivers/gpu/i915/parity/lcd/lcd_show_ktest.h) | `tests/display/lcd-show-ktest.h` |
| [parity/lcd/lcd_trans_regs.h](../../src/drivers/gpu/i915/parity/lcd/lcd_trans_regs.h) | `data/display-lcd-trans-regs.inc` |
| [parity/lcd/lcd_wm_types.h](../../src/drivers/gpu/i915/parity/lcd/lcd_wm_types.h) | `display/internal.h（型/inlineを所有headerへ再配分）` |
| [parity/lcd/lcdg_ktest.h](../../src/drivers/gpu/i915/parity/lcd/lcdg_ktest.h) | `tests/display/lcdg-ktest.h` |
| [parity/lcd/opreg_pci_config.h](../../src/drivers/gpu/i915/parity/lcd/opreg_pci_config.h) | `display/internal.h（型/inlineを所有headerへ再配分）` |
| [parity/lcd/opreg_struct.h](../../src/drivers/gpu/i915/parity/lcd/opreg_struct.h) | `display/internal.h（型/inlineを所有headerへ再配分）` |
| [parity/lcd/opregion_fwtest.h](../../src/drivers/gpu/i915/parity/lcd/opregion_fwtest.h) | `tests/display/opregion-fwtest.h` |
| [parity/lcd/opregion_ktest.h](../../src/drivers/gpu/i915/parity/lcd/opregion_ktest.h) | `tests/display/opregion-ktest.h` |
| [parity/lcd/parity_acpi_glue.inc](../../src/drivers/gpu/i915/parity/lcd/parity_acpi_glue.inc) | `data/parity-acpi-glue.inc` |
| [parity/lcd/parity_hotplug.h](../../src/drivers/gpu/i915/parity/lcd/parity_hotplug.h) | `display/hotplug.h` |
| [parity/lcd/parity_lcd_calc.h](../../src/drivers/gpu/i915/parity/lcd/parity_lcd_calc.h) | `display/state.h` |
| [parity/lcd/parity_lcd_kernel.h](../../src/drivers/gpu/i915/parity/lcd/parity_lcd_kernel.h) | `display/modeset.h` / `display/scanout.h` / `display/state.h` / `tests/display/kernel-scenarios.h` |
| [parity/lcd/parity_lcd_modeset.h](../../src/drivers/gpu/i915/parity/lcd/parity_lcd_modeset.h) | `display/modeset.h` / `display/panel.h` / `display/present.h` / `display/vblank.h` / `tests/display/modeset.h` |
| [parity/lcd/parity_lcd_modeset_int.h](../../src/drivers/gpu/i915/parity/lcd/parity_lcd_modeset_int.h) | `display/internal.h` |
| [parity/lcd/parity_lcd_observe.h](../../src/drivers/gpu/i915/parity/lcd/parity_lcd_observe.h) | `display/diagnostics.h` |
| [parity/lcd/parity_lcd_ops.h](../../src/drivers/gpu/i915/parity/lcd/parity_lcd_ops.h) | `display/internal.h` / `tests/fixtures/display-io.h（実HW操作とfakeの境界を照合）` |
| [parity/lcd/parity_lcd_show.h](../../src/drivers/gpu/i915/parity/lcd/parity_lcd_show.h) | `display/modeset.h` |
| [parity/lcd/parity_lcd_trace.h](../../src/drivers/gpu/i915/parity/lcd/parity_lcd_trace.h) | `display/diagnostics.h` |
| [parity/lcd/parity_n1.h](../../src/drivers/gpu/i915/parity/lcd/parity_n1.h) | `display/takeover.h` |
| [parity/lcd/parity_opregion.h](../../src/drivers/gpu/i915/parity/lcd/parity_opregion.h) | `display/opregion.h` |
| [parity/lcd/scanout.h](../../src/drivers/gpu/i915/parity/lcd/scanout.h) | `display/scanout.h` |
| [parity/lcd/scanout_ktest.h](../../src/drivers/gpu/i915/parity/lcd/scanout_ktest.h) | `tests/display/scanout-ktest.h` |
| [parity/legacy_shim.h](../../src/drivers/gpu/i915/parity/legacy_shim.h) | `context.h` / `request.h` / `reset.h（名前差替えを解消）` |
| [parity/native_precheck.h](../../src/drivers/gpu/i915/parity/native_precheck.h) | `display/takeover.h` |
| [parity/opregion_service.h](../../src/drivers/gpu/i915/parity/opregion_service.h) | `display/opregion.h` |
| [parity/opregion_vbt.h](../../src/drivers/gpu/i915/parity/opregion_vbt.h) | `display/vbt.h` |
| [parity/osdep/dma.h](../../src/drivers/gpu/i915/parity/osdep/dma.h) | `memory.h` |
| [parity/osdep/firmware.h](../../src/drivers/gpu/i915/parity/osdep/firmware.h) | `firmware.h` |
| [parity/osdep/mmio.h](../../src/drivers/gpu/i915/parity/osdep/mmio.h) | `mmio.h` |
| [parity/osdep/pci.h](../../src/drivers/gpu/i915/parity/osdep/pci.h) | `device.h` |
| [parity/osdep/runtime_pm.h](../../src/drivers/gpu/i915/parity/osdep/runtime_pm.h) | `power.h` |
| [parity/osdep/sync.h](../../src/drivers/gpu/i915/parity/osdep/sync.h) | `sync.h` |
| [parity/osdep/trace.h](../../src/drivers/gpu/i915/parity/osdep/trace.h) | `trace.h` |
| [parity/parity.h](../../src/drivers/gpu/i915/parity/parity.h) | `device.h` / `tests/execution/runner.h` |
| [parity/pch.h](../../src/drivers/gpu/i915/parity/pch.h) | `device-info.h` |
| [parity/pcode.h](../../src/drivers/gpu/i915/parity/pcode.h) | `power.h` |
| [parity/power_domains.h](../../src/drivers/gpu/i915/parity/power_domains.h) | `display/power.h` |
| [parity/pte.h](../../src/drivers/gpu/i915/parity/pte.h) | `ppgtt.h` |
| [parity/pxp.h](../../src/drivers/gpu/i915/parity/pxp.h) | `device.h` |
| [parity/reset.h](../../src/drivers/gpu/i915/parity/reset.h) | `reset.h` |
| [parity/resident.h](../../src/drivers/gpu/i915/parity/resident.h) | `device.h` / `request.h` / `display/display.h` |
| [parity/resident_display.h](../../src/drivers/gpu/i915/parity/resident_display.h) | `display/display.h` / `display/scanout.h` / `display/present.h` |
| [parity/runner.h](../../src/drivers/gpu/i915/parity/runner.h) | `device.h`（登録/readinessの本番責務） |
| [parity/tests/mock_dma.h](../../src/drivers/gpu/i915/parity/tests/mock_dma.h) | `tests/fixtures/mock-dma.h` |
| [parity/tests/mock_mmio.h](../../src/drivers/gpu/i915/parity/tests/mock_mmio.h) | `tests/fixtures/mock-mmio.h` |
| [parity/tests/mock_pci.h](../../src/drivers/gpu/i915/parity/tests/mock_pci.h) | `tests/fixtures/mock-pci.h` |
| [parity/timer_calc.h](../../src/drivers/gpu/i915/parity/timer_calc.h) | `sync.h` |
| [parity/vbt/intel_bios.h](../../src/drivers/gpu/i915/parity/vbt/intel_bios.h) | `display/vbt.h` |
| [parity/vbt/intel_vbt_defs.h](../../src/drivers/gpu/i915/parity/vbt/intel_vbt_defs.h) | `data/display-intel-vbt-defs.inc` |
| [parity/vbt/parity_vbt.h](../../src/drivers/gpu/i915/parity/vbt/parity_vbt.h) | `display/vbt.h` |
| [parity/vbt/vbt_ref_types.h](../../src/drivers/gpu/i915/parity/vbt/vbt_ref_types.h) | `display/vbt.h` |
| [parity/vga.h](../../src/drivers/gpu/i915/parity/vga.h) | `display/takeover.h` |
| [parity/wait.h](../../src/drivers/gpu/i915/parity/wait.h) | `sync.h` |
| [tex_fixture_fhd_gen.inc](../../src/drivers/gpu/i915/tex_fixture_fhd_gen.inc) | `tests/fixtures/tex-fixture-fhd-gen.inc` |
| [tex_fixture_gen.inc](../../src/drivers/gpu/i915/tex_fixture_gen.inc) | `tests/fixtures/tex-fixture-gen.inc` |
| [vk/cmd.h](../../src/drivers/gpu/i915/vk/cmd.h) | `render/codec.h` / `render/object.h` / `render/dispatch.h` |
| [vk/cmdbuf.h](../../src/drivers/gpu/i915/vk/cmdbuf.h) | `render/command.h` |
| [vk/compile.h](../../src/drivers/gpu/i915/vk/compile.h) | `compiler/compiler.h` |
| [vk/display.h](../../src/drivers/gpu/i915/vk/display.h) | `render/wsi.h` |
| [vk/eu.h](../../src/drivers/gpu/i915/vk/eu.h) | `compiler/eu.h` |
| [vk/gfx.h](../../src/drivers/gpu/i915/vk/gfx.h) | `render/internal.h` / `render/object.h` / `render/image.h` / `render/pipeline.h` / `render/blit.h` |
| [vk/linux/3dstate-gen12.inc](../../src/drivers/gpu/i915/vk/linux/3dstate-gen12.inc) | `data/3dstate-gen12.inc` |
| [vk/linux/eu-encoding-gen12.inc](../../src/drivers/gpu/i915/vk/linux/eu-encoding-gen12.inc) | `data/eu-encoding-gen12.inc` |
| [vk/linux/surface-state-gen12.inc](../../src/drivers/gpu/i915/vk/linux/surface-state-gen12.inc) | `data/surface-state-gen12.inc` |
| [vk/pipe.h](../../src/drivers/gpu/i915/vk/pipe.h) | `render/batch.h` / `render/pipeline.h` / `render/state.h` |
| [vk/res.h](../../src/drivers/gpu/i915/vk/res.h) | `render/memory.h` / `render/image.h` / `render/descriptor.h` |
| [vk/spirv.h](../../src/drivers/gpu/i915/vk/spirv.h) | `compiler/compiler.h` / `compiler/ir.h` |
| [vk/sync.h](../../src/drivers/gpu/i915/vk/sync.h) | `render/sync.h` |
| [vk/vk-internal.h](../../src/drivers/gpu/i915/vk/vk-internal.h) | `render/internal.h` / `compiler/ir.h` |
| [vk/vk.h](../../src/drivers/gpu/i915/vk/vk.h) | `render/render.h` |
| [vk/vkc.h](../../src/drivers/gpu/i915/vk/vkc.h) | `render/codec.h` |
| [vk/vkref-generated.inc](../../src/drivers/gpu/i915/vk/vkref-generated.inc) | `tests/fixtures/vkref-generated.inc` |
| [vk/wsi.h](../../src/drivers/gpu/i915/vk/wsi.h) | `render/wsi.h` |

## G. この台帳でまだ決めていないこと

1. Iの最終的な関数名・完全署名・公開範囲。所有先は指定したが、既存callerをすべて検証した確定ABIではない。
2. 旧Vulkan処理やcompat helperの削除可否。Hへ吸収できること、必要なopcodeと試験を失わないことを確認する。
3. 中央初期化関数、LCD resident関数等の内部statement単位の切出し。主担当と分割先は本編に記載。
4. 非同期化・recoveryの新実装。現在の同期動作と明示的未対応を保存した配置変更から切り分ける。
5. 全build構成での条件付きsymbol、関数ポインタ、生成マクロ依存。移行対象構成を選んでbuild/link確認する。

確認済みの範囲: 旧関数名と行番号のソース照合、抽出した各定義に一つの主配置先があること、
本編で指定した代表移動との整合、文書間リンク。実装・build・実機検証は未実施。

移行は関数だけのコピーではない。[保全台帳](i915-refactoring-assets.md)の元ファイル全体を残し、
状態・typedef・macro・定数・local static・callback・生成器・出典の移動または削除根拠が揃ってから旧ファイルを除く。
I候補およびCの未解決辺を「export設計済み」と扱わない。未解決のまとまりは旧TU境界で保持し、
その依存を解いてから最終配置へ分割する。最終API未確定は本台帳の明示的な残件である。
