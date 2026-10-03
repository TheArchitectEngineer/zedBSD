<!-- awesome-plan project=zedbsd record=ws072 -->

# WS072: UFS write cache の format の lease と NVMe の timeout の後の回復（BUG-060・BUG-059）

<!-- awesome-plan-current:start -->
Status: completed
Primary Milestone: MG004
Objectives: O1, O2
Parent: [Master](../master.md)
Queue: 2026-09-27 の並列実行（ストレージ担当のサブエージェント、main session の割り当て）
Resume point: —（完了）
<!-- awesome-plan-current:end -->

## 目標

write cached の UFS（ws061-p006・WS063 の既定）と、host の負荷の下の QEMU の NVMe で見つかった 2 つの不具合を直す（2026-09-27 の main session の割り当て「BUG-060 と BUG-059 を 1 つの WS で、bug ごとに 1 Phase」）。

受け入れ: 各 bug の再現の手順で直ったことを確かめる（QEMU）。回帰（boot、UFS の強制終了・journal の試験）。規約（新しい code の指摘 0、既存 file を悪化させない）。

## 結果（2026-09-27、completed）

| bug | 原因 | 修正 | 確認 |
| --- | --- | --- | --- |
| [BUG-060](../bugs/BUG-060.md)（resolved） | formatter の lease（inode の key 付きの backing claim）がある間、volume への持ち主の無い raw の書き込みは拒まれる。write cached の volume の遅延した書き戻し（flusher・`buf_sync`）は guard を持たず EBUSY、journal の commit の失敗で volume が書けなくなった | 書き戻しを filesystem の guard の中で（`src/kern/buf.c`）、lease の下の書き込みは遅延させない（I/O context に lease、`src/kern/file.c`・`buf.c`）、lease の前に fsync（`file.c`）、journal の中身の書き込みに I/O context（`src/drivers/fs/ufs.c`） | write cached の root で `mkfs -t ufs`（`dd` の直後でも）が成功、sync・書き込みも正常。journal の機能 25/25、強制終了 v3・root で UFS OK |
| [BUG-059](../bugs/BUG-059.md)（resolved） | NVMe の driver は timeout で走っている command を全部 ETIMEDOUT で失敗させて queue を作り直すが、失敗した要求を出し直さない | ETIMEDOUT の要求を回復した queue で最大 3 回走らせ直す（`src/drivers/pci/pci-nvme.c` の `nvme_disk_submit`・`nvme_disk_run`） | QMP で 2 台目の NVMe を 2048 byte/秒に絞る再現で、直す前は 4/4 の読みが ETIMEDOUT、直した後は 4/4 成功 |

build は amd64・rpi4 の vmunix で warning 0。boot test PASS（NVMe）。規約: style-check の数は main（この WS の前）と比べて `file.c` 200 → 200、`buf.c` 137 → 136、`ufs.c` 579 → 579、`pci-nvme.c` 336 → 335、変えた行の指摘 0。各 Phase で規約の全文で読んだ（WS が小さいので別の規約の Phase は置かなかった）。QEMU だけ、実機は未実施。

## 追記（2026-09-27）: BUG-066 は ws072-p001 の退行

ws072-p001 は `buf_writeback_context` の全ての書き込み（遅延した書き戻しと、書き手の中のその場の書き込みの両方）を filesystem の guard で包んだ。
その場の書き込みは書き手の guard の中にあり、特に loop device の file（FAT の上の `data.img`）の書き込みでは、新しい guard が loop の claim の
持ち主を継承して cache の line（claim の extent より広い）の書き込みを EBUSY（17）で拒んだ。hybrid の layout（amd64 の USB・pcat）の
overlay の data の mount が失敗して起動が止まる（[BUG-066](../bugs/BUG-066.md)）。

直し: filesystem の guard は遅延した書き戻し（`buf_writeback`: flusher・sync・reclaim）だけが取る。書き手の中の書き込み
（`buf_writeback_context`）は前のとおり書き手の guard を継承する（`src/kern/buf.c` の `writeback_line`）。

確認（QEMU）: amd64 の hybrid の image（`ZEDBSD_VARIANT=hybrid`、UEFI・USB）で、直す前の `buf.c`（16802c61）は画面に
`loop1: write block=128 count=16 flags=2 error=17`・`VFS initialization failed (17)`、直した後は boot test PASS
（`build/ws072/boot-hybrid/login.png`）。native の image で BUG-060 の `mkfs -t ufs` と sync は直した後も成功。pcat の image は
BUG-062（i386 の `sched.c`）で build できず未実施。

## Phase 一覧

| Phase | 内容 | Status |
| --- | --- | --- |
| ws072-p001 | BUG-060: format の lease と write cached | cleared（2026-09-27） |
| ws072-p002 | BUG-059: NVMe の queue の回復の後の root の mount の ETIMEDOUT | cleared（2026-09-27） |

Phase の記録は git の履歴にある（WS の完了で削除）。

## 制限・移管

- loop device の file（`FILE_IO_LOOP_BACKING`）を write cached の UFS に置いた場合も、その書き込みは遅延しなくなった（lease と同じ理由）。その構成の試験は未実施。
- BUG-059 の元の条件（host の高負荷の下の boot の繰り返し）と、root の mount の最中の timeout は未実施（同じ `nvme_disk_submit` を通る）。
- 別件: 同じ block device を 2 回 read-write で mount できる不具合を見つけ、main session へ報告した（WS073 へ）。

## 試験の道具（plan/tools へ移した）

- `plan/tools/nvme/timeout-retry.sh IMAGE [BPS] [READERS] [SECONDS]`: QMP の `block_set_io_throttle` で 2 台目の NVMe を絞り、command を driver の timeout より長く待たせて、回復の後の読みの結果を見る。
- `plan/tools/ufs/format-lease-probe.c`: mkfs と同じ手順で file の format の lease を取り、書き・fsync・読みの各段の結果を表示する（cross の toolchain で build して guest で実行）。
