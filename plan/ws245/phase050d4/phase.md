<!-- awesome-plan project=zedbsd record=ws245-p050d4 -->

# ws245-p050d4: 口 xd.h と own の file の付け替え、旧 file と旧 host 試験の削除

Status: planned（2026-10-17 Q1 の判断で p050d を 4 段に分けた 4 段目。p050d3 の後）
Parent: [WS245](../ws.md)
Queue: q1229-i08（P1）
設計: [design.md](../design.md) §6.2 の p050 の行、§3.6、§4.2、§4.3。着手の手がかりは [phase050d](../phase050d/phase.md) の 6 と「own の file が呼ぶ旧の口」

## 範囲

- own の present.c・head.c・output.c・control.c・backlight.c・interrupts.c・display.c・device.c の呼び先を xd.h に（status・show run・resident run・kernel の binding は xd-display）。readout・setup_outputs・n1 の helper・VGA の client の残りを xd の型に。
- 旧 host 試験（host-dp-test・host-lcd-modeset-test）と ktest（lcd-modeset・edp・edp-sync・lcd-show）の oracle の check を xd の試験へ移し、旧の試験を消す（path は Q1 へ）。
- 旧 file（modeset.c・modeset-internal.h・state.c・ddi.c・dp-sink.c の eDP の部分・dp-internal.h・pipe.c・plane.c・aux.c・panel.c・panel-backlight.c・vblank.c・output-shell.c・takeover.c の残り・takeover-internal.h・internal.h の Linux の型）の中身を空にして vmunix.mk から外す（削除は Q1）。p039〜p052 まで残す物（hotplug.c・tc.c・tc-kern.c・dp-ext*.c・hdmi.c・clock.c の TC PLL の pool・diagnostics.c・vbt-shell.c）は殻に xd.h の型を渡す。

## 受け入れ

- 付け替えの後の host 試験の全部（run-xd-host-tests.sh）、`I915_XD_STRICT`、build 6 組 warning 0、試験の組の image が 16 MiB に入る（size）、R11 の数。5330 は全部の段の後に 1 回、5320・SV7・QEMU の boot-test は節目（T1・ユーザーの一覧）。

## 調べ（2026-10-17 q1229-i10、P1、p050d3・p045 の合わせの後。source は未変更）

