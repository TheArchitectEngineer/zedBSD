#!/usr/bin/env python3
"""WS031 E-117: ledger entry and baseline.  usage: record_e117.py"""
import os
NL = chr(10)
W = os.path.expanduser("~/zedBSD/plan/ws031/")
p = W + "results-ws031.md"
s = open(p, encoding="utf-8").read()
assert "増分E-117" not in s
s = s.rstrip(NL) + NL + """
## p011 増分E-117 (2026-09-19): **LCD 再利用 実機 PASS — 同一 driver 生存期間で表示／停止 3 回（pattern 110／111／112）、power-well IRQ hook を正本どおり接続し実 vblank IRQ で待機、輝度 max／half／min、backlight off（scanout 継続）／on、最終停止・全回収。写真 9 枚**。E-116 レビューの境界修正 4 点

### 1. 境界修正（静的レビュー 2.1〜2.4）
1. **abandon の保持**: `parity_lcd_modeset_abandoned()` は状態を消さず `stop_unconfirmed`／retained を立てる。prepare と両 commit は初期化前に拒否。show 本体に device 側 latch（`parity_lcd_show_retained()`）— 以後の run は何も確保する前に -EBUSY、kernel 側 run も lock／状態初期化の前に拒否、外側 teardown はこの latch を見る。解除は `_discard_model()` のみ（保持しているのが register **model** の backend であるとき = model の破棄そのものが隔離）。実 hardware では解除手段なし。
2. **scanout create の状態契約**: NONE（かつ obj 無し）の storage だけ受理、それ以外は記録を一切変えず -EBUSY。storage は zero 初期化が前提と明記。下位の `parity_gt_object_destroy()`／`parity_gt_display_unbind()` も keep object を拒否（`keep_refusals`）— wrapper を迂回しても外れない。
3. **待機 error の意味**: `k_wait_reg` は timeout だけを `-ETIMEDOUT`(-110)、時間基盤／wait primitive の異常は `-EIO`(-5) にし `parity_lcd_backend_fault()` で modeset の最初の anomaly へ記録。sleep（eDP tick sleep の time_faults 増加）と udelay（`parity_udelay` の失敗）も同じ記録へ。10 ms tick は不変。
4. **LCD 試験結果の伝播**: `parity_result` に lcd_test_ran／pass／first anomaly の段／cleanup rc／retained、runner の最終行に `lcd_test=… lcd_first_anomaly_at=… lcd_cleanup_rc=… lcd_retained=…`。probe の成否は変えない（probe=COMPLETE と lcd_test=FAIL が並ぶ — 試行 1 で実証）。

### 2. power-well IRQ hook と vblank（irq.c／power_domains.c）
- `gen8_irq_power_well_post_enable()`: irq_lock（新設 spinlock）下で `intel_irqs_enabled()` を確認し、対象 pipe へ `GEN8_IRQ_INIT_NDX`（残 IIR clear → IER = ~de_irq_mask | vblank | underrun | flip done → IMR = de_irq_mask）。`gen8_irq_power_well_pre_disable()`: `GEN8_IRQ_RESET_NDX` の後、lock 外で `intel_synchronize_irq()`。power-well 本体の post-enable／pre-disable 位置から ops 経由で呼ぶ（従来は数えるだけの placeholder）。
- **同期**: HAL には detach せずに同期する API が無い（`hal_irq_detach_msi_sync` は detach）。handler の入口／出口 count を driver 側に持ち、呼出し時点の入口数に出口数が追いつくまで待つ（100 ms で -ETIMEDOUT と記録）。HAL 変更なし。**不足の明示**: HAL の同期契約は detach 時のみ。
- vblank: `bdw_update_pipe_irq`／`bdw_enable_vblank`／`bdw_disable_vblank` を移植、`drm_vblank_get/put` 相当（i915 の vblank_disable_immediate: 最後の put で即 mask）。handler の vblank bit から pipe ごとの count と completion へ通知（有効な pipe だけ）。待機は「呼出し後の新しい vblank IRQ が対象 pipe で n 回」かつ「hardware frame counter が進んだ」の両方 — 他 pipe の通知・古い pending bit・経過時間だけでは成功しない。`drm_crtc_vblank_restore()`（HAS_PSR 時）は DRM vblank count が無く PSR 非使用のため適応（comment に明記）。
- underrun: 所有者は従来どおり observer（ICL_PIPESTATUS）。IMR で underrun は mask のまま（IER は正本どおり立つ）なので handler は underrun を受けない。

### 3. 輝度と消灯（正本経由）
`intel_panel_set_backlight`／`scale_user_to_hw`／`intel_panel_actually_set_backlight` を生成 file に追加、`intel_edp_backlight_on/off`。API `parity_lcd_modeset_brightness(user, user_max)`／`_backlight(on)`。**VBT の min_brightness は 0..255 の係数**（`get_backlight_min_vbt`）: 実機 15 → backlight.min = 15/255 × 96000 = 5647。正本の `__intel_backlight_enable` は「level ≤ min なら max で再点灯」— host 試験で両方を確認。

### 4. 試験
- host lcd-modeset **101/0**（+22: 再呼出し拒否と保持、model 以外の解除拒否、time-base fault = -EIO かつ最初の anomaly、dither の導出 18→1／24→0、輝度 4 段・消灯中の scanout 継続・再点灯の level 規則、同一 eDP で 3 cycle）、lcd 56/0、dp 72/0。
- GPU-free ktest **502/0**（+42: IRQ hook post／pre・irqs 無効時の不書込み・vblank get/put と IMR・実 handler からの配送・待機の拒否条件 4 種・同期 timeout・PW_A の enable/disable 経由の hook、abandon の再呼出し／新 storage での拒否・下位層の拒否・teardown 後の latch 保持・model 破棄での解除、DBUF_CTL 表と正本 macro `DBUF_CTL_S()` の slice ごと照合、同一 lifetime で 2 回目の表示、in-window 試験失敗時の停止と回収）。`check_generated.sh` 全一致。

### 5. 実機（-DPARITY_LCDR_TEST=1、QEMU timeout 240 s の複製 script。reference 条件は同一）
- **試行 1（e117-…-attempt1.log）: 最初の anomaly = pipe A IMR の読み戻しが書込み値と不一致** 0xefe9f07f vs 0xefeff07f（bit 17／18 が 0 で読める。driver 以前の既定値 0xfff9ffff でも 0）。IER は期待値 0x90700f89 と完全一致、vblank 待機 3 回とも実 IRQ で成功（frame 56→59）、mask 後 100 ms の vblank IRQ 0。停止・回収は正常、runner に `lcd_test=FAIL lcd_first_anomaly_at=picture-up`。判定を「経路が依存する bit（vblank・underrun・flip done）の一致」へ改め、差分 bit は log に残す（意味は解釈しない）。
- **試行 2（e117b-…）: PASS 3/3**。各 cycle: post_enable +1／pre_disable +1、sync timeout 0、IER = 期待値、vblank get で IMR bit0 clear → 待機 3 回成功 → put で set → その後 vblank IRQ 0。輝度 DUTY: max 0x17700 → half 0xc688（50824 = 5647 + (96000−5647)/2）→ min 0x160f（5647）→ off（PWM_CTL 0、DUTY 0、frame counter 1429→1855 で scanout 継続、plane armed のまま）→ on で half に復帰 → 元の 96000。cycle 2／3 は pattern 111／112 を表示し全回収。runner: `probe=COMPLETE … lcd_test=PASS lcd_first_anomaly_at=none lcd_cleanup_rc=0 lcd_retained=0`。
- 写真（e117-photos/、contact sheet あり）: max／half／min で明るさが段階的に低下、backlight-off で消灯、on で復帰、111・112 の表示、最終停止後に消灯。
- 注: cycle 2／3 の `window=-17` は表示用 GGTT 窓が既に確保済み（-EBUSY）の意味で、設計どおり受理。

### 6. 未解決（推測で閉じない）
1. pipe A の GEN8_DE_PIPE_IMR bit 17／18 は書込みで 1 にならない（常に 0 で読める）。正本は読み戻さない。意味未確認、判定外。
2. ICL_PIPESTATUS bit 30／29（E-116 から継続、主線にしない）。
3. vblank 待機の同期は driver 内の入口／出口 count（HAL に synchronize_irq 相当が無い）— 同一 vector の並行実行を前提にしない近似であることを comment に記録。

### 残(次)
GPU で描いた一枚を同じ backing から表示（render と scanout の同一 object、show 本体の画素作成と表示の分離、GPU 完了と可視性の分離）→ 二枚 buffer の同期 flip。回帰 sweep の結果は追記。
"""
open(p, "w", encoding="utf-8").write(s)
p = W + "regression-baseline.md"
s = open(p, encoding="utf-8").read()
if "E-117" not in s:
    s = s.rstrip(NL) + NL + """
## E-117 (2026-09-19)
- host: lcd-modeset **101/0**、lcd 56/0、dp 72/0。GPU-free ktest **502/0**。`check_generated.sh` 全一致。
- 新 mode `-DPARITY_LCDR_TEST=1`（LCD 再利用）: 合格行 = `LCD-R verdict: PASS (cycles passed 3/3 …)` ＋ runner の `lcd_test=PASS`。実行は `RUNSCRIPT=./run-parity-ref-240.sh`（QEMU timeout 240 s、他は reference 条件と同一）。写真は `tools/watch_lcd_markers.ps1`。
- runner 行の末尾に `lcd_test=… lcd_first_anomaly_at=… lcd_cleanup_rc=… lcd_retained=…`（既存の合格正規表現は不変）。
- 回帰 sweep: `handover/tools/sweep_e117.sh`（8 mode ＋ LCD-B）。
"""
    open(p, "w", encoding="utf-8").write(s)
print("recorded")
