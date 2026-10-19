<!-- awesome-plan project=zedbsd record=ws256-p017 -->

# ws256-p017 NVMe の INTx の道と NVMe の root

Status: cleared（2026-10-19 Q1 の判定、T1-724: riscv64 virt で `nvme: PCI controller … INTx` と namespace、dd の生の読み書きと od での読み戻し、native の mkfs・mount・書き込み・cat、起動し直して mount し直しで `p017` が残る事を画面で確認（build/review/t1-724/nvme-remount.png）。amd64 の NVMe の回帰は login prompt で PASS（MSI-X の行は画面に出ず未確認）。scan の「-1 entries」は partition の表の無い生の disk の答えで、I/O の失敗ではない（Q1 の読み、ram0 は MBR があり 4 entries）。実機の NVMe と root は p024）

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

## T1-723 の結果の解析（P1、2026-10-19、source を読んだだけ。build・QEMU は未実施）

- `vfs: scan nvme0n1 geometry error=21`: 21 は `EOPNOTSUPP`（`include/uapi/errno.h`）。vfs の scan は `DISK_IOCTL_GET_GEOMETRY`（CHS の H/S）を聞くが、pci-nvme は disk の ioctl を持たない（実装は ahci・ide・usb-storage・uas だけ）。amd64 の NVMe でも同じ行が出る。害は無く、続く `partition_scan` が LBA 0 を読んでいる（行末の `N entries` が 0 以上なら、その読みは完了している）。
- `mkfs -t ufs /dev/nvme0n1` の Invalid argument: tool の仕様。`--profile=native` の無い `-t ufs` は image の file を作る道（`userland/base/common/format-file.c` の `format_file_run`）で、`S_ISREG` でない物を `EINVAL` で拒む。device を format するのは `mkfs -t ufs --profile=native DEVICE`（`block-command.c`）で、`Type FORMAT nvme0n1:<registration> to continue:` に同じ文字列を打って答える。
- `mount -t ufs nvme0n1 /mnt` の Operation not supported: format されていない disk の superblock の magic が合わない時の `EOPNOTSUPP`（`src/drivers/fs/ufs.c` の superblock の解読）。superblock の読みが完了したので出る error で、I/O が返らなければ mount は返らないか EIO になる。よって INTx の I/O の完了は、読みについては来ている見込みが高い（未確認、下の試験で確かめる）。
- source の不具合: 見つからなかった。pci-nvme.c は変えていない。
- 早い起動の行: 試験の config（`plan/ws256/tests/config-riscv64-virt-pci.mk`）の programs に `dmesg`・`grep`・`cmp`・`od` を足した（`dmesg` は sysctl の `kern.msgbuf` を読む）。riscv64 の build は LLVM の作り直しの後に未確認。

## T1 への依頼（改訂、Q1 経由）

image は `ZEDBSD_CONFIG=plan/ws256/tests/config-riscv64-virt-pci.mk` で作り直す（programs が増えた）。disk は `truncate -s 64M`（host、T1 の dir）、起動は前回と同じ `-device nvme,drive=nv,serial=zt17`。

1. riscv64 virt の NVMe（INTx）、login の後に serial の `type:` で:
   1. `dmesg | grep -e nvme -e 'scan nvme'` → 画面に `nvme: PCI controller … INTx` と `vfs: scan nvme0n1 … N entries`（N ≥ 0）。
   2. 生の device の読み書き: `dd if=/dev/nvme0n1 of=/dev/null bs=512 count=64` が返る。`echo p017raw > /tmp/s`、`dd if=/tmp/s of=/dev/nvme0n1 bs=512 count=1 conv=sync`、`dd if=/dev/nvme0n1 bs=512 count=1 | od -c | grep p017raw` → 画面に `p 0 1 7 r a w`（od の表示）。`/tmp` が無ければ `/` 以外の書ける所に置き換える。
   3. format と mount: `mkfs -t ufs --profile=native /dev/nvme0n1`、表示された `Type FORMAT nvme0n1:<n> to continue:` の文字列をそのまま打つ → `initialized and verified`。`mkdir /mnt`、`mount -t ufs nvme0n1 /mnt`、`echo p017 > /mnt/a`、`sync`、`cat /mnt/a` → `p017`。可能なら umount → mount → `cat`。
   合格: 1〜3 が画面で全部見える（書き込みと読み戻しの完了は割り込みの道なので INTx が動いた証拠）。
2. 回帰 amd64 の NVMe（MSI-X）: 前回と同じ（main の amd64 の image を `boot-test.sh` の NVMe の起動で login prompt まで、可能なら `nvme: PCI controller … MSI-X`）。
3. 回帰 Pi 4 の build: 前回と同じ（warning 0）。
- 判定は画面。serial・console の log は判定に使わない。
- 失敗した時（I/O が返らない・hang）に要る gdbstub の調べ: `nvme_irq` と INTx の dispatch（`pci_intx_dispatch`）に breakpoint、PLIC の該当の source の pending・enable、NVMe の CSTS と CQ の head、command register の INTx Disable の bit。