数（aa09af96b）: 消す・付け替える旧 file は約 4.4 万行（modeset.c 5016、modeset-internal.h 3699、internal.h 3313、dp-sink.c 3379、hotplug.c 3823、power.c 2699、diagnostics.c 2681、clock.c 1687、takeover.c 1661、state.c 1648、panel.c 1522、dp-internal.h 1500 ほか）。own の file が呼ぶ旧の口は 45 個（head.c 17、present.c 7、display.c 17、backlight.c 3、output.c 2、device.c・survey.c 各 1。`drv_i915_lcd_resident_back/_buffer/_flip` は既に own）。build の列は `platform/amd64/vmunix.mk:149-160`（旧の .c は全部載る）、試験の組は `:286`（display: lcd-run・hdmi-output・lcd-flip・lcd-gpu・hdmi-hotplug・aux・hpd-model）・`:288`（display_ktest: edp-ktest・edp-sync-ktest・lcd-modeset-ktest・scanout-ktest・dp-fake-hw・lcd-fake-hw）・`:290`（display_ktest2: lcd-show-ktest・lcdg-ktest・hpd-ktest）。旧の口を呼ぶ ktest は 12 file、host は host-lcd-modeset-test・host-lcd-test・host-dp-test（runner は harness の oldtests.sh だけ）。`plan/ws084/tests/display-host-lib.sh` は display/*.c を全部 compile するので、file を空にすると xd の host 試験の link の入力も変わる。
旧 file の間の依存: hotplug.c・tc*.c・dp-ext*.c・hdmi.c・vbt-shell.c は modeset.c・output-shell.c・state.c を呼ばない。dp-ext-kern.c は dp-sink.c（`drv_i915_drm_dp_dpcd_*`・`dp_kernel_bind`・`edp_device_start`）・aux・edid-read・tc・vbt-shell を、dp-sink.c の fini は `drv_i915_dp_ext_stop` を呼ぶ。`drv_i915_lcd_xd_platform/_xd_io` は旧 file の 30 箇所から、`drv_i915_lcd_error` は clock.c の 16 箇所から呼ばれる。xd-display の口は `xd.h:63-80`（start・south・check_firmware・read_opregion・takeover）だけ。

## 4 段に分ける案（既定、Q1 の確認が要る: p050d を 4 段に分けた時と同じ理由。1 段では p050b・c の 5〜10 倍）

1. **p050d4a 画面の口**: 新しい xd の file（案 `xd-head.c`、口は `xd.h`）に画面の object（plan・`struct i915_xd_output` の記述・`struct i915_xd_screen`・`struct i915_xd_flip`・`struct i915_xd_plane`・backlight の user の値）と select・prepare（own の cfg → `drv_i915_xd_screen_plan` → 記述を plan から直に。output-shell.c の `drv_i915_lcd_ms_xo_describe` の役）・commit_enable/disable（`drv_i915_xd_screen_light/_dark` に xd-output の step と backend の hook を直に）・plane_disable/released・flip（nowait/poll/wait_screen/settle）・status・retained・backlight/brightness を own の型で。環境の hook は kernel の backend（`struct i915_lcd_emit` の read/write/power/vblank/TC/DKL）を直に。head.c・present.c・backlight.c をこの口に付け替え、modeset.c・panel-backlight.c・output-shell.c・plane.c・vblank.c の同じ関数を消す（killdead）。oracle: host-lcd-modeset-test の A〜J の check を新しい xd の host 試験（xd-fake-regs の上）へ。
2. **p050d4b 走らせ方の口**: resident run・show run（ktest の道）・kernel の binding（bind_ops・fill_cfg・preflight・locks_init・output params/cfg/link fallback）・diagnostics の retained の 3 つを xd-display（同じ file）へ。output.c の `drv_i915_lcd_compute_hdmi/_dp_ext` を xd-screen の plan_link に（state.c の compute と `i915_state_from_plan` を消す）。lcd-run・hdmi-output・lcd-flip・lcd-gpu・lcdg・lcd-show の ktest は新しい口に付け替えるか、R11 の基準で消す（path を Q1 へ）。
3. **p050d4c 始まりと終わり**: eDP の device（dp-sink.c の `edp_device_prepare/_init_connector/_fini`、`i915_edp_device_lcd_a` の `drv_i915_lcd_compute`）を xd-edp に、`drv_i915_display_nogem_front/_fini`・`drv_i915_setup_outputs`・VGA の client の登録・takeover world を xd-display/xd-takeover に、display.c の `drv_i915_lcd_world_create/_destroy`・`_set_display_ver/_set_xd_platform` を消す。dp-ext-kern.c が呼ぶ dp-sink.c の DPCD・binding は殻 file（p052 で消える）に移す。edp-ktest・edp-sync-ktest・aux（ktest）・host-dp-test の oracle を xd-edp・xd-aux の試験へ。
4. **p050d4d 旧 file の削除**: 呼び手 0 になった旧 file（modeset.c・modeset-internal.h・state.c・ddi.c・dp-sink.c の eDP の部分・dp-internal.h・pipe.c・plane.c・aux.c・panel.c・panel-backlight.c・vblank.c・output-shell.c・takeover.c の残り・takeover-internal.h・internal.h の Linux の型・watermark.c の modeset の半分・color.c）の中身を空にして vmunix.mk から外す。残す物（hotplug.c・tc*.c・dp-ext*.c・hdmi.c・clock.c の TC PLL の pool・diagnostics.c・vbt-shell.c・power.c の殻）に xd.h の型を渡す。host-lcd-modeset-test・host-lcd-test・host-dp-test・lcd-modeset-ktest・lcd-fake-hw・dp-fake-hw と harness の oldtests.sh・stagecmp.sh の比べを終える（path を Q1 へ）。受け入れ（size、`I915_XD_STRICT`、R11、QEMU の boot-test と 5320・SV7 の節目）はこの段。

各段の比べ: d4a〜d4c は旧 host 試験（stagecmp.sh、旧の口が残る間）と新しい xd の試験、d4d からは xd の試験だけ。

## 再開の手順

1. この案を Q1 が受けたら p050d4a から。最初に `head.c` の 17 の呼び（`drv_i915_lcd_modeset_*`・`_output_*`・`_kernel_*`）の引数と、`struct i915_lcd_modeset_cfg`（internal.h:2320 付近、neutral）・`struct i915_lcd_modeset_status`（internal.h:2360 付近）の field を読む。own の口の型はこの 2 つを流用してよい（Linux の型を含まない）。
2. 環境の hook の作り方は `output-shell.c` の `drv_i915_lcd_ms_xo_env` と modeset.c の `i915_modeset_light_hooks/_dark_hooks`・`i915_modeset_step_*`（modeset.c:4440〜5030）が今の正解。そこから `ms->i915`・`ms->crtc_state`・`dig_port` を通らない形に書き直す。
3. 比べの道具（P1 の build/tmp）: `stagecmp.sh <前の段> <名前>`、`kbuild.sh <名前>`、`killdead.sh`、`staledecl.py`/`rmdecl.py`、`onehost.sh <試験> <out>`。
