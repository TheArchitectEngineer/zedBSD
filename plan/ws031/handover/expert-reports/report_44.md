# WS031 第44報 — CDCLK 変更経路を fake で最後まで実行(CD-2a 完全再設定 / CD-2b crawl / CD-4 通知失敗の部分更新)。per-txn PCODE fake + 書込み trace。GPU-free 検証(ktest 145/0)

ご指示の第一着手 = **PREPARE を承認する fake 応答を用意し、CD-2 を init_hw() から最後の状態更新まで実行 → 最後の通知だけを失敗させて CD-4 を閉じる**を実施しました。CDCLK 本体(第43報)は再作成せず、未実行経路を通す段階です。build 0 error/warning。**実 GPU は渡さない構成で検証**。

実機(`run-parity-nogpu.sh`、chaos、image 14745a2a):`CAS-SELFTEST PASS` / **ktest 145/0**(140→145 = CD-2a/variant/CD-2b/guard/CD-4 の 5 件、失敗 0)/ **probe=NOT_RUN**(SYNC_ONLY)/ selftest=PASS。

---

## 1. 最初の作業:fake を「変更途中まで成功」構成にする

- **per-transaction scripted PCODE**(`struct fake_pcode_txn{ exp_mbox, exp_data, resp_data, resp_data1, status, ready_delay }`):全取引一律 status を廃止し、**PREPARE は成功・HW 変更は進み・最後の通知だけ失敗**を同一実行で起こせます。MAILBOX 書込みで**期待コマンドと入力データを照合**し、想定外の取引・入力不一致・未消費の応答があれば `ptxn_bad=1` で試験を失敗させます。再要求も「二回目だから承認」とせず実際の要求内容と段階を照合。`ready_delay` で READY 解除を遅らせ、待機が実際に使われることを確認できます(既定 0=即時)。既存の script/sticky/no_ready は `ptxn==NULL` 時の fallback として保持。新しい汎用ハーネスは作っていません。
- **PLL 要求/応答の分離**(第43報の LOCK/FREQ_REQ_ACK モデル保持):driver が書く要求と fake が返す応答状態を別管理。PLL 無効化要求→LOCK 解除、ratio 設定→PLL 有効化要求→LOCK 成立、crawl 要求→LOCK+FREQ_REQ_ACK 成立、要求解除→記録。
- **順序付き書込み trace**(`wt_off/wt_val/wt_n` + `fake_wt_find(off, val, mask)`):本番関数が行った操作を順序込みで記録。**テスト側が期待する最終状態を直接書き込んで成功を作らない**ことを担保。trace は test_id/phase 相当をケース単位で `wt_n=0` リセットして分離。

## 2. CD-2:sanitize 確認から変更成功へ(完全再設定と crawl は別試験)

| ケース | 入口 | 検証 |
|---|---|---|
| **CD-2a 完全再設定** | init_hw() | PLL-off → sanitize(cdclk=0/vco=~0)→ **PREPARE→PLL 無効化(PLL_ENABLE clear 書込)→有効化(ratio14→PLL_ENABLE)→CDCLK_CTL=0x780164→通知→状態更新**。pll_en < ctl の順序、hw.cdclk=179200/vco=537600/voltage=0 |
| CD-2a variant | init_hw() | PLL LOCK 中でも CDCLK_CTL decimal 不整合 → 現値非ゼロでも unknown を経て完全再設定(179200/537600) |
| **CD-2b crawl** | bxt_set_cdclk() | 既知有効(307200/614400)→要求(556800/1113600/v2)。**PLL 無効化せず** ratio29+FREQ_REQ→LOCK\|ACK→FREQ_REQ 解除。CDCLK_CTL=0x380458。pll_dis 不在/FREQ_REQ 存在 |
| CD-2b guard | bxt_set_cdclk() | 現 VCO==要求 VCO → 不要な crawl 要求を出さない(0x46070 書込みゼロ) |

- 38.4 MHz fixture の期待値(ref38400/bypass19200/cdclk179200/vco537600/ratio14/除数3/voltage0)は**公開版 adlp_cdclk_table の最小値選択から得た fake 試験の期待値**であり、本番へ固定値を書く指示としては扱っていません。
- 合格は書込み trace と返却フィールドで検査(`hw_sequence_reached=1` だけに依存しません)。**完全再設定と crawl は別 trace**(最終周波数一致でまとめない)。CD-2-sanitize(cdclk=0/vco=~0)は残置し、CD-2 変更成功の代替にはしていません。

## 3. CD-4:通知失敗は「全不変」でなく部分更新を検証

CD-2a fixture を複製し、PREPARE/PLL 応答は成功、**最後の PCODE 通知だけ失敗**(注入 raw status 0x11 → -EACCES)。memcmp==0 は使わず個別比較:

