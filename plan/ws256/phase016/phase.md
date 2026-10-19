<!-- awesome-plan project=zedbsd record=ws256-p016 -->

# ws256-p016 QEMU virt の ECAM の FDT host と virt の繋ぎ

Status: in-progress（実装・host の確かめは済み。QEMU は T1 の結果待ち。cleared は Q1 の判定の後）

- 親: [WS256](../ws.md)。Queue: q1271 / q1271-i01（P1、2026-10-19）。設計: [design-peripherals.md](../design-peripherals.md) 改訂 2 §3.5（再レビュー 2 で「今の設計のままでよい」、Q1 2026-10-19）。
- 範囲: `src/drivers/pci/pci-ecam-fdt.c`（`pci-host-ecam-generic`）、virt の繋ぎ（ECAM の host・xHCI・usb-hid・keyboard の input）。
- 受け入れ: build warning 0、host の試験、T1: QEMU virt（`-device qemu-xhci -device usb-kbd`）で USB keyboard から login。
- HAL: 変えていない（`include/hal/`・`src/hal/` 無変更）。PCI core（pci.c）も無変更。

## 作った物・変えた物

- `include/drivers/pci/pci-ecam-fdt.h`・`src/drivers/pci/pci-ecam-fdt.c`（新）: describe（`reg` の ECAM、`bus-range`、`dma-coherent`、`ranges` の最初の 32-bit memory の窓、`interrupt-map-mask`・`interrupt-map`（controller の `#interrupt-cells` = 1 だけ、PLIC）の表 最大 64 行）→ start（ECAM の窓を bus-range の分だけ map、root bus の endpoint の memory BAR を窓の頭から自然な整列で置く。測る間は command を 0 にして後で戻す。I/O BAR・bridge は置かない）→ publish（DMA は 32 bit・coherent は DT から、`drv_pci_bus_create_root`・`drv_pci_bus_scan_tree`）。bus_ops は brcmstb と同じ形（config は ECAM の直の load/store で lock 無し、窓の外の bus は all-ones、map_bar は窓の CPU 側、allocate は INTx だけ: pin を bridge ごとに swizzle → root bus の devfn と pin を mask して表を引く）。MSI は無し（virt に AIA 無し）。出典の comment は PCIe Base Spec（ECAM の配置）、IEEE 1275 PCI binding・Devicetree Spec（ranges・interrupt-map）、PCI-to-PCI Bridge Spec（swizzle）。GPL の code は見ていない。
- `src/drivers/platform/riscv-virt/virt-pci.h`・`virt-pci.c`（新）: `drv_pci_init`、driver の登録（USB core、usb-storage・usb-hid・usb-hub は config の y の物、xHCI、NVMe）、`pci-host-ecam-generic` の node を探して describe・start・publish。refresh で `drv_pci_xhci_probe_roots`・`drv_pci_nvme_probe_namespaces`、input で `drv_usb_hid_input_ready`。host が無い・失敗は boot を止めない。
- `src/kern/platform/riscv-virt.c`: 呼び出し 3 箇所（init の UART の後に `probe_pci()`、refresh、input_init の頭）と末尾の `probe_pci()`（FDT を開いて `drv_riscv_virt_pci_init`）だけ。
- `platform/riscv64/vmunix.mk`: `RISCV64_KERNEL_SOURCES += $(KERN_QUOTA_SOURCES)` の後に `RISCV64_PCI_SOURCES_virt`（virt-pci.c・pci.c・pci-ecam-fdt.c は常に、usb・xhci・hid・hub・nvme は config の y で）の block を足しただけ。P2 の bootloader.mk・disk-image.mk には触っていない。
- `plan/ws256/tests/config-riscv64-virt-pci.mk`（新）: config-riscv64-virt.mk に XHCI・USB_HID・USB_HUB・NVME を y、programs に dd・mkdir・mkfs・mount・sync を足した物。
- `plan/ws256/tests/ecam/`（新）: host の試験（`run.sh OUTDIR`、virt 風の dts と偽の ECAM）。

## 確かめた事（host、2026-10-19、worktree p1）

- `make ZEDBSD_CONFIG=plan/ws256/tests/config-riscv64-virt-pci.mk -j16 build/riscv64-virt/vmunix`: exit 0、`warning:` 0（log `build/tmp-p016-rv2.log`）。pci-ecam-fdt.c・virt-pci.c・pci-nvme.c が compile され、vmunix の強い undefined 0（Makefile の検査）。
- `make ZEDBSD_CONFIG=plan/ws256/tests/config-riscv64-virt.mk -j16 vmunix`（PCI の driver が全部 n）: exit 0、warning 0。
- `make ZEDBSD_CONFIG=config/ci/config-amd64.mk -j16 vmunix`: exit 0、warning 0（pci-nvme.c を含む）。`config/ci/config-rpi4.mk`: exit 0、warning 0。
- `plan/ws256/tests/ecam/run.sh build/tmp/p016-ecam`: 34 件 PASS（窓・bus-range・32-bit の窓を I/O・64-bit の窓より選ぶ・dma-coherent・mask・16 行、BAR の配置（32-bit・64-bit の上半分 0・I/O は置かない・command の復元・bridge は触らない）、config の read・窓の外は all-ones・非整列の拒否、BAR の map と窓の外の拒否、slot 0〜3 と mask で 6→2 の INTA〜INTD の PLIC 番号、pin 0 は ENODEV、MSI-X は ENOTSUP）。
- `git diff --check` 0。
- 未実施: QEMU（T1、下）、実機（p020）。

## tools の変更の依頼（Q1、決定 #28 (b)）

`plan/tools/boot-test.sh` の BOOT_THEN の `type:` は serial の socket に打つので、USB keyboard の道を通らない。QMP の `send-key` で打つ action（例 `keys:TEXT` = TEXT の各文字と Return を QMP の `send-key` で送る、qcode は英小文字・数字・space・`-`・`/`・`.` 程度で足りる）が要る。QMP の socket は riscv64-virt でも既に開いている。これが入るまで、下の走りは画面の判定だけで、打鍵の判定は T1 が QMP に手で `send-key` を送ってよいかを Q1 が決める。

## T1 への依頼（Q1 経由）

image（worktree p1 の commit、または main への merge の後）:

```
make ZEDBSD_CONFIG=plan/ws256/tests/config-riscv64-virt-pci.mk -j16 build/riscv64-virt/vmunix build/riscv64-virt/root.img
```

（root.img は p007 の userland 入りの UFS の root、root の password は root。）

- 走り A（合否）: `BOOT_MODE=riscv64-virt INITRD=build/riscv64-virt/root.img QEMU_EXTRA='-device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0' BOOT_PATTERN=$'pci: ECAM host at 30000000\nlogin:' OUTPUT=… plan/tools/boot-test.sh build/riscv64-virt/vmunix`。合格: ECAM の host の行・xHCI の attach（`xhci`/`usb` の行）・login prompt が画面に出る。
- 走り B（合否、keys の action が入ってから）: A に `BOOT_THEN=$'keys:root\nexpect:[Pp]assword\nkeys:root\nexpect:^root@\\S+:~\\$\nkeys:uname -m\nexpect:^riscv64\\s*$'`（p007 の走り A の `type:` を `keys:` にした物）。合格: USB keyboard の打鍵で login し、`uname -m` の結果が画面に出る。
- 判定は画面（PNG・`.txt`）。serial・console の log は判定に使わない。FAIL なら P1 が gdbstub で止めて解析する（boot log を取り直さない）。
