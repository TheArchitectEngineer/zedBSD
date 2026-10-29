<!-- awesome-plan project=zedbsd record=ws313-p002 -->

# ws313-p002: HAL の BSP と build の配線

Status: in-progress（実装済み・実機未試験）
Queue: q1675-i01（P7）
WS: [WS313](../ws.md)、設計 [design.md](../design.md) §2〜§4

## 目的

`src/hal/arm64/bsp-s905x5m/` を足し、ODROID-C5 の kernel を build できるようにする（起動・UART・FDT・GIC・timer・PSCI の SMP と reset・電源断・bootloader の framebuffer）。

## 実装（2026-10-29）

- `bsp-s905x5m/`: board.h（uart_B 0xfe07a000）、locore-board.h（device の 1 GiB 0xc0000000、Aff1 の core 番号、image の移動）、uart.c（Amlogic の UART、U-Boot の速さを保つ）、
  fdt.c（Pi 5 の parser を元に、status が off の node を除く・linux,usable-memory を優先・simple-framebuffer を読む・GIC を arm,cortex-a15-gic でも見る）、board.c（PSCI の CPU_ON・SYSTEM_RESET・SYSTEM_OFF）、
  activity-led.c（無し）、framebuffer-boot.c（DTB の simple-framebuffer を console に）。
- 共通: `bsp-rpi-common/framebuffer.c` の mailbox の部分を `framebuffer-mailbox.c` に分け（Pi の build に足した）、`rpi_framebuffer_adopt()`（alpha の byte を立てる opaque_bits つき、Pi は 0）。`fdt.h` に bootloader の framebuffer の欄。
- build: Makefile（odroid-c5 の platform、BUILD=build/odroid-c5、serial の console の switch）、`platform/arm64/vmunix.mk`（BSP の directory の表、board の define、C5 の kernel の source、Mali の include の口、Pi の image の rule を C5 では外す）、`platform/arm64/s905x5m.mk`、config 2 本。
- kernel: `src/kern/platform/odroid-c5.c`、`rpi4-console.c` に C5 の UART の枝（RFIFO 0x4・STATUS 0xc・RX empty bit 20）。
- HAL の API（hal.h）の変更は無い。

## 確かめ

- `make ZEDBSD_CONFIG=config/ci/config-odroid-c5.mk build/odroid-c5/vmunix`: PASS、warning 0（周辺は stub の時点）。
- 回帰: `config-rpi5.mk` と `config-rpi4.mk` の vmunix が warning 0。Pi の実機・QEMU の起動は未実施。
- `make ZEDBSD_CONFIG=config/ci/config-odroid-c5.mk odroid-c5-fdt-host-test`: PASS（uart fe07a000 irq 201、4 CPU、予約 7、fb 7f800000）。
- 実機: 未実施。QEMU: arm64 の `virt` の machine の対応が tree に無く（QEMU は raspi4b だけ）、C5 の kernel は QEMU では起動を試せない（未実施）。
