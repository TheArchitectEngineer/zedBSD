<!-- awesome-plan project=zedbsd record=ws130-p002 -->
# ws130-p002: IPv6 の kernel の核

Phase ID: `ws130-p002`
Parent: [WS130](../ws.md)
Status: in-progress（2026-10-05 P1。UAPI 承認・適用、kernel の核を実装、host 試験と build 済み、T1 待ち）
設計: [p001](../phase001/phase.md) §3（判断 H1〜H8 はユーザーが案のとおり承認、2026-10-05）

## 範囲

UAPI の差分（H1 の後）、`ipv6.c`・ICMPv6（echo・error）・NDP（近隣の cache・NS/NA・NUD・DAD・redirect）・MLDv2・address と route の表と ioctl、
RS の送出・RA の検査と route socket の事象、loopback の `::1`。transport（`AF_INET6` の UDP・TCP・ICMPv6 の socket）は p003。

## 1. UAPI の差分（H1: 適用の前にもう一度確かめる）

正確な差分: [uapi.diff](uapi.diff)（`git apply -p1` で当たる。host の compiler で構文と大きさの `_Static_assert` を確かめた）。
**承認（2026-10-05 夜、ユーザー、Q1 の問いへの答え）: 送った差分（493fb758）のとおり、§3.6 からの 3 つの変更を含めて承認。変えずに適用した。**

| file | 追加 |
| --- | --- |
| `netinet.h` | `IPPROTO_HOPOPTS`（0）・`ROUTING`（43）・`FRAGMENT`（44）・`ICMPV6`（58）・`NONE`（59）・`DSTOPTS`（60）。`IPPROTO_IPV6` の option（番号は SOL_SOCKET と同じく FreeBSD の物）: `IPV6_UNICAST_HOPS` 4・`MULTICAST_IF` 9・`_HOPS` 10・`_LOOP` 11・`JOIN_GROUP` 12・`LEAVE_GROUP` 13・`V6ONLY` 27・`RECVPKTINFO` 36・`PKTINFO` 46。`struct in6_pktinfo`。`IN6_LIFETIME_INFINITE`。`in6_addr` の注記（「何も route しない」）を直す |
| `netif.h` | `IN6_IFF_*`（TENTATIVE・DUPLICATED・DEPRECATED は kernel、TEMPORARY・AUTOCONF・DHCP・NODAD は呼ぶ側）、`IN6_IFADDRS_MAX` 16、`struct in6_aliasreq`（60 byte）、`struct in6_ifaddr_entry`・`struct in6_ifaddrs`、`SIOCAIFADDR_IN6` 0x89a0・`SIOCDIFADDR_IN6` 0x89a1・`SIOCGIFADDRS_IN6` 0x89a2・`SIOCSIFINET6` 0x89a3（`ifreq` の `ifr_flags`） |
| `route.h` | `struct in6_rtentry`（56 byte）と `SIOCADDRT_IN6` 0x89a4・`SIOCDELRT_IN6` 0x89a5・`SIOCGRTENTRY_IN6` 0x89a6（IPv4 の `rtentry` は `sockaddr` が 16 byte で `sockaddr_in6` が入らないので別の struct と番号）。route socket: `struct rtm_header`、`RTM_ROUTERADV`（48 byte の後に RA の本体、最大 1460）・`RTM_ADDRINFO`（56）・`RTM_NEIGHBOR`（48）、`RTM_F_OVERFLOW` |

- 設計 §3.6 からの変更: (1) IPv6 の事象は `socket(AF_ROUTE, SOCK_RAW, AF_INET6)` で作った socket だけに届く（BSD の protocol の引数の慣例）。protocol 0 の socket は今と同じ `RTM_IFINFO` だけなので、
  `sizeof(struct rtm_ifinfo)` で読む今の networkd が知らない長さの記録で channel を落とさない。(2) route の ioctl は `SIOCADDRT` の番号を共有せず、`_IN6` の番号を分ける。
  (3) IPv6 の address・route の ioctl は `AF_INET` の socket でも受ける（`AF_INET6` の socket は p003 で来る。`net`・networkd は今の `AF_INET` の socket のまま使える）。
