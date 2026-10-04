<!-- awesome-plan project=zedbsd record=ws132-p004 -->

# ws132-p004: volumed（媒体の検出・通知・利用者の操作での mount・eject・抜去の片付け）

Status: in-progress（2026-10-05、P2 / q723。設計を書いた。下の Q-1〜Q-4 を Q1 に確かめてから kernel と protocol の部分を実装する）
Disposition: normal
Parent: [WS132](../ws.md)
Focused goal: fg019（ベータ1）
Queue: q723（P2、ws132-p004 → p005）
依存: [p002](../phase002/phase.md)（`/dev/system` の DISK の事象、main に統合済み）、[p003](../phase003/phase.md)（backend の事象の口）

## ユーザーの決定（D3、2026-10-05 未明、[p001](../phase001/phase.md) の末尾）

「自動mountはせず、通知を出します。通知をクリックするとFilesの左ペインのDevicesグループにアイコンが表示されるほか、Todayにもアイコンが表示され、何回か点滅します。このアイコンをダブルクリックすると/media/以下にマウントできます。」

Q1 の補足（2026-10-05）: mount は nosuid・noexec、持ち主は console の session の利用者。eject がある。WS156 の通知は未実装なので、代わりを設計して Q1 に示す。

## 範囲

- p004（この Phase）: root の daemon `volumed`（`userland/base/volumed/`）、その socket の protocol、compositor の backend の口（`libkeiland-backend-zedbsd/volume-zedbsd.c`）、kernel の mount の不足分（noexec、FAT の持ち主）、通知の代わり。
- [p005](../ws.md): libkeiland の `kl_system_devices_*`（mount の要求の追加）、Files の左の pane の Devices の group、Today の icon（数回の点滅）、double click での mount、eject の button と context menu。

## 今の source（2026-10-05 に読んだ）

| 所 | 今 |
| --- | --- |
| 事象 | `/dev/system` の `KERN_SYSTEM_EVENT_DISK`: ADD・REMOVE、subject は disk の名前（`da0`・`da0s1`）、detail `parent=<名前か -> removable=<0/1> block=<byte> blocks=<数>`（`src/kern/disk.c` の `disk_post_event`） |
| disk の情報 | `BLKGETINFO`（`include/uapi/block.h`: flags に REMOVABLE・PARTITION・READ_ONLY、parent、sector の数）、`BLKGETIDENTITY`（`include/uapi/blkid.h`: type・label・uuid）。起動の時に既にある disk は `/dev` の block device を見て調べられる |
| mount | `mount(2)` は superuser だけ。flags は `MNT_RDONLY`・`MNT_NOSUID`・`MNT_WRITETHRU`・`MNT_NOJOURNAL`。**noexec は無い**。引数 `struct mount_args`（version 1、fspec だけ） |
| FAT | 持ち主と mode を持たない形式。mount は全部を **root・0755** で見せる（metadata の file がある path を除く）→ session の利用者は読めるが書けない |
| UFS | disk の上の持ち主と mode のまま |
| unmount | superuser だけ、`MNT_FORCE` あり。使っている process は `KERN_SYSTEM_GET_FILE_USAGE` |
| 利用者 | sessiond が seat の device（`/dev/gpu*`・`/dev/input/event*`・`/dev/backlight/*`）を session の利用者に chown する（`userland/desktop/sessiond/seat.c`）。greeter の間は greeter の利用者、session の無い時は root |
| 口 | `kl_system_manager_v1` の `kl_system_devices_v1` は枠だけ: `eject(request, id)`、`device(id, kind, state, name, location)`、compositor は eject に unsupported を返す。libkeiland の `kl_system_devices_get/eject`、`struct kl_device`（KL_DEVICES_MAX 16） |
| 通知 | compositor に app の通知は無い（WS156 は planning） |

## 設計

### V1. volumed（root の daemon、`userland/base/volumed/`）

