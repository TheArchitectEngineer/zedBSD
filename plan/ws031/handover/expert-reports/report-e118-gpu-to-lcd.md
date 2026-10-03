# WS031 E-118 報告: GPU で描いた一枚を同じ backing から LCD へ表示（実機 PASS、写真あり）＋ IRQ 同期の安全化

2026-09-19 / 対象: Dell Latitude 5330 eDP、VFIO/QEMU、新 mode `-DPARITY_LCDG_TEST=1`

## 0. 結論

- **GPU が 1920×1080 の scanout buffer 自身の backing へテクスチャ画像を描き、CPU が全 2,073,600 画素を書き込まずに照合し、その同じ object を LCD へ表示して停止・回収した。実機 1 回目で PASS。** 写真では、8×8 texture の 64 ブロックが画面いっぱいに表示されている（四隅: 左上 暗、右上 マゼンタ、左下 シアン緑、右下 淡黄 = texture variant 0 の定義どおり）。
- **同じ backing の証拠**: PPGTT の leaf（先頭 0x100331000、末尾 0x100b19000）と GGTT の PTE（0x100331001／0x100b19001 = 同じ page に present bit）が一致した。scanout object と render target は同一 object で、draw の後に CPU が画素を書く経路は無い。
- **IRQ 同期の安全化（最優先の指摘）**: drain に失敗したら **POWER_REQUEST を下ろさない**。その well は所有したまま保持し、以後のすべての well disable も拒否する（latch）。modeset は停止未確認として扱い、probe の teardown は IRQ handler を残したまま資源を保持する。時間基盤の異常（-EIO）と「handler が期限内に終わらない」（-ETIMEDOUT）は分けて返す。
- **同期方式**: HAL（amd64）が同一 vector の handler を重ねて実行しないという保証は、コードから確認できなかった。そのため driver 内で **pipe ごとの受付閉鎖 → source 停止 → in-flight の drain** を行う方式にした。HAL の変更は不要。
- 輝度は user 単位で保存・復元するように直し、実機試験では DUTY・FREQ・PWM enable を合否判定に入れた。vblank の参照・状態・count は一つの lock 規則の下に置き、待機者は pipe ごとに一人とした。「mask 後 0 回」の基準点は、mask の確認と drain の後に取るよう直した。
- host lcd-modeset **105/0**（+4）、lcd 56/0、dp 72/0。GPU-free ktest **515/0**（+13）。生成物は再現性全一致。reftex の既定出力（32×32）も byte 一致。回帰 sweep は §7 を参照。

写真: `e118-photos/e118-gpu-picture.jpg`（GPU 画像）、`e118-after.jpg`（停止後に消灯）。

## 1. IRQ 同期失敗で well を落とさない（§2）

```
parity_power_well_disable()
  ├─ pre_disable hook (irq.c): 受付を閉じる → GEN8_IRQ_RESET_NDX → 当該 pipe の in-flight を drain
  │     ├─ 0          → POWER_REQUEST を下ろす（従来どおり）
  │     └─ -ETIMEDOUT / -EIO → pwc->irq_sync_failed = 1、-EBUSY を返す（REQUEST は触らない）
  └─ irq_sync_failed の後: ALWAYS_ON 以外の well disable をすべて拒否（disable_refusals）
parity_power_well_put(): disable が -EBUSY なら refcount = 1（失敗した停止が所有し、次の get で再 enable しない。kept_wells）
LCD binding: put で refusal が増えたら backend_fault →（commit_disable の tail で error）→ stop_unconfirmed → buffer abandon
probe teardown: irq_sync_failed なら IRQ uninstall をせず、DMA／scratch／BAR／bus master を保持
```
- 試験（ktest）: PW_A を get → pipe A に in-flight を 1 件残したまま put → drain が timeout し、**well は enabled のまま**（REQ|STATE を読み戻して確認）、refcount 1、kept 1 となる。その後は別の pipe の well の disable も拒否される。閉じた pipe の IIR を handler は読まず ack もしない。drain 中の時間基盤 fault は -EIO になり、timeout とは別に数える。
- host: commit tail で pipe の電源返却が拒否されたら、disable は ERRORS、DC_OFF と pipe の電源は保持され、以後の prepare は拒否される。

