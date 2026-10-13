<!-- awesome-plan project=zedbsd record=ws231 -->

# WS231: AHCI（SATA）の driver

<!-- awesome-plan-current:start -->
Status: planned（2026-10-14 Q1 作成）
Primary Milestone: MG002
Related Milestones: なし
Objectives: O1
Parent: [Master](../master.md)
Queue: q981（p001、P2）
Target: 2026-10-14 18 時のコードフリーズに間に合えばベータ2、間に合わなければベータ3（ユーザー「はい、今日の18時をコードフリーズにして、そこに間に合うならAHCIも追加します。しかし、間に合わなければUSBブートでOKで、ベータ3に回してOKです。」）
Resume point: p001。
<!-- awesome-plan-current:end -->

## 原文（2026-10-14 ユーザー）

「AHCIドライバってどれくらいの作業量になりそうですか？」（Q1: 基本 3〜4 LW、全部入り 6〜8 LW）
「はい、今日の18時をコードフリーズにして、そこに間に合うならAHCIも追加します。しかし、間に合わなければUSBブートでOKで、ベータ3に回してOKです。」

## 目標

- `src/drivers/pci/pci-ahci.c`: PCI の class 01.06.01 の HBA を attach し、各 port の SATA の disk を disk の層（`struct disk`・`disk_ops`、NVMe の `pci-nvme.c` と同じ形）に出す。
- 基本（今日）: HBA の初期化（AE、BIOS/OS の handoff、port の COMRESET）、command list・FIS・command table・PRDT、IDENTIFY、READ/WRITE DMA EXT、FLUSH CACHE EXT、MSI、32 slot、port の reset による回復。
- 最小の suspend・resume は今日入れる（2026-10-14 ユーザー）。
- 後（ベータ3 か残り）: NCQ、TRIM、hotplug、ATAPI。ALPM・DevSleep などの link の省電力は相性の問題があるので入れない（2026-10-14 ユーザー）。
- 試験: QEMU の q35 の内蔵の AHCI（ich9-ahci）で T1、実機は 6〜8 世代の laptop の SATA の SSD（BIOS の SATA の mode が RAID（RST）なら AHCI に切り替える）。

## Phase

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 基本の AHCI の driver と host 試験、T1 への依頼 | cleared（2026-10-14、T1-609・T1-610） | — |
| [p002](phase002/phase.md) | 規約の全文の見直し（AHCI の driver・header・pcat.c の AHCI の部分・host 試験） | cleared（P2、書き方のみ） | p001 |
