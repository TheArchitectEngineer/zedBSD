# WS031 P5 計画：`intel_display_driver_probe_nogem()` — readout + sanitize 優先（判断①）

計画のみ。実装は未着手です。正本（v6.8.12 `intel_display_driver.c` / `intel_modeset_setup.c` / `intel_display.c` / `intel_ddi.c` / `intel_dpll_mgr.c` / `skl_watermark.c`）を読んだ上で書いています。

## 0. 結論と、先に申し上げるべき2点

- P5 は **5つの小増分（P5-0〜P5-d）**に分け、それぞれ GPU-free 試験 → 実機1回 → 報告、で進めます。
- **規模の訂正**：第E-86報の工程表で P5 を「2〜3 DMC」と見積もりましたが、正本を精読した結果 **約5.5〜6 DMC** が実態です（readout の対象がレジスタ群として広く、sanitize が実 HW を初めて OFF にする段だからです）。見積もりが甘かったことをお詫びします。
- **P5 に入る前に、P4 のハンドラを1点補強する必要**があります（§5 リスク1）。P4 は vblank 割込みを **enable 済み**なので、P5-d で pipe の vblank を on にした瞬間に本物の割込みが来ます。現状のハンドラは DE_PIPE の IIR を ack しないため、そのまま行くと割込みストームになります。

## 1. 正本の呼び出し列と、ADL-P での実効内容

`intel_display_driver_probe_nogem()` の順序どおり。右欄は ADL-P（display ver 13, xe_lpd, PCH_ADP）で何が起きるかを正本から確定したものです。

| # | 正本 | ADL-P での実効 | 分類 |
|---|---|---|---|
| 1 | `intel_wm_init` → `skl_wm_init` | `intel_sagv_init` + `skl_setup_wm_latency`：**PCODE `GEN9_PCODE_READ_MEM_LATENCY` を2回**読んで latency[0..7] を得る（num_levels=6 if HAS_HW_SAGV_WM）。既存 PCODE 基盤で可 | HW読 |
| 2 | `intel_panel_sanitize_ssc` | LVDS/SSC 系。ADL-P では実質 no-op（分岐は保持） | — |
| 3 | `intel_pps_setup` | `pps.mmio_base = PPS_BASE`（HAS_PCH_SPLIT でない） | 状態 |
| 4 | `intel_gmbus_setup` | gmbus.mmio_base=PCH_DISPLAY_BASE、pin 毎に i2c adapter 登録 + `intel_gmbus_reset`。**i2c adapter 機構は無い** → pin 表・reg0・GPIO 設定は保持し、adapter 登録は「未実装を記録」（color_init と同じ方式） | 状態+記録 |
| 5 | `intel_crtc_init` ×4 | crtc alloc / pipe / num_scalers / **universal plane 生成（primary + sprites + cursor）** / fifo_underrun init(false) / funcs=bdw / color・drrs・crc init | オブジェクト |
| 6 | `intel_plane_possible_crtcs_init` / `intel_shared_dpll_init` / `intel_fdi_pll_freq_update` / `intel_update_czclk` | dpll: **adlp_plls = DPLL0/DPLL1/TBT/TC1..4 の7本**を登録。fdi/czclk は ADL-P で return | 状態 |
| 7 | `intel_display_driver_init_hw` | `intel_update_cdclk`（既存 `parity_intel_update_cdclk`）→ dump → `cdclk_state.logical=actual=hw` → **`adlp_display_wa_apply`**（Wa_22011091694: GEN9_CLKGATE_DIS_5 DPCE_GATING_DIS set / Bspec 49189: GEN8_CHICKEN_DCPR_1 DDI_CLOCK_REG_ACCESS clear） | HW書(rmw×2) |
| 8 | `intel_dpll_update_ref_clks` | `ref_clks.nssc = cdclk.hw.ref` | 状態 |
| 9 | `intel_hdcp_component_init` | `is_hdcp2_supported` なら component_add。**component 機構は無い** → 記録 | 記録 |
| 10 | `intel_update_max_cdclk` | ver>=11: `ref==24000 ? 648000 : 652800`（ADL-P ref=38.4MHz → **652800**） | 状態 |
| 11 | `intel_hti_init` | `has_hti` なら HDPORT_STATE 読出し（xe_lpd の has_hti は P5-a で表確認） | HW読 |
| 12 | `intel_vga_disable` | VGA_CNTRL 読→未 disable なら legacy IO で SR01 SCREEN_OFF + udelay(300) + VGA_DISP_DISABLE 書込。**E-71 の VGA legacy IO 実装を再利用** | HW書 |
| 13 | `intel_setup_outputs` | `intel_pps_unlock_regs_wa`(HAS_DDI→return) → `intel_ddi_crt_present` → **`intel_bios_for_each_encoder(intel_ddi_init)`**（VBT child = PORT A/B/C の3件、E-66 の missing_defaults） | §3 |
| 14 | `intel_modeset_setup_hw_state` | **本丸（§2）** | HW読+書 |
| 15 | `intel_acpi_assign_connector_fwnodes` | ACPI/fwnode。無い → 記録 | 記録 |
| 16 | `intel_crtc_initial_plane_config`（active crtc のみ） | **判断①により実施しない**（BIOS fb 引き継ぎ無し） | skip |
| 17 | `ilk_wm_sanitize` | `drm_WARN_ON(ver >= 9) return` → **ADL-P では guard で即 return**。guard を忠実に移植 | — |

