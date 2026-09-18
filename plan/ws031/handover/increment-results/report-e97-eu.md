# WS031 E-97 報告: 実機 EU 試験（1 回）— HANG、署名は従来と同一

日付: 2026-09-18 / 対象: zedBSD parity（Linux 6.8.12 正本、execlists）/ 実機: ADL-P 8086:46a8（VFIO）

## 結論

明示解除をいただいた実機 EU 試験を 1 回実行しました。**結果は HANG**で、署名は big-bang 時代（E-16〜E-30）と同一です。Linux 通常初期化を parity で完走させた GT（P0〜P7、WA 表は実機 SRM 検証済み）でも、EU スレッドは完了しません。

```
EU-TEST HANG: engine=rcs0 dss=5 max_threads=559 batch=322 dw timed_out=1 (2 s) wedged=1
  ready=c0ffee10  eu=dead0000  done=dead0000  cs=dead0000   idd_rb_ok=1 kernel_rb_ok=1
  ipehr=70040000 (walker 直後の MEDIA_STATE_FLUSH)  row_instdone=8610e87f  fault=0  eu_dis=0  power ack=3/3/3
  ctx: CTX_CTRL=00090008 (default_state 継承, restore inhibit 無し)  PDP0=1:00213000
teardown 正常（engines reset rc=0, objects live=0）
```

（補足：EU 試験ビルドでは GPU-free の ktest が完走していませんでした。新規に入れた INHERIT 試験を、対象の default_state を解放する呼び出しの後ろに挿入してしまい、解放済みポインタを参照して停止したためです＝試験側の誤り。EU 試験自体は probe 側で先に完了しているので結果には影響しません。挿入位置を直した後は GPU-free／実機とも ktest 367/0、probe は COMPLETE です。）

- CS は walker 直前の READY marker まで実行し、IDD とカーネル本文を PPGTT 経由で正しく読み戻しています（写像は健全）。walker 投入後、EU スレッドが完了せず MEDIA_STATE_FLUSH で停止。GPU fault 無し、EU 電源 ack は 3。
- **今回の条件**：(1) Linux 正本どおりに初期化した GT、(2) `__engines_record_defaults` が保存した **engine->default_state を継承した新規 context**（正本 `lrc_init_state`）、(3) execbuf 形の request（invalidate → init breadcrumb → `gen8_emit_bb_start` → fini breadcrumb）、(4) forcewake 全保持、driver_register 後、(5) batch/kernel/IDD/VA は Linux で完走した L-C1 と同一バイト。
- したがって、**移植済み・検証済みの初期化要素（WA 表・MOCS・PAT・RC6/RPS・golden context・indirect-ctx BB）は単独原因から除外**されます。

## 試験後に見つけた自分の移植誤り（要・再試験判断）

E-95 で入れた `allocate_va_range` の実ページ表 PDE を、scratch 塔と同じ **PPAT_UNCACHED** で符号化していました。正本 `set_pd_entry` は `gen8_pde_encode(…, I915_CACHE_LLC)` ＝ **PPAT_CACHED_PDE（0）** です（UNCACHED は scratch 塔のみ）。今回の EU 試験の PPGTT はこの uncached PDE で走っています（CS の読み戻しが一致しているので写像自体は有効でしたが、正本との差です）。修正済み・GPU-free 試験も更新済みですが、**修正後の EU 再試験は行っていません**（1 回の解除でしたので、再試験はご指示ください）。

## 残る差分候補（Linux 陽性対照 E-23/E-25＝同一 GPU・execlists との差）

1. **MCR（multicast）レジスタの実効値**：ROW_CHICKEN2/4・SAMPLER_MODE・L3SQC 等の EU 向け WA は CS の SRM では検証できません（正本も除外）。steering 付き MMIO 読み戻しで確認できます（安価）。
2. **PDE キャッシュ属性**（上記、修正済み・未再試験）。
3. parity が「適応／N/A」と記録した箇所：GGTT/WC 窓は big-bang 資産（P2）、retire は CSB ポーリング、`intel_pxp_init_hw`（Linux では mei_pxp の bind で KCR init/irq が走る）、runtime PM、hwconfig。
4. 初期化以外の条件：Linux 陽性対照は Linux ブート後の GPU、zedBSD は OVMF＋FLR 後。fuse／クロック／電源状態の差はレジスタ全量 diff（E-27 は GLOBAL 一致・MCR は一部）でしか詰められません。

## 実装（記録）

- `eu_test.{c,h}`：C1 バッチの transcribe、PPGTT 写像、default_state 継承 context、execbuf 形 request、HANG 時の dump とリセット。`PARITY_EU_TEST`（既定 0）で明示ビルド時のみ実行。
- `engine->default_state` を導入し `lrc_init_state` が正本どおり継承（migrate/pxp の pinned context も以後継承）。ktest +2 → 367/0（EU 試験は `PARITY_EU_TEST`=0 で通常ビルドには含まれません）。

## 次（判断事項）

(a) PDE 修正で EU 再試験を 1 回、(b) MCR 実効値監査（steered read）、(c) 停止時の EU/TDL/GAM 状態の追加採取（専門家指定レジスタ）、(d) GuC submission の移植（Linux 既定だが陽性対照は execlists）。どれへ進むかご指示ください。

git commit / push はしていません。HAL インタフェースは不変です。
