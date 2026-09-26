<!-- awesome-plan project=zedbsd record=ws072 -->

# WS072: UFS write cache の format の lease と NVMe の timeout の後の回復（BUG-060・BUG-059）

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG004
Objectives: O1, O2
Parent: [Master](../master.md)
Queue: 2026-09-27 の並列実行（ストレージ担当のサブエージェント、main session の割り当て）
Resume point: p001 cleared。p002（BUG-059）
<!-- awesome-plan-current:end -->

## 目標

write cached の UFS（ws061-p006・WS063 の既定）と、host の負荷の下の QEMU の NVMe で見つかった 2 つの不具合を直す。

- [BUG-060](../bugs/BUG-060.md): write cached の UFS の上の regular file への `mkfs -t ufs` が EBUSY で終わり、以後その volume の sync・umount・書き込みが失敗する。
- [BUG-059](../bugs/BUG-059.md): host の高負荷の QEMU で、起動中の NVMe の I/O queue の回復の後に root の mount が ETIMEDOUT で失敗する。

受け入れ: 各 bug の再現の手順で直ったことを確かめる（QEMU）。回帰（boot、UFS の強制終了・journal の試験）。規約（新しい code の指摘 0、既存 file を悪化させない）。

2026-09-27 の main session の割り当て（「BUG-060 と BUG-059 を 1 つの WS で、bug ごとに 1 Phase」）。

## Phase 一覧

| Phase | 内容 | Status | 依存 |
| --- | --- | --- | --- |
| [ws072-p001](phase001/phase.md) | BUG-060: format の lease と write cached | cleared（2026-09-27。write cached の root で `mkfs -t ufs` が成功、sync・書き込みも正常。強制終了・journal・root の試験 UFS OK） | — |
| [ws072-p002](phase002/phase.md) | BUG-059: NVMe の queue の回復の後の root の mount の ETIMEDOUT | planned | — |

## 判断が要る点

なし。
