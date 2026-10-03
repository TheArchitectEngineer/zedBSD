# WS031 Linux-parity 実装台帳 (porting ledger) — rev2

**基準 revision**: 正本は **Ubuntu linux 6.8.0-139.139**（`source:Package=linux, source:Version=6.8.0-139.139`,
version_signature=`Ubuntu 6.8.0-139.139-generic 6.8.12`, i915.ko srcversion=`F4AF37620F4AC270E54227A`）。
Ubuntu の stable base は **6.8.12**。作業中は保存済み upstream v6.8.12 で確認し（`linux-reference/i915-src`）、
Ubuntu 正本 source 取得後に L0.2 で差分照合する。下表の各行「根拠」に確認方法を明記する。

**根拠 (evidence)**: `SOURCE`=保存 source で分岐/効果を確認 / `CONFIG`=参照 kernel config で分岐を確認 /
`RUNTIME`=実機（ゲスト or zedBSD）で通過/値を確認。「呼ばれる」と「そのGPUでレジスタ/batch を発行する」は必ず別に記す。

**状態語**: `UNIMPLEMENTED` / `PORTED`(対象経路で必要な子処理が閉じたときのみ) / `VERIFIED`(対象入力で順序・出力・
同期・戻り値を確認) / `NOT_TAKEN`(参照もその**具体的分岐**で実行しない。親関数ごと削除の意味ではない)。
親をPORTEDにできるのは対象経路の子処理が閉じたとき。「関数名と呼び出しを置いた」は骨格完成であって移植完了ではない。
参照が許容する error/fallback を一律 fatal にしない。Linux が失敗扱いを log-and-continue にしない。

---

## 作業単位 (実装は P 実行順を保ちつつ依存関係で刻む)
| 単位 | 範囲 | 完了条件 |
|------|------|----------|
| **M0** | 正確な Ubuntu source・config、本 rev の guard/callback 訂正、未確認分岐一覧 | 参照版と各判断の根拠が結び付く → **DONE (E-31/32)** |
| **M1** | OS 適合層: PCI/DMA/MMIO/同期/runtime PM/workqueue/error・後始末 | API ごとの意味と所有権が定義され、空成功 stub が無い → **契約層+mock 試験 DONE (E-33/34, 118 checks)。実 backend wiring は M2** |
| **M2** | P0〜P2: early probe/MMIO/device info/sanitize/DMA/GGTT/bus master/MSI | 対象の入口・分岐・順序・失敗経路を通常 attach で実行 |
| **M3** | P3〜P5: display power/BIOS 等・IRQ・nogem | headless 独自省略なし、参照の能力判定で動く |
| **M4** | P6〜P9 依存一式: GEM/VM・GT PM・engine・context・request・default-state | 通常初期化で成立、selftest による追加補修なし |
| **M5** | 通常 client + 復旧: 提出/retire/reset/resume/teardown | 固定 fixture を通常入口から実行 |

**実行順は P 番号のまま**。ただし P8(default-state) は P9(context/ring/request/待機) の部品を使うため、
後章の部品を先に実装しても**実機の呼び出し順は前倒ししない**。部分段階では未実装到達点で停止し、空成功 stub で
見かけ driver-ready にしない。P6.4/P8.3 の先行実装案は**取り下げ**（下記 §訂正）。

---

## ★rev1 からの訂正（保存 v6.8.12 で確認済。Ubuntu 正本でも L0.2 照合）

### C1: intel_renderstate_emit は Gen12 で batch を発行しない (SOURCE 確認済)
`render_state_get_rodata()` の switch は GRAPHICS_VER 6/7/8/9 のみ→**Gen12 は NULL**
(`gt/intel_renderstate.c:20-31`)。→ `intel_renderstate_init` の `if (so->rodata)` を通らず `so->vma==NULL`。
→ `intel_renderstate_emit`: `if (!so->vma) return 0;` (同 :175 相当) で**batch 発行なし**。
**指示反映**: P8.3 は「呼び出し＋pin/unpin＋対象世代で発行 batch 無しの分岐」を移植。Gen9 null-state 流用や
Gen12 用 batch の創作はしない。rev1 の「null render-state batch を焼く」は**誤り**、撤回。

### C2: intel_clock_gating_init は ADL-P で nop (SOURCE 確認済)
hook 選択に ALDERLAKE/TIGERLAKE/gen12 分岐なし→ADL-P は `else … nop_clock_gating_funcs`
(`intel_clock_gating.c`, IS_PONTEVECCHIO/DG2/…/gen6/5/… の後)。`nop_init_clock_gating` は log のみ。
i915_gem_init のコメント「default-state 記録前に必要な context 設定を含むことがある」は**全 platform 向け一般論**で
ADL-P で設定発行を意味しない。**指示反映**: P6.4 は hook 選択と呼び出し位置を移植。ADL-P で HW 設定を**足さない**のが一致。
rev1 の「golden の欠落核」優先実装は撤回。

