# ws001-p034: df・du

Status: cleared（2026-09-27）
Parent: [WS001](../ws.md)
Queue: なし（2026-09-27 ユーザー指示のサブエージェントの作業。main session が merge する）

## 目的

台帳（ws.md §12）の #33 df と #36 du を XCU（POSIX.1-2024）の要求に合わせ、規約の全文で書き直す。
以前の df は `-k` だけで、operand が無いと `/` だけを書き、名前の欄に path を書いていた。du は `-a` だけだった。

## 範囲と結果

| utility | 変更 |
| --- | --- |
| df | 書き直し。`-k`・`-P`・`-t`（XSI。総量は常に書くので変わらない）、`-P` と `-t` の排他。operand が無ければ kernel の mount 表（`/dev/system` の `KERN_SYSTEM_GET_MOUNTS`、mount と同じ）の全部、operand は同じ device の最後の mount、mount された device の special file はその file system。名前は device（`/dev/...`）、無ければ source か type。数字は statvfs の block を 512 か 1024 の単位へ（桁あふれを避ける）、使用率は切り上げ。mount 表を読めないと operand は名前 `-` で書き、状態 1 |
| du | 書き直し。`-a`・`-s`（排他）・`-k`（切り上げ）・`-x`・`-H`・`-L`。directory は中身の後に書く。hard link と、辿った link で再び会う file は operand 全体で 1 回だけ数える（GNU と同じ）。自分の中で再び会う directory は loop として診断し状態 1。読めない directory は自分の大きさだけを数えて続ける。出力は `%d\t%s`（POSIX の `%d %s` の空白に tab、他の系と同じ） |

GNU と違えた点（`pinned/posix.sh`）: `du -L` の loop を診断する（GNU は黙って飛ばし状態 0）。

guest で見た mount 表（2026-09-27、lean amd64 image）:

```
Filesystem 512-blocks Used Available Capacity Mounted on
/dev/nvme0n1p2      2023424      92320    1931104       5% /
tmpfs                 65536          0      65536       0% /tmp
tmpfs                 65536         16      65520       1% /run
devfs                     0          0          0       0% /dev
tmpfs                 65536          0      65536       0% /dev/shm
/dev/shm              65536          0      65536       0% /shm
```

## 受け入れ条件と結果（2026-09-27）

| 確認 | 結果 |
| --- | --- |
| host の差分試験 `plan/tools/utils/cases/du.sh`（GNU du と。block の数は file system で違うので guest では流さない） | 15/15 |
| 期待値の case（host）`pinned-cases.py` | 10/10（guest だけの case 13 は流さない） |
| amd64 guest `DUMP=1 sh plan/ws001/tests/guest-run.sh build/ws001/guest-p034.out pinned`（`pinned/guest.sh`: df の heading・欄・足し算・切り上げ・`-k`・file operand・全 file system・device operand・無い operand・排他、du の UFS の割り当て・`-k`・合計・hard link・同じ operand・`-s`・`-H`・`-L`・`-x`（devfs の上の tmpfs）、と `pinned/posix.sh`） | 23/23 |
| style（`df/main.c`・`du/main.c`） | 違反 0、`cc -Wall -Wextra` の warning 0 |
| 実機 | 未実施 |

## 見つけたこと

- guest の devfs で `/dev/fd/N` を lstat すると、その番号の開いた directory そのもの（directory の型、同じ inode）が返る。
  du が `/dev` を歩くと、`/dev/fd` を開いた descriptor が `/dev/fd/4` として現れ、loop と診断する（`du /dev` は状態 1）。
  Linux では `/dev/fd/N` は symbolic link。kernel（devfs）の扱いで、この Phase では変えない。
- UFS は file に 8 KiB の block を丸ごと割り当てる（3000 byte の file が 16 単位）。`src/kern/inode.c` の `st_blocks` の式（512 byte 単位の切り上げ）は UFS では使われない。

## 残り

- df の `-t` で総量以外の書式は作らない（`-P` と同じ書式）。
