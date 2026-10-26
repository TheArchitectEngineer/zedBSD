<!-- awesome-plan project=zedbsd record=ws298-p003 -->

# ws298-p003 規約の全文の見直し

- 親: [WS298](../ws.md)。担当 P1（2026-10-27、Q1 の指示「NVMe から起動して survey が PASS したら、p003（規約の全文、attach の error の連鎖を含む）へ」）。
- Status: in-progress（見直しと直しは済み、判定は Q1）
- Disposition: normal

## 範囲

WS298 が変えた C: `src/drivers/pci/pci-vmd.c`・`pci-vmd-plan.c`・`include/drivers/pci/pci-vmd.h`（新規）、`src/drivers/pci/pci.c`（64 bit の BAR の大きさ、1 か所）、`src/kern/platform/pcat.c`（登録）、`plan/ws298/tests/host-vmd-plan.c`（host 試験）。[plan/coding-style.md](../../coding-style.md) の全文（§14 の checklist）と照らした。

## 直した事（commit aaa909887、動作は変えない）

- `pci-vmd.c`: 1 つの fallible な call を 1 つの if で確かめる（BAR の読み 3 つ、register の読み 2 つ、大きさの拒否 2 つの `if (error == 0) error = …` と複合の条件を分けた）。file の順（macro → enum → struct）に合わせ `enum vmd_stage` を macro の後へ。loop・paragraph の目的の comment（firmware の log の内側の loop 2 つ、`host = context;` 3 か所と `host = argument;`）。
- `pci-vmd-plan.c`: 目的の comment（`close_port_windows`・`size_bar` の始めの代入、`order_bars` の内側の while、`place_port` の loop の中の判断）。
- `pcat.c`: 条件の中の call をやめ、`vmd_error` に受けてから確かめる。
- host 試験: `main`・`check`・`model_find`・`get`・`model_write` の paragraph と loop と return の comment。

## 確かめ

| 確かめ | 結果 |
| --- | --- |
| `sh plan/ws298/tests/run-host-vmd.sh` | `host-vmd-plan: PASS` |
| `make ZEDBSD_CONFIG=plan/tools/hw5330/config-5330.mk BUILD=build/p1-5330 vmunix` | rc=0、warning 0 |
| `git diff --check` | 出力なし |
| 素の 5330、NVMe から（`5330-run.sh --nvme-kernel build/p1-5330/vmunix p1-ws298-p003-nvme-aaa909887`） | `5330-run: PASS`。`/dev/nvme0n1p5 on / type ufs`、`vmd: root port ffff:e0:06.0 used bus e1 …`、`nvme: /dev/nvme0n1 …` |

## 制限・未実施

- clang-format は使っていない（この tree の C は手で整える規約のため）。formatter の確かめは無い。
- 見直しは P1 自身（実装の担当）による。別の担当の見直し（conformance-reviewer）は未実施。
- 出典の comment（Values/Procedure referenced）は block ごと・複数行の形を確かめた。file の先頭に出典は無い。
