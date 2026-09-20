#!/usr/bin/env python3
"""WS031 E-116: ledger entry, provenance section (from the generator's manifest), baseline.  usage: record_e116.py"""
import os, json
NL = chr(10)
R = os.path.expanduser("~/zedBSD/")
W = R + "plan/ws031/"

p = W + "results-ws031.md"
s = open(p, encoding="utf-8").read()
assert "増分E-116" not in s
s = s.rstrip(NL) + NL + """
## p011 増分E-116 (2026-09-19): **LCD-B 実機 PASS — 対象 LCD に既知 pattern(id 110)を 1 枚表示し、正本の停止経路で安全に停止・全資源回収。写真で表示を確認**。commit 外側・kernel binding・実 scanout 結合 ktest を接続

### 1. commit 外側（正本 `intel_atomic_commit_tail()` を 1 crtc 分に縮約、順序と callee は正本のまま）
- wrapper: `parity_lcd_modeset_commit_enable()`／`_commit_disable()`（`parity_lcd_modeset.c`）。enable: DC_OFF get → `intel_modeset_get_crtc_power_domains()` → [CDCLK: 変更なし] → `intel_dbuf_pre_plane_update()`（`update_mbus_pre_enable` + slice old|new）→ `intel_mbus_dbox_update()` → crtc enable → plane update → `intel_dbuf_post_plane_update()` → `intel_modeset_put_crtc_power_domains()` → DC_OFF `put_async_delay(17)`。disable: DC_OFF → domains 差分 → plane disable → crtc disable → DBUF pre／DBOX／post → domains put → DC_OFF put。
- CRTC power domain は正本 `get_crtc_power_domains()` の mask そのもの（生成 file）: PIPE_A／TRANSCODER_A／encoder の `PORT_DDI_LANES_A`／shared DPLL のための `DISPLAY_CORE`。encoder 自身の DDI_IO／AUX 参照とは別 owner（二重取得ではない）。`intel_display_power_get_in_set`／`put_mask_in_set` も生成 file。
- CDCLK: 正本 `intel_crtc_compute_min_cdclk`（pixel rate／2 = 70400）＋ plane（`icl_plane_min_cdclk` 70400）＋帯域（`intel_bw_crtc_min_cdclk` 11000）→ `bxt_calc_cdclk`（adlp table、ref 38400）= **179200 kHz／VCO 537600／voltage level 0 = 通常初期化が残した現在値** → 正本の変更なし経路（`intel_cdclk_changed` 偽）。異なる場合は prepare が理由つきで拒否（CDCLK programming は未接続、黙って通さない）。
- DBUF／MBUS: 既存の共有 state（`display_core` の `gen9_dbuf_slices_update` 本体 = power-domains lock と `dbuf_enabled_slices` を所有）へ ops 経由で接続。new = slice 0xf／joined。disable commit では pipe 停止後に old|new → new(0x1)、MBUS un-join。
- SAGV／QGV: **明示的な適応**（decided、log あり）— 初期化の強制 disable 状態（最大帯域 QGV point のみ）を維持し relax しない。prepare で必要帯域（564 MB/s）≦ 許可 point の derated 帯域（実機 14899 MB/s）を検査、不明なら拒否。PMDemand は正本条件（display ver < 14）で return。
- 適応 2 件（log に decided として出る）: enable で anomaly が出たら plane を arm しない／disable が error を返したら commit 後半（DBUF 縮小・power domain 返却・DC_OFF 返却）を**実行しない**（`stop_unconfirmed`）。

### 2. 観測（`parity_lcd_observe.c`、model と実機で同一 code、register は正本 macro）
ICL_PIPESTATUS(0x70058、underrun mask 0x9c000000 = bit31／28／27／26)を試験が所有: 正本の clear 位置（`intel_set_cpu_fifo_underrun_reporting(true)` の位置 = crtc enable 内）で clear、commit の各点（begin／pipe enabled／plane armed／plane disabled／pipe disabled／end）と安定表示中に採取、**記録してから clear**、開始・停止期間と安定期間を別集計。frame 進行は frame counter のみで判定（経過時間では判定しない）。vblank 割込み mask は pipe の power well が on の間だけ判定。停止の証拠は **pipe-disabled 点（well がまだ on）で採取**。

### 3. kernel binding（`parity_lcd_kernel.c`）と共有本体（`parity_lcd_show.c`）
値の出所は file 冒頭に列挙（常駐 eDP の DPCD／EDID／LCD-A、今回 boot の VBT、`display_nogem` の WM latency、`display_core` の DBUF、`cdclk.hw`、帯域 table＋QGV mask、DMC loader state、今回 pin した scanout）。Linux dump の値は log の比較材料のみ。最初の anomaly／有効化した状態／後始末の結果を別記録（後始末の error は最初の anomaly を上書きしない）。abandon は backing・DMA mapping・GGTT・pin・owner を保持し、外側 teardown（`parity_gt_mem_fini`＋ probe の DMA device／scratch／BAR／bus master 解放）も回収しない。

### 4. 試験
- host 統合 **79/0**（+28: commit 外側の会計と順序、DBUF／MBUS 設定欠落の検出 2 variant、CDCLK 不一致・帯域不明・hook 欠落の拒否、frame counter 凍結を経過時間で進行と判定しない、underrun を開始期間／安定期間に分けて失わない、停止不能時に DC_OFF・domain・DBUF を奪わない、PIPE_MISC dither）。lcd 56/0、dp 72/0。
- GPU-free ktest **474/0**（+14 `lcd_show_ktest.c`: 実 DMA 確保の scanout object＋実 mutex＋model で LCD-B 本体を 3 系統 — 正常／早期失敗（plane 未 arm、全返却、最初の anomaly 保持）／停止不能（ABANDONED: PTE 全数生存・unpin/destroy 拒否・次の run 拒否・`parity_gt_mem_fini` 後も保持））。
- `check_generated.sh` 全一致。回帰 sweep（8 mode）は別記。

### 5. 実機（-DPARITY_LCDB_TEST=1、log は handover/increment-results/、写真は e116-photos/）
- **試行 1（e116-…-attempt1.log）: preflight で停止、display へ write なし**。原因 = pipe A の register（GEN8_DE_PIPE_IMR(A) 等）は power well A の中にあり、commit が PIPE_A domain を取るまで 0 を読む。preflight の register 判定を IRQ state の判定へ改め、observer は well on の間だけ mask を判定、model にも同じ事実を入れた（host 試験で検出できる形に）。
- **試行 2（…-first-picture.log）: PASS、写真 e116-lcdb-picture.jpg に pattern 表示**（白枠、左上赤／右上緑／左下青／右下黄、左に F、右に 110、下に color bar、上に grey ramp = 定義どおり、反転・回転・ずれなし）。停止後の写真は消灯。sink: 2.7 Gbps×2、status 77 00 01、**train_set 01 01 = sink が実行時に要求した vswing 1**（model の 0 とは異なる — 保存値の再生ではない証拠）。frame counter 17→1257（40 round、約 20.7 s）。underrun: 開始・停止 0／安定 0。register の Linux dump 一致 16/17（相違 1 = DBUF_CTL_S1 の power bit 31:30 が不成立: 0x0043c000 対 0xc043c000 — 原因は下の DBUF 表の誤り）。
  - この run の「停止後の frame counter 0→0／TRANSCONF 0」は **well off 後の読み出しで証拠にならない**と判明 → pipe-disabled 点（well on）で採取するよう修正。
- **試行 3（e116b-…）: PASS、停止証拠 = well on のまま frame counter 1276→1276（50 ms 静止）＋ TRANSCONF state bit clear**。
- 写真と log から parity の欠落を 1 件発見: crtc state に正本の dither 判定（`intel_modeset_pipe_config`: pipe_bpp == 18 なら dither）が無く PIPE_MISC が dither なしだった（Linux の display_info は dither=yes）。修正し host 試験を追加。**試行 4（e116c-…）: PASS、PIPE_MISC 0x00800150**、写真 e116c-lcdb-picture-dither.jpg。
- **写真が既存 code の誤りを発見**: 試行 2〜4 の写真は背景（単色 0x102040）と色 block に細かい縞があり、log には `DBUF slice 3 power enable timeout` と DBUF_CTL_S1(0x45008) の power bit 不成立が出ていた。原因 = `display_core.c` の `dbuf_ctl_s[]` が {0x44FE8, 0x44300, 0x44304, 0x44308} で、正本（`skl_watermark_regs.h`: S0..S3 = 0x45008／0x44FE8／0x44300／0x44304）と 1 つずれていた（P3 期の手移植の誤り。「slice 1」が実際は 2 番目の slice を on にし、4 番目は DBUF でない register へ）。**underrun status は一度も立たなかった** — register 上の合格と表示の正しさは別、という専門家の指摘どおり。表を正本値へ修正（ktest の fake も）。**試行 5（e116d-…）: PASS、Linux dump 一致 17/17、写真 e116d-lcdb-picture-dbuf.jpg は単色背景・滑らかな grey ramp・黒の第 8 bar まで定義どおり**。通常初期化後の slice は 0x3（firmware が残した S2 を保持＋S1）、停止後は 0x1（正本どおり）。
- 全 run: first anomaly none、bring-up／cleanup error 0、unresolved step 0、power ref 残 0、buffer 解放済み、readback 一致。

### 6. 未解決として記録（推測で閉じない）
1. **既存 power-well code は正本の `gen8_irq_power_well_post_enable()`／`_pre_disable()` を実行していない**（数えるだけの placeholder、かつ `pwc.irqs_enabled` が 0 のまま）。実測: well on 後の pipe A は IMR 0xfff9ffff／IER 0 = hardware 値（vblank・underrun は mask、割込みは一切届かない）。この試験には適合するが正本の状態（IMR = de_irq_mask、IER = ~mask | vblank | underrun | flip done）ではない。pipe 割込みを使う段（LCD-C の切替、vblank）より前に実装が必要。
2. ICL_PIPESTATUS の bit 30／29 が pipe 有効化後つねに set（0x60000000）。正本 header に定義が無く、正本は読まず clear もしない。underrun mask 外なので判定に入れていない。意味は未確認。
3. （解決）写真の縞と DBUF_CTL_S1 の相違は上記 DBUF 表の誤りで説明がつき、修正後に消えた。ただし「DBUF slice が off でも underrun status が立たない」ことは事実として残る: 表示の正しさは写真（または CRC 等）でしか判定できない。
4. backlight: VBT の min brightness は 15（cfg の想定 6 は試験 fixture の値）— 実機 input の log に従う。level=max(96000)。

### 残(次)
gen8 power-well IRQ hook の実装 → LCD-A 残り／backlight 制御 → LCD-C（buffer 切替: vblank／flip done が要る）。compiler 拡張は LCD 本線の後。
"""
open(p, "w", encoding="utf-8").write(s)
print("ledger updated")