---

## L0 — 参照固定・台帳整備 (M0)
| ID | 項目 | 状態 | 根拠 | 記録 |
|----|------|------|------|------|
| L0.1 | 参照環境凍結 + manifest | VERIFIED | RUNTIME | 6.8.0-139.139 確定(version_signature/dpkg source:Version/srcversion) |
| L0.2 | Ubuntu 正本 source 取得 + v6.8.12 差分照合 | VERIFIED | SOURCE | apt-get source 取得済(ubu-i915-src)。**port-scope 全ファイル ADL-P で v6.8.12 と同一**。差分 2 件は Xe-LPG/Xe-HP(非 ADL-P)のみ(manifest L0.2) |
| L0.3 | config 採取 + 関連 CONFIG 確定 | VERIFIED | CONFIG | config-6.8.0-139-generic 保存。DEBUG_GEM=未設定, PXP=y, GVT=y, WERROR=未設定 |
| L0.4 | 本 rev の行訂正 (C1/C2 + 下記各行) | VERIFIED | SOURCE | v6.8.12 で確認済、Ubuntu 同一を L0.2 で確認 |

---

## P0 — PCI core + i915_driver_probe 前段
`pci_enable_device → i915_driver_create → i915_driver_early_probe → intel_vgpu_detect → intel_gt_probe_all`

| ID | ref file:function | 入口条件 | guard(入力)→callback / 効果・早期return | zedBSD | 状態 | 根拠 |
|----|-------------------|----------|------------------------------------------|--------|------|------|
| P0.1 | pci core: enumerate/resource/quirk, bridge window, DMA/IOMMU | 常時 | 親 bridge/platform 含む resource・quirk | drv_pci_* | PORTED(部分) | SOURCE | quirk/親 bridge/ASPM 判定 未移植 |
| P0.2 | i915_driver.c `pci_enable_device(pdev)` | 常時 | D0 遷移+resource 有効化+fixup(単なる PCI_COMMAND write でない) | enable_memory+save_enable_state | PORTED(部分) | SOURCE | 内部処理の意味を保持要 |
| P0.3 | i915_driver.c `i915_driver_create` | 常時 | device_info コピー, runtime PM 準備 | i915_alloc+stage | UNIMPLEMENTED | SOURCE | runtime PM/device_info 構造なし |
| P0.4 | i915_driver.c `i915_driver_early_probe` | 常時 | spinlock/mutex/workqueue init, pm_early | 断片 | UNIMPLEMENTED | SOURCE | |
| P0.5 | i915_driver.c `intel_vgpu_detect` | **常時(vGPU 限定でない)** | 対象世代で **PCI BAR 領域を map し magic 確認**→非vGPU なら active=false | なし | UNIMPLEMENTED | SOURCE | ★訂正: 物理 passthrough は**非vGPU になる経路**を再現。親削除でない |
| P0.6 | gt/intel_gt.c `intel_gt_probe_all` | 常時(単一 GT) | GT root 構造/mmio 準備 | engine 構造に内包 | UNIMPLEMENTED | SOURCE | GT オブジェクト分離なし |

## P1 — i915_driver_mmio_probe
`intel_gmch_bridge_setup → intel_uncore_init_mmio → intel_gmch_bar_setup → intel_device_info_runtime_init → intel_display_device_info_runtime_init → intel_gt_init_mmio → sanitize_gpu`

| ID | ref file:function | 入口条件 | 効果・早期return | zedBSD | 状態 | 根拠 |
|----|-------------------|----------|------------------|--------|------|------|
| P1.1 | intel_gmch_bridge_setup | 常時 | host bridge 参照取得 | なし | UNIMPLEMENTED | SOURCE | |
| P1.2 | intel_uncore_init_mmio | 常時 | forcewake domain/MCR steering をアクセス経路へ | drv_i915_uncore_init | PORTED(部分) | SOURCE | forcewake 参照管理は P7.2 と連動 |
| P1.3 | intel_device_info_runtime_init | 常時 | fuse から DSS/slice/EU/sseu を実行時確定 | 定数(selftest で popcount) | UNIMPLEMENTED | SOURCE | device_info に格納する経路なし |
| P1.4 | gt/intel_gt.c `intel_gt_init_mmio` | 常時 | GT MMIO 初期化(WA table 前段の mmio) | 断片 | UNIMPLEMENTED | SOURCE | |
| P1.5 | i915_driver.c `sanitize_gpu` | 常時 | forcewake 下で既存 GPU 状態を sanitize(engine 停止等) | drv_i915_gt_reset(別位置) | UNIMPLEMENTED | SOURCE | ★追加: gt_init_mmio と**別呼び出し**。元位置(mmio_probe 末尾)で独立記録 |
| P1.6 | intel_display_device_info_runtime_init | 常時 | display runtime info(pipe_mask 等)確定 | なし | UNIMPLEMENTED | SOURCE | ★headless=省略ではない。M3 で能力判定として保持 |

