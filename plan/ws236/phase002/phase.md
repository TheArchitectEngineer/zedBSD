<!-- awesome-plan project=zedbsd record=ws236-p002 -->

# ws236-p002: VCHIQ の核（kernel、WS207 p004 と共用）

Parent: [WS236](../ws.md)
Status: in-progress（P3、2026-10-14。実装・host の試験・build まで済み。実機（VPU との接続）は p003 の後の R1 の試験と一緒に T1 へ）
Phase disposition: normal

## 範囲

[design.md](../design.md) §3。Pi 4 の VCHIQ の slave の側: 共有の memory（slot zero・data の slot 64・fragment・page list）、
mailbox の channel 3 への受け渡し、CONNECT、service の open・close、message の送受、bulk（long bulk の page list）、doorbell の割り込み、受信の thread。

## 変えた file

| file | 内容 |
| --- | --- |
| `src/drivers/platform/rpi4/rpi4-vchiq-wire.c`・`.h`（新） | slot zero の配置（`_Static_assert` で 1288 byte）、message id、stride、slot の予約・公開（PADDING、slot の跨ぎ、空きの slot が無ければ EAGAIN）、VPU の stream の読み・slot の返却、event、page list（物理 page 番号 << 8 \| 続く数、1 語 256 page まで、64 GiB まで） |
| `src/drivers/platform/rpi4/rpi4-vchiq.c`・`.h`（新） | driver: DT の `brcm,bcm2711-vchiq`（BELL0 +0x00 読み、BELL2 +0x08 書き、GIC SPI）、DMA の窓 0xc0000000 の uncached の 272 KiB、受信の thread（doorbell と 20 ms の poll）、service の表 8、bulk の queue（方向ごと 4）、page list 32、送信は send lock の下で slot が空くまで最大 1 秒（自分で recycle を見る、受信の thread に頼らない）。QEMU（emulated）では attach しない |
| `src/drivers/platform/rpi4/rpi4-firmware.c`・`.h` | `drv_rpi4_firmware_mailbox_post(channel, value)`（`#if CONFIG_DRIVER_BCM2711_VCHIQ` の中、property の request と同じ buffer の順番で直列） |
| `src/kern/platform/rpi4.c` | `#if CONFIG_DRIVER_BCM2711_VCHIQ` で GPU の attach の前に `drv_rpi4_vchiq_init` |
| `Makefile`・`platform/arm64/vmunix.mk`・`config/drivers/architecture/arm64.drivers`・`config/drivers/menu.list` | `CONFIG_DRIVER_BCM2711_VCHIQ`・`CONFIG_DRIVER_BCM2711_VIDEO`（既定 n）、menu の行（Platform・GPU） |
| `plan/ws236/tests/config-rpi4-vchiq.mk`・`vchiq-wire-test.c`・`run-vchiq-wire-test.sh`（新） | 試験の config と host の試験 |

## 検証（2026-10-14、host）

- `sh plan/ws236/tests/run-vchiq-wire-test.sh` → `vchiq-wire: all PASS`（通常と ASan/UBSan）。slot zero の値と offset、ARM → master の 4000 message（slot の不足で待ち、master の recycle で再開）、
  master → ARM の 3000 message と slot の返却、page list の符号化（整列・非整列・256 page の区切り・8 GiB の page・ENOSPC・ERANGE）、message id・stride・event。
- build: `make -j16 BUILD=build/ws236-rpi4 ZEDBSD_CONFIG=plan/ws236/tests/config-rpi4-vchiq.mk build/ws236-rpi4/vmunix` → PASS、warning 0（rpi4-vchiq.o・rpi4-vchiq-wire.o を含む）。
- 既定（off）: `ZEDBSD_CONFIG=config/ci/config-rpi4.mk` の vmunix → PASS、warning 0。変えた共有の file（`rpi4.c`・`rpi4-firmware.c`）は VCHIQ=0 の preprocess の結果が変更前（HEAD）と byte で同じ（`build/ws236-cmp/`）。
- amd64: 変えた file は arm64 だけ（Makefile の -D 2 行は全 platform の flag に入るが code は変わらない）。amd64 の build は p004 の終わりにまとめて確かめる。
- 実機: 未実施（VPU と繋がるかは p003 の後の §12 の試験で T1 に）。

## 残り

- 実機での CONNECT・service の open（p003 の R1 の試験で一緒に確かめる）。
- close の時に outstanding の bulk があれば page list を返さない（VPU がまだ書くかもしれない）。MMAL の client が close の前に bulk を終える。
