# WS031 工程表：現在地(P3 intel_mode_config_init)→ 実機EU試験まで

方針(A)承認を受け、12982 を実機正として受理、台帳E-86を追記済みです。以下、実機EU試験に至るまでの工程を、正本(v6.8.12 i915_driver_probe)の順序に沿って示します。

## 0. 前提と物差し

- **ゴールの定義**: 「実機EU試験」＝保留中のPS描画再現テストを、parity で初期化し切ったGT(context image / WA表 / MOCS / RC6 状態が正本準拠)の上で実機1回実行すること。これ自体は「描画再試験」に当たるので、到達時に**専門家の明示解除**をいただいてから実行します。
- **正本の probe 順序**: early → mmio → hw → **display_noirq(現在地はこの末尾付近)** → **irq_install** → **display_nogem** → **gem_init(→gt_init)** → pxp → display_probe → driver_register。
- **規模の物差し**: DMCユニット(F1〜F4、増分E-82〜E-86 の5増分)を「1 DMC」とします。

## 1. P3後段：display_noirq の残り（≈0.5 DMC）

| 正本 | 内容 | 備考 |
|---|---|---|
| intel_mode_config_init | mode_config の上限/既定値(最小・最大幅、cursor、plane/crtc の骨組み) | zedBSD に DRM 本体が無いので parity 内の状態オブジェクトとして定義 |
| intel_cdclk_init | cdclk state object | state-version 管理 |
| intel_color_init | color state | |
| intel_dbuf_init | dbuf state(slice/allocation の初期値) | |
| intel_bw_init | bw state(DRAM/BW は済) | |
| intel_pmdemand_init | ADL-P 固有 pmdemand state | |
| intel_init_quirks | DMI ベース quirk 表 | 実機は該当無しの見込み、表の照合まで |
| intel_fbc_init | FBC 可否判定(ADL-P fbc mask) | |

ほぼ状態オブジェクト確保＋初期値。GPU-free で決定論試験、実機1回。**承認不要の範囲(HAL非変更)と解釈し、このあと直ちに着手します。**

## 2. P4：intel_irq_install（≈1 DMC）★HAL接点

- 正本: intel_irq_reset(gen11 の display/GT IRQ を全マスク＋ack) → request_irq(MSI) → gen11 postinstall(**GT**: gen11_gt_irq_postinstall＝RCS/BCS/VCS/VECS の enable、GuC/PM mask ／ **display**: gen8_de_irq_postinstall＝DE pipe/port/misc/HPD ／ master unmask)。
- 既存 **MSI-split / runner を維持**し、既存 MSI 経路に「gen11_irq_handler 相当」を接続する形。tick/timer 規約には触れません。
- **ここが唯一 HAL 圧の出うる箇所**。出たら停止して方針確認(規約どおり)。
- 試験: GPU-free＝fake IIR/IER/IMR で reset/postinstall のレジスタ列照合。実機1回＝install/uninstall と IIR read/ack 経路(EU 側の user-interrupt は使いません)。

## 3. P5：intel_display_driver_probe_nogem（≈2〜3 DMC）★スコープ判断

- 正本の内容: intel_wm_init、panel_sanitize_ssc、pps_setup、gmbus_setup、pipe毎 crtc_init(+plane)、shared_dpll_init、fdi_pll_freq_update、update_czclk、**intel_modeset_init_hw(cdclk update/dump/init_hw＝cdclk sanitize)**、hdcp_component_init、**intel_setup_outputs(DDI/TC port 検出、encoder/connector 生成、VBT)**、**intel_modeset_setup_hw_state(BIOS が残した表示状態の readout→sanitize：不整合な pipe/PLL/power well の無効化、power well 参照の整理)**、acpi connector fwnodes、**intel_initial_plane_config(BIOS fb 引き継ぎ)**、wm_get_hw_state。
- EU試験だけ見れば直接不要ですが、忠実性と「hang が電源/表示状態由来でない」ことの確認のため正本順序で実装します。ただし DRM のオブジェクトモデル(connector/encoder/crtc/plane)を丸ごとは持ち込めないので、**状態の readout と sanitize を忠実に、オブジェクトは parity 内の最小表現**とします。
- **判断ポイント①**: (i) 忠実フル ／ (ii) **readout＋sanitize を優先、setup_outputs は検出・ログまで、initial_plane_config は fb 引き継ぎ無し(Linux でも fb 無しは正常経路)** → **(ii) 推奨**。

