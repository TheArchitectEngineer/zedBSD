<!-- awesome-plan project=zedbsd record=ws245-p021 -->

# ws245-p021: backlight の PWM（xd-panel）

Status: in-progress（2026-10-16 P1: 実装・host 試験・build は済み。ユーザーの決定（2026-10-16「i915は、実装だけ先に終わらせ、全部マージしたものをテストして、動かなければ二分探索で半分マージしてテスト、だめならさらに半分、で進めます。まずはぜんぶ実装してください。」、Q1 経由）で 5330 の回帰は全部の段の後に 1 回。それまで cleared にしない）
Parent: [WS245](../ws.md)
Queue: Q1 の指示（2026-10-16「p020 以降の残りの段を全部（design の順に p046 まで）実装して branch に積む」）
設計: [design.md](../design.md) §6.2 の p021 の行（改訂 6）、§2.11・§3.2 の xd-panel

## 変えた物

| file | 中身 |
| --- | --- |
| `display/xd-panel.c/.h`（新、Zlib） | backlight の PWM: `drv_i915_xd_backlight_controller_usable`（controller の数と、ICP〜ADP の 2 つ目の controller は SOUTH_CHICKEN1 の second PPS IO の bit）、`_read`（CTL の極性と enable、FREQ の周期）、`_period`（raw clock の kHz ÷ Hz、最も近い値へ丸め）、`_min_duty`（VBT の最小 /255、64 で切る）、`_scale`（範囲の写し、丸め、64 bit）、`_duty`・`_set_duty`、`_enable`（動いていれば先に止める → FREQ → DUTY → 極性 → posting read → enable。止めたかを返す）、`_disable`（DUTY → CTL の enable を落とす rmw）。register は xd-io だけで触る。手順は block ごとに `Procedure referenced:`（Linux v6.8.12 intel_backlight.c の cnp_backlight_controller_is_valid・cnp_hz_to_pwm・get_backlight_min_vbt・cnp_enable_backlight・cnp_disable_backlight） |
| `display/panel-backlight.c`（殻） | PWM の hook の 2 段（cnp_pwm_funcs・pwm_bl_funcs の表）、`i915_bxt_get/set_backlight`、`i915_cnp_enable/disable_backlight`、`i915_cnp_backlight_controller_is_valid`、`i915_cnp_hz_to_pwm`、`i915_get_backlight_max_vbt`・`_min_vbt`、`drv_i915_backlight_level_to_pwm/_from_pwm`（呼び手 0）を削除し、xd-panel を呼ぶ形に。残る物: 利用者の範囲 ↔ backlight の範囲の写し（`i915_scale` は範囲の WARN を出してから xd の scale）、反転（`invert_brightness` の param と quirk）、PCH の種類から controller の数、VBT の周波数の選び（log）、setup・enable・disable・set の段と lock、log の文。register の窓は file の static の xd-io（backlight の lock の下で使う）で、hook が呼び出しの device の backend（`emit->read32/write32/usleep`）を通る（device・model・記録器のどれでも旧と同じ書き込みの列になる）。R11: 1 → 0 |
| `display/panel-backlight.h` | `drv_i915_backlight_level_to_pwm/_from_pwm` の宣言を削除 |
| `display/modeset-internal.h` | `struct intel_panel_bl_funcs` と `intel_panel.backlight` の `funcs`・`pwm_funcs` を削除。R11: 8 → 0（ws118・ws051 の参照、`P1 exports` の段の符号） |
| `tests/display/host-xd-panel-test.c`（新） | 21 の check: 19.2 MHz/200 Hz = 0x17700、24 MHz = 120000、0 Hz、丸め、最小 6/255 = 2259、255 → 64 で 24094、scale の端・中・範囲外・空の範囲・逆の範囲、controller 0・2・muxed の 1（bit 無し／有り）・muxed でない 1、firmware の PWM の読み、動いている PWM の start（止める → 周期 → duty → 極性 → enable の 5 書き込み）、止まっている controller 1 の start（0xc835x、4 書き込み）、stop（duty → 極性を保って enable を落とす）、duty だけの書き込み |
| `plan/ws245/tests/run-xd-host-tests.sh` | `host-xd-panel-test` を足した |
| `platform/amd64/vmunix.mk` | i915 の行に `xd-panel.c` |

