<!-- awesome-plan project=zedbsd record=ws298-p002 -->

# ws298-p002 Intel VMD の driver の実装と 5330 での確かめ

- 親: [WS298](../ws.md)。Queue: q1613 / q1613-i01（担当 P1、2026-10-27）。設計: [design.md](../design.md) 版 2。
- Status: in-progress
- Disposition: normal

## 受け入れ

- host 試験（`plan/ws298/tests/run-host-vmd.sh`）が PASS。
- `config-5330.mk` の image の build が warning 0。
- 素の 5330（USB から起動）で: `vmd:` の行（bus の範囲・root port・NVMe の BAR）、`nvme:` の attach が segment ffff、`vfs: ... physical disks=2`、NVMe の LBA 1 の `EFI PART` と連続の読み、nvme の timeout・recovery の行が 0、VMD の割り込みの回数の行、zedBSD から reboot して Debian が NVMe から上がる。

## 実施

### 1. 5330 の値（読むだけ、2026-10-27）

design.md §2.3 の表。前提は全部成り立つ。

### 2. 実装（commit 462833ee8）

- `src/drivers/pci/pci-vmd-plan.c`（hardware に触らない半分: bus の開始・CFGBAR の offset・bus 番号と窓と BAR の割り当て・子の message の表）、`include/drivers/pci/pci-vmd.h`。
- `src/drivers/pci/pci-vmd.c`（driver: 467f に付き、BAR を claim、CFGBAR を map、"SHDW" を拒み、MSI-X の table を全部 mask してから entry 1 を確保、bypass を切り、割り当て、segment ffff の root bus を作って scan。bus の ops は config・map_bar・allocate_irqs・msi_establish/disestablish。detach・suspend は EBUSY。`pci.vmd=off` で付かない）。
- 結線: `Makefile`（`CONFIG_DRIVER_PCI_VMD`、amd64 で y）、`platform/amd64/vmunix.mk`（ACPI 無しの時は `pci-window.c` も）、`src/kern/platform/pcat.c`（AHCI の後で登録）。`docs/reference/kernel-boot-parameters.md` §7f。

### 3. 確かめ

| 確かめ | command | 結果 |
| --- | --- | --- |
| host 試験 | `sh plan/ws298/tests/run-host-vmd.sh` | `host-vmd-plan: PASS` |
| build | `make ZEDBSD_CONFIG=plan/tools/hw5330/config-5330.mk BUILD=build/p1-5330 disk-image` | rc=0、kernel の warning 0（perl の locale と OpenSSL の deprecated は別物） |
