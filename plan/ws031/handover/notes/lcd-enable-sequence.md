# eDP modeset の enable 列（正本の呼出し元から機械的に得た列）

WS031 E-113（2026-09-19）。対象 = ADL-P、eDP／DP SST、combo PHY の port A、pipe A／TRANSCODER_A、1920×1080、HBR×2、18 bpp。path は `agent-1:~/zedBSD/` 基点。

## これは何か
正本（Linux 6.8.12 i915）の **呼出し元そのもの** — `hsw_crtc_enable()`、`intel_ddi_pre_pll_enable()`、`intel_ddi_pre_enable()` → `intel_ddi_pre_enable_dp()` → `tgl_ddi_pre_enable_dp()`、`intel_enable_ddi()` → `intel_enable_ddi_dp()` — を生成 file（`parity/lcd/intel_display_port.c`、`intel_ddi_port.c`）に取り込み、recorder に対して走らせた結果です（`parity_lcd_emit_enable_sequence()`）。**順序は正本の text が決めています**。
- 移植済みの callee は実際に走り、register 操作（write／rmw）として列に出ます。
- **未移植の callee は「名前つき step」**として、正本が呼ぶ位置に出ます（`parity/lcd/lcd_seq_compat.h` に 1 個ずつ定義。定義の無い callee は compile できないので、黙って消えることはありません）。
- `>` で始まる step は zedBSD 側の dispatcher（正本の `intel_encoders_*()` は atomic state の connector を走査して encoder の hook を呼ぶ。ここでは encoder が 1 個なので、hook を `intel_ddi_init()` と同じ対応で束ねて直接呼ぶ）。
- **何も書き込んでいません**。列は手順の記述で、値のある行は計算値です。

## 読み方の注意（この表が言っていないこと）
- step が「この機体で必要か／何もしない分岐か」は**この表からは分かりません**。例: `icl_program_mg_dp_mode` は Type-C PHY 用で combo PHY では早期 return のはずですが、それは callee を読んで確認してから書きます（E-111 で順序を記憶で書いて誤った反省）。右端の列は今は「同名関数が parity 側にあるか」の機械的な検索結果だけです。
- 取られなかった分岐（Type-C、HDMI、big joiner、TGL より前、DP 2.0、MST、port sync）の callee は列に出ません。host 試験がそれを確認しています。
- plane の更新（`icl_plane_update_noarm／arm`、E-112）は crtc enable の後の commit 段で、この列の外です。disable 列は未取り込み。
- `intel_ddi_init_dp_buf_reg()`（DDI_BUF_CTL の値を `intel_dp->DP` に置くだけ）は `intel_dp_set_link_params` の直後に走っていますが、register 操作が無いので列には出ません。

## 列（host 試験 `run-lcd-host-test.sh` の出力から `tools/lcd-e113/gen_seq_table.py` で生成）