## P2 — i915_driver_hw_probe
`dram_edram_detect → i915_set_dma_info → perf_init → ggtt_probe_hw → aperture_remove → ggtt_init_hw → gt_tiles_init → memory_regions_hw_probe → ggtt_enable_hw → pci_set_master → pci_enable_msi → gvt_init → opregion_setup → pcode_init → dram_detect → bw_init_hw`

| ID | ref file:function | 入口条件 | guard→効果・早期return | zedBSD | 状態 | 根拠 |
|----|-------------------|----------|-------------------------|--------|------|------|
| P2.1 | i915_set_dma_info | 常時 | DMA mask 設定(**GGTT probe より前**) | なし | UNIMPLEMENTED | SOURCE | 3 種アドレス分離の起点(M1 契約) |
| P2.2 | i915_ggtt_probe_hw | 常時 | GGTT サイズ/種別 probe | ggtt_start 内 | PORTED(部分) | SOURCE | 順序: DMA→ggtt probe 保持 |
| P2.3 | i915_ggtt_init_hw | 常時 | GGTT init + scratch/PTE encode | ggtt_start 内 | PORTED(部分) | SOURCE | PTE encode 世代確認要 |
| P2.4 | intel_memory_regions_hw_probe | 常時 | SMEM region 登録 | なし | UNIMPLEMENTED | SOURCE | object alloc の裏付け |
| P2.5 | i915_ggtt_enable_hw | 常時 | GGTT enable | ggtt_start | PORTED(部分) | SOURCE | |
| P2.6 | pci_set_master | 常時 | bus master(**GGTT enable の後**) | set_bus_master(true) | PORTED | SOURCE/RUNTIME | 順序ほぼ一致 |
| P2.7 | pci_enable_msi | MSI 可時 | MSI 有効化(不可なら INTx) | irq_start | PORTED(部分) | SOURCE | MSI/INTx 分岐保持 |
| P2.8 | intel_opregion_setup | ACPI OpRegion 有 | OpRegion map/VBT 取込(passthrough x-igd-opregion=on) | なし | UNIMPLEMENTED | SOURCE | headless 判定含む取込を移植 |
| P2.9 | intel_pcode.c `intel_pcode_init` | 常時呼び出し | **`if(!IS_DGFX) return 0`** → ADL-P(統合)は即成功 | なし | NOT_TAKEN(効果) | SOURCE | ★訂正: DGFX 用 poll を ADL-P に新設しない。呼び出し位置は保持 |
| P2.10 | intel_dram_detect / bw_init_hw | 常時 | DRAM info / bandwidth init | なし | UNIMPLEMENTED | SOURCE | taken 判定要(display bw 依存) |

