#!/usr/bin/env python3
"""WS031 E-118: ledger entry and baseline.  usage: record_e118.py"""
import os
NL = chr(10)
W = os.path.expanduser("~/zedBSD/plan/ws031/")
p = W + "results-ws031.md"
s = open(p, encoding="utf-8").read()
assert "増分E-118" not in s
s = s.rstrip(NL) + NL + """
## p011 増分E-118 (2026-09-19): **GPU で描いた full-HD 画像を同じ backing から LCD へ表示 実機 PASS（初回）**、IRQ drain 失敗で well を落とさない修正、vblank の lock 規則、輝度の user 単位復元

### 1. IRQ 同期の安全化（レビュー §2・§3）
- pre-disable hook は int を返す。pipe の受付を閉じる → `GEN8_IRQ_RESET_NDX` → pipe の in-flight を drain。-ETIMEDOUT（終わらない）と -EIO（時間基盤）を区別する。失敗したら `pwc->irq_sync_failed` を立て、POWER_REQUEST を下ろさず -EBUSY を返す。以後、ALWAYS_ON 以外の disable はすべて拒否。put は refcount=1 に戻して所有を保つ（kept_wells）。LCD binding は refusal を backend_fault に変え、commit_disable の tail の error が停止未確認になる。probe の teardown は IRQ uninstall をせず資源を保持する。
- HAL（amd64）の確認: MSI の destination は割当時に固定、dispatch は vector ごとに in_handler flag 1 個だけで、呼出しが重ならない保証はコード上で読み取れない。→ driver 内の pipe gate（inflight++ → gate 確認、停止側は gate 閉 → reset → inflight==0 を待つ。いずれも seq_cst）。`parity_intel_synchronize_irq` の説明を「重ならない場合に限る。power-well 経路は依存しない」に訂正。
- vblank: refs／enabled／count／IMR を IRQ lock の下へ（locked helper）。待機者は pipe ごとに一人（二人目は -EBUSY）。即時 mask は E-117 限定経路の適応と明記（Linux の vblank_disable_immediate とは異なる）。実機 IRQ 判定の基準点は put → mask の読み戻し → drain の後。

### 2. 輝度（§5）
backlight device の user brightness を modeset が保持する（register 時に `scale_hw_to_user`、max 復帰にも追従）。復元は user 値で行う（host: user 30000 から始めて同じ DUTY に戻ることを確認）。実機 step は FREQ 不変、PWM enable、DUTY = user→hw 変換値を合否に入れた。

### 3. GPU 一枚表示（§6）
- show 分割: `parity_lcd_show_prepared()`（PINNED の buffer を表示・停止。作成・書込み・解放はしない。停止確認で PINNED のまま所有者へ返す）。CPU pattern の `parity_lcd_show_run()` は wrapper として残し、結果は不変。
- `tools/reftex.c` を argv で寸法を受ける形にした（既定 32×32 の出力は既存 inc と byte 一致、PS sha256 同一）。`reftex 1920 1080 rt` → `tex_fixture_fhd_gen.inc`（PS: scale 即値 1/1920・1/1080 だけが異なる。isl の render target RSS: B8G8R8A8 linear pitch 7680）。描画矩形と頂点は生成した寸法から出す。texture／sampler／packet 語は T1 と同一（試験で確認）。
- 同じ backing: scanout object の 2025 page を PPGTT 0x100800000 へ insert（GGTT 表示窓の binding とは別）。walk の leaf がその object の page であることを確認。VA 配置表と重複検査（GPU-free）。
- 順序: CPU 公開（prefill＋clflush）→ GPU 描画 → retire＋park → CPU は clflush してから読むだけ → 同じ object を表示 → 停止確認 → 再照合 → PPGTT PTE を scratch へ → GPU 側 object 解放 → unpin／destroy。timeout 時は gpu_done=0 で buffer を abandon。
- **実機（e118-run-parity-hw-lcdg.log）**: render PASS、marker 4 つ、**画素 2,073,600/2,073,600**、texture・guard 無変更、MOCS 6。**同じ backing**: PPGTT leaf 0x100331000／0x100b19000 = GGTT PTE 0x100331001／0x100b19001。表示: 最初の anomaly なし、underrun 0、停止確認、停止後の再照合で誤り画素 0、PTE 2025/2025 を scratch へ、unpin／destroy、電源参照の残り 0。runner `lcd_test=PASS`。写真 e118-photos/e118-gpu-picture.jpg（8×8 texture の 64 ブロック、四隅の色が variant 0 の定義どおり）、停止後は消灯。

### 4. 試験
host lcd-modeset **105/0**（+4: 電源返却の拒否 → 停止未確認、user 単位の復元ほか）、lcd 56/0、dp 72/0。GPU-free ktest **515/0**（+13: drain 失敗 → well が enabled のまま・所有・latch・別 well も拒否、閉じた pipe を handler が拒否、drain 中の時間基盤 fault は -EIO、二人目の待機者を拒否、full-HD の VA 重複なし、生成 state の内容、batch の描画矩形・頂点、全画素 verifier（誤り 2 画素を 2 と数える）、prepared 表示の所有権）。

### 5. 未解決
PPGTT の PTE は scratch に戻すが、GPU TLB の無効化は次回提出に委ねている（この mode では以後の提出なし。LCD-C の反復描画の前に正本の unbind／TLB invalidation へ接続する）。既存 T1〜T3 は解放後も PTE を残す（回帰基準なので変えていない）。IMR bit 17/18・PIPESTATUS bit 30/29 は継続扱い。
"""
open(p, "w", encoding="utf-8").write(s)
p = W + "regression-baseline.md"
s = open(p, encoding="utf-8").read()
if "E-118" not in s:
    s = s.rstrip(NL) + NL + """
## E-118 (2026-09-19)
- host: lcd-modeset **105/0**、lcd 56/0、dp 72/0。GPU-free ktest **515/0**。`check_generated.sh` 全一致。reftex の既定出力（32×32）は既存 `tex_fixture_gen.inc` と byte 一致。
- 新 mode `-DPARITY_LCDG_TEST=1`（GPU 描画を同じ backing から表示）: 合格行 = `LCD-G verdict: PASS` ＋ `lcd_test=PASS`。`RUNSCRIPT=./run-parity-ref-240.sh`。
- 回帰 sweep: `handover/tools/sweep_e118.sh`（8 mode ＋ LCD-B ＋ LCD-R ＋ LCD-G）。
"""
    open(p, "w", encoding="utf-8").write(s)
print("recorded")
