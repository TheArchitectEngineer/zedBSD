<!-- awesome-plan project=zedbsd record=ws072p001 -->

# ws072-p001: BUG-060（format の lease と write cached）

Phase ID: `ws072-p001`
Parent: [WS072](../ws.md)
Status: cleared
Queue: 2026-09-27 の並列実行

## 目的

[BUG-060](../../bugs/BUG-060.md): write cached の UFS の mount の上で、regular file への `mkfs -t ufs FILE` が format を書いた後 EBUSY で終わり、以後その volume の sync・umount が EBUSY になる。直す。

## 受け入れ

write cached の root（journal あり）で、書いたばかりの file（sync の前も後も）への `mkfs -t ufs [--journal-size=N]` が `ufs initialized` で終わり、その後の sync と書き込みが成功し、作った volume が host の検査で UFS OK。回帰: boot、UFS の強制終了（v3）、root の強制終了、journal の機能の試験。規約。

## 原因

1. mkfs は `KERN_FILE_FORMAT_RESERVE` で file の lease（backing claim、inode の key 付き）を取る。key 付きの claim がある間、`backing-claim.c` の `mutation_reserve` は **その volume のどこへの持ち主の無い raw の書き込みも** 拒む（「Truly unowned raw aliases ... reject their complete volume」）。filesystem の書き込み（`disk_write_filesystem_context` の guard の中）は許される。
2. write through の volume では書き込みは全部その guard の中で device に届くので問題が無かった。write cached（ws061-p006）では書き込みは buffer cache に残り、後で flusher・`buf_sync`（fsync・sync・journal の commit）が **guard を持たない thread から** 書き戻す。これが volume 全体の拒否に当たり EBUSY。
3. journal の commit（`j3_commit_locked` の `buf_sync`）が失敗すると `ms->writable = 0` になり、以後その volume は書き込めない（umount・sync・作成の失敗はこれ）。
4. さらに、formatter 自身の書き込みも遅延すると、書き戻しは lease の範囲（claim の extent）そのものへの書き込みで、filesystem の guard でも拒まれる。lease の前に cache に残った file の中身（`dd` の直後など）も同じ。
5. batched journal の中身の書き込み（`j3_write`）は呼び出し元の I/O context を捨てていた（`NULL` を渡していた）。

## 修正

| 部分 | 内容 | 場所 |
| --- | --- | --- |
| 遅延した書き戻し | `buf_writeback_context` は device への書き込みを filesystem の mutation の guard の中で行う（書き込みは buffer を dirty にした時に受け入れられている。書き戻しの thread は guard を持たない）。直下の raw の書き込みは同じ thread の guard を継承する | `src/kern/buf.c` |
| lease の下の書き込み | file の I/O は formatter の lease を I/O context（`claim`）に載せる。buffer cache は claim の付いた書き込みを遅延させず、その場で device へ書く（`writes_delayed`） | `src/kern/file.c`（`file_io_begin_cred` の context の初期化）、`src/kern/buf.c` |
| lease の前の書き出し | `file_format_reserve` は lease を取る前に file を fsync する（cache に残った file の中身を lease の前に device へ） | `src/kern/file.c` |
| journal の中身の書き込み | `j3_write` に呼び出し元の I/O context を渡す | `src/drivers/fs/ufs.c` |

副作用: loop device の file（`FILE_IO_LOOP_BACKING`、context に backing claim が付く）への書き込みも write cached の volume で遅延しなくなる（lease と同じ理由で、遅延すると書き戻しが拒まれる）。

## 確認（QEMU、native の image、NVMe、2026-09-27）

| 確認 | 結果 |
| --- | --- |
| build | amd64 warning 0（我々の source）、rpi4 の vmunix warning 0 |
| 直す前（`build/ws063/new.img`・`p004.img`） | probe（`plan/ws072/tests/format-lease-probe.c`: lease → pwrite → fsync）で fsync が EBUSY。`mkfs` は EBUSY、その後 `sync` も EBUSY |
| 途中（claim の書き込みの遅延なしだけ） | 変わらず EBUSY（原因 1・2 が残る） |
| 修正の後 | probe の fsync が成功。`dd` の直後（sync なし）の file への `mkfs -t ufs --journal-size=8` が `ufs initialized`、`sync` が成功、続く書き込みも成功。2 つ目の file にも成功。host の検査で UFS OK |
| journal の機能（`plan/tools/ufs/journal-func.sh`） | 25 の検査が OK、mkfs が `ufs initialized`、`ZJ3R` 8 MiB、全 volume UFS OK |
| 強制終了（`plan/tools/ufs/crash-test.sh`、v3） | 3・5・8 秒: 2117・2640・3182、途切れない prefix、UFS OK |
| root の強制終了（`plan/tools/ufs/root-crash.sh`、4 秒） | sync した file が残る、root の partition が UFS OK |
| boot test | PASS（`build/ws063/boot-fix/login.png`） |
| 規約（`style-check.py`、main との比較） | `file.c` 200 → 200、`buf.c` 137 → 136、`ufs.c` 579 → 579。新しい行の指摘 0 |

未実施: loop device の file を write cached の UFS に置いた試験、FAT の上の lease（書き戻しの経路は同じだが、FAT の volume は write cached にならない）、make・sh の差分試験。実機は未実施。

## 見つけたこと（別件）

同じ block device を 2 回 read-write で mount できる（`mount -t ufs /dev/nvme0n1p2 /w` が root と並んで成功する。base の image でも同じ）。main session へ報告した（Bug Board への登録を依頼）。

## 結果（2026-09-27、cleared）

受け入れを満たした。BUG-060 は resolved にできる（Bug Board の索引の更新を main session に依頼）。
