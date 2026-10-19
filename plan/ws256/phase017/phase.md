<!-- awesome-plan project=zedbsd record=ws256-p017 -->

# ws256-p017 NVMe の INTx の道と NVMe の root

Status: in-progress（virt の部分の実装・build は済み。QEMU は T1 の結果待ち。実機の部分（p020・p015 の後）は未着手）

- 親: [WS256](../ws.md)。Queue: q1271 / q1271-i01 の virt の部分（P1、2026-10-19）。設計: [design-peripherals.md](../design-peripherals.md) 改訂 2 §3.6（B2 の選択: INTx の道を足す）と再レビュー 2 の Minor 1（Q1 2026-10-19: 「INTx の許可だけを足し、ISR で INTMS → INTMC の往復はしない」）。
- 範囲（この試行）: pci-nvme に INTx の道。MSI-X・MSI の道は変えない。QEMU virt の `-device nvme` で読み書きと mount。
- HAL・pci.c: 変えていない。

## 変えた物（`src/drivers/pci/pci-nvme.c`）

- `drv_pci_device_allocate_irqs` の flags に `DRV_PCI_IRQ_ALLOW_INTX` を足した（core は MSI-X → MSI → INTx の順に試し、ENOTSUP/ENODEV の時だけ次へ進むので、MSI-X が取れる amd64 の道は変わらない）。
- INTMS/INTMC: 足していない。enable の後の `INTMC = 1`（`nvme_controller_enable` の末尾、MSI-X 以外）と disable の `INTMS = all`（MSI-X 以外）が既に INTx を覆う（Minor 1）。ISR の INTMS → INTMC の往復はしない。level の線は CQ を drain して doorbell を書けば下がる。共有の線は core の `pci_intx_dispatch` が全 handler を呼び、`nvme_irq` は自分の CQ が空なら 0 を返す。
- runtime の mask（shutdown・suspend・I/O の recovery が使う `nvme_runtime_irq_mask`/`_restore`）: INTx では message の capability が無く EOPNOTSUPP になって quarantine するので、INTx の枝を足した（`nvme_runtime_intx_mask`/`_restore`: command register の INTx Disable を立てて readback で確かめ、restore はその bit だけを元に戻す。capability の代わりに offset 0x04 を返し、呼び手の非 0 の検査が通る）。
- attach の log の名前に `INTx` を足した（`MSI-X`/`MSI`/`INTx`）。

## 確かめた事（host、2026-10-19）

- build: riscv64 virt（PCI 入り）・riscv64 virt（PCI 無し）・amd64（`config/ci/config-amd64.mk`）・arm64（`config/ci/config-rpi4.mk`）の vmunix が exit 0・warning 0（[p016](../phase016/phase.md) の記録と同じ走り）。
- host の単体試験: pci-nvme の INTx の枝には無し（短い host の試験で偽の PCI core を組むと大きいので、T1 の QEMU の回帰で確かめる）。
- 未実施: QEMU（T1、下）、実機（p020 の後）、NVMe の root（p015 の後）。

## T1 への依頼（Q1 経由、p016 の依頼と 1 つの QEMU の枠にまとめてよい）

1. riscv64 virt の NVMe（INTx）: image は p016 と同じ。disk は `truncate -s 64M build/tmp/p017-nvme.img`（host、T1 の dir）。
   `BOOT_MODE=riscv64-virt INITRD=build/riscv64-virt/root.img QEMU_EXTRA='-drive file=…/p017-nvme.img,if=none,id=nv,format=raw -device nvme,drive=nv,serial=zt17' BOOT_PATTERN=$'nvme: PCI controller\nlogin:' …`。合格: 画面に `nvme: PCI controller … INTx` と namespace の公開。続けて serial の `type:` で（login の後）: disk の名前を `ls /dev` で確かめ、`mkfs -t ufs /dev/<disk>`、`mkdir /mnt`、`mount -t ufs <disk> /mnt`、`echo p017 > /mnt/a`、`sync`、`cat /mnt/a` → 画面に `p017`。可能なら umount して mount し直して `cat` も。合格: mkfs・mount が成功し、書いた文字列が読める（I/O の完了は割り込みの道なので、INTx が動いた証拠）。
2. 回帰 amd64 の NVMe（MSI-X）: main の amd64 の image を `boot-test.sh` の NVMe の起動（既定の `-device nvme` の道）で起動し login prompt まで。合格: login prompt、可能なら画面の `nvme: PCI controller … MSI-X` の行。
3. 回帰 Pi 4 の build: `make ZEDBSD_CONFIG=config/ci/config-rpi4.mk -j16 vmunix` が warning 0（host で P1 が確かめ済み。実機・QEMU の起動は不要、pci.c と pci-brcmstb.c は無変更）。
- 判定は画面。serial・console の log は判定に使わない。
