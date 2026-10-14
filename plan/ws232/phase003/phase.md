<!-- awesome-plan project=zedbsd record=ws232-p003 -->
# ws232-p003: Retro を Base の下へ、PC-98 の driver を Drivers > Platform へ

Status: cleared（2026-10-14 P1、host 試験 PASS。Q1 の確認待ち）
Disposition: normal
Parent: [WS232](../ws.md)
依存: [p001](../phase001/phase.md)

## 原文（2026-10-14 ユーザー）

「make menuconfigで、PC-98の場合、Retroが一番上に来てます。Baseの下にしてください。あと、PC98のマウスとかSCSIとかすべてのドライバは、DriversのPlatformに入れてください」
続けてクリック「全部の CPU で Base の下」。

## 範囲

1. `tools/menuconfig.py` の主の menu で、全部の CPU で Retro を Base の直後に置く。
2. PC-9800 の時、Drivers > Platform に PC-98 の driver を全部入れる。PC-98 以外の CPU の Drivers の表示は変えない。
3. host 試験（`plan/tools/menuconfig-target-host-test.py`）を並びと Platform の内容に合わせる。

## 設計

- 主の menu: CPU / Board、Boot Option、Drivers、Development、Base、**Retro**、Desktop（i386 では出ない）、Packages、Firmware、Build boot image、Exit。
- `config/drivers/menu.list` を `category|platforms|keys` の 3 列にした。`*` の行は今までの CPU 共通の分類（内容は同じ）。
  platform を名指す行は、その platform でだけ、名指した driver を共通の分類から抜いてその分類の先頭に置く。`fixed` は
  「その platform を名指す（`*` でない）常に build する driver の行」を表す。PC-98 の行は `Platform|pc98|fixed CONFIG_DRIVER_GRAPHICS CONFIG_DRIVER_LGY98`。
  名指した key がその platform の行に無ければ menuconfig は止まる（書き間違いを黙って落とさない）。
- `config/drivers/architecture/pc98.drivers` に `platform/pc98/vmunix.mk` が常に組み込む物の固定の行を足した: bus mouse（`pc98-busmouse.o`）、
  PC-98 と MBR の disklabel（`disklabel/pc98.o`・`mbr.o`）、8259 の割り込み制御と 8253 の timer（HAL の `bsp-pc98/pic.c`・`pit.c`）。
  既存の IDE、keyboard と text console、Cirrus Core Graph の graphics（bool）、LGY-98（bool）と合わせて 7 行。固定の行は config.mk に書かない（今まで通り）。
- 固定の行の見せ方は Platform の他の行と同じ `[*] <label>`。選ぶと「always built into the kernel」の message を出し、値は変えない。
- PC-9800 の Drivers: Platform = PC-98 の 7 行 → 共通の ACPI・BCM2711 BT（他の分類と同じく、別の CPU の driver の選択も見せて残す今までの規則）。
  GPU から Cirrus（`CONFIG_DRIVER_GRAPHICS`）、Ethernet から LGY-98 が抜ける。
- SCSI: PC-98 の SCSI の driver は tree に無い（`src/drivers/platform/pc98/` は graphics・busmouse・ide・keyboard・lgy98 だけ。SCSI は
  x68k の MB89352 と USB の UAS だけ）。作らず、Q1 に報告した。

## 確かめ

- `timeout 300 make menuconfig-host-test` → `MAC-T001 menuconfig round-trip: PASS (6 targets, 3 amd64 Variants, obsolete capacity removed, package requirements resolved, the WS193 menu, i386 PC/AT and PC-9800 with Retro after Base and without the Desktop, the PC-98 drivers under Platform)`。
  - `check_menu_layout`: 主の menu の並び（Retro は Base の直後）。
  - `check_i386_and_retro`: amd64・rpi4・i386・pc98 の全部で Retro が Base の直後、pc98・i386 の config.mk の書き出しと `make -n` の解決。
  - `check_pc98_platform_drivers`（新規）: PC-9800 の Platform の行と順、LGY-98 の切り替え、固定の行を選んだ時の message と値の不変、GPU・Ethernet から抜けること。amd64・i386・rpi4・sun4u・x68k の Platform・GPU・Ethernet が変わらないこと。
- main（e9d93053b）の `tools/menuconfig.py`・`config/drivers` と比べて、amd64・i386・rpi4・sun4u・x68k の `driver_groups` が同一、
  pc98・amd64・i386 の既定の config.mk の書き出しが byte で同一（host の python で比較）。
- 未実施: QEMU・実機（menuconfig の host の変更だけで、image の中身は変わらない。T1 への依頼は無し）。curses の実画面での表示（試験は `choose` を差し替えた walk）。

## 残件

- `docs/release/zedbsd-1.0.0-beta2-guide.md` の 204 行「has a **Retro** menu at the top」が古くなった。担当の範囲外なので Q1 に直しを依頼した（例「has a **Retro** menu, right after Base, for …」）。
- PC-98 の SCSI の driver は無い（作るなら別の WS）。

## 後の変更（2026-10-14）

- Drivers の分類の規則（`menu.list` の platform の行、共通の ACPI・BCM2711 BT を PC-98 の Platform にも出す点）は [p004](../phase004/phase.md) で置き換えた:
  Platform は選んだ board の driver だけ、他の分類はどの CPU でも同じ。PC-98 の Platform の中身（7 行）と walk の試験は p004 の `check_platform_drivers` に残した。
