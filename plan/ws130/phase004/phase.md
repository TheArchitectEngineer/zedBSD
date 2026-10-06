<!-- awesome-plan project=zedbsd record=ws130-p004 -->
# ws130-p004: libc の IPv6（`inet_pton`・`inet_ntop`・`getaddrinfo`・`getnameinfo`・resolver）

Phase ID: `ws130-p004`
Parent: [WS130](../ws.md)
Status: test-wait（q832、P1、2026-10-06: 正常系を実装、host の試験 PASS、T1 の試験待ち）
設計: [p001](../phase001/phase.md) §7（libc）、H5（DNS の順は p006 の networkd の側）
依存: [p003](../phase003/phase.md)（T1-243 PASS）

## 範囲

`inet_pton`・`inet_ntop`（`AF_INET6`、RFC 4291 の書き方を読み RFC 5952 の形で書く）、`if_nametoindex`・`if_indextoname`、resolver の AAAA と IPv6 の
name server（`resolv.conf` の `nameserver fe80::1%ue0`）、`getaddrinfo`（`AF_INET6`・`AF_UNSPEC`、`AI_V4MAPPED`・`AI_ALL`・`AI_ADDRCONFIG`、`%zone`、
RFC 6724 の宛先の順）、`getnameinfo`（`AF_INET6`、`%zone`、`NI_NUMERICSCOPE`、ip6.arpa の PTR）。

## 実装（2026-10-06、P1）

| 部分 | file | 中身 |
| --- | --- | --- |
| address の文字 | `userland/base/libc/socket.c` | `inet_pton(AF_INET6)`（`::`・埋め込みの IPv4・hex の大小）、`inet_ntop(AF_INET6)`（RFC 5952: 小文字・先頭の 0 無し・2 つ以上の 0 の最長の連なりを `::`、同じ長さは左、v4-mapped は点）。x68k・sparcv9 の libc も socket.c だけで足りるように static の関数で持つ。`if_nametoindex`・`if_indextoname`（`SIOCGIFINDEX`・`SIOCGIFNAME`） |
| header | `include/libc/netdb.h`・`include/libc/net/if.h` | `AI_ALL`・`AI_ADDRCONFIG`・`AI_V4MAPPED`（FreeBSD の値）、`NI_NOFQDN`・`NI_DGRAM`・`NI_NUMERICSCOPE`、`IF_NAMESIZE`・`if_nametoindex`・`if_indextoname` |
| DNS | `resolver-dns.c`・`resolver-internal.h` | AAAA（type 28）の答えを `addresses6` に、AAAA の問いに address が無ければ `EAI_NONAME`。`resolver_inet6_ptr_name`（ip6.arpa）、`resolver_inet6_preferred`（宛先に届く source があるか: loopback は loopback から、link-local の source は link の中だけ） |
| resolver | `resolver.c` | `resolv.conf` の name server を順に IPv4 と IPv6（`%zone` は interface の名前か番号）で持ち（`list`、nslookup の IPv4 の `servers` はそのまま）、UDP・TCP を family で開く。CNAME を AAAA でも辿る |
| `getaddrinfo` | `resolver.c` | 数字の address（IPv4、`%zone` の IPv6、`AF_INET6`＋`AI_V4MAPPED` の v4-mapped、family の違いは `EAI_ADDRFAMILY`）、名前は AAAA（`AF_INET` でない時）と A（`AF_INET6` でない時、`AI_V4MAPPED` は AAAA が無い時か `AI_ALL` で v4-mapped に）、node 無しは IPv4 の後に IPv6（今の IPv4 だけの利用者の順を変えない）。`AI_ADDRCONFIG` は family ごとの最初の address に UDP の connect が通る family だけ。両方ある時、最初の IPv6 の宛先への source が `resolver_inet6_preferred` なら IPv6 を先に、でなければ IPv4 を先に（各 family の中の順は保つ） |
| `getnameinfo` | `resolver.c` | `AF_INET6` の数字（link-local は `%interface`、`NI_NUMERICSCOPE` で `%番号`）、名前は ip6.arpa の PTR |
| 試験 | `plan/ws130/tests/host-libc6.sh`・`.c`、`userland/tests/ipv6-probe/libc.c`（`-l [NAME]`）、`plan/ws130/tests/ipv6-p004.sh` | host: AAAA の問いと答え（CNAME の後の 2 つ、CNAME だけ）、PTR の名前、source による順。guest: 上の `-l` の段と p003 の再実行 |

## 確認（host、2026-10-06）

| 確認 | 結果 |
| --- | --- |
| `make -j16 disk-image`（libc を含む、`-Werror`） | exit 0、自前の warning 0（外部 package の openssl の warning は libc の header が変わって作り直されたもの） |
| i386・aarch64 の clang で `socket.c`・`resolver.c`・`resolver-dns.c` を compile | warning 0 |
| `make ZEDBSD_CONFIG=plan/ws130/tests/config-amd64-ipv6.mk build/amd64/bin/ipv6-probe` | exit 0、warning 0 |
| `sh plan/ws130/tests/host-libc6.sh`（plain・ASan/UBSan） | PASS ×2 |
| `inet_pton`・`inet_ntop` の IPv6 の部分を host で取り出して glibc と比べた（一時、commit しない） | 23 の入力で受け入れと bytes が glibc と同じ、RFC 5952 の形が期待どおり。glibc と違うのは非推奨の IPv4 互換 `::1.2.3.4` を zedBSD は `::102:304` と書く所だけ（RFC 5952 は v4-mapped だけを点で書く） |
| `style-check.py` | 新しい試験の file は指摘 0。libc の 3 file は前からの指摘（178 行）に新しい code の指摘が足された（空行・条件の中の呼び出しなど）: 規約の見直しは p009 で |

未実施: QEMU（T1: `test-image.sh plan/ws130/tests/config-amd64-ipv6.mk BUILD`、`files-guest.sh start`、`ipv6-p004.sh`、`LOOKUP_NAME` があれば DNS の名前も）。

## 積み残し

[WS177 backlog-p1](../../ws177/backlog-p1.md) に書いた（`AI_ADDRCONFIG` の判定、`NI_NOFQDN`、IPv6 の name server の nslookup の表示）。