- service: `/etc/service.d/volumed`（type=daemon、`/sbin/volumed`、after=syslogd、restart=on-failure、required=NO）、`rc.conf` で enabled・optional（networkd・audiod と同じ流儀）。
- 起動: `/dev/system` を開いて `KERN_SYSTEM_EVENT_DISK` を購読してから、`/dev` の block device を全部 `BLKGETINFO` で調べる（購読を先にして、調べている間の抜き差しを落とさない）。
- volume にする物: REMOVABLE な disk の葉（partition、または partition の無い disk）で、`BLKGETIDENTITY` の type が zedBSD の mount できる物（`fat`・`ufs`）。ADD の後、同じ親の事象が 300 ms 来なくなってから調べる（disk の ADD の直後に partition の ADD が続くため）。partition を持つ disk 自身は volume にしない。
- volume の状態: `available`（mount していない、新しい）→ `mounted`（`/media/<名前>`）→ eject で `available`（「安全に取り外せます」）。抜かれたら（REMOVE）一覧から消す。mount 中に抜かれたら `MNT_FORCE` で unmount し、mount 先の directory を消す。
- **自動 mount はしない**（D3）。
- mount: `/media/<label>`（label が無い時は disk の名前）。名前は `/`・`.`・制御文字を `_` に、同じ名前があれば `-2`・`-3`。`/media` は root・0755 で作る。flags は `MNT_NOSUID | MNT_NOEXEC`（Q-1）。持ち主は要求した利用者（V2 の検査を通った者）: FAT は mount の持ち主の option（Q-1）、UFS は disk の上のまま。mount 先の directory は 0755・root。
- eject: unmount（force しない）。EBUSY なら `KERN_SYSTEM_GET_FILE_USAGE` で使っている process の名前を 1 つ返す（Files が「〜が使用中です」と出す）。成功で directory を消し `available`。USB の media の取り出しの命令は送らない（USB メモリには無い）。
- log: syslog（`volumed: ...`）と、試験のための 1 行の log（`VOLUMED ADD id=da0s1 fs=fat label=... size=...`、`VOLUMED MOUNT id= path= uid= error=`、`VOLUMED EJECT id= error= user=`、`VOLUMED REMOVE id= forced=0|1`）。

### V2. volumed の socket（`/run/volumed.sock`、SOCK_STREAM、0666）

1 行の text（`\n` 終わり、256 byte まで）。

| 向き | 行 |
| --- | --- |
| client → | `HELLO 1`、`MOUNT <request> <id>`、`EJECT <request> <id>` |
| → client | `VOLUME id=<id> state=available\|mounted fs=<fat\|ufs> size=<byte> label=<label> path=<mount 先か -> new=<0\|1>`（label は `%xx` で空白と `=` を逃がす） |
|  | `GONE id=<id>`、`DONE`（それまでの VOLUME が一揃いの状態） |
|  | `RESULT <request> <errno> [user=<process の名前>]` |

- HELLO で今の一覧（VOLUME… DONE）、以後は変化ごとに VOLUME か GONE と DONE。
- 権限: `getpeereid` の uid が **seat の利用者**（`/dev/gpu0` の持ち主。sessiond が session の利用者に渡し、session の無い時は root）か root の時だけ MOUNT・EJECT を受ける。他は `RESULT <request> EACCES`。一覧は誰でも読める（名前と大きさだけ）。greeter（session の無い利用者）は mount しない: seat の持ち主が greeter の利用者（`_greeter`）の時は EACCES。
- `new=1` は「挿されてから一度も mount していない」。Files と通知の代わりが点滅に使う。

### V3. compositor の backend（`libkeiland-backend-zedbsd/volume-zedbsd.c`）

- networkd・audiod の backend と同じ形: socket に繋ぎ（無ければ 2 秒ごとに繋ぎ直す）、HELLO、行を読んで一覧を持ち、DONE で compositor の host の callback `volumes_changed` を呼ぶ。`kl_backend_volume_*`（一覧の取り出し・mount・eject・fd・update）を `keiland-backend.h` に足す。Linux・FreeBSD は ENOTSUP の stub（ベータ1 では不要、2026-10-05 ユーザー）。
- compositor の `kl_system_devices_v1`: `device(id, kind=1 (removable storage), state (0 available・1 mounted・新しい物は bit 0x100), name=label, location=mount 先)`、`eject` を volumed に渡し、RESULT を `result` に。**mount の要求を足す**（Q-2）。

### V4. 通知の代わり（WS156 が入るまで、Q-3）