| 検査対象 | 合格条件 |
|---|---|
| PREPARE | 成功(prepare_status=0) |
| PLL/CDCLK_CTL | 変更操作が trace に実在(pll_en≥0, ctl≥0) |
| 変更後通知 | この段階だけが指定 errno(-EACCES) |
| HW 巻戻し | テスト/wrapper が独自に戻していない |
| 最後の状態更新 | **intel_update_cdclk 未実行** → hw.cdclk=0(179200 にならない) |
| 個別フィールド | **hw.vco=537600**(PLL helper が更新済)/ voltage_level≠要求値2(上書きされない) |

「要求設定を丸ごとコピーしない」と「一切不変」の区別を厳密化しました(helper の途中更新 hw.vco は正本と一致)。生 status(0x11)と変換後 errno(-EACCES=zedBSD では -25)を区別し、時間基盤異常(-EIO)とは別扱い。実機ログ:`PCODE freq set failed (err -25, freq 179200)`。

## 4. combo PHY の void 契約(説明の是正、採用は継続)

out 引数分離(第43報)は保持。ただし **void は「常に成功」ではなく「戻り値で成否を返さない契約」**と明記します。D3 接続時は `return 0` に頼らず、正本処理が戻った後に**既存 fault 状態(parity_wait_time_base_faulted 等)を親で確認**し、MMIO/時間源/未実装の異常なら次の HW 操作へ正常続行しない、という局所方針を D3 実装で用います(本報は方針確定、実接続は D3)。

## 5. 次(D0/D3 統合、stub 禁止)と並行作業

- **D3 親**:`intel_power_domains_init_hw(false) → icl_display_core_init(false)` を上位から fake で最後まで。ADL-P の実効列:DC_state 無効化 → PCH reset handshake(HSW_NDE_RSTWRN_OPT rmw)→ **combo PHY(本体済)+ fault 確認** → PW1 enable(既存 power_well ops)→ **CDCLK(本体済)** → gen9_dbuf_enable(gen9_dbuf_slices_update + enabled_slices_mask、slice 毎 DBUF_CTL_S 要求+poll)→ tgl_bw_buddy_init(BW_BUDDY 表)→ xelpd WA(CHICKEN_DCPR_2 / DISPLAY_ERR_FATAL_MASK=~0)→ POWER_DOMAIN_INIT 参照保持 → 全 well 個別 sync_hw → initializing=false。cleanup=driver_remove(INIT 参照 put、well を一律 OFF にしない)。※ gen12_dbuf_slices_config と icl_mbus_init は ADL-P で no-op(正本確認済)。今回の combo PHY・CDCLK を本番と同じ device 状態・同じ関数本体で呼び、子を stub へ戻しません。
- **D0 の 3 実装**:fuse 実 poll(PW1 PG0 前/PG1 後)、DC_off gen9_set_dc_state・gen9_dc_off ops(DMC payload 確認)、VGA arbiter get→I/O→put 所有権。
- **並行 3(実装+実行結果で閉じる)**:sleep-range 実 backend(絶対期限+timer+waitq、20–25ms 待機中に同一 CPU の別 thread が実行され timer を安全回収)/ preemption 実 scheduler 試験(同一 CPU 二重禁止中に実 IRQ から切替要求 → IRQ 処理は進むが切替は延期、最外側解除後に処理)/ PCODE 追加区間 2 試験(追加区間の承認成功・時間源異常を scripted time で、どちらの出口でも preempt/mutex 復元)。20–25ms は sleep 実装確認用の試験入力で、PCODE 通常再要求間隔を変えるものではありません。

**実 GPU 次回起動条件**(変更なし):CDCLK 変更成功・通知失敗の試験完了(本報で達成)+ 実 sleep/実 preemption/PCODE 追加区間の確認 + D0 本番処理と D3 の INIT 取得・同期・cleanup が fake 完了 → 参照条件の通常 probe 一回 → intel_power_domains_init_hw(false) 完了 → INIT 参照保持で intel_dmc_init 入口(P3 全体/描画ではない)。作業中は run-parity-nogpu.sh を継続(probe=NOT_RUN は正しい)。

**保持**:MSI 分離 / runner / 共有 backend / 既存試験、per-pipe vblank、時間 error 伝播、VGA 登録、電源 map(30 wells)+ 是正 ops、PCI probe PM、時間 fault 試験、共通 PCODE + 三層 retry、combo PHY、CDCLK、preemption/sleep-range 契約。GPU=vfio-pci、drm 非 blacklist、attach 先行。描画再試験・GPU-hang 探索へは戻りません。

次報:D3 親の接続部(display_core_init 列 + INIT 参照/sync_hw/cleanup)、D0 の 3 実装、fake の DBUF/BW 応答、CD-2/CD-4 で得た本体の再利用状況、並行 3 項目の関数本体・試験結果。`last_completed_op` は attach 未実行のため該当なし(GPU-free)、CDCLK 変更経路 = GPU-free 検証済(145/0)。
