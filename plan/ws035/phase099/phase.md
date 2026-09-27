<!-- awesome-plan project=zedbsd record=ws035p099 -->

# ws035-p099: BIOS（pcat）の loader の logo

Phase ID: `ws035-p099`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-28 main の割り当て「the BIOS/pcat loader logo (so the legacy boot is graphical too) — small」。p096 の残り）

## 実装（2026-09-28）

- `bootloader/common/logo-path.c`・`.h`（新、p096 の `uefi/logo.c` から切り出し）: `zbl_logo_path`（`logo=PATH`）と
  `zbl_parameter_present`（token）。UEFI と BIOS の loader が共有。
- `bootloader/bios/logo.c`・`logo.h`（新）: PPM（P6、255、一辺 4096 以下）を 512 byte の sector ごとに受ける decoder。完成した
  画素を framebuffer への書き込み（byte offset と画素）として返し、最初の画素の色で画面を塗る合図（fill_now）を 1 度出す。
  logo が画面より大きければ中央を切り出す。asm が読む field の offset は header の定数と `_Static_assert`。
- `bootloader/pcat/bootzbsd.S`: `enter_long_mode`（amd64）で VBE の mode を決めた後に `bios_draw_logo`: 引数の record から
  logo の path、FAT で file を探し（`path_role` 2: 無い file・壊れた chain は `fatal_path`・`fatal_fat_chain` から
  `bios_logo_abort` に戻り、起動は続く）、sector を読んで C に渡し、unreal mode の GS で framebuffer に書く。i386（pcat）の
  loader は同じ source で link だけ（logo は描かない: VBE は amd64 の経路だけ）。loader の大きさ 41472 → 45568 byte
  （上限 0xd000）。
- 画像: `zedimage-host` の overlay layout にも `--logo`（payload の FAT の `/logo.ppm`）、`check-amd64-gpt-image.noct` の
  hybrid-payload の一覧は logo.ppm を任意に。`platform/amd64/vmunix.mk`: BIOS の image（`bios-hdd-image.img`）に logo を
  置き、その zedbsd.cfg は `ZEDBSD_GRAPHICAL_BOOT` が y なら graphical の 3 行（`zedbsd-bios-graphical-<y|n>.cfg`）。
- `plan/ws035/tests/boot-shots.py`: `--bios`（SeaBIOS・IDE、payload の FAT の zedbsd.cfg を書き換える）。

## 検証（amd64、QEMU、2026-09-28）

- BIOS の image（`logo=logo.ppm kmsg=quiet`）を kernel の入口で止めた画面: 1024x768 の VBE に logo だけ
  （`p099-20260928-bios-logo.png`）。
- 存在しない `logo=nothere.ppm`: logo 無しでそのまま login まで（`-bios-missing-logo.png`）。
- 構成 y の BIOS の image（cfg に 3 行）: GPU の無い QEMU で greeter から getty_console に代わり login prompt
  （`-bios-default-login.png`）。
- UEFI の loader（共有への切り出しの後）: kernel の入口の画面は logo だけ（`-uefi-logo-recheck.png`）。
- i386 pcat の `BOOTZBSD.EXE` は build できる（起動は未実施）。
- 規約: 新しい 4 file の style-check 0。build warning 0。
- 実機: 未実施。

## 残り

- HAL の早期 console の提案（p097）が当たるまで、BIOS でも HAL の早期の行が logo の上に出る。
