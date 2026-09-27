<!-- awesome-plan project=zedbsd record=ws073p006 -->

# ws073-p006: BUG-067 — devfs の文字 device の node が chmod・chown を受ける（`mesg n`）

Status: cleared（2026-09-27）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-067](../../bugs/BUG-067.md)（main の依頼で p005 の後に追加）

## 目的と受け入れ

所有者（と root）が devfs の文字 device の node（`/dev/console` など）の mode と所有者を変えられ、`mesg n`・`mesg y` が console で働く。
変更は後の lookup と、変更の前に開いた descriptor の fstat に見える。所有者でない user の変更は従来どおり EPERM。

## 再現（修正前、QEMU。main の測定用 guest image の kernel）

[tests/devfs-chmod.sh](../tests/devfs-chmod.sh): `chmod g-w /dev/console`・`chmod 600 /dev/null`・`chown 5:6 /dev/null`・`mesg n < /dev/console` が
全て `Operation not supported`、22 件中 16 件 FAIL。

## 原因

`devfs_setattr()`（`src/kern/devfs.c`）は `/dev/pts/N` の pseudo terminal だけを受け、それ以外を EOPNOTSUPP にしていた。文字 device の node は
lookup ごとに作り直す一時の inode なので、node に書いても次の lookup で既定（`crw-rw-rw-`、root:wheel）に戻り、記録の場所が無かった。

## 修正

`src/kern/devfs.c`: 文字 device の node（`i_fop == &cdev_file_ops`）の mode・uid・gid の変更を devfs の表 `devfs_attributes`
（device 番号を key にした 32 slot、spinlock `devfs_attributes_lock`、kernel の寿命）に記録する（`devfs_cdev_setattr()`）。初めての変更は node の
今の値から始めるので chmod は所有者を、chown は mode を保つ。`devfs_cdev_inode()`（lookup）と `devfs_getattr()` は記録を node に写す
（`devfs_cdev_attributes_apply()`）。権限の確認は従来どおり VFS（`inode_chmod_allowed` など）。block device と directory は変えない。
表が満ちたら ENOSPC。`struct cdev`（「不変の世代」）と HAL は触れていない。

## 検証（QEMU、KVM、4 GiB、4 vCPU、NVMe。実機は未実施）

- build: `make -j48 ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-guest.mk BUILD=build/ws073-a vmunix` warning 0。i386 pcat の vmunix も warning 0。
- guest（`tests/kernel-image.sh`、main の mesg を `build/ws073-u/bin/mesg` として置いた）: `MESG=/tmp/mesg sh tests/devfs-chmod.sh` 22 件全て PASS
  （修正前 16 件 FAIL）。salvage/ws001 の `pinned/guest.sh` の「mesg on the console」の期待（`mesg n` で 1、`is n`、group と other の write が `--`、
  `mesg y` で 0、`is y`、`w`）と同じ内容を含む。uid 65534 の process の `chmod /dev/null`・`chown /dev/console` は EPERM（47）。
- boot test: lean amd64 image（`plan/ws045/tests/config-amd64-base.mk`、warning 0）で `plan/tools/boot-test.sh` PASS（`build/ws073-img/boot-test-p006/login.png`）。
- 規約: `tests/style-diff.py src/kern/devfs.c` 0。

## 残り

- ws001-p040 の `pinned/guest.sh` の「mesg on the console」の case を戻すのは salvage/ws001 の片付けの側（main へ連絡）。
- 変更は再起動で消える（FreeBSD の devfs と同じ）。login が端末の所有者を変える処理（console・ttyN）は確かめていない（未実施）。
