<!-- awesome-plan project=zedbsd record=ws232 -->

# WS232: menuconfig に i386（PC-9800・PC/AT）と Retro の menu を足す

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。p001〜p003 cleared、p004 は host 試験 PASS で Pi の実機の確かめだけ → 2026-10-27 ユーザー「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」）
Completed: 2026-10-27（q1608）
Primary Milestone: MG002
Related Milestones: なし
Objectives: O1
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

1. `tools/menuconfig.py` の CPU に i386、board に PC-9800（`pc98`）と PC/AT（`pcat`）。
2. 主の menu に「Retro」（Xzed・zwm・zshell・zterm）。全部の CPU で Base の直後。
3. menuconfig の host 試験（`make menuconfig-host-test`）。
4. `config/ci/config-pc98.mk` が build を通る。
5. pc98 の image が `~/qemu-pc98` の QEMU で login prompt まで起動する（T1）。
6. PC-9800 の時、Drivers > Platform に PC-98 の driver を全部。
7. Drivers の WiFi の次に Bluetooth、board の driver は Platform だけ、Pi の WiFi・BT の config・DTB の確かめ。

## ユーザーの決定

- 2026-10-14（原文）「make menuconfigで、CPU i386, Board PC-9800, Board PC/ATを追加してほしいです。さらに、アーキにかかわらずmenuconfigのトップにRetroというメニューを追加して、Xzed, zwm, zshell, ztermを追加してほしいです。」（2026-10-09 の決定「X11とTestsはメニューから削除し、直接記述のみにする」のうち、X11 の 4 つの program はこの指示で Retro に戻る。Tests は直接記述のまま）
- 2026-10-14（Q1 経由）「WS232はconfig/ci/config-pc98.mkがビルドを通るように修正をお願いします。」「WS232は ~/qemu-pc98にあるqemuを使って起動できるところまでお願いします。」
- 2026-10-14（クリック）「i386 では Desktop を自動で外す」: CPU を i386 にした時は Desktop の program を全部外し、Desktop の menu を出さない。
- 2026-10-14（p003）「make menuconfigで、PC-98の場合、Retroが一番上に来てます。Baseの下にしてください。あと、PC98のマウスとかSCSIとかすべてのドライバは、DriversのPlatformに入れてください」、クリック「全部の CPU で Base の下」。
- 2026-10-14（p004）Drivers の WiFi の次に Bluetooth（Intel の USB の BT）、Platform 以外の分類はどの CPU でも同じ一覧、board の driver は Platform にだけ。

## 結果

- p001: i386 の CPU と board、Retro の menu（`make menuconfig-host-test` PASS、commit 7401f3fb8 ほか）、`config/ci/config-pc98.mk` の build。目標 5（pc98 の QEMU で login prompt）の T1 の結果は Phase の記録に無い（未確認）。
- p002: 規約の全文の見直し（menuconfig・rtld の #if）。
- p003: Retro を全 CPU で Base の直後へ、PC-98 の driver を Drivers > Platform へ（host 試験 PASS）。PC-98 の SCSI の driver は tree に無く、作らない。
- p004: Drivers に Bluetooth、board の driver は Platform だけ（host 試験 PASS、rpi4 の image の build は warning 0）。Pi の実機の確かめは未実施。

## 制限・移管

- 実機の確認は未実施（p004 の Pi の WiFi・BT）。pc98 の QEMU の login の確かめは未確認。p004 の VCHIQ の既定は判断されないまま閉じた（WS236 で VCHIQ は既定で on）。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws232-p001 | i386 の CPU と board、Retro の menu、pc98 の build・起動 | cleared（pc98 の QEMU の起動の結果は記録に無い） |
| ws232-p002 | 規約の全文の見直し（menuconfig・rtld の #if） | cleared |
| ws232-p003 | Retro を全 CPU で Base の直後へ、PC-98 の driver を Platform へ | cleared |
| ws232-p004 | Drivers に Bluetooth、board の driver は Platform だけ、Pi の config・DTB の確かめ | in-progress のまま閉じた（host 試験 PASS、Pi は未実施） |

## 記録の所在

Phase の計画・結果は削除する。git の commit `fb1f6afee` の `plan/ws232/` にある。Queue の履歴は [plan/history](../history/index.md)。
