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

## 1 回目の実機の結果と対処（2026-10-22 P3）

- 実機（ユーザー、Pi 5 2 GB D0、main の CI config の build）: kernel は起動し、`init: started getty_console` まで。直後に `kern: pid 1 killed by signal 10 (vector 47) at 0x1000775b0`（写真 `build/review/rpi5/uat-20261022-1.webp`）。
- 解析: pc 0x1000775b0 は libc.so（ld.so の後、0x100020000 に置かれる）の +0x575b0 = syscall の `svc #0` の直後の `ret`。戻り番地 0x1000427ac・0x10004278c は `nanosleep`、0x100043d18 は `sleep`（libc.so は 2026-10-21 の build と 2026-10-22 09:03 の main の build で同一を `cmp` で確かめた）。vector 47 = ESR の EC 0x2f = SError。init は sleep から戻る所で、kernel が先に起こした非同期の外部 abort（device の access）を EL0 への戻りで受けた被害者で、init の誤りではない。SError の vector（3・7・11・15）は同期の例外の handler に流れ、user の fault として SIGBUS になっていた。
- 対処（HAL の実装、hal.h は変えない）: `trap.S` に SError の入口を分け、`int.c` の `arm64_serror_handler` は止めも signal もせずに報告する（user への戻りで受けた物は即座に画面へ、kernel で受けた物は記録して次の割り込みで `ARM64 SError N esr=… cpu C in the kernel at pc … lr …`）。kernel の worker（`arm64_kernel_task_entry`）と起動の CPU は SError を受けるようにし（`daifclr #4`）、ELR・SPSR を書いてから eret までは SError を止める（`RESTORE_FRAME` と user への入口に `daifset #4`）。元の device の access はまだ分からない。次の試験の pc で特定する。
- 記録の file: bootlog の service（BUG-288）は Pi 4 だけ boot の FAT を出していた（`vfs.c` の `HAL_BOARD_RPI4`）。Pi 5 も出し、kernel の log の ring を Pi 4 と同じ 256 KiB にした（`klog.c`）。次の試験から SD の FAT の `log/boot-NNNNNN.txt` に全部の log が残る。

## 2 回目の実機の結果と対処（2026-10-22 P3）

- 実機: `ARM64 SError 1 esr=be000011 cpu 0 on return to user pc 1000775b0; a kernel device access raised it earlier`。画面では Wi-Fi の `chip powered down` の直後。init は止まらず login まで（1 回目の対処は効いた）。
- 読み: user への戻りで受けた = SError を止めた状態の kernel の code が起こした。system call・割り込みの中か、SError を止めた状態から切り替わった worker。RP1 の側は `the root port has no bus behind it` で root complex の register 以外に触れずに戻っていたので、RP1 の BAR・設定空間ではない。時刻（20 s、Wi-Fi の thread の直後）から Wi-Fi の段が最も疑わしい。
- 対処: `src/drivers/platform/rpi5/rpi5-serror.c`（Pi 5 だけ）。SError を止め、`dsb sy; isb` の後に ISR_EL1.A を読み、立っていれば `rpi5: SError pending after <段>` を log に出してから SError を一瞬許して HAL に受けさせる（次の段は clean で始まる。HAL は `ARM64 SError N … in the kernel at pc …` を出す）。段: SD の起動の後、RP1 を探した後、USB・Ethernet の起動と refresh の後、Wi-Fi の file の読み・WL_ON の読み・pin・cfg・host の起動・電源・列挙・返却・電源断の後。
- 未実施: 実機（3 回目の試験）。