案 A（推奨）: **system bar の通知の領域に媒体の icon を出す**。`new=1` の volume がある間、右上の system bar（既存の icon の並び）に USB の媒体の icon を出し、出た時に 3 回点滅する。click で Files を `files --devices` で起こす（既に開いていれば前に出す。Files は Devices の group と Today の該当の icon を 3 回点滅）。一度 mount するか抜かれると icon は消える。WS156 が入ったら、backend が同じ契機で通知を出し、この icon は残すか消すかを WS156 で決める。compositor の追加は bar の icon 1 つと click の起動だけ。

案 B: compositor の最小の popup（画面の下の中央に 3 秒の板、click で Files）。WS156 の popup の一部を先に作ることになり、WS156 の設計と重なる。

案 C: Files への合図だけ（Files が開いていれば Devices に出て点滅）。Files が閉じていると何も見えない。

### V5. kernel の不足分（Q-1、UAPI の追加）

```c
/* include/uapi/mount.h */
#define MNT_NOEXEC  0x00000010U	/* files on the mount are not executed (exec answers EACCES) */
#define KERN_MOUNT_ARGS_VERSION_OWNER 2U
struct mount_args {		/* version 2 adds the owner FAT presents (version 1 stays accepted) */
	uint32_t size;
	uint32_t version;
	char fspec[KERN_MOUNT_FSPEC_MAX];
	uint32_t owner_uid;	/* version 2: the owner of every file of a filesystem without owners (FAT) */
	uint32_t owner_gid;
	uint32_t flags;		/* version 2: KERN_MOUNT_ARGS_OWNER when owner_uid/gid are given */
	uint32_t reserved;
};
/* include/uapi/statvfs.h */
#define ST_NOEXEC 0x00000010UL
```

- kernel: `sys_mount_call` が `MNT_NOEXEC` と version 2 を受ける（size で 1 と 2 を分ける）。`struct mount` に `MOUNT_NOEXEC` と持ち主（uid・gid・有無）。exec（`src/kern/exec.c`）は noexec の mount の file を EACCES（interpreter の script も同じ）。statvfs に `ST_NOEXEC`。FAT は持ち主の option がある時、root と既定の representation（今の 0755・root）を `owner_uid`・`owner_gid` で見せる（metadata の file の記録はそのまま優先）。
- 動的な library の mmap(PROT_EXEC) は止めない（Linux の noexec は止めるが、zedBSD の今の利用者には要らない。記録だけ）。

### 試験

- host: volumed の名前の作り方（label の逃がし、重複の `-2`）、行の protocol の parse、権限の判定（seat の持ち主・root・他・greeter）、事象の 300 ms のまとめ（偽の `/dev/system` と偽の disk で）。kernel の noexec と FAT の持ち主は host の VFS の試験の harness に足す（あれば）。
- QEMU（T1）: QMP の `device_add usb-storage`（FAT の小さな image）で `VOLUMED ADD`、client の道具（`volumectl list|mount|eject`、試験の image だけ）で mount（`/media/<label>`、session の利用者で書ける、`nosuid,noexec` が statvfs に出る、exec が EACCES）、eject（使用中なら EBUSY と process 名）、mount 中の `device_del` で forced の REMOVE と directory の片付け。compositor の bar の icon（案 A）の PNG。
- 実機: p007 の UAT（USB メモリ）。

## Q1 に確かめる点

| # | 何を | 推奨 |
| --- | --- | --- |
| Q-1 | kernel と UAPI の追加: `MNT_NOEXEC`・`ST_NOEXEC`・`struct mount_args` の version 2（FAT の持ち主）、`struct mount` と exec と FAT の変更（V5） | 許可（D3 の nosuid・noexec と、利用者が FAT の媒体に書けることに要る） |
| Q-2 | `kl_system_manager_v1` を version 5 にして `kl_system_devices_v1` に request 2 `mount(uint request, string id)`（since 5）と device の state の bit（new）を足す。WS113 p005 の D-PROTO（displays）は version 6 に繰り下げ | 許可（ws160 が version 4 を使ったため） |
| Q-3 | 通知の代わり: 案 A（system bar の媒体の icon、click で Files） | 案 A |
| Q-4 | 共有の file の変更: `userland/base/etc/rc.conf` に volumed、`userland/base/init/Makefile` に service（networkd と同じ所） | 許可 |

## 状態

- 2026-10-05: 設計を書いた（この file）。Q-1〜Q-4 の答えの後に kernel・protocol を実装する。答えに依らない volumed の核（検出・一覧・socket・権限）は先に進める。

## Q1 の決定（2026-10-05 未明、夜の自律の間。ユーザーが起きたら朝の報告で示す）

