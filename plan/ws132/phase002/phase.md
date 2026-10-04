<!-- awesome-plan project=zedbsd record=ws132-p002 -->

# ws132-p002: /dev/system の事象の核、UAPI、送り手、KERN_SYSTEM_GET_POWER

Status: in-progress（2026-10-05 P1 generation17 / q707-i01。実装・build・host の試験まで。QEMU の試験を Q1 経由で T1 に依頼する。結果の判定まで cleared にしない）
Disposition: normal
Parent: [WS132](../ws.md)
Queue: q707 / q707-i01（Q1 の投入）。design-reviewer は省く（2026-10-05 ユーザー）
Design: [ws132-p001](../phase001/phase.md) の K1・K2

## 範囲と受け入れ条件

- K1: `/dev/system` の open ごとの購読（`KERN_SYSTEM_EVENT_SUBSCRIBE`）、read（記録の整数倍、O_NONBLOCK で EAGAIN、待ちは signal で EINTR）、poll（POLLIN）、64 件の ring とあふれの `OVERFLOW` 記録、`KERN_SYSTEM_GET_POWER`。
- K2: 送り手。ACPI の固定の電源・sleep の button、control method の button（PNP0C0C・PNP0C0E）、蓋（PNP0C0D）、AC（ACPI0003）、電池（PNP0C0A）、disk、input、USB、network。
- build（warning 0）、変えた所の host の試験。QEMU の試験は T1（Q1 経由）。

## 実装（2026-10-05）

| 所 | 内容 |
| --- | --- |
| `include/uapi/system.h` | 種類の bit（POWER 0x1・LID 0x2・AC 0x4・BATTERY 0x8・DISK 0x10・INPUT 0x20・NETWORK 0x40・USB 0x80・OVERFLOW 0x80000000）、action（ADD 1・REMOVE 2・CHANGE 3・PRESS 4）、`struct system_event_subscription`、128 byte の `struct system_event`（static_assert）、`struct system_power_info`、`KERN_SYSTEM_EVENT_SUBSCRIBE`（ioctl 17）・`KERN_SYSTEM_GET_POWER`（ioctl 18） |
| `include/kern/system-event.h`、`src/kern/system-event.c`（新） | subscriber の一覧と ring。`kern_system_event_post()` は spinlock（irqsave）の中で allocation をせずに購読者の ring に入れ、待ちを起こし、lock の外で `poll_notify()`。満ちた ring は最も古いものを捨てて数え、次の read の先頭に OVERFLOW（value = 数）。lock は最初の open か post が作る（複数の CPU の最初の post は atomic の状態で一つだけが作る）。全 platform の vmunix.mk に追加 |
| `src/drivers/generic/system-device.c` | cdev に close・read・poll。subscriber は最初の購読で作る（ioctl だけの open は何も持たない）。購読は reserved が 0 でなければ EINVAL、0 や未知の bit も EINVAL（OVERFLOW だけの購読も EINVAL）。`KERN_SYSTEM_GET_POWER` は weak の `drv_acpi_power_get()`（ACPI の無い platform は全部不明） |
| `src/drivers/acpi/acpi-power.c`（新）、`acpi.h`、`acpi-kern.c` | namespace を歩いて present な蓋・AC・電池（`_STA` の battery bit）・button を最大 8 個とる。Notify の handler（AML を評価できない）は button なら即 PRESS を post、他は印を付けて thread を起こす。thread は `_LID`・`_PSR`・`_BIX`（無ければ `_BIF`）＋`_BST` を読み、値が変わったものだけ CHANGE を post（電池は % = remaining×100/last full、detail `charging=0/1`）。Notify を出さない firmware のため電池は 60 秒ごとにも読む。固定の sleep button も post するように handler を入れた。EC の attach の後で `drv_acpi_power_attach()` |
| `src/kern/disk.c` | `disk_create()` の成功で ADD、`disk_gone()`（実際に外した時だけ）と `disk_gone_if_idle()` の成功で REMOVE。subject は disk の名前、detail `parent=<親 or -> removable=<0/1> block=<byte> blocks=<数>` |
| `src/drivers/generic/input.c` | register の成功で ADD、unregister の終わりで REMOVE。subject `eventN`、detail `bus=<bustype> props=<16進> name=<名前>` |
| `src/drivers/usb/usb.c` | enumerate の configured の log の所で ADD、`device_finalize()` の disconnected の log の所で REMOVE。subject `usbB.A`、detail `port=… vendor=… product=… class=…` |
| `src/kern/net/net-device.c` | `net_device_create()` で ADD、`net_device_gone()` で REMOVE（route socket の ARRIVAL・REMOVAL と同じ所）。subject は interface 名、detail `ifindex=… mtu=…` |
| `userland/tests/systemevents/`（新、試験の道具） | `-c CLASSES -n COUNT -t MS` で購読して 1 行 1 事象（`event SEQ CLASS ACTION VALUE SUBJECT DETAIL`）、`-p` で電源の状態、`-x` で拒否の確認 |

