<!-- awesome-plan project=zedbsd record=ws159-p004 -->

# ws159-p004: compositor の touchpad の層

Status: cleared（2026-10-05 Q1: T1-125 の ws159-p004 PASS（pad の resolution の直しの後）、T1-101 の zdesktop-p013-touch PASS。実機は p005）。以前: in-progress（2026-10-05 P1 generation17 / q713-i01。実装・host の試験・build まで。QEMU は T1、実機は UAT）
Disposition: normal
Parent: [WS159](../ws.md)
Queue: q713 / q713-i01（設計 [p001](../phase001/phase.md) の D8）

## 範囲と受け入れ

- touch pad（MT protocol B で `INPUT_PROP_POINTER`）を touch screen と分け、指を pointer の motion・button・scroll にする層。
- 規則は BUG-166 の仕様（2026-10-04 ユーザー）と D8: 1 本の指で移動（速いほど遠く）、tap（180 ms 以内・3 mm 未満）で click（1 本は左、2 本は右、3 本は中）、tap の後 300 ms 以内に触れて動かすと drag（離すまで左を押したまま）、すぐ離せば double click の 2 回目、押し込み（BTN_LEFT）は左（2 本なら右）で押したまま動かすと drag（押した瞬間の 1 mm は動かさない）、2 本指で scroll（自然な向き、2.5 mm で 1 notch）。
- backend の caps の properties は zedBSD だけ（Linux・FreeBSD は空の stub、2026-10-05 ユーザー）。
- 受け入れ: host の試験（台本）、compositor の build（zedBSD と Linux）warning 0、OS の境界の checker、style-check。QEMU（T1、inject の touchpad で compositor を動かす）、実機（UAT）。

## 実装（2026-10-05）

- `userland/desktop/wayland/touchpad.c`・`touchpad.h`（新）: seat を知らない純粋な状態機械。`zwl_touchpad_event`（slot・tracking・位置・BTN_LEFT）、`zwl_touchpad_frame`（report の終わりに action の列: MOTION（px）・BUTTON・SCROLL（notch））、`zwl_touchpad_tick`（tap の release の時間切れ）、`zwl_touchpad_release_all`。tap の press は指が離れた時、release は 300 ms の後（drag が来なければ）。加速は 5 px/mm（20 mm/s 以下）から 15 px/mm（150 mm/s 以上）。evdev の code は数値で持つ（OS で同じ）。
- `input.c`: `touchpad_node()`（MT かつ properties に `INPUT_PROP_POINTER`）、`attach_touchpad()`（`ABS_MT_POSITION_X/Y` の resolution で層を始め、seat の pointer として登録、log `ZWL INPUT device=… kind=touchpad abs=0 resolution=X,Y`）、report を `apply_touchpad()` へ、`apply_touchpad_actions()`（motion は新しい `pointer_move()`＝マウスの相対の motion と同じ速度の設定と clamp、button は `zwl_seat_button`、scroll は `zwl_seat_axis`、最後に `zwl_seat_frame`）、`zwl_input_tick()`（main の loop から毎回）、close・forget で押したままの button を離す。マウスの `apply_frame` は変えていない。
- `zwl.h`: device に `touchpad`・`pad`、`zwl_input_tick` の宣言。`main.c`: loop で `zwl_input_tick`。Makefile（zedBSD・Linux・FreeBSD）に touchpad.c。
- `libkeiland-backend/keiland-backend-evdev.h`: caps に `properties`。`libkeiland-backend-zedbsd/input-zedbsd.c` が `EVIOCGPROP` で読む（無ければ空）。Linux・FreeBSD の共有の evdev は空のまま（touch pad として扱わず、今までどおり）。
- root の `Makefile`・`config/drivers/pci.drivers`: `CONFIG_DRIVER_PCI_LPSS_I2C` の既定を y に戻した（Q1 が p004 まで n にしていた、f703cba）。

## 確認

- `plan/ws159/tests/run-host-touchpad.sh`: ok（25 checks、ASan・UBSan でも ok）。速い stroke は遅い stroke より遠く、tap の press は離した時で release は 250 ms では未だ・310 ms で出る、tap→100 ms 後に触れて動かすと drag（離すまで release 無し、離して 1 回）、2 回の速い tap は press・release・press・release、2 本の tap は右の click、押し込みの最初の 0.5 mm は動かず、その後は drag、2 本で 10 mm 下へは 4 notch 上（自然）、300 ms 置いた指は tap でない、2 本で押し込むと右。
- compositor: `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/q700/img build/q700/img/bin/wayland` warning 0。`make keiland-linux` warning 0。vmunix（LPSS の既定 y）warning 0。
- `plan/tools/keiland-os-boundary/check.sh` PASS、style-check は新しい file 0、変えた行 0、menuconfig の round-trip PASS。
- 未実施: QEMU（T1）、実機。

- 2026-10-05 T1-101（pen の束）FAIL 2: `resolution=0,0`（test の injector の pad が resolution を持たなかった）で「touch pad taken」が FAILED、tap-drag で窓が右の止まる所（x 1129）まで行き press-drag が動けなかった。直し: injector の pad の軸に 12 単位/mm（`INPUT_INJECT_PAD_RESOLUTION`、`src/drivers/generic/input-inject.c`）、press-drag は左へ 30 mm（x が減る）。pen の kernel の build は warning 0。再試験は T1。

## 残り

- touch pad の設定（速度・自然な向き・tap の有無）は Settings の ws089-p024 の口ができてから。今は既定値（自然な向き ON、tap ON、マウスの速度の設定 `pointer_speed` は効く）。
- 慣性の scroll、3・4 本指の gesture（WS142）、手のひらの除外（kernel が Confidence 0 の指を出さないので最低限は済む）は後。
- BUG-190・166・167・156 の再評価は実機の UAT（ユーザー「I2C-HIDの実装後に再度評価しましょう」）。

## Q1 の判定（2026-10-05）

T1-125 の ws159-p004 PASS（pad の resolution の直しの後）、T1-101 の zdesktop-p013-touch PASS。実機は p005。**cleared**。
