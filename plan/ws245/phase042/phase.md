<!-- awesome-plan project=zedbsd record=ws245-p042 -->

# ws245-p042: display.c の段の縮約・log の文言・xd-diag

Status: planned（2026-10-17 q1229-i14 で P1 が調べだけ。実装は次の世代: 下の「再開の手順」）
Parent: [WS245](../ws.md)
Queue: q1229-i14（P1、未着手）
設計: [design.md](../design.md) §6.2 の p042 の行、§3.6（段の表）、§4.2 の末尾（`struct i915_display` から消える field）、§4.4（残す・捨てる log）、§7 #12（ユーザー「この WS で縮める」）

## 範囲（design の行）

- `display.h` の段（`init_opregion`・`init_noirq`・`irq_prepare`・`init_nogem`・`register`）を own の段（start・serve・suspend・resume・stop）に縮め、`device.c` の呼び側を直す。
- log の文言の一覧（§4.4 の残す／捨てる、H4 の harness `plan/ws075/tests/hdmi/h4-ctl.py:62-64` が読む 3 行の書式）を確定し §6.4 に書く。
- `xd-diag`（register の survey、名前付き register の比較表、`i915.debug=display` の xd-io の記録の dump、capture の口）。
- 消す: `diagnostics.c`（今 1884 行）、`display.c` の Linux の段の名前と `drm_dev`・`drmm`・vblank の init、`struct i915_display` の旧 field（§4.2 の末尾）。
- 受け入れ: 段の順の試験（host の stand-in で start → serve → suspend → resume → stop）、log の一覧の grep、`h4-ctl.py:62-64` の正規表現が一致。実機: SV7（Gen9）の起動（device.c の呼び側が変わる）。

## 調べた事（2026-10-17 P1、base f5f5f7ab2）

- `device.c` の start の順（`drv_i915_device_start`）: prepare → registers → memory（`drv_i915_display_create` → Gen9 の時 `drv_i915_gen9_firmware_check` → `drv_i915_display_init_opregion`）→ `i915_start_display_noirq`（init_noirq）→ interrupts（`drv_i915_display_irq_prepare` と install の後の `drv_i915_display_irq_log`）→ `i915_start_display_nogem`（init_nogem）→ gt → pxp → `i915_start_display_register`（register）→ publish（rps・telemetry・worker の create の後に `drv_i915_display_resident_deps`）。stop: `i915_stop_gt` の中の `drv_i915_display_stop_early`、`drv_i915_display_stop_outputs`、irq の uninstall の後の `drv_i915_display_fini`、`drv_i915_display_destroy`。suspend/resume は `drv_i915_device_suspend_hw/_resume_hw` が irq の reset/postinstall を挟んで 4 つの口を呼ぶ。
- display の段は GT の start と irq の install の間に挟まっている。design の 5 段（start・serve・suspend・resume・stop）に純粋に縮めるには device.c で GT と display の順を並べ替える必要があり（nogem の sanitize と GT の start の前後、noirq と irq の install の前後）、5330 の起動を変える危険がある。
- `diagnostics.c` の本番の呼び手: resident.c（`drv_i915_lcd_trace_*`、`drv_i915_lcd_observer_*`、`drv_i915_lcd_kernel_step/_error/_debug`、`drv_i915_lcd_log_trace/_observer`、`drv_i915_lcd_debug_enabled`）、display.c（`drv_i915_lcd_last_resort_stop`、`drv_i915_display_survey`）、device.c（`drv_i915_lcd_debug_set`）、survey.c（`drv_i915_lcd_debug_enabled`）。file の上部の comment に Linux の名前と dump の値の比較の記述（出典の書き方が guardrail の形でない: p043 の監査でも当たる）。
- display.c の Linux の残り: `drm_dev_init()`・`drm_vblank_init()` の記述（21 行目、97 行目）、`drv_i915_drm_dev_init/_fini`・`drv_i915_drmm_add_action_or_reset`（1238〜1330 行）、`i915_drm_vblank_crtc_cleanup`、`display->drm_inited`。

## 既定の案（実装の前に Q1 に確かめたい点）

- (a) 既定: GT・irq との挟まりは変えず、隣り合う段だけ 1 つにし、名前を own にする: create・Gen9 の firmware check・OpRegion・core（noirq）を `drv_i915_display_start`（i915_start_memory の display の部分と i915_start_display_noirq は間に何も無い。Gen9 の check は start の中へ）、nogem を `drv_i915_display_outputs`、register を `drv_i915_display_serve`、stop_early を `drv_i915_display_stop_serving`、fini を `drv_i915_display_stop`。irq_prepare/irq_log（割り込みの半分の bind）、resident_deps、suspend/resume の 4 つ、stop_outputs、destroy は今のまま。段の順の host 試験は display の口を stand-in で順に呼ぶ形（device.c は host で build しない）。
- (b) design のとおり 5 段: device.c で display の start を GT の start の後にまとめる（nogem・register と同じ所）。noirq の N0（firmware の display の確かめ: GGTT の範囲が GOP の framebuffer と重ならないか）は GT の GGTT の書き込みより前に要るので、(b) でも N0 だけは前に残る。

## 再開の手順（次の世代へ）

1. 上の (a)/(b) を Q1 に確かめる（返事が無ければ (a)）。
2. display.c の drm_dev・drmm・vblank の init と `drm_inited` を消し、呼び手（`drv_i915_drmm_add_action_or_reset` の登録先）を own の stop の列に直す。
3. diagnostics.c を xd-diag.c（xd の型と xd-io だけ）と own の殻に分ける: observer（frame counter・underrun）、last-resort stop、名前付き register の survey と比較表、`i915.debug=display` の dump、capture の口。resident.c・display.c・device.c・survey.c の呼びを付け替え、diagnostics.c を空にして vmunix.mk から外す（削除は Q1）。
4. §4.2 の末尾の旧 field の残りを grep で数え、本番の読み手 0 の物を消す。
5. log の一覧を §6.4 に書き、`h4-ctl.py:62-64` の正規表現（`modeset.c:3802`・`:3904`、`present.c:1711` の書式）が今の文言に当たるか確かめる（当たらなければ R11 の基準で直すか消すかを Q1 に）。
6. build 3 組 warning 0、run-xd-host-tests と他の WS の試験、記録（ws.md の表・段の境の SHA）。SV7 の起動は T1 に頼む物として記録。
