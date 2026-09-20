# WS031 E-116 報告: LCD-B — 対象 LCD に 1 枚表示して安全に停止（実機 PASS、写真あり）

2026-09-19 / 対象: Dell Latitude 5330 (ADL-P 8086:46a8, 1028:0b02) eDP panel、VFIO/QEMU、`-DPARITY_LCDB_TEST=1`

## 0. 結論

- **対象 LCD に既知 pattern (id 110) を表示し、20 秒の観測窓のあと正本の disable 経路で停止、全資源を回収した。** software 状態・hardware 観測・写真の 3 種の証拠がそろった（別々に記録）。
- 最終 run（e116d）: `first anomaly: none`、bring-up／cleanup error 0、unresolved step 0、sink status `77 00 01`（CR+EQ+lock、2.7 Gbps×2）、frame counter 15→1256（40 round）、underrun 開始・停止期間 0／安定期間 0、停止証拠 = power well on のまま frame counter 1273→1273＋TRANSCONF state clear、buffer 解放・readback 一致、**register の Linux dump 一致 17/17**（比較のみ。production code は dump 値を使っていない）。
- **写真が register では見えない誤りを 2 件あぶり出した**（§4）。うち 1 件は P3 期からの既存 code の誤り（DBUF_CTL の register 表が 1 つずれていた）で、underrun status は一度も立たなかった。「register 上の合格と表示の正しさは別」というご指摘のとおりだった。
- 未解決 2 件を §5 に明記（power-well の IRQ hook が placeholder のまま／ICL_PIPESTATUS bit 30・29）。推測で閉じていない。

写真（`e116-photos/`）: `e116-lcdb-before.jpg`（消灯）→ `e116d-lcdb-picture-dbuf.jpg`（最終: 定義どおり）→ `e116-lcdb-after.jpg`（停止後消灯）。途中経過 `e116-lcdb-picture.jpg`／`e116b-…`／`e116c-…-dither.jpg` は DBUF 表修正前で、背景に縞がある。

## 1. 接続したもの（前回レビューの項目順）

### (1) vblank／underrun の省略根拠 — 訂正済み、binding で確認
- 「正本も無操作」ではなく「この非公開・同期・単一 buffer の診断経路での明示的な適応」として記録（`lcd_seq_compat.h`、log に `(decided)` 行）。LCD-C の切替や Vulkan present へは一般化しない。
- binding の確認: (a) software vblank count を待つ経路なし（進行判定は frame counter のみ）、(b) pipe の vblank 割込み mask を **毎 sample 読んで記録**（well on の間だけ判定）— 全 51 sample で masked、(c) この経路は worker／callback／待機者を作らない。全 device 割込みの disable で代用していない。
- ただし §5-1 の事実に注意: mask が「masked」だったのは hardware の既定値によるもので、我々の code が正本どおり program した結果ではない。

### (2) PIPESTATUS の観測 — `parity_lcd_observe.c`（model と実機で同一 code）
開始前の値を保存 → 正本の clear 位置（crtc enable 内の `intel_set_cpu_fifo_underrun_reporting(true)` の位置）で underrun mask を clear → commit の各点（begin／pipe enabled／plane armed／plane disabled／pipe disabled／end）と安定表示中に採取。**記録してから clear**（次の期間と区別でき、取りこぼさない）。開始・停止期間と安定期間は別集計（全部 OR して fail にもせず、安定後に全部消しもしない）。log に register 名・address・bit mask（ICL_PIPESTATUS 0x70058、mask 0x9c000000 = bit 31／28／27／26）。所有者 = 試験（underrun 割込みは mask のままなので IRQ handler は読まず消さない）。

### (3) WM／DDB — 計算・割当・writer まで（E-115 後半）＋ DBUF／MBUS の実 programming（今回）
- 期待値の分解どおり: PLANE_WM 0x80004010 = enable／1 line／16 block、PLANE_BUF_CFG 0x0fdb0000 = [0,4060)。入力は通常初期化の値（latency 3/54/83/102/147/147/144/144、SAGV 35 us、IPC）と今回の commit の値（plane、format、pitch、pixel rate）。dump 値は入力に使っていない。
- model 検査: DDB 範囲・DBUF 内・WM0 enable（E-115）に加え、**DDB が載る slice が on か／joined が要るのに MBUS_CTL が joined か**。設定欠落 variant 3 種（PLANE_BUF_CFG 消失、DBUF slice 要求消失、MBUS_CTL write 消失）がそれぞれ violation 1 件になることを host 統合試験で確認。

