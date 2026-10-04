<!-- awesome-plan project=zedbsd record=ws132-p005 -->

# ws132-p005: libkeiland の devices（mount）と Files の Devices・Today・点滅・double click での mount・eject

Status: in-progress（2026-10-05、P2 / q723。実装・build・host の試験まで。QEMU は T1、判定は Q1）
Disposition: normal
Parent: [WS132](../ws.md)
Focused goal: fg019（ベータ1）
Queue: q723（P2）
依存: [p004](../phase004/phase.md)（volumed、compositor の `kl_system_devices_v1` version 5、bar の媒体の icon）

## 範囲（D3 のユーザーの決定と Q1 の判断 Q-3 案 A）

- libkeiland: `kl_system_devices_v1` version 5 の `mount` と `busy`（KL_VERSION 28）。
- Files: 左の pane の **Devices** の group（mount していない媒体も出す）、**Today** の Devices の card、新しい媒体の **3 回の点滅**（挿された時、bar の icon からの `files --devices` の時）、**double click で mount**（`/media/<label>`、mount の後に開く）、mount 中の媒体の行の **eject** の button、eject の結果の message（安全に取り外せる、使用中（program 名）、権限が無い）。

## 実装

| 部分 | file |
| --- | --- |
| libkeiland | `keiland/keiland.h`（KL_VERSION 28、`KL_DEVICE_STORAGE`・`KL_DEVICE_MOUNTED`・`KL_DEVICE_NEW`、`kl_system_devices_mount`、`kl_system_devices_busy_program`）、`libkeiland/system/system.c`（mount、busy の event、devices の listener に result と busy）、`system-protocol.c`（devices の interface の version 5、`mount`・`busy` は since 5）、`system-view.c`（device の無い done は一覧を空にする: compositor は毎回全部を送るので、最後の媒体が消えた時に空になる）、`system-private.h`、`exports.map`（`exports.py` で作り直し） |
| Files の core | `files/devices.c`（新規、host でも build: 一覧の取り込み・点滅の引き継ぎ・mount の後に開く・mount と eject の request）、`files/files.h`（`struct fm_device`、`FM_SECTION_DEVICES`、place の `device`、request 2 つ、`FM_BUTTON_EJECT_PLACE`）、`files/places.c`（`fm_places_init` は devices を保ち、Favorites と Locations の間に Devices、device の mount 先は Locations に重ねない）、`files/ui.c`（Devices の題、点滅の行、mount していない行は淡く、eject の button、点滅の間は frame を続ける）、`files/ui-input.c`（mount していない行は double click で mount、single click では何もしない、eject の button）、`files/ui-home.c`（Today の Devices の card、点滅、double click で mount、mount 済みは click で開く） |
| Files の main | `files/main.c`（`--devices`、window の display で `kl_system_open`、loop ごとに `kl_system_dispatch` と devices の取り込み、mount・eject の要求と結果の message。desktop の mode では開かない） |
| 試験 | `plan/ws132/tests/run-host-files-devices.sh`・`host-files-devices.c`（host）、`plan/ws131/tests/host-system.c`（mount の要求が compositor に届き答えが返る）、`plan/ws132/tests/p005-guest.sh`（T1） |

試験のための log: `ZFILES DEVICES count=`、`ZFILES DEVICE id= name= mounted= new= path= blink=`、`ZFILES DEVICE row|card id= x= y= width= height=`（一覧の変化の後に一度）、`ZFILES DEVICE ask|result|open ...`、zdesktop の `ZWL MEDIA icon x= ...`。

## 確認（2026-10-05）

- `sh plan/ws132/tests/run-host-files-devices.sh` PASS（新しい媒体の点滅の始まり・薄くなる・3 回で止む、同じ一覧で再び点滅しない、`--devices` で再び点滅、行の single click は mount しない・double click で mount の要求、mount の答えの一覧で開く、Locations に重ならない、eject の button で eject の要求、Today の card の single・double click、空の一覧で section と card が消える）。`plan/ws131/tests/host-system.sh` PASS（mount の要求と答え）、`plan/tools/files/host-default.sh`・`host-model.sh` PASS。
- build（warning 0）: zedBSD の files・wayland・settings・monitor（libkeiland）、Linux の Keiland 全体（-Werror）。`keiland-os-boundary/check.sh` PASS。style-check: 新しい file は違反 0、変えた既存の file は新しい違反 0。
- 未実施: QEMU（T1: `config-amd64-p004.mk` の image で `p005-guest.sh`）、実機。
- 既知の制限: mount したまま抜かれた媒体は kernel の BUG-192 のため一覧に残る（p004 の記録）。

## T1-144・145（2026-10-05）

- bar の媒体の icon（new・icon=1・click で `files --devices`）と抜去で消えるのは ok。FAIL ×2: Files の `ZFILES DEVICE …` の行が一つも出ない。
- 原因: `libkeiland/system/system-protocol.c` の `kl_system_manager_v1_interface` の version が 4 のままで、p005 で bind を version 5 にした。zedBSD の libwayland の `wl_registry_bind` は interface の表の version を超える bind を EINVAL で断るので manager が NULL になり、`system_bind` はそれを ENOMEM と報告していた（Files の log `ZFILES DEVICES none errno=4`）。host の試験は Linux の libwayland で走り、これを断らないため通っていた。**同じ表を使う kl_system の全 client（Settings・Monitor など）も p005 の後は guest で manager を取れなかった。**
- 直し: 表の version を `KL_SYSTEM_MANAGER_VERSION`（5）に。`system_bind` は bind の失敗の errno（無ければ ENOMEM）を返す。`plan/ws131/tests/host-system.c` に表の version が `KL_SYSTEM_MANAGER_VERSION` と等しいことの確認を足した。
- 確認: `plan/ws131/tests/host-system.sh` PASS、`run-host-files-devices.sh` PASS、build（warning 0）: zedBSD の files・settings・wayland・monitor・volumed、Linux の Keiland（-Werror）。style-check: 変えた file に新しい違反 0。
- 未実施: QEMU の再試験（T1 に依頼）。
- T1-147（WS089 の `settings-wifi-bugs.sh`、`ZSETTINGS NETWORK none`）も同じ原因: 88296ab（T1-109）では表と `KL_SYSTEM_MANAGER_VERSION` がどちらも 3、ws160 で両方 4、p005 で定数だけ 5 にした。表を定数に結んだので、この直しで Settings の kl_system も戻る（`settings-p021.sh` も同じ）。
