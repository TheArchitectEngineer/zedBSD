# WS031 第42報 — 主作業:combo PHY の関数本体を移植。GPU 非パススルー構成で GPU-free 検証(ktest 134/0、probe=NOT_RUN)

ご指示の主作業 = **combo PHY の関数本体移植**を実施しました。preemption 制御(第41報)・PCODE 三層・BIOS 要求引継ぎは保持。build 0 error/warning。**実 GPU は渡さない構成で検証**(D 未完中は実 device の P0→P3 を実行しない、のご指示に従いました)。

実機(`run-parity-nogpu.sh` = 参照 launcher から vfio-pci device を除いた構成、image 2e4254f5):`CAS-SELFTEST PASS` / **ktest 134/0** / **probe=NOT_RUN**(GPU 非パススルー → runner が SYNC_ONLY、実 device P0→P3 は走りません)/ CPUs ready 4 / cleanup=0 published=0。

---

## 1. combo PHY 本体(combo_phy.{c,h})— 正本 intel_combo_phy.c + intel_combo_phy_regs.h 基準

制御フローを残し、MMIO 呼出しだけを osdep へ適合させました。

- **parity_intel_combo_phy_init → for_each_combo_phy**(ADL-P = PHY_A, PHY_B):各 PHY で **verify_state → 適切なら skip、不一致だけ init_one**。戻り値 = 初期化した本数(0 = 全て適切)。
- **verify_state**(icl_combo_phy_verify_state):
  ```
  icl_combo_phy_enabled(PHY_MISC power-down clear && COMP_DW0 COMP_INIT)
   → DISPLAY_VER>=12: check TX_DW8_LN0(ODCC SEL|DIV2) / PCS_DW1_LN0(DCC RUN_DCC_ONCE)
   → icl_verify_procmon_ref_values(COMP_DW1 mask / DW9 / DW10)
   → phy_is_master(PHY_A): check COMP_DW8 IREFGEN
   → check CL_DW5 CL_POWER_DOWN_ENABLE
  ```
  **COMP_INIT だけでは初期化済と判定しません**(複数設定を照合)。「常に書く版」「enable bit だけの簡略版」にはしていません。
- **init_one**(icl_combo_phys_init body):PHY_MISC(DE_IO_COMP_PWR_DOWN clear)→ TX_DW8_GRP(ODCC)→ PCS_DW1_GRP(DCC)→ set_procmon(DW1 rmw / DW9 / DW10 write)→ master IREFGEN(DW8 rmw)→ COMP_INIT(DW0 rmw)→ CL_POWER_DOWN_ENABLE(CL_DW5 rmw)。**lane 側読(LN0)→ group 側書(GRP)を別 offset で保持**。
- **procmon**:COMP_DW3 の process(bit28-26)/voltage(25-24)情報から `icl_procmon_values[5]`(0.85V/0.95V/1.05V × dot0/1、dw1/dw9/dw10 は正本値)を選択。未知入力は 0.85V dot0 へ fallback(正本 MISSING_CASE)。ADL-P 固定値や GPU 周波数からの推測はしていません。
- ADL-P 判定:`has_phy_misc` = 全 PHY true、`phy_is_master` = PHY_A のみ(JSL/EHL/RKL/DG1/ADL-S の分岐は非該当)。レジスタ:COMBOPHY_A=0x162000 / B=0x6C000、COMP=+0x100+4·dw、CL=+4·dw、PCS_GRP=+0x600 / LN0=+0x800、TX_GRP=+0x680 / LN0=+0x880、PHY_MISC 0x64C00/04。

## 2. 試験(GPU-free、ktest 128 → 134/0)

fake MMIO に **generic offset store**(任意レジスタ)+ **GRP→LN(0) broadcast**(lane/group を別 offset に保つ)を追加。3 ケース(+ verify/enable の分):
| 入力状態 | 合格条件 |
|---|---|
| 未 enable(COMP_INIT 無) | verify=0 |
| COMP_INIT だけ(procmon 不一致) | **verify=0**(初期化済扱いしない) |
| init_one 実行 | **COMP_INIT/IREFGEN(master A)/CL_POWER_DOWN/procmon(DW9=0x62AB67BB)を正しい reg/mask/順序で書く**。実行後 verify_state=1(GRP→LN broadcast 反映) |
| 全適切(top-level) | 再 init 0 本 |
| 未設定(top-level) | 2 本(A, B)を初期化 |

