# WS031 第43報 — 主作業:CDCLK 本体移植(hook選択→現値取得→sanitize→PCODE前後変更)。combo PHY 返却契約是正。GPU-free 検証(ktest 140/0、probe=NOT_RUN)

ご指示の主作業 = **CDCLK を一続きで実装**(hook/table 選択 → 現値取得 bxt_get_cdclk → sanitize → 変更 bxt_set_cdclk)を実施しました。順序1(combo PHY の親接続返却契約)も先に是正。combo PHY(第42報)は保持。build 0 error/warning。**実 GPU は渡さない構成で検証**(D 未完中は実 device P0→P3 を実行しない、のご指示に従いました)。

実機(`run-parity-nogpu.sh`、chaos、ADL-P 8086:46a8 rev0c を vfio-pci に残置、image f40715595):`CAS-SELFTEST 120/110 双方向 PASS` / **ktest 140/0**(134→140 = 新規 CDCLK 6 件、失敗 0)/ **probe=NOT_RUN**(GPU 非パススルー → runner SYNC_ONLY、実 device P0→P3 走らず)/ selftest=PASS / CPUs4 / cleanup=0 published=0。

---

## 1. 順序1:combo PHY 親接続の返却契約是正(初期化本数をエラーと取り違えない)

- `parity_intel_combo_phy_init(m, trace, unsigned *initialised_out)` を **戻り値 0 固定(正本 void 契約 = 常に成功)** へ変更しました。初期化した本数は out-param(診断)に分離。**D3 親は return==0 を成功判定でき、`>0` をエラーとして誤読しません**。「初期化本数=戻り値」だと共通エラー判定器が非0を失敗と読む懸念を排除。
- ktest 2 件を更新:全適切 → `rc==0 && n==0`、未設定 → `rc==0 && n==2`。**2 本初期化しても rc==0 で親が続行できる**ことを明示検証。

## 2. 主作業:CDCLK(cdclk.{c,h} 新規)— 正本 intel_cdclk.c 基準、ADL-P(display ver13, HAS_CDCLK_CRAWL=1 / HAS_CDCLK_SQUASH=0)

制御フローを残し、レジスタ・PLL lock 待ち・PCODE だけ osdep へ適合。

- **device 状態**:`parity_cdclk_dev{ hw(config: ref/vco/cdclk/bypass/voltage_level), table, funcs, display_ver, has_cdclk_crawl/squash, m, sb_lock, 診断7項 }`。config は kHz 単位、**vco 特殊値 = 0(PLL off)/ ~0u(unknown=full 再設定)を型で保持**(~0u を実周波数として計算に使わない)。読出値・sanitize 後 SW 状態・要求値を別記録(observed_before/sanitized/requested/prepare_status/hw_sequence_reached/notify_status)。
- **hook 選択** `parity_intel_init_cdclk_hooks` + `parity_adlp_display_step`:adlp_revids[] を引き **rev 0x0c → STEP_D0**(**PCI 生 revision を stepping に直用しない**)。STEP_D0 ∉ [STEP_A0, STEP_B0) → **adlp_cdclk_table + tgl_cdclk_funcs**、crawl=1/squash=0 を設定(PLL 操作なし、table/callback と SW 状態準備のみ)。RPL-U(別 SKU)は非該当。
- **現値取得** `bxt_get_cdclk`:icl_readout_refclk(SKL_DSSM の refclk 19.2/24/38.4)→ bxt_de_pll_readout(BXT_DE_PLL_ENABLE の PLL_ENABLE|LOCK 判定、**未 LOCK は vco=0**、ratio→vco)→ ver≥12 で bypass=ref/2 → vco==0 は cdclk=bypass、それ以外は CDCLK_CTL の CD2X div sel で cdclk=vco/div。squash=0 ゆえ squash_ctl は読まず。voltage=tgl_calc_voltage_level。**table 代入=HW read の代用にしていません**(必ずレジスタから復号)。
- **sanitize** `bxt_sanitize_cdclk`:intel_update_cdclk 後、vco==0 か cdclk==bypass なら再設定へ。DPLL 可なら CDCLK_CTL を読み **pipe field(CD2X_PIPE_NONE)を無視**、calc_cdclk/calc_pll_vco/skl_cdclk_decimal|cd2x_div_sel で expected を再構成し一致すれば無変更。不一致は **cdclk=0 / vco=~0u**(適切なら変更しない/要再設定なら full 再設定を要求)。
- **変更** `bxt_set_cdclk` / `_bxt_set_cdclk`:共通 PCODE `skl_pcode_request(CDCLK_CONTROL, PREPARE, READY, READY, 3)` で PCU 準備 → **失敗なら PLL/CDCLK_CTL を書かず return(CD-3)** → crawl(hw.vco>0 && new>0 && !unknown で adlp_cdclk_pll_crawl、それ以外 icl_cdclk_pll_update=disable/enable、LOCK 待ちは parity_wait_reg)→ CDCLK_CTL(cd2x_div_sel | PIPE_NONE | decimal、**INVALID_PIPE ゆえ vblank 待ちなし**)→ `snb_pcode_write(CDCLK_CONTROL, voltage_level)` 通知 → **失敗なら HW 済を巻き戻さず、cdclk.hw を requested で盲目上書きしない(CD-4)** → intel_update_cdclk → voltage_level=requested。squash=0 で midpoint(crawl+squash 両要)は常に不成立=単段。
- **init_hw** `intel_cdclk_init_hw → bxt_cdclk_init_hw`:sanitize → cdclk!=0 && vco!=0 なら無変更(diag_no_change=1) → 否なら calc(min)→set_cdclk。**intel_cdclk_init(後段 P3)ではありません**。P2 の DRAM/BW/電源状態は破壊しません。

