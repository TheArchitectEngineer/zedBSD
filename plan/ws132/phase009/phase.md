<!-- awesome-plan project=zedbsd record=ws132-p009 -->

# ws132-p009: Files の Devices の直し（mount の確認、起動 disk を出さない）

Status: in-progress（実装済み・T1 の試験待ち）
Disposition: normal
Parent: [WS132](../ws.md)
Queue: q755（Q1、2026-10-05、P2 g15）
依存: p004・p005（済み）

## 範囲

2026-10-05 午後の UAT（ユーザー「Filesでのデバイスのマウントは、マウントするかの確認ポップアップがないと、セキュリティ的にあぶない気がしました。それから、起動ディスクのパーティションは表示しなくてもいいかも。」）: (1) mount の前に確認（名前・大きさ・file system、Mount・Cancel）、(2) 起動 disk の partition を Devices と Today に出さない。

## 実装（2026-10-05）

- **volumed**（`userland/base/volumed/main.c`）: scan ごとに kernel の mount table（`KERN_SYSTEM_GET_MOUNTS`）から root（target `/`）の device の名前を取り、scan の device の一覧で親を最上位まで辿って起動 disk を決める。その disk の上の partition は volume にしない（USB stick から起動した時、その stick の partition が媒体に出ない）。root が block device に無い（RAM disk）・表が読めない時は除かない。
- **拡張の protocol**（`kl-system-protocol.h`、kl_system_manager_v1 version 9）: kl_system_devices_v1 に event 4 `volume(string id, string fs, uint bytes_high, uint bytes_low)`（version 9 の object にだけ、各 device の後）。compositor の `system.c` が backend の volume の fs と bytes を送る。
- **libkeiland**（KL_VERSION 33）: `struct kl_device_info`（fs・bytes）と `kl_system_devices_info(system, id, &info)`。view が pending の device ごとに info を持ち、done で一緒に確定。
- **Files**: device が mount されていなければ、double click（Devices の行・Today の card）で `FM_DIALOG_MOUNT` の問いの card（「Mount "USBSTICK"?」・「16 MB, FAT. Programs on it can't be run.」・Cancel と青の Mount、Enter で Mount・Esc で Cancel）。Mount で従来どおり desktop に mount を頼み、mount されたら開く。問いの間に device が消えたら何もしない、mount されていたら開く。log `DEVICE mount confirm id= fs= bytes=`・`DEVICE mount answer id= confirmed=`。

## 検証（2026-10-05）

- build: zedBSD の `bin/wayland`・`bin/files`・`bin/volumed`・`bin/settings`（`ZEDBSD_CONFIG=plan/ws132/tests/config-amd64-p004.mk BUILD=build/p2-p016`）warning 0。Linux の Keiland warning 0。`exports.py` で exports.map を再生成。
- host: `run-host-files-devices.sh` PASS（試験を問いの流れに直した: double click で問い、Cancel で何もしない、Mount で mount の依頼、card の Enter）、`run-host-volumed.sh` PASS、`run-host-volumes.sh` PASS、`plan/ws131/tests/host-system.sh` PASS（ws089-p026 の backend の関数の stand-in が欠けて link できなかったのを直し、fake の volume 1 つで `kl_system_devices_info` の fs・bytes と、一覧に無い id を確かめる）。
- style-check: 変えた file の違反 0。
- guest の試験: `plan/ws132/tests/p005-guest.sh` の 3. を問いの流れに直した（confirm の log と confirm.png、Esc で何も頼まない、二度目の double click と Enter で mount）。
- 未実施: QEMU（T1）、起動 disk の除外の実機（5330 の USB 起動）での確認（UAT）、Linux・FreeBSD の backend の媒体の一覧での起動 disk（zedBSD の volumed だけを直した）。
