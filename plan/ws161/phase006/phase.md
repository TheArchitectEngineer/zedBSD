<!-- awesome-plan project=zedbsd record=ws161-p006 -->

# ws161-p006: 実機の UAT と Linux・FreeBSD の build

Parent: [WS161](../ws.md)
Status: in-progress（実機の UAT は 2026-10-13 ユーザー「OKでした」（USB の YubiKey）。Linux・FreeBSD の build は 2026-10-13 P2 q954 で os-freebsd.c を書いた（main に統合）。fidoctl は zedBSD だけ（2026-10-13 ユーザー (c)）。FreeBSD の本当の header での os-freebsd.c の build は未実施）
Phase disposition: normal
Queue: q954（P2、2026-10-13。承認: ユーザー「これ以上ベータ2に機能追加しないつもりなので、実装を終わらせましょう。」と p001 §9.6 の p006）

## 範囲（Linux・FreeBSD の build の分）

- p001 §9.4: libpasskey の os 層は zedBSD・Linux・FreeBSD。Linux の `os-linux.c`（`/dev/hidrawN`）は p004 で済み、FreeBSD の `os-freebsd.c` は design-reviewer の m8 で p006 に入れた。NFC は Linux・FreeBSD では v1 で無し。
- Linux・FreeBSD の Keiland で libpasskey を使う物は道具の `fidoctl` だけ（Settings の鍵の頁は session manager 経由で、Linux・FreeBSD の backend では使わない）。

## 実装（2026-10-13 P2）

| file | 内容 |
| --- | --- |
| `userland/base/libpasskey/os-freebsd.c`（新） | FreeBSD の hidraw(4) の `/dev/hidrawN`。hidraw(4) は Linux 互換の request（`HIDIOCGRDESCSIZE`・`HIDIOCGRDESC`・`HIDIOCGRAWINFO`・`HIDIOCGRAWNAME`、`struct hidraw_report_descriptor`・`hidraw_devinfo`）を持つので、os-linux.c と同じ形（include は `<sys/types.h>`・`<dev/hid/hidraw.h>`、記述の大きさの上限は `sizeof(report.value)`）。grab は無し、NFC の slot は無し。一次資料: freebsd-src の `sys/dev/hid/hidraw.h`（2026-10-13 に main を読んだ: 定義は上と同じ、`HID_MAX_DESCRIPTOR_SIZE` 4096、`_KERNEL` の条件なし） |
| `userland/base/libpasskey/os.h` | FreeBSD の行を注記に |
| ~~`userland/base/fidoctl/Makefile.linux`・`Makefile.freebsd`~~ | 書いたが、下のユーザーの決定 (c) で Q1 が main から外した |

## 確かめ（2026-10-13 P2、host = Debian 13）

| コマンド | 結果 |
| --- | --- |
| `make -f userland/desktop/keiland-linux.mk -f userland/base/fidoctl/Makefile.linux KEILAND_LINUX_BUILD=build/ws145-p005-linux -j16 build/ws145-p005-linux/bin/fidoctl` | rc 0、warning 0、`fidoctl list` → `devices 0`・`cards 0`、libcrypto.so.3 を link |
| `OUT=build/ws161-p006-fidoctl sh plan/ws161/tests/fidoctl-host-test.sh` | PASS |
| `cc -fsyntax-only -Wall -Wextra -Werror` で os-freebsd.c（`dev/hid/hidraw.h` を Linux の header に向けた代わりの物で） | 通る（FreeBSD の本当の header での build は未実施） |
| `python3 plan/tools/style-check.py userland/base/libpasskey/os-freebsd.c` | 指摘 0 |

## 判断（Q1 へ、2026-10-13）→ ユーザーの決定 (c)

**2026-10-13 ユーザー（Q1 経由）: (c)「Linux・FreeBSD には入れない」**。fidoctl は zedBSD だけ。Linux の Keiland の build に libssl-dev を必須にしない。fidoctl の Makefile.linux・Makefile.freebsd は Q1 が main から外し、os-freebsd.c は残した（libpasskey の FreeBSD の os 層、後で使う時のため）。

元の問い:

fidoctl を `keiland-linux.mk`・`keiland-freebsd.mk` の既定の all に入れると、Linux の build に libssl-dev（dnf/yum は openssl-devel、pacman は openssl）が新しく必須になる（FreeBSD は base の OpenSSL）。(a) 入れて `tools/build/keiland-prerequisites.sh`・`LINUX.md` に足す（P2 の推し）、(b) 別の target にして依存を増やさない、(c) 入れない。

## 未実施

- os-freebsd.c の FreeBSD の本当の header での build（Linux・FreeBSD の Keiland の build に fidoctl を入れないので、build の規則は無い。使う時に確かめる）。
- NFC（ACR1252U と YubiKey 5 NFC）の実機の確かめ（ws.md の 2026-10-10 の記録で未確認のまま）。

## 2026-10-13 夜: ベータ3 へ（Q1）

ユーザー「Linux/FreeBSDではFIDO2はまだ入れなくていいです。あとでLinux標準のFIDO2について調べて入れますが、ベータ3にします。」→ P2 の os-freebsd.c（c2fcd7d3d）と fidoctl の Makefile.linux・Makefile.freebsd は main から外した（git の履歴に残る）。Linux 標準の FIDO2 の調べと Linux・FreeBSD の対応はベータ3。
