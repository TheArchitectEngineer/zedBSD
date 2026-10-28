<!-- awesome-plan project=zedbsd record=ws298 -->

# WS298 Intel VMD（Volume Management Device）の driver

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 08:33 Q1: p001〜p003 cleared。素の 5330 で VMD の下の NVMe が見えて読め、NVMe から zedBSD が起動する）
Completed: 2026-10-27（q1612 の関係）
Primary Milestone: MG003
Related Milestones: なし
Objectives: —
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

Intel VMD（5330 の 8086:467f、Alder Lake の VMD）の下にある NVMe を、BIOS の設定を変えずに zedBSD から使えるようにする: VMD の CFGBAR を ECAM として下の bus を列挙し、MEMBAR の窓で device の BAR を扱い、MSI-X の割り込みを VMD を通して届ける。既存の pci-nvme の driver がその下の NVMe に付き、zedBSD から NVMe が見えて読める（`physical disks=2`、NVMe の GPT と連続の読み）。

範囲の改め（2026-10-27、design §12 D2、Q1 の判断 (a)）: 前は「5330 の NVMe の区画から root を mount できる」まで。NVMe から起動して root を mount するのは q1612 の後段（`5330-run.sh` の NVMe の mode）で確かめた。

## ユーザーの決定

- 2026-10-27 06:57 Q1 の問い「5330 の NVMe は Intel VMD（RST、RAID On）の後ろにあり、今の zedBSD からは見えません。どうしますか？」へのユーザーのクリック「zedBSD に VMD の driver を書く」。
- design §12 の D1 (b)・D2 (a) は Q1 の技術判断（ユーザーへ報告）。

## 結果

- `src/drivers/pci/pci-vmd.c`・`pci-vmd-plan.c`・`include/drivers/pci/pci-vmd.h`（新規）、`src/drivers/pci/pci.c`（64 bit の BAR の大きさの計算を「最も下の立った bit」に、f0effe7ad）、`src/kern/platform/pcat.c`（登録）。設計は [design.md](design.md)（版 2、[design-review.md](design-review.md) を反映）。
- 実機（素の 5330、2026-10-27、f0effe7ad）: `vmd: root port ffff:e0:06.0 used bus e1 …`、`nvme: /dev/nvme0n1 namespace=1 …`、`vfs: native boot disk=none physical disks=2`、nvme0n1 の LBA 1 が `EFI PART`。nvme の timeout・recovery・quarantine の行は 0。
- 読みの速さ（実機、f0effe7ad、`/dev/nvme0n1` を dd）: 1・16・64・256 MiB は 0〜1 秒、512 MiB は 5 秒、1024 MiB は 52 秒、195 GiB の位置の 256 MiB は 71 秒。512 MiB を越えると 10 MB/s 前後から 3.6 MB/s まで落ちる。原因（VMD の経路・buffer cache・memory の圧迫・DRAM-less の BG5 の性質）は未確認。
- 規約の全文の見直し（aaa909887）の後、素の 5330 で NVMe から起動（`/dev/nvme0n1p5 on / type ufs`、`5330-run: PASS`）。
- 段の診断の語: `pci.vmd=off`・`pci.vmd=read`・`pci.vmd=noscan`。`plan/tools/hw5330/5330-run.sh` に `CFG_ADD='語 …'`。

## 制限・移管

- 512 MiB を超える読みが遅い（Future Work の候補、受け入れ「見えて読める」の外）。
- VMD の S0 idle の suspend は EBUSY。
- segment 0 の他の device の BAR の大きさは、大きさを log に出す device（xHCI・LPSS・i915 の uncore）だけを直しの前後で比べた（他は未確認）。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws298-p001 | 設計（VMD の register・bus の列挙・窓・MSI-X の付け替え・PCI の枠組みへの載せ方）と design-reviewer | cleared（2026-10-27、版 2、D1 (b)・D2 (a)） |
| ws298-p002 | 実装、host 試験、5330 の実機で NVMe が見える・読める | cleared（2026-10-27 08:23、f0effe7ad、素の 5330 で D・G PASS） |
| ws298-p003 | 規約の全文の見直し | cleared（2026-10-27 08:34、aaa909887、NVMe からの起動 PASS） |

## 記録の所在

Phase の計画・結果・試験（`plan/ws298/phase001`〜`phase003`、`plan/ws298/tests/` の host 試験）は削除する。git の commit `fb1f6afee` の `plan/ws298/` にある。Queue の履歴は [plan/history](../history/index.md)。
