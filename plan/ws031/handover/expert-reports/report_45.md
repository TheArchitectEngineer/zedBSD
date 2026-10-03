# WS031 第45報 — D3 親初期化 intel_power_domains_init_hw(false) を本番子関数・共有 device 状態で fake 上最後まで実行(D-NORMAL/PRESERVE/FAULT/REMOVE 四ケース)。fuse 実 poll。GPU-free 検証(ktest 153/0)

ご指示の第一着手 = **D3 親を既存の combo PHY／CDCLK へ接続し、同じ device を使う D-NORMAL を最後まで通す**を実施しました。CDCLK 本体は再作成せず接続。子関数は return 0 の stub に差し替えず、MMIO／PCODE／時間の外部接点だけを fake にしています。build 0 error/warning。**実 GPU は渡さない構成で検証**。

実機(`run-parity-nogpu.sh`、chaos、image 2d971aa5):`CAS-SELFTEST PASS` / **ktest 153/0**(145→153)/ **probe=NOT_RUN**(SYNC_ONLY)/ selftest=PASS。**D3 正常経路の統合試験が一本、最後まで通っています**。

---

## 0. CD-4 fixture 整合(第44報のご指摘)

CD-4 を **crawl setter fixture(要求 voltage 2 vs 変更前 0)** へ修正し、voltage が真に判別材料になるようにしました。通知失敗後:`hw.voltage_level==0`(要求 2 で上書きされない=正本の保持値)/ `hw.cdclk==307200`(intel_update_cdclk 未実行ゆえ 556800 にならない)/ `hw.vco==1113600`(crawl helper が更新済)を個別検査。生 status 0x11 と変換後 errno(-EACCES)を区別。

## 1. D3 親と既存部品の接続(display_core.{c,h}、共有 device 状態)

- `parity_display_core{ pd, cd, pwc, m, sb_lock, dram_type/channels, initializing, dbuf_enabled_slices, init_wakeref_held, pm_wakeref, 診断 }`。**同一 MMIO backend・同一 sb_lock・同一 power_domains/well・同一 cdclk.hw・P2 の DRAM 情報を子で複製・再初期化せず共有**。fake 試験でも子へ渡す前に「この先が通るよう状態を直す」操作を挟んでいません。
- `intel_power_domains_init_hw(false) → icl_display_core_init(false)`:DC_state 無効化 → PCH reset handshake(HSW_NDE_RSTWRN_OPT rmw)→ **combo PHY 本体**(戻り後に `parity_wait_time_base_faulted()` で fault 確認=void 契約を実際に接続、異常なら停止)→ **PW1 明示 enable**(pd->lock、既存 ops)→ **CDCLK 本体** → gen9_dbuf_enable → tgl_bw_buddy_init → xelpd WA(CHICKEN_DCPR_2 / DISPLAY_ERR_FATAL_MASK=~0)→ **POWER_DOMAIN_INIT 参照取得・保持**(disable_power_well=1 ゆえ追加参照なし)→ **全 well 個別 sync_hw** → initializing=false。gen12_dbuf_slices_config と icl_mbus_init は ADL-P で no-op(正本確認済)。
- **単一巨大 mutex で包んでいません**:pd->lock は PW1 enable / sync_hw / dbuf の各所で取得し、CDCLK は別の sb_lock を使うため入れ子で同じ mutex を取り直しません。

## 2. fuse(D0 の一部):PW1 の実 poll

power_well_enable の「modelled wait」を実装へ置換。has_fuses → `pg = ICL_PW_CTL_IDX_TO_PG(idx) = idx + PG1`。**PW1(pg==PG1)は enable 前に PG0 fuse(SKL_FUSE_STATUS の dist bit)待ち + Wa_16013190616(GEN8_CHICKEN_DCPR_1 DISABLE_FLR_SRC)**、REQ→ACK 後に **自 PG(PG1)fuse 待ち**。PG0=5us/他=1us、timeout は warn+継続、-EIO で停止。fake は **fuse 状態を well の REQ/STATE と別入力**にしています(REQ を書いたら fuse も自動成立、にはしていません。遅延 fuse も表現可能)。

## 3. DBUF／BW_BUDDY:正本の待機・保存状態