# provenance: the generated files of E-114..E-116, from the generator's own manifest
m = json.load(open(R + "src/drivers/gpu/i915/parity/lcd/port_lcd_calc.manifest.json"))
p = W + "provenance-ledger.md"
s = open(p, encoding="utf-8").read()
if "E-114〜E-116" not in s:
    rows = []
    for src in sorted(m["kept"].keys()):
        kept = m["kept"][src]
        sha = m["sources"].get(os.path.basename(src), "?")
        rows.append("| `" + src + "` | " + sha[:16] + "… | " + str(len(kept)) + " |")
    s = s.rstrip(NL) + NL + """
## 12. E-114〜E-116: LCD modeset 経路の生成 file（generator の manifest から機械的に作成）

生成器 `handover/tools/port_lcd_calc.py`＋表 `port_lcd_modeset.json`。関数本体・macro・型は固定した正本 text からの抽出で、手入力していない。各生成 file の先頭 license／copyright comment は**正本 file の先頭 comment をそのまま複写**したもの（名義を推測・入力していない）。再現性は `check_generated.sh`（全出力の byte 一致＋ DRM 正本の SHA256SUMS）。出力ごとの sha256 と採用した関数名は `src/drivers/gpu/i915/parity/lcd/port_lcd_calc.manifest.json`。

| 正本 file（Linux v6.8.12 系、ubu-i915-src／drm-v6.8.12） | sha256（先頭） | 採用単位数 |
|---|---|---|
""" + NL.join(rows) + NL + """
zedBSD project code（正本由来でない）: `parity_lcd_modeset.{c,h}`、`parity_lcd_modeset_int.h`、`parity_lcd_ops.h`、`parity_lcd_trace.{c,h}`、`parity_lcd_observe.{c,h}`、`parity_lcd_regs.c`（register 名と address は抽出 macro、Linux dump の値は比較用）、`parity_lcd_show.{c,h}`、`parity_lcd_kernel.{c,h}`、`lcd_fake_hw.{c,h}`、`lcd_modeset_ktest.{c,h}`、`lcd_show_ktest.{c,h}`、`lcd_*_compat.h`、`parity_*_glue.inc`（glue は正本の caller の流れを 1 crtc 分に縮約、出典関数名を comment に明記）。
"""
    open(p, "w", encoding="utf-8").write(s)
    print("provenance updated")

p = W + "regression-baseline.md"
s = open(p, encoding="utf-8").read()
if "E-116" not in s:
    s = s.rstrip(NL) + NL + """
## E-116 (2026-09-19)
- host: lcd-modeset **79/0**、lcd 56/0、dp 72/0、vk fixtures 10。GPU-free ktest **474/0**。`check_generated.sh` 全一致。
- 新 mode `-DPARITY_LCDB_TEST=1`（LCD-B）: 合格行 = `LCD-B verdict: PASS`（furthest stage=released）＋ `first anomaly: none`＋写真。window 20 s、QEMU の timeout 120 s 内（boot 約 30 s）。
- 既存 8 mode の sweep: `handover/tools/sweep_e116.sh`（結果は ledger E-116 の追記を参照）。
"""
    open(p, "w", encoding="utf-8").write(s)
    print("baseline updated")
