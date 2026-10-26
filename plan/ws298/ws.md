<!-- awesome-plan project=zedbsd record=ws298 -->

# WS298 Intel VMD（Volume Management Device）の driver

- Master: [plan/master.md](../master.md)（2026-10-27 06:57 作成）。Primary Milestone: MG003。関係: q1612（5330 の NVMe に zedBSD）。
- Status: incomplete（2026-10-27 06:57 新設、P1）
- 出典: 2026-10-27 06:57 Q1 の問い「5330 の NVMe は Intel VMD（RST、RAID On）の後ろにあり、今の zedBSD からは見えません。どうしますか？」へのユーザーのクリック「zedBSD に VMD の driver を書く」。

## 単一の到達目標

Intel VMD（5330 の 8086:467f、Alder Lake の VMD）の下にある NVMe を、BIOS の設定を変えずに zedBSD から使えるようにする: VMD の CFGBAR を ECAM として下の bus を列挙し、MEMBAR の窓で device の BAR を扱い、MSI-X の割り込みを VMD を通して届ける。既存の pci-nvme の driver がその下の NVMe に付き、zedBSD から NVMe が見えて読める（`physical disks=2`、NVMe の GPT と連続の読み）。

範囲の改め（2026-10-27、design §12 D2、Q1 の判断 (a)）: 前は「5330 の NVMe の区画から root を mount できる」まで。NVMe から起動して root を mount するのは q1612 の後段（`5330-run.sh` の NVMe の mode）で確かめる。

## 制約

- Linux の drivers/pci/controller/vmd.c は GPL: 値（register の offset・bit）と手順の参照だけ、code は写さず、block ごとに Value(s)/Procedure referenced（[Guardrail](../guardrail.md)）。Intel の公開の資料があればそれを優先。
- HAL の API を変えるなら差分ごとの事前承認。
- 実機は 5330（ssh 10.0.30.3）。遠隔の電源は無い。

## Phase

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 設計（VMD の register・bus の列挙・窓・MSI-X の付け替え・zedBSD の PCI の枠組みへの載せ方）と design-reviewer。[design.md](design.md) 版 2（[review](design-review.md) 反映、D1 (b)・D2 (a) 決定） | — | cleared |
| [p002](phase002/phase.md) | 実装、host 試験、5330 の実機で NVMe が見える（physical disks=2）・読める。素の 5330 で D・G PASS（f0effe7ad） | p001 | cleared（2026-10-27 08:23） |
| [p003](phase003/phase.md) | 規約の全文の見直し。直しは aaa909887、host 試験・build・素の 5330（NVMe から）PASS、判定待ち | p002 | in-progress |