| # | 種別 | 内容 | 正本での定義 | parity 側に同名関数 |
|---|---|---|---|---|
| 0 | step | `intel_dmc_enable_pipe` | `display/intel_dmc.c` | なし |
| 1 | step | `> intel_encoders_pre_pll_enable` | `display/intel_display.c` | なし |
| 2 | step | `main_link_aux_power_domain_get` | `display/intel_ddi.c` | なし |
| 3 | step | `intel_enable_shared_dpll` | `display/intel_dpll_mgr.c` | なし |
| 4 | step | `> intel_encoders_pre_enable` | `display/intel_display.c` | なし |
| 5 | step | `intel_set_cpu_fifo_underrun_reporting` | `display/intel_fifo_underrun.c` | なし |
| 6 | step | `intel_dp_set_link_params` | `display/intel_dp.c` | なし |
| 7 | step | `intel_pps_on` | `display/intel_pps.c` | `dp/intel_pps_port.c` |
| 8 | step | `intel_ddi_enable_clock` | `display/intel_ddi.c` | なし |
| 9 | step | `intel_display_power_get(ddi_io_power_domain)` | `display/intel_display_power.c` | なし |
| 10 | step | `icl_program_mg_dp_mode` | `display/intel_ddi.c` | なし |
| 11 | step | `intel_ddi_enable_transcoder_clock` | `display/intel_ddi.c` | なし |
| 12 | write | `0x60400 = 0x0a210002` | （移植済み writer の出力） | — |
| 13 | step | `encoder->set_signal_levels (icl_combo_phy_set_signal_levels)` | `display/intel_ddi.c` | なし |
| 14 | step | `intel_ddi_power_up_lanes` | `display/intel_ddi.c` | なし |
| 15 | step | `intel_ddi_mso_configure` | `display/intel_ddi.c` | なし |
| 16 | step | `intel_dp_set_power(D0)` | `display/intel_dp.c` | なし |
| 17 | step | `intel_dp_configure_protocol_converter` | `display/intel_dp.c` | なし |
| 18 | step | `intel_dp_sink_enable_decompression` | `display/intel_dp.c` | なし |
| 19 | step | `intel_dp_sink_set_fec_ready` | `display/intel_ddi.c` | なし |
| 20 | step | `intel_dp_check_frl_training` | `display/intel_dp.c` | なし |
| 21 | step | `intel_dp_pcon_dsc_configure` | `display/intel_dp.c` | なし |
| 22 | step | `intel_dp_start_link_train` | `display/intel_dp_link_training.c` | なし |
| 23 | step | `intel_dp_stop_link_train` | `display/intel_dp_link_training.c` | なし |
| 24 | step | `intel_ddi_enable_fec` | `display/intel_ddi.c` | なし |
| 25 | step | `intel_dsc_dp_pps_write` | `display/intel_vdsc.c` | なし |
| 26 | write | `0x60410 = 0x00000001` | （移植済み writer の出力） | — |
| 27 | step | `intel_dsc_enable` | `display/intel_vdsc.c` | なし |
| 28 | step | `intel_uncompressed_joiner_enable` | `display/intel_vdsc.c` | なし |
| 29 | write | `0x6001c = 0x077f0437` | （移植済み writer の出力） | — |
| 30 | step | `bdw_set_pipe_misc` | `display/intel_display.c` | なし |
| 31 | write | `0x60030 = 0x7e4b17e4` | （移植済み writer の出力） | — |
| 32 | write | `0x60034 = 0x00800000` | （移植済み writer の出力） | — |
| 33 | write | `0x60040 = 0x00042bfe` | （移植済み writer の出力） | — |
| 34 | write | `0x60044 = 0x00080000` | （移植済み writer の出力） | — |
| 35 | write | `0x6007c = 0x00000000` | （移植済み writer の出力） | — |
| 36 | write | `0x60028 = 0x00000000` | （移植済み writer の出力） | — |
| 37 | write | `0x60000 = 0x081f077f` | （移植済み writer の出力） | — |
| 38 | write | `0x60004 = 0x081f077f` | （移植済み writer の出力） | — |
| 39 | write | `0x60008 = 0x079f078f` | （移植済み writer の出力） | — |
| 40 | write | `0x6000c = 0x04670437` | （移植済み writer の出力） | — |
| 41 | write | `0x60010 = 0x04670000` | （移植済み writer の出力） | — |
| 42 | write | `0x60014 = 0x0448043a` | （移植済み writer の出力） | — |
| 43 | rmw | `0x420c0 clear=0x00000000 set=0x80000000` | （移植済み writer の出力） | — |
| 44 | write | `0x60420 = 0x00000000` | （移植済み writer の出力） | — |
| 45 | write | `0x6002c = 0x00000000` | （移植済み writer の出力） | — |
| 46 | rmw | `0x420c0 clear=0x18000000 set=0x00000000` | （移植済み writer の出力） | — |
| 47 | write | `0x70008 = 0x00000000` | （移植済み writer の出力） | — |
| 48 | step | `skl_pfit_enable` | `display/skl_scaler.c` | なし |
| 49 | step | `intel_color_load_luts` | `display/intel_color.c` | なし |
| 50 | step | `intel_color_commit_noarm` | `display/intel_color.c` | なし |
| 51 | step | `intel_color_commit_arm` | `display/intel_color.c` | なし |
| 52 | step | `hsw_set_linetime_wm` | `display/intel_display.c` | なし |
| 53 | step | `icl_set_pipe_chicken` | `display/intel_display.c` | なし |
| 54 | step | `intel_initial_watermarks` | `display/intel_wm.c` | なし |
| 55 | step | `> intel_encoders_enable` | `display/intel_display.c` | なし |
| 56 | write | `0x60404 = 0x00000000` | （移植済み writer の出力） | — |
| 57 | write | `0x60400 = 0x8a210002` | （移植済み writer の出力） | — |
| 58 | step | `intel_audio_sdp_split_update` | `display/intel_audio.c` | なし |
| 59 | step | `intel_enable_transcoder` | `display/intel_display.c` | なし |
| 60 | step | `intel_ddi_wait_for_fec_status` | `display/intel_ddi.c` | なし |
| 61 | step | `intel_crtc_vblank_on` | `display/intel_crtc.c` | なし |
| 62 | step | `drm_connector_update_privacy_screen` | （macro／inline） | なし |
| 63 | step | `intel_edp_backlight_on` | `display/intel_dp.c` | なし |
| 64 | step | `intel_dp_set_infoframes` | `display/intel_dp.c` | なし |
| 65 | step | `trans_port_sync_stop_link_train` | `display/intel_ddi.c` | なし |
| 66 | step | `intel_hdcp_enable` | `display/intel_hdcp.c` | なし |