### (4) commit 外側 — 正本 `intel_atomic_commit_tail()` の順序のまま
`parity_lcd_modeset_commit_enable()`／`_commit_disable()`:
- **CRTC power domain**: 正本 `get_crtc_power_domains()` の mask（生成 file）= PIPE_A／TRANSCODER_A／PORT_DDI_LANES_A／DISPLAY_CORE。encoder 自身の DDI_IO・AUX とは別 owner（model で domain ごとに 1 参照ずつを確認）。
- **DC_OFF**: commit 全体を囲む（get → … → `put_async_delay(17)`）。既存の DC_off 本体と既存の async put work を使用。
- **CDCLK**: 正本の要求計算（crtc 70400／plane 70400／帯域 11000 → adlp table ref 38400 → 179200 kHz、VCO 537600、level 0）= 現在値 → 正本の変更なし経路。不一致なら prepare が理由つきで拒否（試験あり）。
- **DBUF pre／post、MBUS**: 使用前に slice 確保（old|new）＋ MBUS join ＋ DBOX、後で new へ。disable commit では **pipe-off wait の後**に縮小・un-join（run log の順序を試験で確認）。既存の共有 state（`display_core` の本体）へ接続。
- **SAGV**: 適応（decided）— 強制 disable 状態を維持、必要帯域 564 MB/s ≦ 許可 QGV point 14899 MB/s を prepare で検査。PMDemand は正本条件で return。
- 適応 2 件: enable で anomaly → plane を arm しない／disable が error → commit 後半を実行せず全部保持（`stop_unconfirmed`）。

### (5) kernel binding — `parity_lcd_kernel.c`
値の出所を file 冒頭に列挙し、input を log に出力（PLL／link = 常駐 eDP の LCD-A、VBT = 今回 boot の blob、WM = `display_nogem`、DBUF = `display_core`＋MBUS_CTL 読み出し、CDCLK = `cdclk.hw`、QGV = 帯域 table＋強制 disable の mask、DMC = loader state、scanout = 今回 pin した object）。待ちは `parity_wait_reg`＋常駐 eDP の tick sleep、lock は実 mutex。**最初の anomaly／有効化した状態／後始末の結果を別記録**し、後始末の error は最初の anomaly を上書きしない。abandon は backing・DMA mapping・GGTT・pin・owner を保持、外側 teardown（`parity_gt_mem_fini`、probe の DMA device／scratch／BAR／bus master）も回収しない。

### (6) 試験 — 既存を維持し、指定の 4 種だけ追加
| | 前回 | 今回 |
|---|---|---|
| host 統合（lcd-modeset） | 51/0 | **79/0** |
| lcd／dp host | 56/0、72/0 | 同じ |
| GPU-free ktest | 460/0 | **474/0**（+14 `lcd_show_ktest.c`） |
- WM／DDB の host 統合＋設定欠落検出（上記 3 variant）。
- GPU-free kernel 試験: 実機と同じ `parity_lcd_show.c`（modeset 本体＋scanout 管理本体）を実 DMA 確保・実 mutex・実参照で 3 系統（正常／training 失敗／停止不能）。
- 所有権: 停止不能時に unpin／destroy 拒否、次の run 拒否、`parity_gt_mem_fini` 後も PTE 全数生存・内容読める。DC_OFF・power domain・DBUF を pipe の下から奪わない。
- 観測: frame counter 凍結＋0.5 s 経過 → 「進行」と判定しない。underrun を arm 時に立てる → 開始期間の記録に残り安定期間は clean、clear 後も失われない。安定中に立てる → 安定期間の記録に残る。
- 生成物の再現性 `check_generated.sh` 全一致。WM／DDB と IRQ の重要 register は正本 header と照合（DBOX の bit 位置は私の手計算が誤りで、header で訂正した）。

## 2. 実機 run の経過（隠さず全部）

| run | 結果 | 内容 |
|---|---|---|
| 1 `…-attempt1.log` | **preflight で停止、display へ write なし** | pipe A の register（IMR 等）は power well A 内で、PIPE_A domain を取るまで 0 を読む。preflight の判定を IRQ state に改め、observer は well on の間だけ判定、**model にも同じ事実を入れた** |
| 2 `…-first-picture.log` | PASS、初表示 | 写真で pattern 確認。ただし「停止後 frame counter 0→0」は well off 後の読み出しで証拠にならないと判明 → pipe-disabled 点で採取へ |
| 3 `e116b` | PASS | 停止証拠を well on のまま採取（1276→1276、TRANSCONF clear） |
| 4 `e116c` | PASS | dither 欠落を修正（PIPE_MISC 0x00800150） |
| 5 `e116d` | **PASS、dump 17/17、写真も定義どおり** | DBUF_CTL 表を修正 |

