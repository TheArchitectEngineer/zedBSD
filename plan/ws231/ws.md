<!-- awesome-plan project=zedbsd record=ws231 -->

# WS231: AHCI（SATA）の driver

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。p001・p002 cleared。基本の AHCI の driver が 2026-10-14 のコードフリーズに入った）
Completed: 2026-10-27（q1608）
Primary Milestone: MG002
Related Milestones: なし
Objectives: O1
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

`src/drivers/pci/pci-ahci.c`: PCI の class 01.06.01 の HBA を attach し、各 port の SATA の disk を disk の層（`struct disk`・`disk_ops`、NVMe の `pci-nvme.c` と同じ形）に出す。基本は HBA の初期化（AE、BIOS/OS の handoff、COMRESET）、command list・FIS・command table・PRDT、IDENTIFY、READ/WRITE DMA EXT、FLUSH CACHE EXT、MSI、32 slot、port の reset による回復、最小の suspend・resume。

## ユーザーの決定

- 2026-10-14（原文）「AHCIドライバってどれくらいの作業量になりそうですか？」（Q1: 基本 3〜4 LW、全部入り 6〜8 LW）
- 2026-10-14「はい、今日の18時をコードフリーズにして、そこに間に合うならAHCIも追加します。しかし、間に合わなければUSBブートでOKで、ベータ3に回してOKです。」
- 2026-10-14: 最小の suspend・resume は今日入れる。ALPM・DevSleep などの link の省電力は相性の問題があるので入れない。

## 結果

- p001: 基本の AHCI の driver と host 試験（`run-ahci-protocol-test.sh`）。QEMU q35（ich9-ahci）で T1-609・T1-610 PASS（poll の数え 0、close の後と poweroff の flush、throttle、BUG-304 の書き込みの量）。
- p002: 規約の全文の見直し（書き方だけ）。

## 制限・移管

- NCQ・TRIM・hotplug・ATAPI は入れていない（作成の時は「後（ベータ3 か残り）」。ベータ3 の段は 2026-10-27 に取り消し、行き先は未定）。
- 実機の SATA（6〜8 世代の laptop の SSD）は未確認。
- 回帰に残す試験（`run-ahci-protocol-test.sh` と `ahci-protocol-test.c`、image の `config-amd64-ahci.mk`）は plan/tools/ へ移す（Q1 の OK の後）。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws231-p001 | 基本の AHCI の driver と host 試験、T1 への依頼 | cleared（T1-609・T1-610） |
| ws231-p002 | 規約の全文の見直し | cleared |

## 記録の所在

Phase の計画・結果・試験は削除する（回帰に残す試験は plan/tools/ へ移す）。git の commit `fb1f6afee` の `plan/ws231/` にある。Queue の履歴は [plan/history](../history/index.md)。