- `socket()` の `AF_INET6`（UDP・TCP・ICMPv6 の echo）は p003。header の変更は要らない（`AF_INET6`・`sockaddr_in6` は既に在る）。

## 2. kernel の段取り

| 段 | 中身 | UAPI |
| --- | --- | --- |
| a | `wire.h` の IPv6・ICMPv6・NDP の形、`ipv6-address.c`（interface ごとの address の表、寿命、tentative・deprecated、source の選択 RFC 6724）、`route6.c`（最長一致・metric・寿命）、`nd6.c`（近隣の cache の状態遷移、NS/NA、NUD、DAD、redirect）を host で組める純粋な部分と、host 試験 | 要らない（kernel の中の型） |
| b | `ipv6.c`（入力: version・長さ・hop limit、拡張 header、fragment は落とす、宛先の判定、ICMPv6 Parameter Problem。出力: route、NDP の解決、MTU、loopback）、`icmp6.c`（echo の返事、error、Packet Too Big の PMTU の cache）、`mld6.c`（report・query）、RS の送出と RA の検査、worker の timer、Ethernet の 0x86DD と多 cast の MAC（33:33） | 要らない |
| c | ioctl（address・route・IPv6 の有効と無効）、route socket の事象（`AF_INET6` の protocol の socket） | 承認の後 |
| d | build（-Werror）、host 試験、T1 の依頼は p003 の後にまとめる（p008 の方針）か、p002 の終わりに `ping6` の無い範囲（NDP・DAD・RA の事象を道具で見る）で 1 回 | — |

## 実装（agent/p1）

| 段 | commit | 中身 |
| --- | --- | --- |
| a | e8854cea | `src/kern/net/in6.h`・`in6.c`（address の種類・scope・prefix・solicited-node・33:33 の MAC・RFC 6724 の policy 表）、`in6-address.c`（interface の address の表: 追加と更新、DAD の段、寿命で deprecated・expired、duplicate、carrier の戻りで DAD をやり直す、RFC 6724 の source の選択）、`in6-route.c`（最長一致・metric・寿命・purge）、`in6-neighbor.c`（RFC 4861 の状態遷移）。host 試験 `tests/in6-host-test.sh` |
| b | edacf815・1415f06c | `ipv6.c`（device ごとの record、loopback の `::1`、入力: header・宛先の判定・拡張 header（fragment は落とす、H6）・Parameter Problem、出力: route・source・PMTU・loopback・多 cast の MAC・NDP、timer: carrier・DAD・RS（link-local が preferred になった時、3 回 4 秒おき）・MLD の report・route の寿命）、`icmp6.c`（checksum、echo の返事、error（RFC 4443 の制限と毎秒 10 回）、Packet Too Big の PMTU の cache）、`nd6.c`（近隣の cache と保留の packet、NS/NA、DAD の衝突、RA の link の値、Redirect の host route）、`mld6.c`（MLDv2 の report、router alert、query への答え）。`core.c` の worker の timer、`net-device.c` の purge の hook、`checksum.c` の IPv6 の pseudo header |
| c | cd080bec | UAPI（承認の差分を変えずに適用）、`ipv6-ioctl.c`（`SIOCAIFADDR_IN6`・`SIOCDIFADDR_IN6`・`SIOCGIFADDRS_IN6`・`SIOCSIFINET6`・`SIOCADDRT_IN6`・`SIOCDELRT_IN6`・`SIOCGRTENTRY_IN6`、`AF_INET` の socket で、変更は superuser）、address の追加で connected route（/128 以外）と MLD の report、`route-socket.c`（`AF_INET6` の protocol の socket に `RTM_ROUTERADV`・`RTM_ADDRINFO`・`RTM_NEIGHBOR`、可変長の記録、overflow の印は種類ごと） |
| d | 40c616e6・a6fe59b7 | `userland/tests/ipv6-probe`（試験の image だけ）、`tests/config-amd64-ipv6.mk`、`tests/ipv6-p002.sh` |