送り手は全て weak の `kern_system_event_post` を呼ぶ（各層の host の fixture は事象の核なしで link できる）。

## 確認

| 確認 | 結果 |
| --- | --- |
| `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/q713 build/q713/vmunix` | 成功、warning 0（2026-10-05） |
| `plan/ws132/tests/run-host-system-event.sh`（ASan・UBSan） | 527 checks passed: 購読の拒否、種類の filter、記録の各 field、sequence、長い文字列の切り詰め、短い read、OVERFLOW（70 件 → OVERFLOW 6 と新しい 64 件、1 件の read は OVERFLOW だけ）、二人の購読者と close、blocking read と signal の EINTR、poll_notify は lock の外、lock の深さ |
| `plan/ws132/tests/run-host-acpi-power.sh`（ASan・UBSan） | 137 checks passed: 蓋・AC・電池・button をとり不在の電池と他の node はとらない、attach の時の GET_POWER（開・接続・50%・充電中）、蓋・AC の変化は 1 件の CHANGE、変化の無い Notify は何も出さない、電池の 0x81 で新しい %、button の 0x80 は handler から即 PRESS、他の Notify 値は無視、60 秒の周期で電池を読み直す、`_BIX` が失敗したら `_BIF` |
| `python3 plan/tools/style-check.py`（新しい file 全部、変えた hunk） | 新しい file は指摘 0（host の試験の setjmp は C99 7.13.1.1 で条件の中に置く他無く、注記した）。既存の file の変えた hunk の指摘 0 |
| `systemevents` の cross compile（`-c`） | 成功、warning 0 |
| QEMU（`plan/ws132/tests/p002-guest.sh`、image は `plan/ws132/tests/config-amd64-events.mk`） | **未実施**。T1 に依頼（Q1 経由） |
| 実機（5330 の蓋・AC・電池・電源ボタン） | **未実施**（ws132-p007 の UAT） |

試していない所: `plan/ws004/tests/run-xhci-concurrent-urbs-test.sh`（`usb.c` を compile する host の試験）は、この変更の前から動かない（`plan/tools/driver-fragments/prepare.py` が `src/drivers/disklabel/pc98-auto.c` の節を見つけられない、`src/kern/io-stats.c` が無い）。usb.c は kernel の build で確かめた。

## QEMU の試験（T1 への依頼の内容）

- image: `plan/tools/guest/test-image.sh plan/ws132/tests/config-amd64-events.mk BUILD`（SSH guest＋`systemevents`、追加の file 無し）。
- 起動: `plan/tools/guest/guest.py start IMAGE` → `wait`。
- 試験: `plan/ws132/tests/p002-guest.sh [OUTDIR]`。QMP で USB stick（16 MiB の空の image）・USB keyboard・USB network adapter を抜き差しし、`system_powerdown` を押す。
- 合格: 全行 ok（refusals、power-state = 全部不明、stick の usb/disk の add・remove、keyboard の usb/input の add・remove、adapter の usb/network の add・remove、power-button の press、sequence の増加、順、alive）。

## 残り

- QEMU の結果の判定（T1、Q1）。
- 蓋・AC・電池は QEMU に無い。実機で確かめる（p007）。
- p003〜 は D1〜D3 の判断の後。

## T1-106 の FAIL の直し（2026-10-05）

T1-106 の `p002-guest.sh` は FAIL ×2（2 回とも同じ）。どちらも試験の誤りで、kernel の事象は正しく出ていた（`event 12 disk add 0 sda parent=- removable=1 block=512 blocks=32768`・`event 13 disk remove …`、usb の add・remove、power の press）:

- stick の disk: 照合が subject を `[a-z]+[0-9]+` としていたが、disk は `sda`（数字無し）。`[a-z]+[0-9]*` に直した。
- keyboard・adapter: QMP の device_add が `usb port 5 (bus xhci.0) not found (in use?)`・`port 6` で断られた。harness の device が port 1〜3 を使い、qemu-xhci の残りは USB 3 の port（full speed の device は付かない）。stick を抜いた後の port 4 に keyboard、その後に adapter を挿すようにした（期待の `port=` も 4、adapter は vendor 0525）。
- 再試験: T1 に Q1 経由で依頼（同じ image、`plan/ws132/tests/p002-guest.sh`）。