## 2. `intel_modeset_setup_hw_state()` の内訳（P5 の中核）

```
POWER_DOMAIN_INIT get
intel_early_display_was            -- IS_DISPLAY_VER(10,12) のみ → ADL-P(13) は不発（分岐保持）
intel_modeset_readout_hw_state     -- [readout]
get_encoder_power_domains
intel_pch_sanitize                 -- HAS_PCH_IBX のみ → 不発
per-crtc: sanitize_fifo_underrun / drm_crtc_vblank_reset / (active) dmc_enable_pipe + crtc_vblank_on
intel_fbc_sanitize                 -- FBC が HW 上 active なら deactivate
intel_sanitize_plane_mapping
per-encoder: intel_sanitize_encoder (+ intel_ddi_sanitize_encoder_pll_mapping)
intel_modeset_update_connector_atomic_state
intel_sanitize_all_crtcs           -- active かつ encoder 無し → intel_crtc_disable_noatomic  ★
intel_dpll_sanitize_state          -- on だが未使用の PLL を disable(+adlp_cmtg_clock_gating_wa)  ★
intel_wm_get_hw_state              -- skl_wm_get_hw_state + skl_wm_sanitize(DBUF 誤配置なら全 plane off)
per-crtc: intel_modeset_get_crtc_power_domains
POWER_DOMAIN_INIT put
intel_power_domains_sanitize_state -- BIOS が残した未使用 well を disable  ★
```

**readout（`intel_modeset_readout_hw_state`）で読むもの**：
- per pipe: `hsw_get_pipe_config` → `hsw_enabled_transcoders`（TRANS_DDI_FUNC_CTL 走査）→ `TRANSCONF.ENABLE` で active 判定 → active なら timings（TRANS_HTOTAL/HBLANK/HSYNC/VTOTAL/VBLANK/VSYNC + ver13 の TRANS_SET_CONTEXT_LATENCY）、PIPE_SRCSZ、output_format（PIPE_MISC）、color config、WM_LINETIME、scaler(pfit)、TRANS_MULT、chicken_trans の framestart_delay。すべて `power_get_in_set_if_enabled` で gate。
- per plane: `skl_plane_get_hw_state`（PLANE_CTL.ENABLE + pipe 選択）
- per encoder: `intel_ddi_get_hw_state`（DDI_BUF_CTL.ENABLE → TRANS_DDI_FUNC_CTL の port select 走査で pipe 特定）
- per PLL(7本): `combo/tbt/dkl _get_hw_state`（enable bit + cfgcr 等の hw_state 読出し）
- connector: **P5 では 0 件**（§3 参照）
- 派生: min_cdclk / data_rate / `intel_bw_crtc_update` / pmdemand params

**★ = この段で parity が初めて実 HW を OFF にする箇所**（well / PLL / pipe）。ここが P5 の本質であり、同時に最大のリスクです（§5）。

## 3. 判断①の具体化：「readout+sanitize 優先、setup_outputs は検出まで」の設計

