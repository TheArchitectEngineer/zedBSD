<!-- awesome-plan project=zedbsd record=ws073p015 -->

# ws073-p015: boot の slot を /boot/boot0〜3 に自動で mount する（最終の layout。p009・p014 の公開を置き換える）

Status: planned（2026-09-27、WS073 の wrap up の時点。未着手）
Disposition: normal
Parent: [WS073](../ws.md)

## ユーザーの判断（2026-09-27、原文）

「amd64 UEFIでも、/boot/boot0みたいなマウントは自動でやりましょう。ESPはfstabです。スワップだけでもマウントします。rootfs.imgは読み込み専用なので、書き込みできなくても、読み込めていいと思います。」

## 意味（main の整理）

- 全ての platform（amd64 UEFI を含む）で、`bootN:` の file（overlay-root、overlay-data、または swapN だけでも）が参照する boot の slot を
  `/boot/boot0`〜`/boot/boot3` に自動で read-write に mount する。
- 直接の selector（`rootpart=`、UUID・PARTUUID）でだけ使う slot は mount しない。
- `/boot` はただの directory。ESP は fstab だけで `/boot/esp`（ESP 自身が参照される slot である場合の扱いを決める）。
- 使用中の file（rootfs.img・data.img・swapfile）は読めるが、書き込み・truncate・削除・改名は backing-claim の registry が断る。
- p009 の `/boot`・`/boot/esp` の公開と p014 の「amd64 は公開しない」を置き換える。

## 作業の案

1. `vfs_publish_boot_filesystems()`（`src/kern/vfs.c`）を、slot ごとに「`bootN:` の参照があるか」で決める形に書き換え、`/boot/bootN` に bind（p009 の bind と
   p012 の sync、p014 の adoption はそのまま使える）。参照の有無は `kern_boot_parameters`（overlay-root・overlay-data・swapN の `bootN:` の値）と
   runtime の swap の追加（`bootN:PATH`）から。
2. ESP が参照される slot のとき: `/boot/bootN` に出し、fstab の `/boot/esp` の行は p014 の adoption で同じ状態の別の view になる。
3. backing-claim の確認（下）を試験で確かめる。
4. amd64 native・hybrid、pcat、pc98、rpi4 の boot test と、各 layout で `/boot/bootN` の中身の確認。

## backing-claim の範囲（p014 で調べた分）

- コードを読んだ範囲: FAT の file の write（`file_io_begin_cred`）、truncate（`inode_truncate_transaction`）、unlink（`inode_unlink_locked`）、rename
  （`inode_rename_locked` の source と target）、link は `backing_mutation_begin_inode[_claimed]` を通り、swap・loop の claim を持つ file への mutation は
  `mutation_reserve()`（`src/kern/backing-claim.c`）が owner の違いで断る設計。
- 試験では未確認: ESP に swapfile を作って `swapon` し `>>`・truncate・rm・mv を試す手順が 300 秒で timeout（guest は応答、止まった段は未調査）。
  この Phase の最初に、止まった原因（16 MiB の dd・mkswap・swapon のどれか）を調べてから確かめる。
