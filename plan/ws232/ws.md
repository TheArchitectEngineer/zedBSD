<!-- awesome-plan project=zedbsd record=ws232 -->

# WS232: menuconfig に i386（PC-9800・PC/AT）と Retro の menu を足す

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-14 P1: p004 を追加・実行中。以下は p001〜p003 の完了の時点の記録）（2026-10-14 Q1: menuconfig の i386（PC-9800・PC/AT、Desktop を自動で外す）と Retro の menu、config-pc98.mk の build（rtld の tlsdesc_slot）、T1-607 で pc98 の image が ~/qemu-pc98 の QEMU で login まで起動（PASS）。i386 PC/AT の image の build と実機は未実施）
Primary Milestone: MG002
Related Milestones: なし
Objectives: O1
Parent: [Master](../master.md)
Queue: q982（p001、P1）、p003（2026-10-14 P1）
Target: ベータ2（18 時のコードフリーズ）
Resume point: p004（Drivers の Bluetooth と Platform の規則、Pi の WiFi・BT の config・DTB の確かめ）は host 試験 PASS、T1 の Pi の実機待ち。p003（Retro を Base の下へ、PC-98 の driver を Drivers > Platform へ）は host 試験 PASS、Q1 の確認待ち。
<!-- awesome-plan-current:end -->

## 原文（2026-10-14 ユーザー）

「make menuconfigで、CPU i386, Board PC-9800, Board PC/ATを追加してほしいです。さらに、アーキにかかわらずmenuconfigのトップにRetroというメニューを追加して、Xzed, zwm, zshell, ztermを追加してほしいです。」
（2026-10-09 の決定「X11とTestsはメニューから削除し、直接記述のみにする」のうち、X11 の 4 つの program はこの指示で Retro の menu に戻る。Tests は直接記述のまま）

（p003、2026-10-14 ユーザー）「make menuconfigで、PC-98の場合、Retroが一番上に来てます。Baseの下にしてください。あと、PC98のマウスとかSCSIとかすべてのドライバは、DriversのPlatformに入れてください」、続けてクリック「全部の CPU で Base の下」。
（目標 2 の「一番上」はこの指示で「Base の直後」に変わる）

## 目標

1. `tools/menuconfig.py` の CPU の選択（`MENU_CPUS`）に i386 を足し、board に PC-9800（`pc98`）と PC/AT（`pcat`、i386）を選べるようにする。`PLATFORMS` の既存の i386・pc98 の定義と、選んだ時の variant・config.mk の書き出しを確かめる。
2. 主の menu の一番上（アーキによらず常に）に「Retro」を足し、Xzed・zwm・zshell・zterm を選べるようにする（今の menu の group は x11-servers・x11-applications）。
3. menuconfig の host 試験（`make menuconfig-host-test`）を足して PASS。i386 pcat・pc98 で config.mk を書き出し、`make -n` で解決すること（build はできれば）。
4. （2026-10-14 ユーザー、Q1 経由「WS232はconfig/ci/config-pc98.mkがビルドを通るように修正をお願いします。」）`config/ci/config-pc98.mk` で普通の target（world、できれば image）が build を通る。WS227 で 4 つの library を Desktop へ移した影響と Retro の program を含めて失敗を直す。toolchain は変えない。
5. （2026-10-14 ユーザー、Q1 経由「WS232は ~/qemu-pc98にあるqemuを使って起動できるところまでお願いします。」）pc98 の image が `~/qemu-pc98/build/qemu-system-i386`（PC-98 の machine）で login prompt まで起動する。QEMU は担当が自分で起動せず、T1 への依頼の文（command line・machine・disk の付け方・PNG の判定）を phase.md に書き、Q1 が T1 に流させる。
6. （p003）主の menu で、全部の CPU で Retro を Base の直後に置く。
7. （p003）PC-9800 の時、Drivers > Platform に PC-98 の driver を全部入れる（PC-98 以外の CPU の表示は変えない）。PC-98 の SCSI の driver は tree に無く、作らない。
8. （p004、2026-10-14 ユーザー）Drivers の WiFi の次に Bluetooth（Intel の USB の BT）。Platform 以外の分類はどの CPU でも同じ一覧、board の driver（Pi の LAN・WiFi・BT・audio・GPU・SD など）は Platform にだけ出す。Pi の Platform の driver は config で有効。Pi の image で WiFi・BT が使えない件の config・DTB の確かめ。

## 決定

- 2026-10-14 ユーザー（クリック「i386 では Desktop を自動で外す」）: menuconfig で CPU を i386（PC-9800・PC/AT）にした時は
  Desktop の program を全部外し、Desktop の menu を出さない（`config/ci/config-pc98.mk` と同じ形）。amd64・arm64 に戻した時は
  既存の CPU の切り替えの規則（build しない選択は menu の状態に残し、書き出さない）に従う。実装と試験は
  [p001](phase001/phase.md)（commit 7401f3fb8）。

## Phase

| Phase | 内容 | 目標 | Status | 依存 |
| --- | --- | --- | --- | --- |
| [p001](phase001/phase.md) | i386 の CPU と board、Retro の menu、pc98 の build・起動 | 1〜5 | cleared（2026-10-14） | 無し |
| [p002](phase002/phase.md) | 規約の全文の見直し（menuconfig・rtld の #if） | 規約 | cleared | p001 |
| [p003](phase003/phase.md) | Retro を全 CPU で Base の直後へ、PC-98 の driver を全部 Drivers > Platform へ | 6・7 | cleared（Q1 の確認待ち） | p001 |
| [p004](phase004/phase.md) | Drivers に Bluetooth、board の driver は Platform だけ、Pi の WiFi・BT の config・DTB の確かめ | 8 | in-progress（host 試験 PASS、T1 の Pi の実機待ち） | p003 |
