<!-- awesome-plan project=zedbsd record=ws298-p002 -->

# ws298-p002 Intel VMD の driver の実装と 5330 での確かめ

- 親: [WS298](../ws.md)。Queue: q1613 / q1613-i01（担当 P1、2026-10-27）。設計: [design.md](../design.md) 版 2。
- Status: cleared（2026-10-27 08:23、Q1 の判定。実装と素の 5330 の確かめは P1）
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
| host 試験（42 bit の BAR を足した後） | 同上 | `host-vmd-plan: PASS`（f0effe7ad） |

### 4. 素の 5330（USB から起動、P1 が `plan/tools/hw5330/5330-run.sh` で直に、2026-10-27）

QEMU の確認は無い（QEMU に VMD の model が無い）。以下は全部、素の 5330 の証拠。log の全文は P1 の worktree の `build/hw5330/logs/p1-ws298-*`（git の外）、要約は `plan/tools/hw5330/evidence/p1-ws298-{d-full,g-nvme-1g}-f0effe7ad.md`。

| 回 | kernel・cfg | 結果 |
| --- | --- | --- |
| 1（07:4x） | 462833ee8、`--image` | 5 分 ssh が答えず FAIL。ユーザーが電源を入れ直した（観測「Keilandデスクトップは表示されていましたが、USB LANが異常なランプ点滅で、ドライバも異常状態でした。」）。USB の FAT の bootlog（`/log/boot-000001.txt`）に `vmd:` の行が無い: VMD は付いていなかった（下の BAR の大きさの bug で CFGBAR の map に失敗し、当時の版は何も書かずに戻った）。USB LAN（0bda:8156、usb-cdc-ncm）は付いたが link の行が出なかった。VMD が付く前の事なので VMD とは無関係と見ている（原因は未確認）。ユーザー「USB LANは非常に安価な製品なので、不安定なこともあると思いますから、再発しなければ深追いしなくてよいです。」→ 記録だけ。以後の 6 回では再発していない |
| A | 9b20acd5e、`pci.vmd=off` | PASS（対照。`vmd: disabled by pci.vmd=off`、ue0 link 2500 Mb/s） |
| B | 9b20acd5e＋失敗の log、`pci.vmd=read` | PASS。`vmd: CFGBAR 0x6052000000/0xfffffc0002000000 cannot be mapped (4)` → 原因: PCI core の 64 bit の BAR の大きさの計算（`~mask + 1`）が、上位の address を 42 bit しか実装しない VMD の上位の 0 を大きさに入れていた。core と plan の両方を「最も下の立った bit」に直した（f0effe7ad、Q1 了承） |
| B2 | f0effe7ad、`pci.vmd=read` | PASS。CFGBAR 32 MiB を map し、firmware の状態（e0:06.0 buses 00e1e100・窓 72007200、e1:00.0 BAR0 72000004）を log |
| D | f0effe7ad（全部） | PASS。`vmd: root port ffff:e0:06.0 used bus e1 window 0x72000000/0x100000`、`vmd: ffff:e1:00.0 BAR0 0x72000000 size 0x4000 64-bit`、`nvme: PCI controller ffff:e1:00.0 … MSI-X`、`nvme: /dev/nvme0n1 namespace=1 blocks=…1dcf32b0`（500118192 = Debian の見る大きさと同じ）、`vfs: native boot disk=none physical disks=2`、nvme0n1 の 6 区画が公開（`geometry error=21` は NVMe に CHS が無いだけ）。zedBSD から reboot した後、Debian が NVMe から上がった（5330-run の Linux への戻り） |
| E | f0effe7ad | PASS。`/dev/nvme0n1` の LBA 1 が `EFI PART`。EXTRA は 60 秒の上限で 1 GiB の読みの途中で切れた |
| F | f0effe7ad | PASS。nvme0n1 の読み 1・16・64・256 MiB がそれぞれ 0・1・0・1 秒、p4 の boot sector が `ZEDBSD` `FAT32`（P3 が作った物） |
| G | f0effe7ad | PASS。512 MiB 5 秒、1024 MiB 52 秒、195 GiB の位置の 256 MiB 71 秒。nvme の timeout・recovery・quarantine の行は 0 |

- 割り込み: VMD の handler の回数は割り込みの中で log を出さない作りにした（下）ので、回数の行は `message 0 (0 interrupts served so far)`（attach の時）だけ。代わりに、nvme の command の timeout は 10000 ms で、256 MiB（64 KiB の command 4096 個）が 1 秒で読めたので、完了の割り込みは VMD を通って届いている。
- 遅くなる読み: 512 MiB を越えると 10 MB/s 前後、さらに 3.6 MB/s まで落ちた。timeout の行は無い。VMD の経路か、zedBSD の buffer cache・memory の圧迫か、DRAM-less の BG5 の性質かは未確認（Future Work の候補、WS298 の受け入れ「見えて読める」の外）。
- segment 0 の他の device の BAR の大きさ（core の直しの前後）: kernel log に大きさの出る xHCI 00:0d.0・00:14.0（64 KiB）、LPSS 00:15.0・00:15.1（4 KiB）、i915 の uncore（8 MiB）は A（直す前）と G（直した後）で同じ。他の device は大きさを log に出さないので比べていない（未確認）。i915 の survey・USB・LAN は直した後も PASS。
- 割り込みの中の log を外した: 1 回目の版は子の最初の割り込みで kern_logf を呼んでいた。console の経路を割り込みの中で通る危険を避け、回数は thread の文脈（`host_msi_establish`）でだけ出す。
- 段の診断の語: `pci.vmd=off`・`pci.vmd=read`・`pci.vmd=noscan`（docs §7f）。`plan/tools/hw5330/5330-run.sh` に `CFG_ADD='語 …'`（その 1 回だけ cfg に足し戻す）。survey は image の `/bin/backlight-probe` を使うようにした（P4 の worktree を指していた既定を外した）。

## 未実施・残り

- `pci.vmd=noscan` の段は実機で使っていない（全部の段が通ったので要らなかった）。
- p003（規約の全文の見直し）: attach の中に `if (error == 0) error = …` の連鎖が残る（coding-style の「fallible な call を 1 つずつ確かめる」に合わせて直す）。
- 5330 の NVMe から zedBSD を起動するのは q1612 の後段（D2）。
