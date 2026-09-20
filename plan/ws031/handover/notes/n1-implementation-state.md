# N1 (firmware display takeover) — implementation state, E-124

Started 2026-09-20 after E-123 (HDMI, two screens).  The user asked for N1 in ONE go: readout + sanitize + takeover
+ re-light, then as many bare-metal runs at the end as it takes.

## Design of the run (native)

On bare metal the only console is the screen, so the run must stay readable:

1. **Readout + sanitize**, everything printed (visible on the firmware's framebuffer console).
2. A pause (~40 s) so the screen can be photographed before anything changes.
3. **Takeover**: the reference's `intel_crtc_disable_noatomic()` stops the firmware's pipe A (screen goes black).
4. **Re-light with our own modeset**, and the plane points at **the firmware's framebuffer** (GGTT pages 0..300,
   640x480, the console's own memory) instead of a new buffer: the console's text appears again, on a pipe we own,
   so the verdict lines after the takeover are visible and photographable.
5. Hold, then stop through the reference's path (and the E-123 LAST-RESORT stop remains the safety net).

The driver's GGTT allocations sit at the top of GGTT (`probe.c:854`), so the firmware's pages 0..300 are never
rewritten (`native_decide.c:13` already checks the no-overlap invariant).

## What is generated (round 94, `tools/lcd-e124/round94.py`)

- `intel_display.c` extras: hsw_panel_transcoders, hsw_enabled_transcoders, hsw_get_transcoder_state,
  intel_get_transcoder_timings, intel_get_pipe_src_size, bdw_get_pipe_misc_output_format, hsw_get_pipe_config,
  intel_crtc_get_pipe_config, intel_crtc_readout_derived_state, intel_encoder_get_config, intel_set_plane_visible,
  intel_plane_fixup_bitmasks, intel_plane_disable_noatomic, transcoder_ddi_func_is_enabled, intel_crtc_dotclock.
- `intel_ddi.c` extras: intel_ddi_get_encoder_pipes, intel_ddi_get_hw_state, intel_ddi_read_func_ctl,
  ddi_dotclock_get, intel_ddi_get_config, intel_ddi_get_clock, _icl_ddi_get_pll, icl_ddi_combo_get_pll,
  icl_ddi_combo_get_config, intel_ddi_sync_state, intel_ddi_get_power_domains.
- `intel_dpll_mgr.c` extras: intel_dpll_get_hw_state, combo_pll_get_hw_state, icl_pll_get_hw_state,
  readout_dpll_hw_state, intel_dpll_readout_hw_state, sanitize_dpll_state, intel_dpll_sanitize_state.
- `skl_universal_plane.c`: skl_plane_get_hw_state.  `skl_watermark.c`: skl_wm_level_from_reg_val,
  skl_pipe_wm_get_hw_state, skl_pipe_ddb_get_hw_state, skl_ddb_get_hw_plane_state, skl_ddb_entry_union,
  skl_wm_get_hw_state, skl_dbuf_is_misconfigured, skl_wm_sanitize, skl_wm_get_hw_state_and_sanitize.
- `intel_vblank.c`: intel_crtc_update_active_timings.  `intel_crtc.c`: intel_crtc_wait_for_next_vblank.
- NEW unit `lcd/intel_modeset_setup_port.c` (+ `parity_modeset_setup_glue.inc`): intel_crtc_disable_noatomic and
  its two halves, the connector / encoder state resets, intel_sanitize_crtc / _all_crtcs / _encoder,
  readout_plane_state, intel_modeset_readout_hw_state, get_encoder_power_domains, intel_modeset_setup_hw_state.

## Compat (`lcd/n1_compat.h`, included from `lcd_modeset_compat.h` so every unit sees it)

Done: the device's object walks (crtcs / encoders / connectors registered by the runner), the readout's power
accessors (`*_if_enabled`, `put_all_in_set`), REG_FIELD_GET / ffs, and the platform branches this machine never
takes (Broxton PHY, LSPCON).

**Remaining (the current build's 55 distinct errors)**: cursor watermark registers (CUR_WM / CUR_BUF_CFG /
CUR_WM_TRANS / SAGV), the transcoder M/N readout (intel_cpu_transcoder_get_m1_n1 / m2_n2), drm_rect_init,
for_each_cpu_transcoder_masked / HAS_TRANSCODER, bigjoiner helpers (enabled_bigjoiner_pipes,
get_bigjoiner_master_pipe, intel_bigjoiner_adjust_pipe_src — recorded steps on this machine), DSI
(bxt_get_dsi_transcoder_state), assert_enabled_transcoders, drm_crtc_wait_one_vblank, and the fields a readout
fills that the compat structs do not have yet (crtc->hw_readout_power_domains, drm_crtc.state, display.dbuf,
intel_shared_dpll_funcs.get_hw_state).

## Next steps

1. Finish the compat iteration (generate what is cheap, record as steps what belongs to unported subsystems).
2. Write the N1 runner (`parity_n1_run`) with the phases above and a `-DPARITY_N1_TEST=1` flag; log every readout
   value, the sanitize decisions, and the takeover's register readbacks.
3. Bare-metal image + run; the screen is the evidence (photograph before the takeover and after the re-light).

## Regression

The final E-123 sweep (17 modes: the 14 accepted ones + HDMI-B + DUAL + DUAL-SHARED) is running on the final E-123
source; its result belongs to E-123, not to this work.

## 2026-09-20 進捗（compat の残り）

rounds 94..97 適用後、ビルドの残りは 8 種類:
- `duplicate member enabled_power_domains`（round95 で追加した行が既存と重複: struct intel_crtc から片方を削る）
- `conflicting types for to_i915`（どこかの unit に別定義。lcd_compat.h の定義に寄せる）
- power domain: `intel_display_power_domain_set` と `intel_power_domain_mask` の取り違え（n1_compat のマクロ）
- `display.dbuf`（skl_watermark の readout が触るデバイス側の DBUF オブジェクト）
- `crtc_state.sink_format`（readout が埋める新しい項目）
- 残りは上記に伴う構文エラー

## 2026-09-20 続き（rounds 95..100 適用後）

残り 26 件（種類では 8 つ前後）。内訳:
- `to_i915` の衝突（intel_crtc_port.c:338 付近。lcd_compat.h の定義に一本化する）
- 電源ドメイン: `intel_power_domain_mask *` を `intel_display_power_domain_set *` に渡している箇所（intel_display_port.c 829/832/1060/1112 付近。hw_readout_power_domains は set に変更済み、呼び出し側の整合を取る）
- `intel_crtc_compute_pixel_rate` / `to_intel_plane` / `struct intel_plane_state` の不完全定義（readout が plane を触る箇所。生成に追加するか compat で補う）
- 付随する構文エラー

sweep(E-123 最終ソース)は受け入れ済み 14 モードで 14/14 PASS。追加 3 モード(hdmib/dual/dualsh)は N1 のビルドが通ってから流し直す。

## 2026-09-20 E-124 完了分（実装とVM確認）

生成に追加: `intel_modeset_setup.c` 全体（readout / sanitize / takeover、+ intel_sanitize_plane_mapping,
intel_early_display_was）、`intel_ddi.c` の intel_ddi_connector_get_hw_state / _sanitize_encoder_pll_mapping /
_icl_ddi_is_clock_enabled / icl_ddi_combo_is_clock_enabled、`skl_universal_plane.c` の skl_plane_get_hw_state、
`skl_watermark.c` の skl_ddb_entries_overlap、`intel_bw.c` の intel_bw_crtc_update / _num_active_planes、
`intel_dpll_mgr.c` の intel_dpll_get_freq / icl_ddi_combo_pll_get_freq。

glue: `parity_modeset_setup_glue.inc`（読み出し用オブジェクト登録 = pipe ごとの crtc と primary plane、
束ねられた screen の encoder/connector、readout 用 device、電源の *_if_enabled、使い捨て atomic state、
parity_n1_readout / _takeover / _release）、`parity_n1.h`。
runner: `parity_lcd_kernel_n1_run()`（bind prepare → readout → 30 秒の撮影待ち → takeover →
ファームウェアの framebuffer を指したまま再点灯 → 40 秒 → 停止）。`-DPARITY_N1_TEST=1`。
N0 は N1 ビルドでは ACTIVE_PIPE を停止理由にしない（native_decide.c）。

### 実装中に見つかった重大バグ 2 件（どちらも構造体の並び）

1. `struct intel_crtc_state` は `uapi` を先頭にしなければならない。`to_intel_crtc_state()` はキャストで、
   参照実装では uapi が先頭。先頭でないと readout の memset が 1 要素ぶん外へ溢れ、隣の配列（plane 登録簿）
   を壊す。症状は「plane の hook が NULL になり無出力でハング」。
2. `struct intel_crtc` は `base` を先頭にしなければならない。`to_intel_crtc(NULL)` が NULL であることに
   sanitize 側が依存している（`crtc ? ... : NULL`）。先頭でないと非 NULL のゴミを返して落ちる。

いずれも「キャスト／container_of で先頭性に依存する構造体には、先頭に項目を足さない」という一般則。

### VM（GPU passthrough）での確認

readout は完走し、`active pipes 0x0` = ファームウェアの画面なし →「nothing to take over」で PASS。
電源参照 0、wait timeout 0、unresolved steps 27（未移植の記録）、decided 2。
takeover と再点灯はベアメタルでしか試せない（VM のファームウェアは画面を点けない）。

## 2026-09-20 E-124 ベアメタル結果（N1 実機 PASS）

7 回のベアメタル起動で段階的に潰した停止要因（すべて「ファームウェアの画面が生きているときだけ通る枝」）:

1. **P5c の破壊的 sanitize**: 旧 readout は動作中 pipe の電源ドメインを参照確保しないため、DDI_IO A ウェルが
   「未使用」に見えて落とされ、画が消えた（バックライトは点いたまま）。N1 ビルドでは実施しない。
2. **LCD テストの preflight**: 「ディスプレイが idle でない」で門前払い → N1 ビルドでは RUNNING を正常条件に。
3. **P7 intel_initial_commit**: アクティブ crtc があると BLOCKED で probe 終了 → N1 ビルドでは記録して続行
   （参照実装もログのみで続行する）。
4. **P7 intel_power_domains_enable**: INIT 参照の解放で、誰も参照していないウェル＝ファームウェア画面の
   ウェルが落ちる。参照実装は直前の intel_initial_commit が引き継いでいるから安全。N1 ビルドでは
   **N1 のテストが終わるまで INIT 参照を保持**。
5. **encoder の readout フック未束ね**: 有効な encoder では `encoder->get_config()` が呼ばれ、NULL で
   amd64 fault v=14 err=0x10 rip=0。`intel_ddi_get_config/sync_state/get_power_domains/get_hw_state` を束ねた。
6. **takeover の atomic state に crtc state 未記録**: `hsw_crtc_disable` が old_crtc_state を NULL 参照。
   参照実装の `intel_atomic_get_crtc_state()` は crtc を state に追加するので、それを再現。
7. **readout デバイスの `drm.vblank` が NULL**: 有効 crtc の `drm_calc_timestamping_constants()` が NULL 書き込み
   （amd64 fault err=2 cr2=0、rip を nm で引くと intel_crtc_update_active_timings+200）。配列を持たせた。
   併せて `display.funcs.color`・`crtc->base.funcs`（frame counter）も束ねた。

**結果（実機・写真）**: `PICTURE UP: pattern id=124` → `N1: the panel is lit from OUR buffer ... the console is
mirrored into it` → `console mirror: 45 frames copied (rc=0)` → `flip to the firmware framebuffer: rc=0 result=0
live 0xfdfc0000 -> 0x00000000 frame 5446 -> 5447` → `flip back: rc=0`。すなわち readout / sanitize / takeover /
自前 modeset での再点灯 / コンソールのミラー / flip 往復 が実機で動作。

**判明した残件**: ファームウェアの `PLANE_SURF` は **0x00000000**（GGTT 先頭）で、そこは probe の GGTT 初期化が
scratch PTE で上書き済み。よって「ファームウェア自身のバッファをその GGTT アドレスで表示」は黒になる。
本物を出すにはファームウェアの物理ページを GGTT に貼り直す必要がある（参照実装の fbdev 引き継ぎ相当）。
コンソールのミラー（CPU コピー）で可読性は確保済み。