## 4. P6：i915_gem_init → intel_gt_init（≈3〜4 DMC、EU試験の核心）★backend 判断

- 正本順: gem_init_userptr → **intel_uc_fetch_firmwares(GuC/HuC)** → wopcm_init → gem_init_ggtt(GGTT/WC は big-bang 資産を parity へ移行) → **intel_gt_init**：init_scratch、timelines、**intel_gt_pm_init(forcewake、RC6/RPS 準備)**、uc_init、**intel_gt_init_workarounds(GT WA表の ADL-P 該当分)**、hwconfig、**intel_engines_init(engine setup、execlists、ring、context image、engine WA表／context WA表、indirect-ctx WA)**、intel_gt_init_hw(apply WA、**intel_mocs_init**、uc init hw、engines resume)、**__engines_record_defaults(各 engine に空要求を投げて default context image を採取)**、__engines_verify_workarounds、gem_init__contexts、migrate、**intel_gt_resume → intel_rc6_enable / intel_rps_enable**。
- **最重要**: context/engine/GT の WA表、MOCS、SSEU(RPCS)、RC6/render power gating は、いずれも保留中の EU/PS hang の候補領域。parity の本命はここです。
- **判断ポイント②(最大の分岐)**: ADL-P の Linux 既定は **enable_guc＝HuC認証＋GuC submission**(v6.8 `__uc_expand_default_options`、ADL-S だけ HuC のみ)。忠実にやると GuC fw(DMA/WOPCM/認証)＋CTB＋GuC submission が丸ごと必要(**＋3〜5 DMC**)。一方 **execlists(enable_guc=0)は upstream の正規設定**で、big-bang と同じ backend です。
  → **推奨: execlists で先に EU試験まで到達し、hang が残れば GuC を次段(P6')**。保存の Ubuntu 正本が GuC で動いていた事実は差分として明記しておきます。

## 5. P7：intel_display_driver_probe / i915_driver_register（≈0.5〜1 DMC、EU試験の前提ではない）

- initial_commit(初期 modeset commit)、overlay、hpd_init、watermark ipc；register(fbdev、sysfs、audio、gt_driver_register、**intel_power_domains_enable / rpm wakeref 最終整理、runtime PM enable**)。
- **推奨: EU試験の前は power_domains_enable と rpm 状態の最終整理までに留め、initial_commit/fbdev は EU試験後。**

## 6. 実機EU試験（実機1回、要・明示解除）

- 既存の PS 描画再現テストを、parity 初期化済み GT 上で実行。
- 結果の読み方: **(a) hang 消滅** → parity ユニット単位(WA表/MOCS/RC6/SSEU)で差分を二分探索し原因を特定。**(b) hang 残存** → 残る差分は GuC submission → P6' へ。

## 規模まとめ（1 DMC＝5増分）

P3後段 0.5 ／ P4 1 ／ P5 2〜3 ／ P6 3〜4(execlists) ／ P7前半 0.5 ／ EU試験 0.5 → **合計 ≈7.5〜9.5 DMC**(execlists)、GuC 追加なら **＋3〜5**。

## 規約の維持

HAL(10ms tick、timer contract、KERN_CLOCK_HZ、waitq deadline)非変更。P4 で圧が出たら停止→確認。git commit/push 無し、GPU＝vfio-pci、drm 非 blacklist、MSI-split/runner/shared-sync-backend/attach 先行維持。各ユニットは GPU-free 試験 → 実機1回 → 報告＋台帳。

## ご判断いただきたい点（2つ。回答待ちの間は P3後段を進めます）

1. **P5 のスコープ**：(ii) readout＋sanitize 優先、で良いか。
2. **P6 の backend**：execlists 先行 → GuC は hang 残存時、で良いか。