段の phase.md の必須の項目:
- 消す file: 無し（`panel-backlight.c` は殻として p050 まで: design §4.2）。
- 消す関数と残る呼び手: 上の panel-backlight.c の static 9 個（呼び手は panel-backlight.c の中の hook の表と互いだけ → 0）と公開の 2 個 `drv_i915_backlight_level_to_pwm/_from_pwm`（`grep` で呼び手 0）。`struct intel_panel_bl_funcs` は panel-backlight.c だけが使っていた（`grep` → 0）。
- 殻が更新する旧の写し: `connector->panel.backlight` の `controller`・`active_low_pwm`・`pwm_level_max`・`pwm_level_min`・`pwm_enabled`・`level`・`min`・`max`・`enabled`（setup で xd の読みから埋め、xd を呼ぶたびに `controller`・`pwm_level_max`・`active_low_pwm` から xd の `struct i915_xd_backlight` を組む）。旧の host 試験 `host-lcd-modeset-test` の A・F（PWM の max 0x17700、duty、enable、brightness の set と off/on、retained・灯っていない screen への set の error）が写しを見る。
- 表 R: SBLC_PWM_CTL/FREQ/DUTY の書き手は段の後も 1 つ（xd-panel。diagnostics.c は読むだけ）。表 R に行は無い。
- hook と lock: backlight の lock（`I915_LCD_LOCK_BACKLIGHT`、殻が取る）の下で xd を呼ぶ。xd-panel は lock を取らない。
- R11: panel-backlight.c 1 → 0、modeset-internal.h 8 → 0、panel-backlight.h 0。新しい file は 0。

## 旧と同じに保った物と違い

- 確かめ方: 旧の host 試験 `host-dp-test`（72）と `host-lcd-modeset-test`（123）を段の前（09b7da423 を `git archive` した tree）と後で `plan/ws031/display-ref -v` で流した。dp は一致。lcd は失敗 0 で、backend の記録の見え方が 2 箇所違う: (1) enable の posting read が backend の `posting_read`（model は記録しない）ではなく `read32` として 1 行記録される（A の enable: 180 → 181 entries）、(2) disable の CTL の enable を落とす操作が backend の `rmw32` ではなく `read32` と `write32` になる（26 writes・12 rmw → 27 writes・11 rmw）。書き込みの register・値・順は同じ。kernel の backend では `posting_read`・`rmw32` も read と write（`modeset.c` の `i915_kernel_posting_read`・`i915_kernel_rmw32`）なので、MMIO の書き込みの列（p047 の記録）は変わらない見込み。
- 違い: 無い controller の判定は旧と同じ数え方（MTL 以降 2、DG1 1、ICP 以降 2、他 1）を殻に残した。

## 確かめた事

- host（2026-10-16）: `sh plan/ws245/tests/run-xd-host-tests.sh build/p021/xd-host` → regs 498・io 50・platform 89・vbt 137・survey 57・aux 43・gmbus 24・panel 21、失敗 0。`I915_XD_STRICT` の検査（xd-panel.c を含む）は通る。
- build: 下。
- 5330: 未実施（全部の段の後に 1 回）。見る物: `backlight-probe` の get/set/range（96000）、起動で brightness=100、survey の SBLC_PWM_CTL/FREQ/DUTY が基準と同じ。

## build

- 2026-10-16: `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/p021/k-default build/p021/k-default/vmunix` warning 0（vmunix の sha256 42714c727e24549c…）。`I915_TESTS=y` の `I915_TEST_SET=vke2・display・display_ktest・display_ktest2・execution` の 5 組も warning 0。`git diff --check` は空。

## 判定の材料（2026-10-18 P1、q1241）

証拠: i20 = q1229-i20（T1、2026-10-17、5330 の実機、tip 92f134d99。survey の md は t1 の 396cbfce4 の `plan/ws245/tests/evidence/t1229i20-tip-20261017.md`・`t1229i20-tip-debug-20261017.md`、dmesg と `5330-survey-diff.py` の出力は t1 の worktree の `plan/ws245/temp/5330/t1229i20-*`（git の外））。i21 = q1229-i21（merge 後の main 61dde9d6b、24f2329ea の `t1229i21-main-merged-20261017.md`、dmesg は同じ場所）。基準 = q1229-i18 の main の dmesg（同じ場所の `t1229i18-main-*`）。debug の比べ（`t1229i20-diff-main-debug.txt`）の差は phase046 の q1229-i19 の表の I・H の行だけ（steady の差は PP_CONTROL の survey 2 の 0x6f/0x67 = H7 だけ）、素の起動の比べ（`t1229i20-diff-main.txt`）steady 差 0 PASS。i21 も steady 差 0。

満たした基準:

- 起動の基準（graphical、msgbuf_dropped 0、HANDOFF、lease 1 claimed、picture up、device lost 0、backlight-probe の get・set 100・get）は i20・i21 とも PASS（brightness=100）
- survey の SBLC_PWM_CTL 0x80000000・FREQ 0x17700（= 96000）・DUTY が基準と同じ（steady 差 0）

満たしていない・未確認の基準と残る物:

- `backlight-probe` の range の行は survey の md に無い（FREQ の値で代えた）

P1 の見立て（Status は変えない。判定は Q1）: cleared にしてよい。
