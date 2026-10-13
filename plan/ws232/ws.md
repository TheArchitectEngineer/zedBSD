<!-- awesome-plan project=zedbsd record=ws232 -->

# WS232: menuconfig に i386（PC-9800・PC/AT）と Retro の menu を足す

<!-- awesome-plan-current:start -->
Status: planned（2026-10-14 Q1 作成）
Primary Milestone: MG002
Related Milestones: なし
Objectives: O1
Parent: [Master](../master.md)
Queue: q982（p001、P1）
Target: ベータ2（18 時のコードフリーズ）
Resume point: p001。
<!-- awesome-plan-current:end -->

## 原文（2026-10-14 ユーザー）

「make menuconfigで、CPU i386, Board PC-9800, Board PC/ATを追加してほしいです。さらに、アーキにかかわらずmenuconfigのトップにRetroというメニューを追加して、Xzed, zwm, zshell, ztermを追加してほしいです。」
（2026-10-09 の決定「X11とTestsはメニューから削除し、直接記述のみにする」のうち、X11 の 4 つの program はこの指示で Retro の menu に戻る。Tests は直接記述のまま）

「WS232はconfig/ci/config-pc98.mkがビルドを通るように修正をお願いします。」
「WS232は ~/qemu-pc98にあるqemuを使って起動できるところまでお願いします。」

## 目標

1. `tools/menuconfig.py` の CPU の選択（`MENU_CPUS`）に i386 を足し、board に PC-9800（`pc98`）と PC/AT（`pcat`、i386）を選べるようにする。`PLATFORMS` の既存の i386・pc98 の定義と、選んだ時の variant・config.mk の書き出しを確かめる。
2. 主の menu の一番上（アーキによらず常に）に「Retro」を足し、Xzed・zwm・zshell・zterm を選べるようにする（今の menu の group は x11-servers・x11-applications）。
3. menuconfig の host 試験（`make menuconfig-host-test`）を足して PASS。i386 pcat・pc98 で config.mk を書き出し、`make -n` で解決すること（build はできれば）。
4. `config/ci/config-pc98.mk` で build が通るようにする（2026-10-14 ユーザーの追加の指示）。
5. pc98 の image が `~/qemu-pc98/build/qemu-system-i386`（PC-98 の machine）で login prompt まで起動する（2026-10-14 ユーザー）。起動の確かめは T1。