期待操作列は正本から作成(移植先定義から再生成していません)。read/rmw/write の対象と順序を比較しています。

## 3. GPU 非パススルー構成での検証

ご指示「D 未完中は実 device の P0→P3 を実行しない / 定期 attach 確認のために GPU を渡さない」に従い、**vfio-pci device を除いた launcher** で起動しました。runner は GPU を検出できず **SYNC_ONLY**(probe=NOT_RUN)となり、GPU-free 試験(combo PHY 含む 134 checks)のみを実行します。combo PHY は D 本来の位置(icl_display_core_init)で後に実 GPU へ接続します。

## 4. 次(D 続行)と並行作業

- **CDCLK 三段**:hook/table(intel_init_cdclk_hooks、ADL-P stepping)→ 現値取得・sanitize(bxt_sanitize_cdclk、適切なら変更せず)→ 変更(計算 → PCODE 変更準備 → PLL/divider → 変更後通知 → cdclk.hw 保存)。fake 4 ケース。
- **D0**:fuse 実 poll(PW1 PG0 前 / PG1 後)/ DC_off(gen9_set_dc_state・gen9_dc_off ops、DMC payload 確認)/ VGA(vga_get/put 所有権)。
- **D3**:icl_display_core_init(false) 親順 + POWER_DOMAIN_INIT 参照保持 + 全 well 個別 sync_hw + cleanup(driver_remove 対応)。
- **並行**:
  - **sleep-range 実 backend**:kern_usleep_range の入口を保持し、絶対期限(earliest/latest)+ 高分解能 timer(clockevent 期限割込)+ 待機状態を既存 clock/waitq へ接続(登録直後満了で眠り続けない、早期起床は同じ絶対期限へ、終了後に timer 解放済状態へ触れない、scheduler tick の次イベントを上書きしない)。「20µs 以内に必ず戻る」は合格条件にしません。
  - **preemption 実 scheduler 試験**:同一 CPU の thread A/B、A が二重 preempt 禁止、実 timer IRQ から再スケジュール要求 → IRQ 処理されるが A→B 切替なし → 一回 enable でまだ切替らず → 最終 enable で保留処理 → B 実行 + count/IRQ 復元。実 IRQ・切替要求が発生した実行のみ合格。
  - **PCODE 追加区間 2 試験**:通常区間未承認・追加区間で承認(sleep なし、成功後 preempt/mutex 復元)/ 追加区間で counter 読出失敗(通常 timeout に化けず preempt/mutex 復元し時間基盤異常返す)。scripted time。

**実 GPU 次回起動条件**(fake 統合後 一回):combo PHY 3 ケース + CDCLK 4 ケース + 通常 sleep/追加 preempt-off 契約 + D0 本番接続 + 親関数 INIT 取得/全 well 同期/cleanup が fake 通過 → 参照条件 通常 probe。到達 = intel_power_domains_init_hw(false) 完了 + INIT 参照保持のまま intel_dmc_init 入口(P3 全体/描画ではない)。

**保持**:MSI 分離 / runner / 共有 backend / 既存試験、per-pipe vblank、時間 error 伝播、VGA 登録、電源 map(30 wells)+是正 ops、PCI probe PM、時間 fault 試験、共通 PCODE + 三層 retry、combo PHY。GPU=vfio-pci、drm 非 blacklist、attach 先行。描画再試験・GPU-hang 探索へは戻りません。

次報:combo PHY 本体(本報)+ CDCLK 実装差分、preempt/enable・切替判定・usleep_range 実 backend・PCODE 通常/追加区間の関数本体/diff。`last_completed_op` は attach 未実行のため該当なし(GPU-free)、combo PHY = GPU-free 検証済。
