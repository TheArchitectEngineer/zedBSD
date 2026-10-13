<!-- awesome-plan project=zedbsd record=ws231-p001 -->
# ws231-p001: 基本の AHCI の driver

Status: planned（2026-10-14 Q1、q981、P2）
Disposition: normal
Parent: [WS231](../ws.md)
Queue: q981
依存: 無し

## 範囲

[ws.md](../ws.md) の「基本（今日）」。設計の要点を phase.md に短く書いてから実装する（Q1 が design-reviewer を通す。期限があるので実装と並行）。
- NVMe（`src/drivers/pci/pci-nvme.c`）の attach・disk の登録・bio の処理・DMA・MSI の書き方に揃える。USB storage の error の扱いも参考に。
- 既存の root の選び方（boot の root の指定、disklabel）で AHCI の disk が選べること。
- HAL の API は変えない。`plan/coding-style.md` の全文を適用。Makefile（`platform/*/vmunix.mk` の driver の一覧）に足す。

## 確かめ

- amd64 の kernel の build warning 0。host で試せる部分（FIS・command の組み立て、PRDT の分割）の host 試験。
- T1 への依頼の文: QEMU q35 の ich9-ahci に disk image を付け、boot の log でなく SSH で読み書き（dd と cmp）、partition の認識、AHCI の disk からの root の mount（できれば）、boot-test.sh の PNG。

## 設計の要点（2026-10-14 P2）

file: `include/drivers/pci/pci-ahci-protocol.h`（register の offset・bit、FIS・command header・PRDT の組み立てと IDENTIFY の解釈を、kernel に依らない inline 関数で。host 試験はこれを include する）、`include/drivers/pci/pci-ahci.h`（`drv_pci_ahci_driver_register()`・`drv_pci_ahci_probe_ports()`）、`src/drivers/pci/pci-ahci.c`。config は `CONFIG_DRIVER_PCI_AHCI`（amd64 で既定 y）、`src/kern/platform/pcat.c` で NVMe と並べて登録・probe する。

1. **attach**（PCI の列挙の中、割り込みの前）: class 01.06.01 に一致。BAR5（ABAR）を claim・map、PCI の enable の状態を保存し memory を有効にする。GHC.AE=1、GHC.IE=0。CAP・PI・CAP2 を読み、command slot の数 = min(CAP.NCS+1, 32)。MSI を 1 本（無ければ INTx）確保して handler を登録。重い処理（handoff・COMRESET・IDENTIFY）は時計と sched_yield が使える probe に回す（NVMe の namespace の probe と同じ理由）。
2. **probe**（`drv_pci_ahci_probe_ports()`、pcat.c の USB の 5 秒待ちの後、NVMe の probe の隣。理由は NVMe と同じで、AHCI の disk が USB の root の発見を終わったように見せない）: CAP2.BOH なら BIOS/OS handoff（OOS=1、BOS=0 を 25 ms＋BB なら 2 s まで待つ）。PI の各 port で: ST・FRE を落として CR・FR が消えるのを 500 ms まで待つ → port の DMA の memory（command list 1 KiB、受信 FIS 256 B、command table を slot 数ぶん 1 つの coherent buffer に 384 B ずつ（128 B 境界）、bounce 64 KiB）を確保して PxCLB・PxFB に書く → FRE=1 → COMRESET（PxSCTL.DET=1 を 2 tick 以上、DET=0、PxSSTS.DET=3 を 1 s まで待つ）→ PxSERR・PxIS を消す → PxTFD の BSY・DRQ が消えるのを 10 s まで待つ → PxSIG が ATA（0x00000101）でなければ（ATAPI・port multiplier）使わない → ST=1、PxIE を有効、GHC.IE=1 → IDENTIFY DEVICE（PIO in、DMA で bounce へ）。
3. **IDENTIFY の解釈**: word 83 bit 10（LBA48）が無い disk は今は使わない（READ/WRITE DMA EXT だけを使うため）。容量 word 100〜103、論理 sector の大きさ word 106（bit 14=1・bit 15=0 かつ bit 12）なら word 117〜118 の word 数×2、他は 512。512〜64 KiB の 2 の冪だけ受ける。FLUSH CACHE EXT は word 83 bit 13、無ければ FLUSH を何もせず成功にしない（EOPNOTSUPP を返す、ただし write cache が無効（word 85 bit 5=0）なら成功）。model（word 27〜46、byte を入れ替え）を log に出す。
4. **disk の層**: `disk_alloc` → `disk_alloc_sd_name`（PC/AT の IDE と同じく sdN）→ block の大きさ・数、`d_max_transfer_blocks = 64 KiB / block`、`d_flags = 0`（NVMe と同じ、DISK_FLUSH_PROOF は付けない）、stats の種類は ATA の disk として `DISK_STATS_KIND_IDE`（SATA の種類を足すと uapi の変更になるので今はしない）、`ioctl` は GET_GEOMETRY（IDENTIFY の CHS、無ければ 255/63）→ `disk_create`。
5. **I/O**（`submit` は NVMe・PC/AT の IDE と同じく同期で `bio_complete` まで行う）: port ごとの mutex で command を 1 つずつ出す（NCQ でない command は libata と同じく port に 1 つだけ。32 slot の command table は用意し、NCQ（後）で並列にする）。64 KiB ごとに: write なら bounce へ複写 → H2D Register FIS（C=1、READ DMA EXT 0x25 / WRITE DMA EXT 0x35 / FLUSH CACHE EXT 0xEA / IDENTIFY 0xEC、LBA48、device=0x40）→ command header（CFL=5、W、PRDTL、PRDBC=0、CTBA）→ PRDT（bounce の連続した領域を 4 MiB ごとに 1 entry、DBC=bytes−1、最後の entry に I）→ PxCI の bit を立てる → 完了を待つ → read なら bounce から複写。
6. **完了と割り込み**: port の spinlock の下の `ahci_port_service_locked()` が PxIS を読んで消し、事象を port に貯め、PxCI の bit が消えたら完了、TFES・HBFS・HBDS・IFS・(PxTFD.ERR) なら失敗とする。handler は GHC.IS の port ごとにこれを呼んで GHC.IS を消し、waitq を起こす。待つ側は NVMe と同じく、idle の thread（起動の早い VFS の発見）なら lock を外して sched_yield しながら自分で service を呼び、普通の thread なら deadline 付きの waitq_sleep の後に service を呼ぶ（割り込みが来なくても進む）。timeout は 30 s（回転する disk の再試行を見込む）。
7. **回復**: 失敗・timeout の後は port を作り直す（ST=0、CR が消えるのを待つ、PxSERR・PxIS を消す、BSY・DRQ が残れば COMRESET、ST=1）。timeout と HBA・link の失敗は 2 回まで同じ request をやり直す（read・同じ data の write・flush は繰り返して安全）。device が ERR を返した時（TFES、media の error）はやり直さず EIO。
8. **detach・shutdown**: detach は disk を出している間は EBUSY（hotplug は後）。shutdown は GHC.IE=0 と各 port の ST・FRE を落として DMA を止めるだけ（cache の flush は電源断の前の sync が disk の層の BIO_FLUSH で出す）。suspend/resume・NCQ・TRIM・ALPM・ATAPI・hotplug は後（ws.md）。
9. **DMA の address**: PC/AT の PCI の DMA は 32 bit の device なので全部 4 GiB 未満。上位の register（CLBU・FBU・CTBAU・DBAU）にも上位 32 bit を書き、CAP.S64A が無いのに 4 GiB 以上の address が来たら attach を断る。
