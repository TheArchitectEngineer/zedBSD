# WS031 E-117 報告: LCD の再利用 — IRQ hook・実 vblank・再点灯・輝度（実機 PASS、写真あり）

2026-09-19 / 対象: Dell Latitude 5330 eDP、VFIO/QEMU、新 mode `-DPARITY_LCDR_TEST=1`

## 0. 結論

- **同一 driver 生存期間で「表示 → 停止」を 3 回（pattern 110／111／112）実行し、3/3 PASS。** 初期化（P0〜P7）や GT reset のやり直しは無く、modeset と panel の通常の状態遷移だけで再利用した。
- **power-well の IRQ hook を正本どおり接続した**（post-enable = `GEN8_IRQ_INIT_NDX`、pre-disable = `GEN8_IRQ_RESET_NDX` + `intel_synchronize_irq`）。各 cycle で pipe A の well on により IER が期待値 `0x90700f89` に一致した。vblank を明示的に unmask し、**pipe A の実 IRQ で待機者が 3 回起き**（新しい vblank かつ frame counter の前進を条件とする）、mask 後は vblank IRQ が 0 回だった。well off の前には pre-disable と同期が走った（同期の timeout は 0 回）。
- **輝度と消灯・再点灯**: 正本の `intel_panel_set_backlight` 経由で max → half → min と変化させ、写真でも段階的に暗くなった。backlight を消している間も scanout は継続し（frame counter が進み、plane は arm されたまま）、再点灯で直前の level に復帰した。最後は元の値に戻した。
- **レビューの境界修正 4 点を回帰試験付きで反映した。** 1 回目の実機試行が「LCD は FAIL、probe は COMPLETE」で終わり、runner が `lcd_test=FAIL` を正しく返すことをその場で実証した。
- host は lcd-modeset **101/0**（+22）、lcd 56/0、dp 72/0。GPU-free ktest は **502/0**（+42）。生成物は再現性全一致。回帰 sweep は §6 を参照。

写真（`e117-photos/`、縮小一覧 `e117b-contact-sheet.jpg`）: max／half／min／backlight-off／backlight-on／cycle 2（111）／cycle 3（112）／最終停止後。

## 1. 境界修正 4 点（§2.1〜2.4）

| 項目 | 修正 | 回帰試験 |
|---|---|---|
| 2.1 abandon の保持 | `parity_lcd_modeset_abandoned()` は状態を消さず、stop_unconfirmed と retained を立てる。prepare と両 commit は**初期化前に**拒否する。show 本体に device 側 latch を持たせ、kernel の run は lock や状態を初期化する前に拒否し、外側 teardown もこの latch を見る。解除は、保持しているのが register **model** の backend であるときの `_discard_model()` だけ（model の破棄そのものが隔離）。実 hardware には解除手段を置かない | host: 再呼出しの拒否（prepare／commit）、保持状態の不変、model でない backend からの解除拒否。ktest: 停止不能 → 同じ入口 → 拒否 → 新しい storage でも確保前に拒否 → 下位層も拒否 → `parity_gt_mem_fini` 後も PTE 全数生存 → latch 保持 → model 破棄でのみ解除 |
| 2.2 create の状態契約 | `parity_scanout_create()` は NONE（obj 無し）の storage だけを受理し、それ以外は記録を一切変えずに -EBUSY を返す。storage は zero 初期化が前提であることを明記した。下位の `parity_gt_object_destroy()`／`parity_gt_display_unbind()` も keep object を拒否する（`keep_refusals`） | ktest: abandon 済み object への create は拒否され記録不変。下位の destroy／unbind を直接呼んでも bound・in_use・PTE は不変 |
| 2.3 待機 error の意味 | timeout は `-ETIMEDOUT`（-110）、時間基盤／wait primitive／MMIO の異常は `-EIO`（-5）に分けて返す。後者は `parity_lcd_backend_fault()` で最初の anomaly に記録する。sleep（eDP tick sleep の time_faults 増加）と udelay も同じ記録へ流す。10 ms tick は変更していない | host: model の PLL-lock 待機に時間基盤 fault を注入 → wait は -5 を返し、最初の anomaly は正本自身の「PLL not locked」より先の時間基盤 fault になる。plane は arm されない。基盤が戻れば停止経路で全回収 |
| 2.4 結果の伝播 | `parity_result` に lcd 結果を追加し、runner の最終行に `lcd_test=… lcd_first_anomaly_at=… lcd_cleanup_rc=… lcd_retained=…` を出す。probe の成否は変えない | 実機試行 1: `probe=COMPLETE … lcd_test=FAIL lcd_first_anomaly_at=picture-up` |