## 2. 同期の保証（§3）
- HAL（amd64）の確認結果: MSI の destination は割当時に固定され（`hal_irq_set_affinity` は MSI を拒否する）、dispatch は vector ごとに `in_handler` を 1 個持つだけである。handler の中で EOI を送った後、前の呼出しが戻り切る前に次の呼出しが始まり得ないという保証は、コードからは読み取れなかった。
- そこで、pipe 単位の受付を driver 内で閉じる方式にした。handler は pipe に入る前に `pipe_inflight++`（seq_cst）してから gate を確認し、閉じていれば戻す。停止側は gate を閉じ（seq_cst）→ source を reset → `pipe_inflight == 0` を待つ。store→load の順序が両側で seq_cst なので、閉じた後に新しく入った処理は必ず gate を見て引き返す。post-enable で gate を開き直す。
- `parity_intel_synchronize_irq()`（入口／出口 count）の説明は「呼出しが重ならない場合に限り成立し、power-well 経路はこれに依存しない」と直した。IRQ 内で sleep はせず、巨大な lock も使わない。

## 3. vblank（§4）
- get／put、enabled、count の更新、IMR の更新は、すべて IRQ lock の下で行う。lock を取った状態で呼ぶ helper（`bdw_enable_vblank_locked` など）に分けたので、二重取得は無い。handler 側の count／wake も同じ lock の下にある。
- 待機者は pipe ごとに一人とし、二人目は -EBUSY を返す（completion の再初期化で一人目を壊さないため）。
- 「最後の put で即時 mask」は **E-117 の限定経路での適応**として記録を直した（Linux の vblank_disable_immediate は、vblank core の処理とイベント処理を経てから無効化する）。present／flip へはそのまま流用しない。
- 実機の IRQ 判定: put → mask の読み戻し → pipe A の drain → **ここで基準を取る** → 100 ms 観測。`parity_wait_vblank` は「表示が進んでいる」ことの確認であって、flip 完了の証明ではない（LCD-C でもこの区別を保つ）。

## 4. 輝度（§5）
- modeset が backlight device の user brightness を保持する（register 時に `scale_hw_to_user`、設定時に更新、`__intel_backlight_enable` による max への復帰にも追従）。復元はこの user 値で行う。host: user 30000 から始め、操作の後に user 値で復元すると、同じ DUTY に戻ることを確認した（hw 値を user として二重に変換しない）。
- 実機の step 判定: FREQ が開始時と同じか、max／half／min／on では PWM enable と「user → `scale_user_to_hw` の変換から求めた DUTY」、off では PWM disable と backlight off。合わなければ FAIL にする。写真は変化の方向の確認に使い、明るさ半分かどうかは数値判定しない。

## 5. GPU 一枚表示（§6）

### 5.1 show 本体の分割
- `parity_lcd_show_prepared(env, so, verify, ctx, report)`: 準備済み（PINNED）の buffer を表示・観測・停止する。**作成・画素書込み・unpin・destroy はしない。** 停止が確認できれば buffer は PINNED で所有者へ戻り（`display_released`）、確認できなければ ABANDONED（latch）になる。
- 既存の CPU pattern 経路（`parity_lcd_show_run`）は、自分の buffer を作り・埋め・この表示部を呼び・自分で回収する wrapper になった（LCD-B／LCD-R の結果は不変）。
- 試験: 未 pin の buffer は拒否されて状態不変。pin 済み buffer は表示・停止後に PINNED で戻り、画素は不変、object 数も不変。所有者が後で解放する。

### 5.2 同じ backing、二つの mapping
- GGTT: `parity_scanout_pin`（表示窓、256 KiB alignment、guard）。PPGTT: 同じ object の 2025 page を `0x100800000` から 1 page ずつ insert する。GPU が walk する表（先頭・中央・末尾）の leaf がその object 自身の page であることを確認した。
- VA 配置表（state 0x100400000、batch 0x100401000、texture 0x100404000、RT 0x100800000 + 8,294,400）を `parity_fhd_va_layout()` で持ち、alignment と重複の無いことを GPU なしで検査する。32×32 fixture は変更せず回帰用に残した。