- Q-1（kernel と UAPI）: **許可**。MNT_NOEXEC・ST_NOEXEC・struct mount_args の version 2（owner_uid・owner_gid・flags、version 1 も受ける）、sys_mount_call・struct mount・exec の EACCES・FAT の持ち主の option。D3 の決定（nosuid・noexec で mount、利用者が使える）に要る。HAL は変えない。kernel は vmunix の link まで、host の試験を足す。
- Q-2（protocol）: **kl_system_manager_v1 の version 5** を ws132 の devices（mount の request と new の bit）に。WS113 の displays（D-PROTO）は version 6 に繰り下げる（Q1 が WS113 の契約に記録）。
- Q-3（WS156 の通知の代わり）: **案 A**（system bar の通知の領域に媒体の icon、new の volume がある間に出して 3 回点滅、click で `files --devices`、Files は Devices の group と Today の icon を 3 回点滅、mount か抜去で消える）。D3 の「通知をクリックすると Files の Devices と Today に icon が出て点滅」に最も近い。WS156 の通知ができたら通知に置き換える候補（朝にユーザーに確かめる）。
- Q-4（共有の file）: **許可**。rc.conf に volumed（enabled・optional）、init の services と Makefile に service（networkd と同じ形）。

## Q1 の判断（2026-10-05）

- Q-1 許可: noexec・`mount_args` の version 2・FAT の持ち主（HAL は不変、vmunix の link まで・host の試験）。
- Q-2: `kl_system_manager_v1` の version 5 を devices に（mount の request と new の bit）。WS113 の displays は version 6（WS113 の契約に記録済み）。
- Q-3: 案 A（bar の媒体の icon・3 回の点滅・click で `files --devices`、Files の Devices と Today も 3 回点滅）。WS156 ができたら置き換えの候補。
- Q-4 許可（`rc.conf`・init の service）。

## 実装（2026-10-05、P2）その 1: kernel と volumed の核

| 部分 | file |
| --- | --- |
| UAPI | `include/uapi/mount.h`（`MNT_NOEXEC`、`struct mount_args_owner` と `KERN_MOUNT_ARGS_VERSION_OWNER`・`KERN_MOUNT_ARGS_OWNER`）、`include/uapi/statvfs.h`（`ST_NOEXEC`） |
| kernel | `src/kern/syscall.c`（mount の引数の version 1・2、`MNT_NOEXEC`）、`include/kern/mount.h`（`MOUNT_NOEXEC`、`fat_mount_args` の持ち主）、`src/kern/exec.c`（noexec の mount の file と interpreter を EACCES。新しい goto を使わず、既存の access の検査に足した）、`src/kern/mount.c`（statvfs の `ST_NOEXEC`）、`src/kern/vfs.c`（root の mount の引数を 0 で初期化）、`src/drivers/fs/fat.c`（持ち主の option で root・全 inode・作る file の既定の持ち主） |
| mount(8) | `userland/base/mount/main.c`（`-o noexec`） |
| volumed | `userland/base/volumed/`（`main.c`: 事象・300 ms の後の走査・socket・mount・eject・抜去の片付け、`names.c`: 名前・行・権限の規則、`volumed.h`、`volumed.service`、`Makefile`（default n、config で選ぶ））、`userland/base/etc/rc.conf`（enabled・optional） |
| 試験 | `plan/ws132/tests/run-host-volumed.sh`・`host-volumed.c`（host）、`plan/ws132/tests/p004-guest.sh`・`config-amd64-p004.mk`（T1）、`userland/tests/volumectl/`（probe） |

- 確認: `sh plan/ws132/tests/run-host-volumed.sh` PASS（ASan・UBSan: 名前 10、escape 4、行 8、権限 5、VOLUME の行 2）。build（warning 0）: amd64 vmunix（kernel include check PASS）、volumed・volumectl・mount。style-check: 新しい file は違反 0、exec.c は違反が 1 つ減った。
- 未実施: QEMU（`p004-guest.sh`、FAT の stick の hotplug、kei の mount・書き込み・noexec・EBUSY の eject・抜去）。
- 次: compositor の backend（`volume-zedbsd.c`）と `kl_system_devices_v1` の version 5、bar の媒体の icon（案 A）。

## T1-137（2026-10-05）