- **DBUF は「要求＋汎用 poll」にしていません**。`gen9_dbuf_slice_set` = **RMW POWER_REQUEST → posting read → udelay(10) → POWER_STATE 読 → 要求と状態不一致で WARN**。`gen9_dbuf_enable` は既に有効な slice を読み、そこへ S1 を加えた要求を作ります(全 slice 一律 ON にしない)。ADL-P=4 slice(S1..S4)。fake は STATE が REQUEST に追従。
- **BW_BUDDY**:tgl_buddy_page_masks 表を(num_channels, type)で引き、PAGE_MASK(0/1)を書込、未一致は BW_BUDDY_DISABLE。**P2 の DRAM 情報を入力**(CDCLK の固定 fixture 値は流用していません)。ver13 ゆえ ver12 の TLB timer WA は無し。

## 4. INIT 参照／同期／driver-remove(通常 domain put で代用しない)

`intel_power_domains_driver_remove()`:主 init_wakeref を取り出し、**runtime-PM 側のみ解放(pm_wakeref)、well の domain refcount は put しません**(wells は有効のまま=再ロード考慮)。disable_power_well=1 ゆえ disable_wakeref の domain put も無し。全 well count=0 の一律 cleanup にはしていません。D 途中で適合層異常なら未取得の参照を解放しません。

## 5. 試験:D3 統合四ケース(子を stub にせず、ktest 145 → 153/0)

| ケース | 主な合格条件 |
|---|---|
| **D-NORMAL** | 親完了(INIT 参照保持＋全 well 同期到達)/ CDCLK が共有 device で実行され cdclk=179200 / DBUF S1 有効＋BW page mask 0x1C / combo PHY_A COMP_INIT＋PW1 enable |
| **D-PRESERVE** | PHY/CDCLK 既に適切 → **PCODE 取引なし**(未消費)/ CDCLK diag_no_change=1 / DBUF 既存 S1 保持 |
| **D-FAULT** | 子途中(PW1 fuse/ACK)に時間異常注入 → 最初の停止位置保持 / **INIT 参照を取得せず** / 全 well 同期に到達せず / remove で未取得参照を解放しない |
| **D-REMOVE** | D-NORMAL 完了から診断終了 → **rpm wakeref のみ解放** / 全 well refcount 合計が不変(>0、domain put なし=wells 有効維持) |

fake に差し替えたのは MMIO・PCODE 応答・時間だけです。fault latch はケース毎に `parity_wait_test_reset_fault()`(test 専用の新規)でクリアして clean に開始。scheduler 全体網羅試験は始めていません。

## 6. 次(D0 残・並行三項目)と実 GPU 条件

- **D0 残**:DC_off(gen9_set_dc_state の enable で DC 無効化・is_enabled でレジスタ読・disable は DMC payload の有無で分岐。DC_off 用の簡略 CDCLK／PHY を別に作らず本体共有。DMC 未初期化 fixture を正しく与える)/ VGA arbiter(資源 get→I/O read→I/O write→put、client 登録と操作中所有権を分離、I/O accessor だけ fake)。fuse 遅延成立の追加試験。
- **並行三項目(実装＋実行結果で閉じる)**:sleep-range 実 backend(絶対期限＋hrtimer/clockevent＋waitq、早期起床で期限を後ろへ延ばさない、20–25ms＋同一 CPU 別 thread)/ preemption 実 scheduler 試験(二重禁止中に実 IRQ→切替要求→延期→最外側解除で処理)/ PCODE 追加区間 2 試験(承認成功・時間源異常で preempt・mutex 復元、scripted time)。
- **実 GPU 次回起動条件**(変更なし):D0 本番処理＋D3 の INIT 取得・同期・cleanup(本報で D3 は fake 完了)＋並行三項目 → 参照条件の通常 probe 一回 → intel_power_domains_init_hw(false) 完了 → INIT 参照保持で intel_dmc_init 入口(P3 全体/描画ではない)。作業中は run-parity-nogpu.sh 継続(probe=NOT_RUN は正しい)。probe.c の frontier は GPU-free 中 UNIMPL のまま(実 device の P3 は走らせません)。

**保持**:MSI 分離 / runner / 共有 backend / 既存試験、per-pipe vblank、時間 error 伝播、VGA 登録、電源 map(30 wells)+是正 ops、PCI probe PM、時間 fault 試験、共通 PCODE+三層 retry、combo PHY、CDCLK、preemption/sleep-range 契約。GPU=vfio-pci、drm 非 blacklist、attach 先行。描画再試験・GPU-hang 探索へは戻りません。

次報:D0 の DC_off／VGA 本体、fuse 遅延試験、並行三項目の関数本体・試験結果、probe.c への実 GPU 接続準備。`last_completed_op` は attach 未実行のため該当なし(GPU-free)、D3 親正常経路＝GPU-free 検証済(153/0)。