LCD-B が PASS した後の再実行は「合格済み mode の再実行」として行った。新しい hardware 操作を足した再実行はない（修正は状態 1 bit と既存 body の register 表）。

sink は実行時に vswing 1 を要求した（`train_set 01 01`）。model の sink は 0 を要求する設定なので、これは保存値の再生ではなく sink の応答から決まった値である証拠になる。

## 3. 写真の判定（最終 `e116d-lcdb-picture-dbuf.jpg`）
白枠が四辺に見える（crop／offset なし）。左上 赤／右上 緑／左下 青／右下 黄。F は左・正立（反転・回転なし）。110 が右（stale でない）。下 band の 8 色 bar（最後の黒まで）、上 band の grey ramp が滑らか。背景は単色の暗い青。停止後は消灯。

## 4. 写真が見つけた誤り
1. **dither**: crtc state に正本の判定（`intel_modeset_pipe_config`: `pipe_bpp == 18` なら dither）が無かった。Linux の display_info は `dither=yes, bpp=18`。atomic check 由来の state を手で組んでいる箇所の欠落。修正＋host 試験。
2. **DBUF_CTL の register 表（既存 code、P3 期）**: `{0x44FE8, 0x44300, 0x44304, 0x44308}` だったが正本は `{0x45008, 0x44FE8, 0x44300, 0x44304}`。「slice 1」が実際は 2 番目を on にし、4 番目の要求は DBUF でない register へ行って `power enable timeout` を出していた。表示は出たが背景・色 block に細かい縞。**underrun status は一度も立たなかった。** 修正後、縞は消え dump 一致 17/17。GPU-free ktest の fake も同じ誤った address を持っていたため検出できていなかった（fake を実装と同じ人間が同じ誤解で書くと検出力がない、という教訓）。

## 5. 未解決（推測で閉じていない）
1. **既存 power-well code は正本の `gen8_irq_power_well_post_enable()`／`_pre_disable()` を実行していない**（数えるだけの placeholder、`pwc.irqs_enabled` は 0 のまま）。実測: well on 後の pipe A は IMR 0xfff9ffff／IER 0（hardware 値）。vblank・underrun は mask され割込みは一切届かないので今回の試験には適合するが、正本の状態（IMR = `de_irq_mask`、IER = `~mask | vblank | underrun | flip done`）ではない。LCD-C（buffer 切替）より前に実装が必要。binding の comment にもこの事実を書いた。
2. ICL_PIPESTATUS の bit 30／29 が pipe 有効化後つねに set（0x60000000）。正本 header に定義が無く正本は読まず clear もしない。判定に入れていない。意味は未確認。
3. VBT の backlight min brightness は実機で 15（host fixture の 6 は fixture の値）。level は max（96000）で点灯。

## 6. 添付
- `e116-review-1-e116-changes.patch`: 今回の変更（E-115 末の commit 5f386896 比、手書き file のみ）— modeset の開始・停止 wrapper、`parity_lcd_kernel.c`、`parity_lcd_show.c`、observer、model、試験、既存 code の修正。
- `e116-review-2-wm-scanout.patch`: WM／DDB 適合層（`lcd_wm_compat.h`、`parity_wm_glue.inc`、`parity_atomic_plane_glue.inc`）と scanout の解放／abandon 処理（`scanout.{c,h}`、`gt_mem.{c,h}`）の現在の全文。
- 写真 6 枚、実機 log 5 本（handover/increment-results/）。

## 7. 回帰
既存 8 mode の sweep（`sweep_e116.sh`、DBUF 表修正後の同一 source、各 mode GPU-free ktest 474/0 の後に実機 1 run）: **8/8 PASS** — EU-REPEAT 5/5、DRAW 1024/1024、R1 12/12、TEX 1024/1024、T3 9/9、BL 4/4、TEX+explicit VBT、AUX＋SCANOUT。log は `e116-run-parity-hw-<mode>.log`。DBUF 表の修正（通常初期化で on になる slice が変わる）後も GPU 側の結果は不変。

## 8. 次
power-well の IRQ hook を正本どおり実装 → LCD-A 残り／backlight 制御 → LCD-C（buffer 切替）。compiler 拡張は LCD 本線の後（既存の拒否と回帰試験は維持）。
