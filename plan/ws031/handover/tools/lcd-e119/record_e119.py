#!/usr/bin/env python3
"""WS031 E-119 records: ledger entry (results-ws031.md) and regression baseline.  usage: record_e119.py <repo root> <sweep line>"""
import sys
root = sys.argv[1].rstrip("/") + "/"
sweep = sys.argv[2]
L = root + "plan/ws031/results-ws031.md"
B = root + "plan/ws031/regression-baseline.md"
ledger = """

## p011 増分E-119 (2026-09-19): **同期 flip 実機 PASS — LCD-C（CPU で用意した A/B）と LCD-D（表示していない側を GPU で描き直して flip）**、GPU 未完了時の保護を外側 teardown まで、PTE 解放契約＋GT TLB 無効化

### 1. GPU 未完了時の保護（レビュー①）
`parity_lcdg_finish()`: submitted かつ未完了なら `parity_fhd_render_keep()`（texture／state／batch／timeline／RT／ring＋context state）、scanout を abandon、`parity_lcd_show_retain_gpu()` で device の GPU latch。probe teardown は `parity_lcd_kernel_gpu_retained()` が真なら engines release と ppgtt／gt_mem fini を行わない。runner は retained=1。GPU-free: FIN-GPU／FIN-REFUSE／FIN-TEARDOWN／FIN-NOTSHOWN／FIN-DISCARD。

### 2. PTE 解放契約と TLB（レビュー②）
`parity_fhd_render_release()`: -EBUSY（GPU 未完了）→ 全 mapping を scratch → 呼出しごとに walk で確認（合算しない、-EIO で ownership を保持）→ `parity_gt_invalidate_tlb_full()`（新規 gt_tlb.c: mmio_invalidate_full 準拠、FORCEWAKE_ALL、reset との直列化、全 engine、gen12 register、OA WA 0xceec）→ 解放。T1〜T3 harness は `eu_scrub_fixture_ptes()`（fixture は不変）。実機 LCD-G: 2028/2028、TLB rc=0、timeouts 0。

### 3. display_acquired、vblank snapshot、IRQ uninstall log（レビュー③ほか）
G-NOTSTARTED（prepare 拒否 → acquired=0、書込み 0、所有者が回収）。vblank 計数は `vbl_snapshot()`。uninstall log は実行時のみ。

### 4. 同期 flip
正本から生成: intel_pipe_update_start／_end、vblank counter／scanline helper、intel_crtc_update_active_timings（intel_enable_crtc の位置）。`parity_lcd_modeset_flip()`: DC_OFF を前後で保持（put_async 17 ms）、完了＝event（新 vblank IRQ＋frame 前進）かつ PLANE_SURFLIVE==新。TIMEOUT／NOT_LATCHED は stuck で両 buffer 保持、以後の flip は拒否。model: SURFLIVE の latch を別に表現、fault 3 種。host section I。kernel ops: vblank_get/put/sleep、irq_off/on（kern_irq_disable の戻り値で復元）、arm_event／wait_event。
- **LCD-C 実機 PASS**（-DPARITY_LCDC_TEST=1）: A=121／B=122、modeset 1 回で A→B→A→B→A、4/4 DONE、各 flip は +1 frame で live 切替、写真 6 枚一致（8 秒保持。3 秒保持の最初の試行はカメラ遅延で写真がずれたため撮り直し、verdict は両方 PASS）。
- **LCD-D 実機 PASS**（-DPARITY_LCDD_TEST=1）: A=0x100800000／B=0x101000000 に RT を常時 map（`parity_fhd_rt_map/unmap`）、`parity_fhd_render_run_ex(rt_va, variant, premapped)`。GPU 描画 9/9（毎回 2073600/2073600、同じ variant は同じ hash）、flip 8/8 DONE、写真 10 枚一致、最後に A/B 2025/2025 を scratch＋TLB（計 11 回、timeouts 0）、両 buffer 回収、lcd_retained=0。

### 5. 試験
host lcd-modeset **115/0**（+10）、lcd 56/0、dp 72/0、check_generated 一致。GPU-free ktest **535/0**（+20）。SWEEP_LINE
報告 = handover/expert-reports/report-e119-sync-flip.md、review diff = increment-results/e119-review-changes.patch（E-118 tree 比）、累積 = e97-e119-changes.patch。

### 6. 未解決
LCD-D は描画ごとに context などを作り直している（同一 context の反復 submit は未）。evasion の sleep 経路は実機未通過。「同じ backing」は代表 page。IMR bit 17/18・PIPESTATUS bit 30/29 は継続。
""".replace("SWEEP_LINE", sweep)
s = open(L).read()
assert "増分E-119" not in s
open(L, "w").write(s.rstrip("\n") + ledger)
base = """

## E-119 (2026-09-19)
- host: lcd-modeset **115/0**、lcd 56/0、dp 72/0。GPU-free ktest **535/0**。`check_generated.sh` 全一致。
- 新 mode `-DPARITY_LCDC_TEST=1`（CPU A/B 同期 flip）: 合格行 = `LCD-C verdict: PASS` ＋ `lcd_test=PASS`。`-DPARITY_LCDD_TEST=1`（GPU back-buffer 描き直し＋flip）: 合格行 = `LCD-D verdict: PASS` ＋ `lcd_test=PASS`。どちらも `RUNSCRIPT=./run-parity-ref-240.sh`。
- どの run の log にも GPU-free ktest 由来の「TLB invalidation did not complete in 4ms」が 8 行出る（fake register による意図的な timeout。判定外）。
- 回帰 sweep: `handover/tools/sweep_e119f.sh`（8 mode ＋ LCD-B ＋ LCD-R ＋ LCD-G ＋ LCD-C ＋ LCD-D）。SWEEP_LINE
""".replace("SWEEP_LINE", sweep)
s = open(B).read()
assert "## E-119" not in s
open(B, "w").write(s.rstrip("\n") + base)
print("recorded")
