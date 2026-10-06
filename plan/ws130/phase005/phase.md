<!-- awesome-plan project=zedbsd record=ws130-p005 -->
# ws130-p005: IPv6 の道具と `net.conf` の `ipv6:`

Phase ID: `ws130-p005`
Parent: [WS130](../ws.md)
Status: uncleared（2026-10-07 T1-287 FAIL ×2: link-local address・ping -6 ::1・ping fec0::2・ping fe80::2%ue0・IPv6 on。追加・削除・route・off・IPv4 は ok。P1 が直す。旧: test-wait）
設計: [p001](../phase001/phase.md) §6（`net.conf`）・§7（道具）、H3（既定で有効、`ipv6: enabled: false` で止める。2026-10-05 ユーザー決定）
依存: [p003](../phase003/phase.md)（T1-243 PASS）、[p004](../phase004/phase.md)（libc、T1-286）

## 実装（2026-10-07、P1）

| 部分 | file | 中身 |
| --- | --- | --- |
| `ifconfig` | `userland/base/ifconfig/main.c` | 表示に `inet6 ADDR prefixlen N [scopeid 0xI] [tentative・duplicated・deprecated・temporary・autoconf・dhcp]`（`SIOCGIFADDRS_IN6`）。`ifconfig IF inet6 ADDR[/LEN]`（`SIOCAIFADDR_IN6`、寿命は無限）・`-inet6 ADDR[/LEN]`（`SIOCDIFADDR_IN6`）・`ipv6 on|off`（`SIOCSIFINET6`） |
| `route` | `userland/base/route/main.c` | `route -6 [show]`（`SIOCGRTENTRY_IN6`）、`route -6 add|delete default|PREFIX/LEN [GATEWAY] [-ifp IF]`（`SIOCADDRT_IN6`・`SIOCDELRT_IN6`） |
| `ping` | `userland/base/ping/main.c` | `-4`・`-6`、IPv6 の literal（`%zone`）と名前（getaddrinfo の順）、raw の ICMPv6 の echo（type 128・129、checksum は kernel）。setuid の前に両方の raw socket を開く |
| `host`・`nslookup` | `userland/base/host/main.c`・`nslookup/main.c` | `host` は A と AAAA（"has IPv6 address"）、`nslookup` は名前の A と AAAA、IPv6 の address の PTR（ip6.arpa） |
| `netutil` | `userland/base/net/netutil.c`・`.h` | `netutil_parse_cidr6`（"ADDR/LEN"、無ければ 128） |
| `net.conf` | `userland/base/net/netconf.c`・`.h` | interface の `ipv6:`（`enabled`・`autoconf`・`dhcp: auto|stateless|stateful|false`・`stable-address`・`temporary`・`addresses`、prefix-length 0〜128）、route の IPv6 の destination・gateway と `interface:`（link-local の gateway に要る）、IPv6 の DNS server。文字の欄を IPv6 の長さ（`NETCONF_ADDRESS_MAX` 45）に。validate: IPv6 の route は IPv6 の gateway、link-local の gateway は interface が要る。write: 既定と違う key だけ（section の無い interface は書かない）。既定の読み出し `netconf_ipv6_enabled` ほか |
| `net` の反映（`net commit`） | `userland/base/net/reconcile.c`・`main.c`、`protocol.h`、`networkd/main.c` | reconcile の program に `IPV6 IF on|off`（file が名指した時）・`STATIC6 IF ADDR/LEN`・`ROUTE6 DEST GW [IF]`・`ROUTE6_CLEAR`（前か後に IPv6 の route がある時）。networkd の op 11〜14（root だけ、confirmed の transaction の範囲に入れた）は `ifconfig`・`route -6` を走らせる。rollback の行と IPv6 の DNS server も読む。IPv4 の route は今のとおり default の 1 つだけ |
| 試験 | `plan/ws130/tests/host-netconf6.sh`・`.c`、`plan/ws130/tests/ipv6-p005.sh` | host: `ipv6:` の全ての key、既定、IPv6 の route と DNS、write と再読み、reconcile の program、4 つの拒否。guest: 上の道具（下の T1 の依頼） |

起動の時の `net.conf` の `ipv6:` の反映（networkd の有線の policy、`lan-configure.c`）は p006（networkd の SLAAC）で行う（今は `net commit` の reconcile の経路だけ）。

## 確認（host、2026-10-07）

| 確認 | 結果 |
| --- | --- |
| `make -j16 disk-image`（`-Werror`） | exit 0、自前の warning 0（Noct の外部の warning だけ） |
| i386・aarch64 の clang で netconf・reconcile・netutil・ifconfig・route・ping・networkd を compile | warning 0 |
| `sh plan/ws130/tests/host-netconf6.sh`（ASan・UBSan） | PASS（22 checks） |
| `plan/ws089/tests/host-lan-configure.c`（netconf の既存の host 試験、script の rm を除いて同じ命令を手で） | PASS |

未実施: QEMU（T1: `ipv6-p005.sh`、`LOOKUP_NAME` があれば DNS も）、`net commit` の guest の確認（console の操作が要る）、規約の見直し（p009）。

## 積み残し

[WS177 backlog-p1](../../ws177/backlog-p1.md) に書いた。