## P3 / P5 — display probe (noirq / nogem)  ※headless=HAS_DISPLAY=false ではない
| ID | ref file:function | 入口条件 | guard→効果 | zedBSD | 状態 | 根拠 |
|----|-------------------|----------|-----------|--------|------|------|
| P3.1 | intel_display_driver_probe_noirq | 常時(入口) | BIOS/VGA/power domain 初期化 → その後 HAS_DISPLAY 分岐 | なし | UNIMPLEMENTED | SOURCE | 入口一括 return しない。DMC/CDCLK/DBUF は元 guard に従う |
| P3.2 | power domain (POWER_DOMAIN_GT_IRQ) 所有 | GT 活動時 | `__gt_unpark` 取得 / park 解放 | なし | UNIMPLEMENTED | SOURCE | ★attach 一回でなく **GT 活動期間の所有権管理**まで移植 |
| P5.1 | intel_display_driver_probe_nogem | 常時(入口) | watermark/clock/表示 HW 状態の取得・整合 → 元 guard | なし | UNIMPLEMENTED | SOURCE | compute 必須かで選別しない |
| P3/5.cap | 能力値を**別々に**実値保存 | — | HAS_DISPLAY / pipe_mask / HAS_DMC / disable_display / OpRegion headless / connector 接続 | なし | UNIMPLEMENTED | RUNTIME | manifest に display v13+DMC ロード記録あり→省略根拠にしない |
| P5.vbt | intel_bios.c:intel_bios_init | 常時 | VBT pointer = OpRegion の VBT → 無ければ SPI/PCI ROM → 無ければ missing defaults | `parity/vbt/intel_bios_port.c`（正本 text から生成）＋`parity/bios.c` の供給元選択 | PORTED（parser）／**接続差あり** | RUNTIME（E-107 実機） | **接続差**: OVMF 下は ASLS=0。正本の `intel_opregion_setup()` は ASLS=0 だと `vbt_firmware` override の取得に到達せず return するので、正本の override 機構は使えない → `intel_bios_init()` が読む入力へ明示 blob を直接つないだ（source=EXPLICIT_BLOB と記録、OpRegion の状態は変えない）。native の OpRegion／RVDA 経路は UNIMPLEMENTED |
| P5.edp.1 | intel_dp.c:intel_edp_init_connector → intel_pps.c:intel_pps_init | eDP child | PPS 選択（VBT controller）、delay = max(BIOS レジスタ, VBT)／両方 0 なら eDP 仕様、レジスタ書込み、firmware が残した VDD の引き取り | `parity/dp/intel_pps_port.c`（生成）＋`parity_edp.c` | PORTED | RUNTIME（E-108 実機 ×2、**E-109 で正位置**） | E-109: `intel_ddi_init` の DP connector 段（`display_nogem.c` の `dp_connector_init` hook）から実行、結果は `struct parity_edp_device` が保持。失敗は encoder を落とす（正本の `goto err`） |
| P5.edp.2 | intel_dp_aux.c:intel_dp_aux_xfer／transfer、drm_dp_helper.c:drm_dp_dpcd_*／drm_dp_i2c_* | AUX 転送ごと | AUX domain 取得 → PPS lock → VDD on（initializing 中は維持）→ 5 回試行 → status 解釈 → put | `parity/dp/intel_dp_aux_port.c`、`drm_dp_helper_port.c`（生成） | PORTED | RUNTIME | E-109: `intel_display_power_put_async` を移植（`power_domains.c`: parked mask 2 段、次の get による取り戻し、delayed work、flush）。PPS／AUX mutex は実 mutex。AUX 完了割り込みは使わずレジスタ polling（正本 6.8 の `intel_dp_aux_wait_done` も polling） |
| P5.edp.3 | intel_pps.c:edp_panel_vdd_work／edp_panel_vdd_schedule_off | VDD 不要になった後 | power_cycle_delay×5 後に worker が VDD off＋AUX 参照返却 | `dp_compat.h` の delayed work（期限の記録）＋`parity_edp_run_due_work()` | PORTED | RUNTIME（E-109 実機: 自動 off、期限前の再取得、停止時の同期取消） | E-109: `parity/backend_delayed.c`（tick で起きる timer thread → 共有 worker）。`cancel`／`cancel_sync`／`flush` は別契約。実行中 body との停止競合は GPU-free の実 thread 試験で確認 |
| P5.edp.4 | intel_dp.c:intel_edp_init_dpcd の残り、intel_panel／intel_backlight | — | sink rate 表（eDP 1.4）、PSR／DSC／MSO caps、DPCD quirk、backlight setup | なし | UNIMPLEMENTED | — | `intel_hpd_enable_detection`、shared-AUX の HPD 確認も未 |
| P5.lcd.1 | drm_edid.c:drm_mode_detailed、intel_dp.c:intel_dp_link_required／max_data_rate、intel_display.c:intel_link_compute_m_n、intel_dpll_mgr.c:icl_calc_dp_combo_pll／icl_calc_dpll_state | eDP 取得後 | EDID→mode、帯域検算、M/N、combo PLL 語（ref = CDCLK readout、WA #22010492432） | `parity/lcd/*`（生成）＋`parity_lcd_calc.c` | **PARTIAL（計算のみ）** | RUNTIME（E-109: 実 AUX データで Linux 値と一致） | rate／lane の選択は `use_max_params` 規則だけ（`intel_dp_compute_link_config` の探索は未移植）、EDID quirk 表なし。HW へは未書込み。残り = DDI buf trans、transcoder／plane 語、CDCLK／DBUF／WM、enable／disable 列 |
| P5.lcd.2 | intel_display.c:intel_cpu_transcoder_set_m1_n1／intel_set_transcoder_timings／intel_set_pipe_src_size、drm_modes.c:drm_mode_set_crtcinfo | crtc enable（hsw_crtc_enable）内 | transcoder A の M/N・timing・PIPESRC 13 語（書込み順つき、LINK_N 最後） | `parity/lcd/intel_display_port.c`＋`drm_modes_port.c`（生成）、emit hook | **PARTIAL（語の生成のみ、未書込み）** | RUNTIME（E-110: 実 AUX データから 13 語が Linux dump と一致） | TRANSCONF／TRANS_DDI_FUNC_CTL／MSA／plane は未 |
| P5.lcd.3 | intel_display.c:hsw_configure_cpu_transcoder（呼出し元ごと）＋hsw_set_transconf／hsw_set_frame_start_delay、intel_vrr.c:intel_vrr_set_transcoder_timings、intel_ddi.c:intel_ddi_init_dp_buf_reg／intel_ddi_set_dp_msa／intel_ddi_enable_transcoder_func | hsw_crtc_enable と DDI の pre_enable／enable 内 | cpu transcoder 17 操作（writer 間の順序も正本）、MSA、TRANS_DDI_FUNC_CTL[2]、DDI_BUF_CTL 値 | `parity/lcd/intel_display_port.c`／`intel_ddi_port.c`／`intel_vrr_port.c`（生成）、emit hook（write／rmw／posting read） | **PARTIAL（語の生成のみ、未書込み）** | RUNTIME（E-111: 実 AUX データから TRANS_DDI_FUNC_CTL=0x8a210002、DDI_BUF_CTL 値+enable=0x80000002 が Linux dump と一致） | enable 列全体の順序（hsw_crtc_enable、DDI pre_enable）は未。Type-C port は拒否 |
| P5.lcd.4 | skl_universal_plane.c:skl_plane_ctl／glk_plane_color_ctl／skl_plane_stride／skl_plane_surf／icl_plane_update_noarm／icl_plane_update_arm ほか 27 関数 | plane update（commit の noarm → arm） | primary plane 1 枚（linear XRGB8888、全画面）の 12 語、PLANE_SURF 最後。watermark は step として記録 | `parity/lcd/skl_plane_port.c`（生成）＋`lcd_plane_compat.h`／`parity_plane_emit_glue.inc` | **PARTIAL（語の生成のみ、未書込み。linear 単一 plane 以外は拒否）** | RUNTIME（E-112: 実 scanout buffer に対する PLANE_CTL 0x94000000／STRIDE 0x78／SIZE 0x0437077f／COLOR_CTL 0x2000 が Linux dump と一致） | skl_plane_check、watermark／DDB、scaler、CSC、CCS／DPT／planar は未 |
| P5.lcd.5 | intel_display.c:hsw_crtc_enable、intel_ddi.c:intel_ddi_pre_pll_enable／intel_ddi_pre_enable／intel_ddi_pre_enable_dp／tgl_ddi_pre_enable_dp／intel_enable_ddi／intel_enable_ddi_dp | modeset commit の crtc enable | enable 列 67 項目（register 操作 22＋未移植 step 45）。順序は正本 text | `parity/lcd/intel_display_port.c`／`intel_ddi_port.c`（生成）＋`lcd_seq_compat.h` | **PARTIAL（列の記述のみ。step 45 個は未移植、未書込み）** | HOST＋GPU-free（E-113: 順序の検査 11 件）。実機では未使用 | disable 列、step ごとの前提条件表は未 |
| P5.fb.1 | intel_fb.c:intel_fb_align_height ほか、intel_fb_pin.c:intel_pin_and_fence_fb_obj、i915_gem_gtt の表示用 pin | plane 更新の前 | scanout buffer（XRGB8888 linear）の確保・256 KiB 整列 pin・guard・解放、IN_USE 中の解放拒否 | `parity/lcd/scanout.{c,h}`、`parity/gt_mem.c` の表示用窓（独立実装、正本の数値に合わせる） | **PARTIAL（buffer のみ、plane 未接続）** | RUNTIME（E-110: 実機で PTE／guard／内容を読戻し一致、解放後 scratch へ復帰） | framebuffer object・fence・rotation・DPT は対象外 |
| P3.dc | intel_display_power_well.c:gen9_disable_dc_states | DC_off well enable | `gen9_set_dc_state(DC_STATE_DISABLE)`（検証 loop＋`dmc.dc_state` 更新）→ CDCLK／DBUF 照合 → combo PHY | `parity/display_core.c:parity_dc_off_enable` | VERIFIED（E-108 で修正） | RUNTIME | E-108 まで `DC_STATE_EN` を直接書いていて software 側 `dc_state` が残り、P7 後に DC_off を取る最初の経路（AUX 取得）で正本の診断「DC state mismatch」が誤発火 → 正本関数経由に修正 |