## 2. IRQ hook と handler の接続（irq.c／power_domains.c）

- **post-enable**: irq_lock（新設の spinlock）の下で `intel_irqs_enabled()` を確認し、対象 pipe だけに対して残っている IIR を clear → IER = `~de_irq_mask | vblank | underrun | flip done` → IMR = `de_irq_mask` の順に書く。power-well 本体の post-enable の位置から ops 経由で呼ぶ（従来は回数を数えるだけの placeholder だった）。
- **pre-disable**: 対象 pipe の IMR／IER／IIR を reset し、lock を外してから同期する。well を落とす前に済ませる。
- **同期**: HAL には detach せずに同期する API が無い（`hal_irq_detach_msi_sync` は detach とセット）。そこで handler の入口と出口の count を driver 側に持ち、呼び出した時点の入口数に出口数が追いつくまで待つ（100 ms で -ETIMEDOUT として記録）。**HAL は変更していない。不足点の明示**: HAL の同期契約は detach 時にしか存在しない。device 全体の MSI detach で pipe 単位の同期を代用してはいない。
- **hook と unmask は別**: post-enable 直後の IMR では vblank は mask のままで、これを GPU-free ktest で確認した。vblank は `drm_vblank_get/put` 相当（`bdw_update_pipe_irq`、i915 の vblank_disable_immediate）で明示的に有効化・無効化する。
- **handler**: 既存の read／ack の後、vblank bit を持つ有効な pipe の count と completion へ通知する。待機は「対象 pipe の新しい vblank IRQ」かつ「hardware frame counter の前進」を両方要求する。他 pipe の通知、古い pending bit、経過時間だけでは成功しない（ktest で 4 種類の拒否を確認）。
- **teardown**: uninstall 時に vblank 参照が残っていれば警告する。待機者はこの試験の thread だけで、worker や callback は作らない。
- **underrun の所有**: 従来どおり observer（ICL_PIPESTATUS）が所有する。IMR で underrun は mask されたままなので（IER は正本どおり立つ）、handler は underrun を受け取らない。
- LCD-B の「vblank は常に masked」という合格条件は削除せず残した。再利用 mode では、試験の中で unmask → 待機 → mask を行い、observer の採取点では再び masked になっている。

## 3. 輝度（正本経由）

- `intel_panel_set_backlight`／`scale_user_to_hw`／`intel_panel_actually_set_backlight` を生成 file に追加した。backlight の消灯・点灯は `intel_edp_backlight_off/on` を使う。
- **VBT の min_brightness 15 は 0..255 の係数**として扱う（`get_backlight_min_vbt`）。backlight.min = 15/255 × 96000 = 5647 で、「15 count」や「15%」ではない。
- 実測した DUTY: max 0x17700（96000）→ half 0xc688（50824 = 5647 + (96000−5647)/2）→ min 0x160f（5647）→ off（PWM_CTL 0、DUTY 0）→ on で 0xc688 に復帰 → 元の値。PWM 周期（FREQ 0x17700）は常に不変。
- 正本の `__intel_backlight_enable` は「level ≤ min の状態から再点灯すると max になる」。この規則は host 試験で確認した。実機では half に戻してから消灯したので half で復帰した。
- 消灯試験と表示停止は別の試験として扱った。消灯中の 7 秒間も frame counter は 1429→1855 と進み、plane は arm されたまま（buffer は使用中）だった。

