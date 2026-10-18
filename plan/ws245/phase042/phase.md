<!-- awesome-plan project=zedbsd record=ws245-p042 -->

# ws245-p042: display.c の段の縮約・log の文言・xd-diag

Status: in-progress（2026-10-17 q1229-i15 で実装・host 試験・build 済み（93ec4f94a）。5330・SV7 は全部の段の後に 1 回（T1））
Parent: [WS245](../ws.md)
Queue: q1229-i14（P1、調べだけ）、q1229-i15（P1、実装）
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

## 実装（2026-10-17 q1229-i15、P1、base 7ac68c76d、commit 93ec4f94a）

Q1 の判断: 段の縮約は (a)（GT と display の挟まりは変えず、隣り合う段だけまとめて own の名前にする。device.c の並べ替えはしない）。

- **段の縮約**（display.h・display.c・device.c）: `drv_i915_display_start`（旧 create + Gen9 の firmware の check + init_opregion + init_noirq。device.c の `i915_start_memory` の末尾から display の部分を外し、続く `i915_start_display` で呼ぶ。間に何も無いので順は同じ）、`drv_i915_display_irq_prepare`・`_irq_log`（同じ）、`drv_i915_display_outputs`（旧 init_nogem）、`drv_i915_display_serve`（旧 register）、`drv_i915_display_stop_serving`（旧 stop_early）、`drv_i915_display_stop`（旧 fini）、`drv_i915_display_destroy`。display.c の static の段も own の名前に（`prepare_state`・`check_firmware`・`bandwidth_start`・`memory_and_clocks`・`arm`・`enable_ipc`・`publish`・`unpublish`）。device->stage の名前も own に（`gen9_firmware_check`・`vga_register`・`power_wells_map`・`display_core`・`bandwidth`・`firmware_display_lit`）。
- **消した Linux の残り**: DRM の device・drmm の list・per-pipe の vblank の worker（4 つの workqueue を作るだけで使う物が無かった）、PM demand（power.c の関数も）、modeset/flip の workqueue（使う物が無かった）、`struct i915_opregion_data`（opd・p2_opd）、`opregion_present`、`chain_*`・`pinned_*`・`show_gpu_retained_gm/_why`・`drm_inited`・`modeset_wq_ok`・`flip_wq_ok`（本番の読み手 0）、`struct i915_driver_probe` の Linux だけの field（initial_commit・overlay・fbdev・na_registrations・opregion_registered・err）、display.c の未使用の define 13。`drv_i915_drm_vblank_get/_put` は `drv_i915_display_vblank_get/_put` に改名（interrupts.c・resident.c）。
- **diagnostics.c の分割**: 新しい `xd-diag.c/.h`（xd の型と xd-io だけ: `drv_i915_xd_diag_firmware_left`（platform の pipe・port・PLL・well の記述で、well が on の pipe だけ読む。旧 survey は旧の power domains で gate した）、`drv_i915_xd_diag_stop_scanout`（旧 last-resort stop。待ちは `drv_i915_xd_io_poll` の 200 ms、旧は 100 回の 1〜2 ms））と own の殻 `run-log.c/.h`（recorder・observer・step/error/debug の hook・i915.debug=display の switch・log。中身は旧の写しで、Linux の記述と `LCD-B` を除いた）。diagnostics.c・.h は空にし vmunix.mk から外した（削除は Q1）。`i915.debug=display` の xd-io の記録の dump は xd-io の `drv_i915_xd_io_log_records` が既に持つ。capture.c は diagnostics を使っていないので付け替えは要らなかった。
- **log の文言**: 一覧を design.md §6.4.1 に書いた。`i915: N0:` → `i915: firmware:`、`LCD-B` → `display run:`（run-log.c・power.c）、`P5-0 survey` → `display: firmware left …`、`LAST-RESORT` → `stop: pipe … is still on`、Linux の段の名前の log を `display: …` に。H4 の harness の 3 行（resident.c・present.c）は変えていない（run-xd-host-tests が grep で固定）。
- **R11**（触った file の開発の管理情報、`grep -cE 'ws[0-9]{3}|WS[0-9]{3}|BUG-[0-9]+|review [A-Z][0-9]|\bP[0-9]+[a-z]? |LCD-B'`）: 後で 0（display.c・display.h・internal.h・power.c/h・interrupts.c/h・resident.c・survey.c・run-log.c/h・xd-diag.c/h・device.c）。device.c は 5（ws052-p009 ×3、ws075-p020、ws134-p007）を消した。

## 確かめた事（host・build）

