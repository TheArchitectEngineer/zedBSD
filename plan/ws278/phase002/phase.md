<!-- awesome-plan project=zedbsd record=ws278-p002 -->

# ws278-p002 config-rpi5.mk と Pi 5 の起動 image

- 親: [ws278](../ws.md)、Queue: q1542 / q1542-i01（P3、2026-10-21）
- Status: in-progress（image の生成まで済み。実機の確認（ユーザー）待ち。cleared は実機の結果を Q1 が判定してから）

## 目的と受け入れの条件

1. `make ZEDBSD_CONFIG=config/ci/config-rpi5.mk` で SD に書く image（`build/rpi5/hdd-image.img`）ができる。build は warning 0（zedBSD の code）。
2. Pi 5 の実機で、HDMI に kernel の log と `login:` の prompt が出る（1 CPU、console は firmware の framebuffer、root は SD の UFS）。
3. rpi4 の build が通り、振る舞いが同じ。

## 設計

[design.md](../design.md) §2。`ZEDBSD_PLATFORM/BOARD := rpi5`、platform の directory は `platform/arm64` を共用、BUILD は `build/rpi5`。rpi4 の HAL・driver に `HAL_BOARD_RPI5` の分岐を足す。hal.h は不変。

## 変えた所（commit c7909625c、dba0bf700）

- build: `Makefile`（platform の対応、BUILD、variant・target、`CONFIG_RPI5_SERIAL_CONSOLE`）、`userland/base/package.mk`、`userland/base/licenses/{zedbsd,llvm-runtime}/Makefile`（rpi5 に license を入れる）、`platform/arm64/{Makefile,vmunix.mk,bootloader.mk,disk-image.mk,run.mk}`（board ごとの define・source・firmware・config.txt）、新しい `platform/arm64/{rpi5-firmware.mk,rpi5-boot-firmware.notice,config-rpi5.txt}`、`platform/arm64/tools/{make-rpi4-hdd-image.py,make-rpi4-ufs-root-hdd-image.py,check-rpi4-hdd-image.py}`（`--board`）、`config/ci/config-rpi5.mk`。noct の platform の一覧は Q1 が足した（main 9d2225cda）。
- HAL: `locore.S`（Pi 5: 起動時の表に 0x10_4000_0000 の 1 GiB の device の block、読み込みの番地が 0x80000 でなければ自分を写す stub）、`bsp.h`（board の名前の macro）、`defs.h`（debug UART・aon GPIO の番地）、`cmain.c`（message の board 名、FDT が 1 GiB の中かの確かめ）、`space.c`（Pi 5 は HAL の device を `hal_space_map_device` で）、`asm.c`（reset・電源断を PSCI）、`watch.c`（HVS を使わない）、`bsp-rpi4/{uart.c,mailbox.c,led.c,fdt.c,boot.c}`（debug UART、identity の bus address、aon_gpio9 の LED、Pi 5 の compatible と /reserved-memory、handoff の名前 `rpi5.*`）、新しい `bsp-rpi5/board.c`（device の対応付け、PSCI）。
- kernel: 新しい `src/kern/platform/rpi5.c`（console と SD の boot device）、`rpi4-console.c`（Pi 5 の UART の番地）、`rpi4-sdhci.c`（Pi 5 の基準の clock 200 MHz）。
- 試験: 新しい `tests/rpi5-fdt-host-test.c`、`make … rpi5-fdt-host-test`。

## 確認（2026-10-21 P3、host）

- `make ZEDBSD_CONFIG=config/ci/config-rpi5.mk rpi5-fdt-host-test`: PASS（C1 の dtb: uart 0x10_7d00_1000 irq 153、sd 0x10_00ff_f000、4 CPU（Aff1、PSCI）、atf の予約。D0 の dtb: uart の irq 152、他は同じ）。
- `make ZEDBSD_CONFIG=config/ci/config-rpi5.mk -j16`: rc=0。warning は upstream の openssh の 204 件（OpenSSL の deprecated、既存）だけで、zedBSD の kernel・userland は 0。image `build/rpi5/hdd-image.img`（sha256 a46c21bf8c9c195ba4ce9c7e361a1e1a864208b4231919193a1731eda45d070b）、vmunix（sha256 73116b6cc35f5649e2ad1cb324f0c404da93daaf1769d6ffa264b3d5c9cc374b）。`check-disk-image` PASS。FAT は `ZEDRPI5`: 2 つの dtb・COPYING.linux・config.txt・cmdline.txt（`login=console`）・vmunix・data.img・swapfile。
- rpi4 の振る舞い: 変更前の tree（e0faaab4c を `git archive`）と変更後で `config-rpi4.mk` の kernel.elf を比べた。section の大きさは同じ、違いは HAL_FATAL の `__LINE__` の即値 9 か所 12 byte だけ（分岐はすべて `HAL_BOARD_RPI5` の前処理で消える）。warning 0。rpi4 の image・QEMU の起動は未実施（kernel が行番号の外で同じため）。
- 未実施: 実機（手順 [tests/rpi5-hw-test.md](../tests/rpi5-hw-test.md)、Q1 経由でユーザーに依頼）。QEMU に raspi5 は無い。

## 残り・次

- 実機で R1（読み込みの番地）・R2（dtb の位置）・R3（SD の 1.8 V）・R4（framebuffer の番地）を確かめる。
- p003（PSCI の SMP）以降は design.md §3。