## 4. 再利用（同一 driver 生存期間）

- 一つの表示所有者が直列に実行する。modeset object は global なので、DPLL／backlight の mutex があっても modeset 全体が多重実行に安全になるわけではない。この制約は log と comment に明記した。
- 各 cycle の入力は実 object から取り直す（DBUF は `display_core` の共有状態、MBUS は register の読み出し）。PPS の power-cycle 待機と VDD-off の予約・取消は、常駐 eDP の既存処理が担う。
- host では同一 eDP で 3 cycle 回し、毎回 training・arm・全回収を確認した。GPU-free ktest では同一 lifetime で 2 回目の表示と、in-window 試験の失敗時にも停止・回収することを確認した。

## 5. 実機 run（隠さず全部）

| run | 結果 | 内容 |
|---|---|---|
| 試行 1 `e117-…-attempt1.log` | **cycle 1 で FAIL（最初の anomaly）、以後の cycle は実行せず** | pipe A IMR の読み戻しが書き込んだ値と一致しなかった（0xefe9f07f、書込み値は 0xefeff07f）。**bit 17／18 が 0 で読める**（driver が触る前の既定値 0xfff9ffff でも 0）。それ以外（IER 一致、実 IRQ で待機 3 回成功、mask 後 0 回）はすべて期待どおり。停止・回収は正常で、runner は `lcd_test=FAIL` を返した。正本は IMR を読み戻さないので、判定を「経路が依存する bit（vblank・underrun・flip done）の一致」に改め、差分 bit は log に残す（意味は解釈しない） |
| 試行 2 `e117b-…` | **PASS 3/3** | 上記のとおり。runner: `probe=COMPLETE … lcd_test=PASS lcd_first_anomaly_at=none lcd_cleanup_rc=0 lcd_retained=0` |

cycle 2／3 の `window=-17` は、表示用 GGTT 窓が既に確保済み（-EBUSY）という意味で、設計どおり受理している。

## 6. 回帰
共有の IRQ／電源管理を変更したまとまりなので、同じ source で回帰を行った（`sweep_e117.sh`: 各 mode で GPU-free ktest 502/0 の後、実機 1 run）: **9/9 PASS** — EU-REPEAT 5/5、DRAW 1024/1024、R1 12/12、TEX 1024/1024、T3 9/9、BL 4/4、TEX＋explicit VBT、AUX＋SCANOUT、LCD-B（`lcd_test=PASS`）。GT 側（GT の割込みを使う試験を含む）は IRQ 層の変更後も結果が変わらない。

## 7. 未解決（推測で閉じていない）
1. pipe A の GEN8_DE_PIPE_IMR bit 17／18 は 1 を書いても 0 で読める。正本は読み戻さない。意味は未確認で、判定には入れていない。
2. ICL_PIPESTATUS bit 30／29 は E-116 から継続（主線にしない。raw 値を保存するだけ）。
3. IRQ の同期は driver 内の入口／出口 count による。同じ vector が並行実行されないことを前提にした近似であることを comment に記録した。

## 8. 添付
- `e117-review-changes.patch`: E-116 末（2bf790a4 + e97-e116 を再構成した tree）に対する今回の変更。対象は手書き file のみ（IRQ hook と handler の接続、4 点の修正、輝度・再利用、試験）。
- 写真 9 枚＋一覧、実機 log 2 本。

## 9. 次
GPU で描いた一枚を同じ backing から表示する（render と scanout で同一 object を参照、show 本体の「画素作成」と「表示・停止」の分離、GPU 完了とCPU 可視性の分離、full-HD 用の座標規約）。その後に二枚 buffer の同期 flip へ進む。
