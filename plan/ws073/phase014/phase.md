<!-- awesome-plan project=zedbsd record=ws073p014 -->

# ws073-p014: amd64 は /boot・/boot/esp を自動で見せず fstab に任せる、fstab の ESP の mount が kernel の hold の上で通る

Status: cleared（2026-09-27）
Disposition: normal（amd64 の自動の公開を止めた部分は ws073-p015 の最終の layout で置き換えた。fstab の adoption は残る）
Parent: [WS073](../ws.md)

## ユーザーの判断（2026-09-27、原文）

「amd64 のUEFIおよびハイブリッドのイメージでは、カーネルが特殊な処理で/bootや/boot/espをマウントせず、fstabに任せてください。つまり、デフォルトの配布イメージではマウントしなくていいです。インストーラがfstabに書けば済むことです。」

（その後の最終の layout の判断は ws073-p015。この Phase の「amd64 で公開しない」はそれで置き換わるが、fstab の ESP の mount が kernel の hold の上で
通る仕組みはそのまま使う。）

## 目的と受け入れ

1. amd64 では kernel が `/boot`・`/boot/esp` を見せない（pcat・pc98・rpi4 は p009 のまま）。
2. amd64 native で kernel が ESP を boot0 として private に持っていても、fstab（と `mount -t msdosfs`）の ESP の mount が EBUSY にならない。
3. fstab の行を持つ image で `/boot/esp` に mount、書き込み、`fsck.fat -n` clean、boot test。

## 設計（可逆な既定）

- kernel の hold は外さない（`bootN:PATH` の runtime の selector と swap の file の source が boot の slot を使うため）。代わりに **adoption**:
  vfs が boot の slot の private な mount に `mount_private_allow_adoption()`（`src/kern/mount.c`、`MOUNT_ADOPTABLE_INTERNAL`）で印を付け、
  `mount_context()` は disk を名指す mount の要求を先に `mount_adopt_private()` に通す。同じ disk の adoptable な private な mount があれば、その root を
  要求の場所に bind する（kernel と namespace は 1 つの FAT の状態を共有する）。種類が違う、または書き込み可能な hold の上の read-only の要求は EBUSY
  （bind では read-only を守れないため）。無ければ従来どおり普通に mount する。
- amd64 では `vfs_publish_boot_filesystems()` が印を付けるだけで公開しない（`#if defined(HAL_ARCH_AMD64)`）。
- userland の mount: `msdosfs`・`vfat`・`msdos` を kernel の `fat` に読み替える（command line と fstab）。
- HAL は触れていない。

## 検証（QEMU、KVM、NVMe。実機は未実施）

- build: amd64 guest の vmunix warning 0、mount の binary。
- [tests/esp-fstab.sh](../tests/esp-fstab.sh)（main の測定用 image の vmunix を差し替え、新しい `/sbin/mount`）: 15 件全て PASS: kernel は `/boot/esp` に何も
  見せない、fstab の `/dev/nvme0n1p1 /boot/esp msdosfs rw 0 0` を `mount -a` が mount（`/dev/nvme0n1p1 on /boot/esp type fat (rw,bind)`）、書き込みと読み戻し、
  別の場所への 2 度目の mount は同じ filesystem の別の view、umount、`mount -t msdosfs` の command line、read-only の要求は EBUSY、umount の後も kernel が持つ。
- `KEEP=yes` で fstab の行を残した disk: `tests/fat-compare.py` で ESP は `fsck.fat -n` clean、`BOOTX64.EFI`・`vmunix`・`zedbsd.cfg` は不変。その disk の
  `plan/tools/boot-test.sh`（UEFI）PASS（`build/ws073-p014-boot/login.png`）。その disk で起動した guest では init の `mount -a` が `/boot/esp` を mount し、
  前の起動で書いた `REBOOT.TXT` が読めた。
- pcat の boot test（BIOS/IDE、warning 0）: PASS、画面に `vfs: boot filesystem published at /boot`（p009 のまま。`build/ws073-pcat/boot-test-p014/login.png`）。
- 規約: `tests/style-diff.py`（mount.c・vfs.c・mount.h・userland の mount）0。

## 未実施・わかったこと（main の質問: backing-claim の範囲）

- コードを読んだ範囲では、FAT の file の書き込み（`file_io_begin_cred` の regular file の write）、truncate（`inode_truncate_transaction`）、unlink
  （`inode_unlink_locked`）、rename（`inode_rename_locked` の source と target）、link は `backing_mutation_begin_inode[_claimed]` を通り、swap・loop の
  claim の file（keyed な FAT の identity）への mutation は `mutation_reserve()` が owner の違いで断る設計である。
- 実際に確かめる試験（ESP の上に swapfile を作り `swapon` してから `>>`・truncate・rm・mv）は guest で 300 秒の timeout になり、結果を得ていない
  （guest は応答していた。dd・mkswap・swapon のどこで止まったかは未調査）。**未実施**として残す（ws073-p015 で確かめる）。