## P4 — intel_irq_install
| P4.1 | intel_irq_install | 常時 | handler/mask 初期化順 | irq_start | PORTED(部分) | SOURCE | 「先に全 enable」に纏めない |

## P6 — i915_gem_init
`init_userptr → [loop: uc_fetch_firmwares → wopcm_init → setup_private_pat] → i915_init_ggtt → intel_clock_gating_init → intel_gt_init → intel_engines_driver_register`

| ID | ref file:function | 入口条件 | guard→効果・早期return | zedBSD | 状態 | 根拠 |
|----|-------------------|----------|-------------------------|--------|------|------|
| P6.1 | intel_uc_fetch_firmwares | enable_guc=0 | GuC/HuC fetch 分岐に入らない(戻り値再現) | なし | NOT_TAKEN | SOURCE/RUNTIME | enable_guc=0(RUNTIME) |
| P6.2 | gt/intel_wopcm.c `intel_wopcm_init` | 常時呼び出し | **`if(!guc_fw_size) return;`** → GuC 無で早期 return | なし | NOT_TAKEN(効果) | SOURCE | ★訂正: 「必ず領域分割」ではない。入口計算と後段設定を分離 |
| P6.3 | **setup_private_pat(gt)** | 常時 | private PAT 書込(**i915_init_ggtt より前, firmware/WOPCM と同 loop**) | engine.c で PAT 0x4800.. (別位置) | PORTED(位置違い) | SOURCE | ★既存 PAT 修正を**この実行位置へ統合**(i915_gem.c:1176) |
| P6.4 | i915_init_ggtt | 常時 | GGTT 追加 init | ggtt_start に一部 | PORTED(部分) | SOURCE | |
| P6.5 | intel_clock_gating.c `intel_clock_gating_init` | 常時呼び出し | ADL-P → **nop_clock_gating_funcs**(HW 書込なし) | なし | NOT_TAKEN(効果) | SOURCE | ★C2: hook 選択+位置を移植、HW 設定は足さない |
| P6.6 | gt/intel_gt.c `intel_gt_init` | 常時 | P7 で展開 | engines_start | PORTED(部分) | SOURCE | |
| P6.7 | intel_engines_driver_register | 常時 | user 可視 engine 登録 | 断片 | UNIMPLEMENTED | SOURCE | client 経路の前提 |