## 確認（2026-10-05）

- host: `plan/ws130/tests/in6-host-test.sh` ok（ASan・UBSan）。`make managed-lan-host-test` PASS（UAPI の header を使う networkd の部分）。
- build（-Werror）: kernel（`build/ws161`、loopback の試験の設定）の link、userland の networkd・net・ifconfig・route・dhcpc・ipv6-probe、warning 0。style-check: 新しい file は 0、変えた file に新しい指摘 0。
- 未実施: QEMU（T1: `plan/tools/guest/test-image.sh plan/ws130/tests/config-amd64-ipv6.mk BUILD` の image で `plan/ws130/tests/ipv6-p002.sh`）、実機。

## 制限（p003 以降）

- transport（`AF_INET6` の socket、ICMPv6 の echo の socket、error の transport への配達）は p003。ping6 は p005。
- carrier の無い間は address の寿命を数えない（carrier が戻ると DAD をやり直し、そこで数える）。carrier は 1 秒ごとに見る（worker は IPv6 の record がある間 1 秒ごとに起きる）。
- DAD の最初の solicitation に乱数の遅れを入れていない（RFC 4862 5.4.2 の 0〜1 秒）。MLD の report も遅れなしで 1 回。
- 同じ prefix・同じ interface の route は 1 本（networkd は優先の router だけを入れる設計、p001 §4）。

## 結果

（実装と host 試験まで。T1 待ち）

## T1-206 の FAIL の原因と直し（2026-10-06 P1）

T1-206（main 553f0625）: boot-test は PASS、`ipv6-p002.sh` は 2 回とも FAIL。root で `IPV6 interface ue0 index=2` の後 `IPV6 FAIL step=list-lo0 error=23`（EFAULT）、
kei（runas）で `IPV6 FAIL step=route-socket error=47`（EPERM。試験は step=add-link-local を待つ）。IPv4 は ok。log は T1 の `/tmp/t1logs/t1-206-ipv6{,-retry}.log`。

- **原因 1（product、libc）**: libc の `ioctl()`（`userland/base/libc/posix.c` の `ioctl_has_argument()`）は、大きさの bit の無い request 番号では知っている物
  （SIOCGIFNAME〜SIOCGIFINDEX、SIOCGIFSTATS、SIOCADDRT・SIOCDELRT・SIOCGRTENTRY）にだけ第 3 引数を渡す。IPv6 の `0x89a0`〜`0x89a6` が無く、kernel には
  引数 0 が届き `ipv6_ioctl()` が EFAULT を返していた。直し: `SIOCAIFADDR_IN6`〜`SIOCGRTENTRY_IN6` の範囲も引数を渡す。kernel は変えない。
- **原因 2（試験）**: raw socket は route socket を含め superuser だけ（`src/kern/syscall.c` の既存の規則、p002 の前から）。probe が最初に route socket を開くので、
  root でない user は変更の ioctl の前に route socket で EPERM になる。kernel の規則は変えず、probe を直した: route socket が EPERM でも続けて一覧（誰でも読める）
  と最初の変更まで進み、変更が EPERM（step=add-link-local）で断られることを示す。変更が通った時だけ route socket の失敗を報告する。root の手順は同じ。
- 確認: zedBSD の build（libc・ipv6-probe、`plan/ws130/tests/config-amd64-ipv6.mk`）warning 0。QEMU は未実施（T1）。

T1 への依頼（T1-206 の再実行、未実行）: agent/p1 の commit（merge 後の main）で `plan/tools/guest/test-image.sh plan/ws130/tests/config-amd64-ipv6.mk BUILD`、
`plan/tools/files/files-guest.sh start BUILD/hdd-image.img`、`plan/ws130/tests/ipv6-p002.sh`。合格: `ok: ipv6-probe`（`IPV6 PASS`）、`ok: kei is refused`
（`FAIL step=add-link-local error=47`）、`ok: IPv4 still works`、`ipv6-p002: PASS`。log を返す。root の probe が list-lo0 より先で FAIL した時はその行と log。