- build 3 組（`sh build/tmp/kbuild.sh p042b`）: default rc=0 warn=0（vmunix sha256 の先頭 9b8ad91b5f46d1bd）、vke2 rc=0 warn=0、execution rc=0 warn=0。
- `run-xd-host-tests.sh build/p042-host`（host-xd-diag-test を runner に足す前の run）: all passed（新しい check: `check-display-steps.py` が device.c の 10 関数の段の順、log の捨てた語の grep、H4 の 3 行）。新しい `host-xd-diag-test` は単独で（`build/tmp/one-host-test.sh`）17 checks 0 failures（点いた pipe A と DDI A の止め方、止まらない transcoder の待ちの上限、何も無い時に書かない、firmware left は書かない・well が off なら pipe を読まない・記述が無ければ読まない）。
- 他の WS: WS075 host-output-test 80/0、WS237 gen9 flip 48/0・mode 71/0、WS229 bootfb 20/0。

## 実機で確かめる事（T1、全部の段の後に 1 回）

- 5330: 起動の log に `i915: firmware: the firmware's output: …`・`display: firmware left …`・`display: core started`・`display: serving: …`・`hotplug path: started`、survey の判定（p047）。`dmesg | grep -c 'drm_vblank_init\|LCD-B\|N0:'` が 0。stop の道（reboot）で `stop: pipe … is still on` が出ない事（driver が自分で暗くしている）。
- SV7（Gen9）: 起動（Gen9 の firmware の check が start の中に移った。`N0-lite (gen9)` の行と GPU の node）。
- 既定の案で進めた点: last-resort の待ちを 200 ms の poll にした（旧 100〜200 ms）。firmware left の pipe の gate を xd の well の状態で（旧は power domains の hw_enabled）。

## 残り・Q1 に渡す物

- 削除（merge の時）: `src/drivers/gpu/i915/display/diagnostics.c`・`diagnostics.h`（空）。`plan/bugs/BUG-266/tests/host-trace-tc.c`・`run-host-trace-tc.sh`（resolved の bug の試験、diagnostics.h を include、master の一覧・未完の WS から参照なし: 2026-10-06 の基準で削除）。
- 他の WS の道具の直し（Q1）: `plan/ws075/tests/hdmi-h1-hw.sh:47`・`plan/ws075/tests/hdmi/usb-poll.sh:20` の進み具合の grep の `LCD-B` を `display run` に（master の Tools に載る h1-hw）。
- `host-capture-test.c` は走らせる runner が無い（README だけ）: 2026-10-06 の基準なら削除候補。design §6.4 は「残す」なので判断は Q1。
- p043 へ: display.c の PCH の判定（Linux の intel_pch.c の写しと `Found … PCH` の log）、power.c・clock.c・watermark.c・interrupts.c の Linux の文言（design §6.4.1 の末尾）。

## 判定の材料（2026-10-18 P1、q1241）

証拠: i20 = q1229-i20（T1、2026-10-17、5330 の実機、tip 92f134d99。survey の md は t1 の 396cbfce4 の `plan/ws245/tests/evidence/t1229i20-tip-20261017.md`・`t1229i20-tip-debug-20261017.md`、dmesg と `5330-survey-diff.py` の出力は t1 の worktree の `plan/ws245/temp/5330/t1229i20-*`（git の外））。i21 = q1229-i21（merge 後の main 61dde9d6b、24f2329ea の `t1229i21-main-merged-20261017.md`、dmesg は同じ場所）。基準 = q1229-i18 の main の dmesg（同じ場所の `t1229i18-main-*`）。debug の比べ（`t1229i20-diff-main-debug.txt`）の差は phase046 の q1229-i19 の表の I・H の行だけ（steady の差は PP_CONTROL の survey 2 の 0x6f/0x67 = H7 だけ）、素の起動の比べ（`t1229i20-diff-main.txt`）steady 差 0 PASS。i21 も steady 差 0。

満たした基準:

- 起動の基準（graphical、msgbuf_dropped 0、HANDOFF、lease 1 claimed、picture up、device lost 0、backlight-probe の get・set 100・get）は i20・i21 とも PASS
- `firmware: the firmware's output`・`core started`・`serving:`・`hotplug path: started`、survey の判定 lit、`drm_vblank_init`・`LCD-B`・`N0:` の数 0（i20・i21）

満たしていない・未確認の基準と残る物:

- reboot の止める道（`stop: pipe … is still on` が無い、`stop: display power released`）は i20 で log が届かず未確認
- SV7（Gen9 の `N0-lite (gen9)`）は未実施

P1 の見立て（Status は変えない。判定は Q1）: 保留（止める道と SV7 が未確認）。