## 3. 試験(GPU-free、ktest 134 → 140/0)

fake MMIO に **BXT_DE_PLL_ENABLE の LOCK/FREQ_REQ_ACK モデル**(PLL_ENABLE→LOCK、FREQ_REQ→ACK)を追加。

| ケース | 合格条件 |
|---|---|
| hook | rev0x0c → **STEP_D0**、adlp_cdclk_table + tgl funcs、crawl1/squash0 |
| readout | DSSM=38.4 / PLL ratio34(locked)/ CD2X÷2 → **ref38400 vco1305600 bypass19200 cdclk652800 voltage3** を復号 |
| **CD-1 変更不要** | 合法 pre-OS 状態 → init_hw が **無変更**(cdclk/vco 保持、HW 書込なし、diag_no_change=1) |
| **CD-2 sanitize 後再設定要** | PLL off → sanitize が **cdclk=0 / vco=~0**(~0 を実周波数化しない) |
| **CD-3 prepare-fail** | PCU が PREPARE 拒否 → prepare_status≠0 かつ **PLL/CDCLK_CTL 未書込**(hw_sequence_reached=0) |

期待値は正本から作成(移植先定義から再生成していません)。実機では CD-3 は注入 PCODE status→err -42 が伝播(時間基盤が正常なため、agent-1 の TCG で出る -5 時間 fault ではなく本来の PCODE エラーで prepare 失敗)。

## 4. 次(D 続行)と並行作業

- **CDCLK 変更経路の残**:CD-2 変更成功の全経路(crawl / icl_cdclk_pll_update / CDCLK_CTL / notify を実 device で通す)+ CD-4 notify-fail(HW 済を巻き戻さず状態を requested で上書きしない)を fake へ接続。関数本体は本報で移植済(まだ fake 試験で通していないのは「PLL/CDCLK_CTL の実書込列」と「notify 失敗時の状態非上書き」の 2 ケース)。
- **D0**:fuse 実 poll(PW1 PG0 前 / PG1 後)/ DC_off(gen9_set_dc_state・gen9_dc_off ops、DMC payload 確認)/ VGA(vga_get/put 所有権)。
- **D3**:icl_display_core_init(false) 親順(DC 無効化→PCH+reset handshake→**combo PHY(本体済)**→PW1→**CDCLK(本体済)**→DBUF→MBUS→BW_BUDDY→末尾)+ POWER_DOMAIN_INIT 参照保持 + 全 well 個別 sync_hw + cleanup(driver_remove 対応)。combo PHY / CDCLK は D 本来位置(icl_display_core_init)で実 GPU へ後接続。
- **並行**:
  - **sleep-range 実 backend**:kern_usleep_range の入口を保持し、絶対期限(earliest/latest)+ 高分解能 timer(clockevent 期限割込)+ 待機状態を既存 clock/waitq へ接続(登録直後満了で眠り続けない、早期起床は同じ絶対期限へ、終了後に timer 解放済状態へ触れない、scheduler tick の次イベントを上書きしない)。「20µs 以内に必ず戻る」は合格条件にしません。20-25ms sleep + 同一 CPU 別 thread で検証。
  - **preemption 実 scheduler 試験**:同一 CPU の thread A/B、A 二重 preempt 禁止、実 timer IRQ から再スケジュール要求 → IRQ 処理されるが A→B 切替なし → 一回 enable でまだ切替らず → 最終 enable で保留処理 → B 実行 + count/IRQ 復元。実 IRQ・切替要求が発生した実行のみ合格。
  - **PCODE 追加区間 2 試験**:通常区間未承認・追加区間で承認(sleep なし、成功後 preempt/mutex 復元)/ 追加区間で counter 読出失敗(通常 timeout に化けず preempt/mutex 復元し時間基盤異常返す)。scripted time。

**実 GPU 次回起動条件**(fake 統合後 一回):combo PHY 3 ケース + CDCLK(read/sanitize/prepare-fail 済 + 変更成功/notify-fail の 2 ケース)+ 通常 sleep/追加 preempt-off 契約 + D0 本番接続 + 親関数 INIT 取得/全 well 同期/cleanup が fake 通過 → 参照条件 通常 probe。到達 = intel_power_domains_init_hw(false) 完了 + INIT 参照保持のまま intel_dmc_init 入口(P3 全体/描画ではない)。

**保持**:MSI 分離 / runner / 共有 backend / 既存試験、per-pipe vblank、時間 error 伝播、VGA 登録、電源 map(30 wells)+ 是正 ops、PCI probe PM、時間 fault 試験、共通 PCODE + 三層 retry、combo PHY、preemption/sleep-range 契約。GPU=vfio-pci、drm 非 blacklist、attach 先行。描画再試験・GPU-hang 探索へは戻りません。

次報:CDCLK 変更成功/notify-fail の関数本体接続 diff、sleep-range 実 backend / preemption 実 scheduler 試験 / PCODE 追加区間 2 試験の本体・結果、D0 差分。`last_completed_op` は attach 未実行のため該当なし(GPU-free)、CDCLK = GPU-free 検証済(140/0)。