## P7 — intel_gt_init
`init_workarounds → forcewake_get(ALL) → init_scratch → gt_pm_init → **gt->vm=kernel_vm(gt)** → set_mocs_index → engines_init → uc_init → intel_gt_resume → init_hwconfig → __engines_record_defaults → __engines_verify_workarounds → (uc_init_late/migrate_init) → forcewake_put(ALL)`

| ID | ref file:function | 入口条件 | guard→効果・早期return | zedBSD | 状態 | 根拠 |
|----|-------------------|----------|-------------------------|--------|------|------|
| P7.1 | intel_gt_init_workarounds | 常時 | gt/engine/**ctx** wa_list を**生成**(gt/intel_workarounds.c) | 選抜 LRI + MMIO 直書 | UNIMPLEMENTED | SOURCE | ★一覧生成関数 + emitter ごと移植。選抜数個を置換 |
| P7.2 | forcewake_get(FORCEWAKE_ALL) | init 中 | 取得/ACK/参照管理 | ALL 常時保持 | PORTED(要適正化) | SOURCE | P7.8/PM とセットで解放を導入(§5) |
| P7.3 | intel_gt_init_scratch | 常時 | GT scratch surface 確保 | なし | UNIMPLEMENTED | SOURCE | |
| P7.4 | intel_gt_pm_init | 常時 | GT PM 状態機械 init | なし | UNIMPLEMENTED | SOURCE | |
| P7.5a | **gt->vm = kernel_vm(gt)** | 常時 | kernel VM 生成(**pm_init 後, mocs_index/engines_init 前**) | kernel_vm(engine 内) | PORTED(位置/寿命要) | SOURCE | ★台帳に追加(intel_gt.c:724)。生成/失敗/寿命を記録 |
| P7.5b | gt/intel_mocs.c `intel_set_mocs_index` | 常時 | **SW 状態 `gt->mocs.uc_index/wb_index` 設定のみ**(HW 書込でない) | — | UNIMPLEMENTED | SOURCE | ★訂正: intel_mocs_init(Global MOCS/L3CC 書込)と**別行**。値一致で 3 者まとめない |
| P7.5c | gt/intel_mocs.c `intel_mocs_init` | resume 経路 | Global MOCS/L3CC を HW 書込 | i915_mocs_init | PORTED | SOURCE/RUNTIME | 値一致確認済(E-27)。P7.8 gt_init_hw 経由が正位置 |
| P7.6 | intel_engines_init | 常時 | execlists/ring/status/breadcrumb setup(参照 engine mask) | engine setup | PORTED(部分) | SOURCE | RCS 以外も mask に従い削らない |
| P7.7 | intel_uc_init | enable_guc=0 | uc_ops_off。**init_hw=`__uc_check_hw`** は残る | なし | UNIMPLEMENTED | SOURCE | ★訂正: init/init_hw/early/cleanup を区別。全消去でない |
| P7.8 | gt/intel_gt_pm.c `intel_gt_resume` | 常時 | sanitize→init_hw→rps→llc→per-engine resume→rc6→uc_resume | gt_reset(一部) | UNIMPLEMENTED | SOURCE | §5。起動 1 回 write でなく経路保持 |
| P7.9 | intel_gt_init_hwconfig | 常時 | hwconfig blob 取得 | なし | UNIMPLEMENTED | SOURCE | taken 判定要 |
| P7.10 | __engines_record_defaults | 常時 | P8 で展開 | selftest golden | PORTED(部分/場所違い) | SOURCE/RUNTIME | 通常経路へ統合必須 |
| P7.11 | __engines_verify_workarounds | **CONFIG_DRM_I915_DEBUG_GEM** | **無効なら `return 0`(検証本体に入らない)** | なし | NOT_TAKEN | SOURCE+CONFIG | ★確定: 参照 config で DEBUG_GEM 未設定→検証本体は走らない。標準経路と診断を区別 |

## P8 — __engines_record_defaults (golden context)
本体: `ce=intel_context_create(engine)` → `rq=i915_request_create(ce)` → `intel_engine_emit_ctx_wa(rq)` →
`intel_renderstate_emit(&so,rq)`【Gen12: batch 無 return 0, pin/unpin のみ】 → `i915_request_add(rq)` →
`intel_gt_wait_for_idle`(既定 image を memory へ flush) → 各 rq の image を `engine->default_state` 保存 → 新 context 継承。

| ID | 要素 | 入口/効果 | zedBSD(E-30) | 状態 | 根拠 |
|----|------|-----------|--------------|------|------|
| P8.1 | intel_context_create(A) + kernel_context(B) へ switch で save | 常時 | ctxA/B/C 手動 | PORTED(部分) | RUNTIME | HW save 実測(A ctrl 0x00090009→0xffff0008) |
| P8.2 | intel_engine_emit_ctx_wa(rq) | 常時 | L3ALLOC+FF_MODE2 のみ | PORTED(部分) | SOURCE | ★P7.1 の wa_list + 前後 barrier + lock/fw ごと |
| P8.3 | intel_renderstate_emit(&so,rq) | 常時呼び出し | **未実装** | UNIMPLEMENTED | SOURCE | ★C1: Gen12 は **batch 発行なし**。呼び出し+pin/unpin+分岐を移植(rodata NULL→vma NULL→return 0) |
| P8.4 | wait_for_idle → default_state 保存 | 常時 | A image 直読 | PORTED(部分) | RUNTIME | timeline/HWSP 経由待機へ |
| P8.5 | 新 context への default_state 継承 | context 作成時 | memcpy+ppHWSP clear+ring/PDP 再所有 | PORTED(部分) | RUNTIME | 通常 context 作成経路に統合 |
| P8.6 | selftest からの分離 | — | selftest 内で GT init/context 補修 | UNIMPLEMENTED | — | selftest は通常経路を使うだけに |

## P9 — request / ring / 提出 / 完了
| ID | ref (gt/intel_ring.c, gt/gen8_engine_cs.c, gt/intel_execlists_submission.c) | zedBSD | 状態 | 根拠 | 記録 |
|----|------|--------|------|------|------|
| P9.1 | ring 予約/wrap/pad/**tail 整列**(intel_ring_begin) | lrc_ring_space/emit | PORTED(部分) | SOURCE | tail 整列は ring 実装移植の帰結として常時保証 |
| P9.2 | Gen12 emit flush/invalidate (gen12_emit_flush_rcs) | prologue(AUX 済 E-29) | PORTED(部分) | SOURCE/RUNTIME | 参照関数/定義を保持して使う(説明から再構成しない) |
| P9.3 | breadcrumb (gen12_emit_fini_breadcrumb_rcs) | request tail | PORTED(部分) | SOURCE | HWSP/CS_STALL/post-sync |
| P9.4 | batch start + arbitration + preparser | request emit | PORTED(部分) | SOURCE/RUNTIME | |
| P9.5 | AUX 同期 (CCS aux inv) | prologue(E-29) | PORTED(部分) | SOURCE/RUNTIME | |
| P9.6 | CSB 処理 + retire | lrc csb / retire | PORTED(部分) | SOURCE/RUNTIME | |
| P9.7 | context pin/公開/restore/save/寿命 (timeline/HWSP) | 断片 | UNIMPLEMENTED | SOURCE | c2replay が使う context 作成方式に対応(pointer 差替のみ不可) |

## P10 — reset / resume / 復旧
| P10.1 | gt_sanitize / intel_gt_reset / 提出停止 | gt_reset | PORTED(部分) | SOURCE | 「reset 発行+同 image 再投入」に短縮しない |
| P10.2 | resume 時再設定 (execlists_resume) | なし | UNIMPLEMENTED | SOURCE | idle→再実行/reset 後の受入試験用 |

## P11 — client 経路
| P11.1 | VM/context/object 作成 → execbuf → 完了/retire | session/execbuffer | PORTED(部分) | RUNTIME | MI/init 動作。通常入口から試験 |

---

## OS 適合層の契約 (M1、空成功 stub 禁止) — 契約層は parity/osdep に実装、mock 契約試験で VERIFIED
状態表記: `VERIFIED(mock)`=契約層を GPU 無し mock 試験で確認(HW-VERIFIED でない)。実 backend wiring は M2。
| 適合層 | 実装 | 契約(入出力/所有権) | mock 試験 | 状態 |
|--------|------|---------------------|-----------|------|
| **メモリ/DMA** | osdep/dma.c, address_types.h | **CPU phys / DMA addr / GPU VA を別 struct 型**。GPU PTE には dma_addr のみ到達。map_sg=count/0, map_sgtable=0/-errno, map_page=sentinel。pin 中 unmap=-EBUSY。orig_nents/nents 分離 | DMA-1..6 (36) | VERIFIED(mock) |
| MMIO/uncore | osdep/mmio.c | 通常/raw access 分離、forcewake refcount+ACK(nested/timeout 巻戻し/over-put 検出)、posting read、masked RMW、MCR 排他 | FW+MMIO+MCR (35) | VERIFIED(mock) |
| PCI/PCIe | osdep/pci.c | config 幅別 access、cap walk(不在は不操作)、enable_device(D0+IO/MEM, STATUS 非汚染 16bit RMW)、bus master、MSI(IRQ 分離, 不在=-ENODEV 非fatal) | PCI (27) | VERIFIED(mock) |
| 同期/実行 | osdep/sync.c | completion(前 complete/mid-wait race/timeout)、queue_work 遅延実行(inline 不可)、FIFO、cancel、idempotent | SYNC+WQ (20) | VERIFIED(mock) |
| 計測 | osdep/trace.c | 固定長 ring、overflow=dropped 計上、静的呼出と実機通過を別記録 | (全試験で使用) | VERIFIED(mock) |
| driver 基盤(runtime PM/OpRegion/firmware/error) | 未 | 未実装は成功で隠さず、開発版はその段で停止(UNIMPL 記録) | — | UNIMPLEMENTED | M2/M3 で対象経路分を実装 |
| 実 backend (drv_pci_*/uncore/drv_dma_*/waitq wrap) | 未 | 契約層の backend vtable を zedBSD API へ接続 | — | UNIMPLEMENTED | M2 |

## 受入試験 (通常経路)
`driver-ready → MI-only → init-only → eu-empty → eu-store → PS 定数色`。各段成功時のみ前進。
退行時は直近移植を修正。途中停止で前移植を戻さない/後続を不要化しない。部分段階は未実装到達点で停止(空 stub 禁止)。

## 計測 (メモリバッファ記録。大量 regdump/無差別 MMIO 読み追加なし)
`phase/function/entry-or-exit`, `guard 入力/選択 callback/return`, `resource ID/acquire-or-release`,
`worker enqueue/start/complete`。**静的に呼び出しを確認した記録**と**実機で通過した記録**を別に保持
(renderstate/clock-gating の読み違い再発防止の中心)。

## 次報提出物 (M0→M1→M2)
1. 更新 manifest + 台帳(Ubuntu source 識別・config・本 rev 訂正・未確認分岐一覧)。
2. 適合層契約 + 実装差分(DMA アドレス/mapping 属性/barrier/forcewake・PM/資源寿命。空成功 stub 無し)。
3. P0〜P2 実行記録(呼び出し順・選択 guard/callback・戻り値・資源 acquire/release。未実装到達点明記)。