- **オブジェクトは parity 内の最小表現**（DRM の crtc/plane/encoder/connector オブジェクトは持ち込まない）：
  - `parity_crtc[4]`：pipe / plane_ids_mask / num_scalers / `parity_crtc_state`（hw.active/enable, cpu_transcoder, adjusted_mode の crtc_* timing, pipe_src, output_format, linetime, pixel_multiplier, framestart_delay, shared_dpll, port_clock, pixel_rate, min_cdclk[], data_rate[], active_planes, inherited）
  - `parity_plane[pipe][n]`：id / type(primary,sprite,cursor) / pipe / visible。**ADL-P の plane 数（sprite 数）は device 表から確定してから**実装（推測しない）
  - `parity_encoder[≤3]`：port / phy / type / power_domain / base.crtc(link) / `get_hw_state` / `is_clock_enabled` / `disable_clock`
  - `parity_dpll[7]`：info(name,id,funcs kind) / on / hw_state / pipe_mask / active_mask / wakeref
- **`setup_outputs` = `intel_ddi_init` の判定部＋encoder レコード生成まで**：port 取得 → port strap → assert_port_valid → port_in_use → DSI 分岐 → HTI 予約 → VBT の DP/HDMI 可否 → lspcon。ここまでは忠実に移植し、**DRM encoder 登録・DP/HDMI サブ初期化・AUX・HPD・connector 生成は行いません**。
- **connector を作らない帰結**（正直に明記）：
  - readout の connector ループは空。`crtc_state->connector_mask/encoder_mask` は更新されない。
  - `intel_sanitize_encoder` の「connector は active だが pipe が無い → encoder を手動 disable」分岐は**発火しません**。
  - `intel_sanitize_crtc` の `intel_crtc_has_encoders` は **encoder の base.crtc リンク**で判定するので、encoder を作れば sanitize は機能します（connector は不要）。
  - つまり判断①の範囲で「pipe/encoder/PLL/well の readout と sanitize」は成立し、「connector 起点の sanitize」だけが対象外です。

## 4. 増分分割（各増分 = GPU-free → 実機1回 → 報告）

### P5-0：診断のみ（挙動変更なし、≈0.1 DMC）
実機で **TRANSCONF(A..D).ENABLE / TRANS_DDI_FUNC_CTL(A..D) / DDI_BUF_CTL(A,B,C).ENABLE / PLANE_CTL(primary A..D).ENABLE / DPLL0,1 enable / 30 well の hw_enabled** を1回読んでログするだけ。
**目的**：この device で BIOS/pre-OS が何を active に残しているかを事実として確定し、§5 リスク2（`crtc_disable_noatomic` が必要か）を判断します。想定は「rombar=0 で GOP は IGD を駆動せず、vfio-pci bind 時に reset されるので pipe は全て inactive」ですが、E-75 で「sanitizing cdclk programmed by pre-os」が出ている以上、**読んでから決めます**。

### P5-a：前段（HW をほぼ触らない、≈1 DMC）
#1〜#12。実 HW アクセスは wm latency の PCODE 読×2、`adlp_display_wa_apply` の rmw×2、HTI 読、VGA disable。オブジェクト（crtc/plane/dpll 表）を構築。gmbus adapter / hdcp component / acpi fwnode は「未実装を記録」。
試験：latency 復号（fake PCODE 2取引）、adlp_plls 7本の id/名前、cdclk logical=actual=hw、WA の rmw 列、VGA disable の IO 列（E-71 の recorder 再利用）、max_cdclk=652800。

### P5-b：setup_outputs（検出・ログまで、≈1 DMC）
#13。VBT child 3件 → `intel_ddi_init` 判定部 → encoder レコード。`intel_ddi_get_hw_state`/`get_encoder_pipes`、`is_clock_enabled`/`disable_clock`（sanitize_pll_mapping 用）を実装。
試験：strap 無し/port 重複/HTI 予約/VBT 非対応 の各 return、正常3件生成、get_hw_state の TRANS_DDI port-select 走査（MST 分岐含む）。

### P5-c：readout（≈2 DMC）
`intel_modeset_readout_hw_state` 全体（§2 の readout 部）。**HW への書込みは無し**（POWER_DOMAIN_INIT の get/put と `power_get_in_set_if_enabled` のみ）。
試験：fake に「pipe A active（TRANSCONF/TRANS_DDI/timings/PLANE_CTL）」「全 inactive」「PLL0 on だが未使用」の3 fixture を用意し、crtc_state/plane visible/encoder link/dpll on・pipe_mask/派生値（min_cdclk 等）を照合。

