<!-- awesome-plan project=zedbsd record=ws130-p006 -->
# ws130-p006: networkd の SLAAC と起動の時の `ipv6:`

Phase ID: `ws130-p006`
Parent: [WS130](../ws.md)
Status: test-wait（q832、P1、2026-10-07: 正常系を実装、host の試験 PASS、T1 の試験待ち）
設計: [p001](../phase001/phase.md) §4、H4（RFC 7217）・H5（DNS の順）（2026-10-05 ユーザー決定）
依存: p002（kernel の RA の事象）、[p005](../phase005/phase.md)（`net.conf` の `ipv6:`、T1-287）

## 実装（2026-10-07、P1）

| 部分 | file | 中身 |
| --- | --- | --- |
| SLAAC の純粋な部分 | `userland/base/networkd/slaac.c`・`.h` | RA の読み（M・O、router lifetime、PIO の L・A・寿命、RDNSS の 3 つまでと寿命、DNSSL を空白で区切って）、RFC 7217 の IID（SHA-256: prefix の上位 64 bit、interface の名前、network の識別（有線は ""）、DAD の回数、秘密の下位 64 bit）、prefix と IID の address、RFC 8981 の一時的な address の寿命（valid 2 日・preferred 1 日−DESYNC の上限） |
| networkd の結線 | `userland/base/networkd/ipv6.c`・`.h`、`main.c`、`Makefile` | `PF_ROUTE`・`AF_INET6` の route socket を poll に足す。interface の到着・carrier で、`net.conf` の `ipv6:` の on・off（`SIOCSIFINET6`、既定 on）、on なら link-local（fe80::/64 ＋ RFC 7217 の IID）と静的な IPv6 の address。起動の時に上がっている interface も同じ。`RTM_ROUTERADV` で、autoconf なら A の /64 の prefix に安定な address（stable-address false なら EUI-64）と一時的な address（乱数の IID を interface と prefix ごとに覚え、次の RA は寿命を延ばすだけ）、router lifetime があれば既定の route（`RTF_DYNAMIC`、寿命つき、kernel が切れたら消す）、DNS が dhcp・merge なら RDNSS を `resolv.conf` の IPv4 の後に（H5、3 つまで、link-local は `%interface`、search が無ければ DNSSL）。秘密は `/var/db/networkd/ipv6-secret`（初回に `getentropy` の 32 byte、0600） |
| resolver | `networkd/main.c` の `write_resolver` | IPv6 の server も書く（p005 の `net commit` の IPv6 の DNS が通るように） |
| 試験 | `plan/ws130/tests/host-slaac.sh`・`.c`、`plan/ws130/tests/ipv6-p006.sh` | host: RA の読み（全ての option）、拒否 2 つ、IID の決定性と入力ごとの違い、address、一時的な寿命。guest: 下の T1 の依頼 |

## 確認（host、2026-10-07）

| 確認 | 結果 |
| --- | --- |
| `make -j16 disk-image`（`-Werror`） | exit 0、自前の warning 0 |
| i386・aarch64 の clang で `ipv6.c`・`slaac.c`・networkd の `main.c` | warning 0 |
| `sh plan/ws130/tests/host-slaac.sh`（ASan・UBSan） | PASS（18 checks） |
| `host-netconf6.sh`・`host-lan-configure`（script の rm を除いて手で） | PASS・PASS |

未実施: QEMU（T1: `ipv6-p006.sh`）、5330 の実機（p008）、規約の見直し（p009）。

## 積み残し

[WS177 backlog-p1](../../ws177/backlog-p1.md) に書いた（DAD の失敗の作り直し、一時的な address の更新、優先の interface だけの既定の route、DHCPv4 の resolv.conf の書き直しで RDNSS が消える、carrier が落ちた時の掃除、M・O の flag での `dhcpc -6` は p007）。
