<!-- awesome-plan project=zedbsd record=ws073p010 -->

# ws073-p010: BUG-071 — FAT に普通の道具で file を作れる（mount の見せる mode で見せる）

Status: cleared（2026-09-27）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-071](../../bugs/BUG-071.md)（ws073-p009 の作業中に見つけた。既存）

## 目的と受け入れ

FAT（ESP の `/boot/esp`、USB stick など）で `echo > file`・`touch`・`cp`・umask 077 の作成が成功し、内容が unmount の後も残る。
FAT は mode・所有者を持たないので、作った file は mount が見せる mode・所有者（0755 root:wheel、または metadata の file の記録）で見せる。

## 再現（修正前、QEMU。p009 の kernel）

2 台目の NVMe に host の `mformat -F` の空の FAT32 を付け `mount -t auto`: [tests/fat-create.sh](../tests/fat-create.sh) 22 件中 14 件 FAIL
（`echo > a.txt` が `Operation not supported`。mkdir だけ通る）。`/boot/esp` への file の書き込みも同じ。

## 原因

`fat_creation_representable()`（`src/drivers/fs/fat.c`）が、作成の要求の mode・所有者が mount の見せる値（既定 0755 root:wheel）と完全に一致する
ことを要り、違えば EOPNOTSUPP にしていた。作成の後の `fat_created_inode_matches()` も同じ一致を調べていた。shell の既定（0644）・cp・touch の
要求は一致しないので、普通の file は作れなかった（0755 を要る mkdir だけが通った）。

## 修正

- `fat_creation_representable()`: 保存できない種類（file type の bit、special、rdev）だけを断り、mode・所有者は問わない。
- `fat_created_inode_matches()` を `fat_present_created_inode()` に置き換え: 汎用の層が要求を当てた後、inode に mount が見せる mode・所有者を与える
  （以後の lookup と同じ値）。file と directory の作成の両方。
- HAL は触れていない。

## 判断が要る点（既定を選んで先へ進んだ。可逆）

- FAT での作成は、要求の mode を捨てて mount が見せる mode（0755 root:wheel）で見せる（FreeBSD の msdosfs・Linux の vfat と同じ）。POSIX の
  「作った file の mode は要求の mode」を FAT では守れない。以前の厳密な振る舞い（一致しなければ EOPNOTSUPP）に戻すなら、この 2 つの関数を戻すだけ。

## 検証（QEMU、KVM、4 GiB、4 vCPU、NVMe。実機は未実施）

- build: `plan/ws035/tests/config-amd64-guest.mk` の vmunix warning 0。
- `tests/fat-create.sh`（空の FAT32 の 2 台目の NVMe）: 22 件全て PASS（修正前 14 件 FAIL）: redirection・touch・cp・umask 077 の作成、
  subdirectory の file、`ls -l` が `-rwxr-xr-x root wheel`、1 MiB の file と cp の copy が unmount と再 mount の後も `cmp` で一致、rename、削除。
- `FILE_WRITE=yes tests/boot-publish.sh`（`/boot/esp` への file の書き込み・読み戻し・削除を含む）: 14 件全て PASS。
- host の `mtools` で guest が作った file の内容を確かめた（`mtype` が一致）。
- boot test: lean amd64（native）と pcat（BIOS/IDE、FAT の overlay）で `plan/tools/boot-test.sh` PASS（`build/ws073-img/boot-test-p010/login.png`、
  `build/ws073-pcat/boot-test-p010/login.png`）。
- 規約: `tests/style-diff.py src/drivers/fs/fat.c` 0。

## 残り

- 同じ確認の host の `fsck.fat -n` が、FAT の driver の既存の metadata の誤りを示した（BUG-071 とは別の原因）: 最上位の directory の `..` が 0 でなく
  root の cluster、FSInfo の空き cluster の数を更新しない、新しい entry の日時が 0（1980-00-00）。data は壊れていない。→ [BUG-072](../../bugs/BUG-072.md)、
  ws073-p012。