### P5-d：sanitize（≈1.5〜2 DMC）★実 HW を初めて OFF にする
§2 の sanitize 部。**P5-0 の結果で分岐**：
- 全 pipe inactive（想定）→ `intel_sanitize_crtc` は各 crtc で早期 return、`crtc_disable_noatomic` は**不要**。実 HW 変更は `sanitize_dpll_state`（未使用 PLL off）と `power_domains_sanitize_state`（未使用 well off）、`fbc_sanitize`。
- active pipe あり → §5 リスク2 に従い**停止して確認**。
試験：well/PLL の「on だが count=0」を fake で作り disable 列を照合、fbc active→deactivate、dbuf 誤配置→全 plane off、vblank reset/on の per-pipe 状態遷移。

### スキップ（判断①）
`intel_crtc_initial_plane_config`。`ilk_wm_sanitize` は ver>=9 guard のみ移植（正本もここで return）。

## 5. リスクと、事前に決めておく判断

1. **P4 ハンドラの補強が P5-d の前提（要実装、HAL非変更）**：P4 の `gen8_de_irq_postinstall` は `GEN8_PIPE_VBLANK` を **IER で enable 済み**。P5-d の `intel_crtc_vblank_on` で pipe の vblank が有効になると本物の割込みが届きますが、現ハンドラは DE_PIPE IIR を読まず ack しないので **割込みストーム**になります。→ P5-d の前に `gen11_display_irq_handler` → `gen8_de_irq_handler` の **IIR read → write-back(ack) → 計数**を実装します（bottom half は無し、ack だけ）。これで「実際に割込みを受信してハンドラが動いた」実証（E-88 で未達）も P5-d で取れます。
2. **active pipe が残っていた場合**：`intel_sanitize_crtc` → `intel_crtc_disable_noatomic` → `display.funcs.display->crtc_disable` = **`hsw_crtc_disable`（フル modeset disable：encoder post_disable / DDI / DPLL / pipe / DBUF）**が必要になり、+2〜3 DMC。判断①の範囲を超えるので **P5-0 の結果を見て停止・ご相談**します。代替案（その時点で提示）：(a) フル disable を移植、(b) pipe を BIOS 状態のまま残し sanitize を「well/PLL のみ」に限定して先へ進む（忠実性の穴として台帳に明記）。
3. **sanitize が実 HW を OFF にする**（well / PLL）：parity としては初めての「切る」操作です。正本どおり `count==0 && is_enabled` の well だけ、`on && active_mask==0` の PLL だけを対象にし、対象と操作列をログで全件残します。誤って必要な well を切ると以降の readout が壊れるため、GPU-free で fake の「BIOS が残した well」を作って disable 列を照合してから実機へ。
4. **インフラ不在の3点**（gmbus i2c adapter / hdcp component / acpi fwnode）：静かな成功にせず「未実装を記録」（`-ENOSYS` 相当のフラグ + ログ）で通します。P5 の目的（readout/sanitize）には影響しません。
5. **推測禁止で確認してから実装する項目**：xe_lpd の **sprite 数（plane 数）**、`has_hti`、`HAS_HW_SAGV_WM`、`num_scalers[pipe]`、`intel_ddi_crt_present` の ver13 判定、`hsw_panel_transcoders`。いずれも device 表・マクロから確定します。
6. **HAL**：P5 全体で HAL に触る箇所はありません（MMIO/PCODE/VGA IO/待機はすべて既存 osdep・parity 基盤）。

## 6. 見積り（1 DMC = 5増分）
P5-0 0.1 ／ P4 ハンドラ補強 0.3 ／ P5-a 1 ／ P5-b 1 ／ P5-c 2 ／ P5-d 1.5〜2 → **≈6 DMC**（第E-86報の 2〜3 から上方修正）。

## 7. ご判断いただきたい点
1. 上記の分割（P5-0 診断を先に1回）で進めてよいか。
2. **P4 ハンドラの IIR ack 補強**を P5 の前提として先に入れてよいか（HAL 非変更、実装 0.3 DMC）。
3. リスク2 の代替案 (a)/(b) は、P5-0 の事実が出てからの判断でよいか（今は決めない）。
