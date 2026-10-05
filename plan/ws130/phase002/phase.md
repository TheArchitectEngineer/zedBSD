<!-- awesome-plan project=zedbsd record=ws130-p002 -->
# ws130-p002: IPv6 の kernel の核

Phase ID: `ws130-p002`
Parent: [WS130](../ws.md)
Status: in-progress（2026-10-05 P1。UAPI の正確な差分を出した（H1 の 2 度目の確かめ待ち）。kernel の内部から実装する）
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

## 結果

（実行中）
