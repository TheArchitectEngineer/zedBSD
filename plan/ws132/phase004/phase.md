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
