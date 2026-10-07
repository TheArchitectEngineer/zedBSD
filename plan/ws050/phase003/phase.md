<!-- awesome-plan project=zedbsd record=ws050-p003 -->

# ws050-p003: ACPI の transport と kernel への組み込み（正常系）

Phase ID: `ws050-p003`
Parent: [WS050](../ws.md)
Status: cleared（2026-10-07 Q1 の判定: host 57/57（5330 の ACPI の table）と T1-330 の boot-test PASS（QEMU に device は無く何もしない）、kernel の build warning 0。実機の確認は 5330 が戻ってから（p006））（旧: in-progress（2026-10-07 q834 P2: 実装、host 57/57（p002 の 31 と p003 の 26）、kernel の build warning 0。QEMU の起動の確認は T1 へ（Q1 に文面）、実機は対象外））
Phase disposition: normal
Queue: q834（P2）

## 範囲（Q1 の ACK 2026-10-07）

`src/drivers/typec/ucsi-acpi.c`（USBC000/PNP0CA0 の探索、`_STA`、VERSION で 1.x/2.x を選んで log、`_CRS` の Memory32Fixed の写像（RAM は拒む）、
`_DSM` 0 の bit 1・2 の必須の検査、write = mailbox + `_DSM` 1、refresh の read = `_DSM` 2 + mailbox、`\ECMU` があれば `drv_acpi_run_locked` の中、
CCI の二度読み、Notify 0x80 は signal だけ）、kthread `ucsi`、`typec-kern.c`（lock・log・写像・signal・thread・`/dev/typec`）、`pcat.c` の attach、
`CONFIG_DRIVER_TYPEC`（amd64 で既定 y、ACPI に従う）。WS049 に公開の口は足さない。

## 実装

- `ucsi-acpi.c`: `drv_ucsi_acpi_attach`（walk で `_HID`・`_CID`（package も）が USBC000/PNP0CA0 の device → probe → notify の install → `/dev/typec` → thread）、
  probe（`_STA` の present、`_CRS` の最初の Memory32Fixed、0x30 未満は拒む、`min(長さ, 0x210)` を `drv_typec_os_map`、VERSION の major 1 → 1.x、
  2 以上 → 2.x（写した範囲に収まらなければ拒む）、`_DSM` 0（Buffer の第 1 byte か integer）の bit 1・2、`\ECMU` の有無）、transport の write/read/wait、
  thread（`drv_ucsi_acpi_start` の後、`drv_ucsi_acpi_step(60 s)` の繰り返し: signal が来た時だけ `drv_ucsi_service`）。
  `_CRS` の visitor は負の値（-1）で止める（`drv_acpi_resources_walk` の約束: 正の値は walk の誤りと区別できない）。
- `typec-os.h` に写像・byte の読み書き・signal・wait・thread・device の口を足した。`typec-kern.c`（前の P2 の書きかけの lock・log を引き継ぎ）:
  `hal_space_map_device(READ|WRITE|NOCACHE)`（page に揃える）、`hal_mmio_read8/write8`、spinlock + waitq + flag の signal、`kthread_create`、
  `/dev/typec`（device 番号 0x00110000、open の時に text を写す、8 KiB）。
- `typec.c` に `drv_typec_text`（connector ごとに 1 行: `connector N: attached partner=ufp power=pd role=sink usb rdo=0x… modes=ff01/… current=ff01 pdos=… generation=G`、
  外れていれば `detached`）。
- `pcat.c`: `drv_acpi_attach()` と BAR の配置の後に `drv_ucsi_acpi_attach()`（ENODEV は黙る）。`Makefile`（`CONFIG_DRIVER_TYPEC ?= $(CONFIG_DRIVER_ACPI)` と -D）、
  `platform/amd64/vmunix.mk`（ACPI の中で `src/drivers/typec/*.c`）、`config/drivers/architecture/amd64.drivers` の menu、`config/ci/config-amd64.mk`。

## 確かめ（2026-10-07）

- host: `make -C plan/ws050/tests` → 57/57（ASan・UBSan）。新しい `acpi`（`ucsi-acpi-host.c`）は AML の interpreter・`ucsi-acpi.c`・核・層を 5330 の
  DSDT と SSDT1〜15（`plan/ws049/tests/latitude5330/`）で動かす。GNVS（OSYS 2015、USTC 1、UBCB）を load の前に置き、EC の RAM を byte の配列（`_REG` で ECRD=1）、
  その後ろに小さな 1.2 の PPM（0xB0=0xE0 で CONTROL を解いて CCI・MESSAGE IN を置き、EC query 0x79 を立てる。wait の shim が `_Q79` を評価）。
  確かめた事: `_STA` 0x0F、`_CRS` = Memory32Fixed（UBCB、0x1000）、`_DSM` 0 = Buffer {0x1F}、`_DSM` 2 が EC の CCI・MESSAGE IN を mailbox へ、`_DSM` 1 が
  CONTROL・MESSAGE OUT を EC へ写して 0xB0 を鳴らす、attach（version 0x0120 → 1.x、`\ECMU` の下）、core の start（2 connector、connector 1 = UFP の PD）、
  `_Q79` の Notify が handler に届く（handler は interpreter の中、Type-C の lock は interpreter の外でしか取らないことを shim が検査）、connector 2 の plug が
  1 回の step で読まれ ACK される、`/dev/typec` の text、RAM の mailbox は attach しない。
- kernel: `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/p2-ci vmunix` で warning 0（4 つの typec の file が入る）。`make -C plan/ws050/tests kernel-check`
  warning 0、stack の最大は `drv_typec_text` の 568 byte。
- style-check 0（新しい・変えた file と試験）、`git diff --check` 0。
- QEMU: 未実施（T1 に依頼する。QEMU に UCSI の device は無いので、起動して何も出ないこと）。実機: 対象外。

## 積み残し（backlog-p2.md）

driver の停止と notify の除去・写像の解除、`_Q79` と `_DSM` の交錯の実機の測定、Notify の来ない時の poll、suspend・resume、複数の UCSI の device、
attach より前の listener の登録。
