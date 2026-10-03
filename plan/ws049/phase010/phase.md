<!-- awesome-plan project=zedbsd record=ws049p010 -->

# ws049-p010: firmware の table の発見と kernel の glue（HAL の承認なしでできる部分）

Phase ID: `ws049-p010`
Parent: [WS049](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーのサブエージェント指示。branch の上で実行）

## 目的と範囲

p006（kernel への組み込み）のうち、HAL の差分（`acpi.rsdp`）の承認を待たずにできる部分を分けた Phase。
kernel の共有の file（`platform/amd64/vmunix.mk`、`Makefile`、`config/drivers/*.drivers`、`src/kern/platform/pcat.c`）は触らない（p006 で承認の後に）。

受け入れ:

- RSDP → XSDT/RSDT → FADT → DSDT・SSDT をたどる `acpi-tables.c` が、host の疑似の物理 memory で、table の file を直接読むのと同じ namespace を作る。
- kernel の glue（`acpi-kern.c`: aml-os.h の kernel 実装、SystemMemory・SystemIO・PCI_Config の handler、`drv_acpi_attach()`）が kernel と同じ
  target・flag で warning 0 で compile でき、外部の symbol が既存の kernel・HAL・PCI の API だけである。
- 規約の検査 0 件。

## 成果

- `src/drivers/acpi/acpi-tables.[ch]`: `drv_acpi_firmware_discover()`（RSDP の checksum、revision 2 以上は XSDT、ほかは RSDT。root table の全 entry の
  header、FADT の copy、FADT の `X_DSDT`/`DSDT` と `X_FIRMWARE_CTRL`/`FIRMWARE_CTRL`）、`drv_acpi_firmware_load()`（DSDT、続いて root table の順に
  SSDT・PSDT。SSDT の失敗は記録して続ける）、`drv_acpi_firmware_find()`（LoadTable の検索。identifier の空白・NUL の詰め物を同じとみなす）。
  物理 memory は platform の reader で読む。
- `src/drivers/acpi/acpi-kern.c`: `drv_acpi_attach()` は `kern_boot_handoff("acpi.rsdp")` で RSDP の物理番地を得る（今の HAL は NULL を返すので
  「ACPI is off」と記録して何もしない）。aml-os.h: `kern_malloc`/`kern_free`、`kern_logf`、stack の予算 8 KiB、`sched_sleep`、`kern_usleep_range`、
  `kern_rtc_read_counter` から 100 ns 単位の Timer、`struct mutex` の interpreter lock、LoadTable の table。region の handler:
  SystemMemory（`hal_space_map_device` の uncached の page を 16 枚 cache、page をまたぐ access は byte ごと、`hal_mmio_*`）、SystemIO（`hal_io_*`、
  64 bit は 2 回）、PCI_Config（`drv_pci_find_device` と `drv_pci_device_config_*`。列挙されていない function は ENODEV）。
- `include/drivers/acpi/acpi.h`: `drv_acpi_attach()`。
- 試験: `plan/ws049/tests/make-firmware.py`（table を疑似の物理 memory に並べ、RSDP（rev 2 と XSDT、または `--rsdt` で rev 0 と RSDT）を作り、
  FADT の DSDT・FACS の pointer を書き換える）、harness の `--firmware memory.txt`、`plan/ws049/tests/compare-firmware.sh`。

## 結果（2026-09-27）

| 確認 | 結果 |
| --- | --- |
| `compare-firmware.sh primergy dsdt ssdt1 ssdt2 ssdt3 facp facs apic mcfg hpet`（XSDT） | same（9768 node） |
| `compare-firmware.sh --rsdt q35 dsdt facp facs apic mcfg`（RSDT） | same（244 node） |
| `compare-firmware.sh pc-nofadt dsdt`（FADT を作る） | same（338 node） |
| `make -C plan/ws049/tests kernel-check`（`acpi-kern.c`・`acpi-tables.c` を含む全 file） | warning 0 |
| 外部の symbol（`llvm-nm -u` から driver の中で定義されるものを除く） | `drv_pci_*`（config の読み書き、`find_device`）、`hal_io_*`、`hal_mmio_*`、`hal_space_{map,unmap}_device`、`kern_*`（`boot_handoff`、heap、log、kcrt、`rtc_read_counter`、`usleep_range`）、`mutex_*`、`sched_sleep`・`sched_ticks` だけ |
| `style-check.py`（`src/drivers/acpi/*`、`include/drivers/acpi/acpi.h`、harness） | 0 件 |
| ASan/UBSan（harness の firmware 経路） | 誤り 0 |

## 未実施・制限

- kernel image への組み込み・起動・QEMU での確認は p006（HAL の承認の後）。この Phase の `acpi-kern.c` は compile しか確かめていない。
- PCI_Config の handler は列挙された function だけに届く（PCI の driver の API）。bridge の二次 bus は `_BBN` だけでは決まらない（p004 の制限）。
- 割り込みの lock の rank は `LOCK_RANK_DEVICE` を借りた（ACPI の rank を `include/kern/lock.h` に足すかは p006 で決める）。