- volumed の応答・自動 mount しない・抜去の後始末は ok。FAIL ×2 は試験の環境: stick の `device_add` が `usb port 4 (bus xhci.0) not found (in use?)`。原因は port の数ではなく（guest.py の qemu-xhci は既に `p2=8,p3=8`）、zdesktop-guest.sh の Venus の guest が `usb-tablet` を port 4 に挿していること。直し: `p004-guest.sh` は bus だけを指定し、port は QEMU に任せる（zdesktop-guest.sh は変えない）。
- guest の `ls` に POSIX の `-n` が無かった → `userland/base/ls/main.c` に `-n`（`-l` と同じで、持ち主と group を数で）を足した。host の試験 `plan/ws132/tests/run-host-ls-n.sh` PASS。

## 実装 その 2: compositor（2026-10-05、P2）

| 部分 | file |
| --- | --- |
| backend | `keiland-backend.h`（`kl_backend_volumes_*`、`struct kl_backend_volume`）、`libkeiland-backend-zedbsd/volume-zedbsd.c`（volumed の行、2 秒ごとの繋ぎ直し、DONE で一揃い、RESULT の queue）、`libkeiland-backend/unsupported/volume-unsupported.c`（Linux・FreeBSD）、`sources.mk`・`Makefile.linux`・`Makefile.freebsd` |
| protocol | `keiland/kl-system-protocol.h`: `kl_system_manager_v1` version 5、`kl_system_devices_v1` に request 2 `mount`（since 5）・event 3 `busy(request, program)`（since 5）、`KL_SYSTEM_DEVICE_KIND_STORAGE`・`KL_SYSTEM_DEVICE_MOUNTED`・`KL_SYSTEM_DEVICE_NEW` |
| compositor | `wayland/media.c`・`media.h`（新規: 一覧、bar の USB の stick の icon、new の volume で 3 回点滅、click で `files --devices`）、`wayland/system.c`（devices の object に一覧と done、変化で全 object に、mount・eject を volumed に渡し答えを `result`（busy の時は先に `busy` で program 名））、`wayland/shell.c`（bar の volume の左に icon、press）、`Makefile`・`Makefile.linux`・`Makefile.freebsd` |
| 試験 | `plan/ws132/tests/run-host-volumes.sh`・`host-volumes.c`（偽の volumed に対する backend の試験）、`plan/ws131/tests/host-system.c` に media の偽物（compositor の system.c が media を呼ぶようになったため） |

- 確認: `run-host-volumes.sh` PASS（ASan・UBSan、15）、`run-host-volumed.sh` PASS、`plan/ws131/tests/host-system.sh` PASS、zedBSD の `wayland`・`ls` と Linux の Keiland（`keiland-linux.mk all`、-Werror）の build、`keiland-os-boundary/check.sh` PASS、新しい file の style-check 違反 0、system.c・shell.c は新しい違反 0。
- 未実施: QEMU（bar の icon と devices の object は p005 の Files の試験とまとめて T1）。

## T1-139（2026-10-05）

16 行 ok、4 行 FAIL ×2。
- (1) `RESULT 1 25`: zedBSD の EACCES は 25（Linux の 13 ではない）。試験の誤り → 試験は `include/uapi/errno.h` から EACCES・EBUSY の数を読む。
- (2) `ls -n` の出力は `-rwxr-xr-x 1 1000 1000 3 … hello.txt`: 持ち主は正しく 1000。FAT の短い名前は小文字で見えるので、試験の `HELLO.TXT` の照合を大文字・小文字を問わない形に。
- (3)(4) mount したままの stick の抜去で volume と folder が残る: **kernel の制限**。`usb-storage.c` の `storage_detach` は disk が使われている（mount 中）と `disk_gone_if_idle` が EBUSY を返して detach が失敗し、USB の core は REMOVE の事象を detach の成功の後にしか出さない（`usb.c` の `post_device_event` は device の解放の後）。そのため disk は LIVE のままで、DISK・USB のどちらの事象も来ず、volumed は抜去を知る手段が無い。volumed は USB の事象も購読するようにした（kernel が直れば、走査で消えた disk を force unmount する）。kernel の直し（物理的に抜かれた mount 中の disk の media を退役させ、fs の I/O を失敗させ、事象を出す）は Q1 に報告（BUG の起票と担当の判断）。試験の step 5 はそれまで FAIL のまま。