### 5.3 shader と state は生成元から
- `tools/reftex.c` を argv で寸法を受ける形にした。既定（32×32）の出力は既存の `tex_fixture_gen.inc` と byte 一致で、PS の sha256 も同じ。`reftex 1920 1080 rt` で full-HD の PS（uv = (pixel+0.5)/(1920,1080)。差分は scale の即値 1/1920 = 0x3a088889 と 1/1080 = 0x3a72b9d6 だけ）と、isl による render target の RENDER_SURFACE_STATE（B8G8R8A8、linear、pitch 7680、size 0x0437077f）を生成した → `tex_fixture_fhd_gen.inc`。
- 描画矩形（1919×1079）と RECTLIST の頂点（1920, 1080）は、生成した寸法から出す。texture、sampler、packet の語は T1 と同一であることを試験で確認した。
- 期待画像: pixel(x, y) = texel(x/240, y/135)（nearest、LOD 0）。texel の境界までの余裕は 0.5/240 と 0.5/135 texel で、sampler の精度より十分大きい。

### 5.4 完了と可視性の分離
1. CPU が texture・state・batch を置き、RT を 0x5a5a5a5a で埋めて clflush で公開する。
2. GPU が描画し、request の retire と engine の park を確認する。
3. CPU 側は clflush で自分のキャッシュ行を捨ててから全画素を読む（**読むだけ**）。
4. 同じ object を表示する。
5. 停止確認の後、もう一度読み戻して画素が不変であることを確かめる。
6. PPGTT の 2025 PTE を scratch に戻し、GPU 側 object を解放する。
7. 最後に unpin／destroy する。

描画が timeout した場合は `gpu_done=0` のまま buffer を abandon し、GPU 側 object も解放しない。

### 5.5 実機結果（`e118-run-parity-hw-lcdg.log`）
- render: request 完了・park、marker 4 つ（before／middraw／after／PS）、**画素 2,073,600／2,073,600**、stale 0、texture と guard は無変更、MOCS 6（uncached）、image fnv 8de54ee98047eb25。
- 同一 backing: SAME PAGES（上記）。
- 表示: 最初の anomaly なし、underrun 0、vblank は mask のまま、停止確認済み。停止後の再照合で誤り画素 0。PTE 2025／2025 を scratch に戻し、buffer を unpin／destroy、表示 page 使用 0、電源参照の残り 0。
- runner: `probe=COMPLETE … lcd_test=PASS`。

## 6. 未解決（継続扱い、推測で閉じていない）
1. pipe A IMR bit 17／18 と ICL_PIPESTATUS bit 30／29 は raw 値を保存するだけ。LCD-R の判定 mask は、新しい IRQ source にそのまま流用しない。
2. PPGTT の PTE は scratch へ戻すが、GPU の TLB 無効化は次回提出時に委ねている。この mode では以後 GPU へ提出しないが、LCD-C で描画を反復する前に、正本の unbind／TLB invalidation の手順へ接続する必要がある。
3. 既存の T1〜T3 試験は、object を解放した後も PPGTT の PTE を残している（今回の full-HD 経路では直した。既存経路は回帰基準なので変えていない）。

## 7. 回帰
共有の IRQ／電源管理／GT メモリを変更したまとまりとして、同じ source で回帰を行った（`sweep_e118.sh`: 各 mode で GPU-free ktest 515/0 の後、実機 1 run）: **11/11 PASS**。内訳は、EU-REPEAT 5/5（GT 割込みを使う代表経路）、DRAW 1024/1024、R1 12/12、TEX 1024/1024、T3 9/9、BL 4/4、TEX＋explicit VBT、AUX＋SCANOUT、LCD-B、**LCD-R 3/3**、**LCD-G**。LCD-R では 7 つの輝度 step すべてで DUTY・FREQ・PWM_CTL が変換値と一致して OK になった（max 96000、half 50824、min 5647、off 0／PWM 0、on 50824、restore 96000、FREQ 0x17700 不変）。IRQ の判定も、drain 後の基準点で 3 cycle とも OK。32×32 の fixture（DRAW／TEX／T3／BL）は、reftex と selftest.c を変更した後も結果が不変。

## 8. 添付
- `e118-review-changes.patch`: E-117 末に対する今回の変更（IRQ の gate／drain／失敗伝播、vblank の lock、輝度の user 単位、show の分割、full-HD の fixture／render／PTE clear、LCD-G、試験。生成 include と reftex.c を含む）。
- 写真 2 枚、実機 log。

## 9. 次
二枚 buffer の同期 flip（初回 modeset とその後の plane 更新を分ける。A→B→A→B、更新の世代番号・対象 pipe・旧／新 surface を結びつけた完了判定、PLANE_SURFLIVE による診断）。その後、表示していない側の buffer を GPU で描き直す。GPU TLB の無効化（§6-2）はその前に接続する。
