<!-- awesome-plan project=zedbsd record=ws130-p003 -->
# ws130-p003: IPv6 の transport（`AF_INET6` の UDP・TCP、`IPV6_V6ONLY`、dual stack）

Phase ID: `ws130-p003`
Parent: [WS130](../ws.md)
Status: in-progress（q804-i01、P1、2026-10-06）
設計: [p001](../phase001/phase.md) §3.1（H2: `IPV6_V6ONLY` の既定は 0、ユーザー承認 2026-10-05）
依存: [p002](../phase002/phase.md)（cleared、2026-10-06 T1-206b）

## 範囲

`socket(AF_INET6, SOCK_DGRAM|SOCK_STREAM)`、`sockaddr_in6` の bind・connect・sendto・recvfrom・getsockname・getpeername、`IPV6_V6ONLY`（既定 0: `[::]` の
socket は IPv4 も v4-mapped の address で受ける）、UDP と TCP の IPv6 の入出力（checksum は必須、source は RFC 6724、link-local は scope の interface）、
TCP の IPv6 の MSS（1440）。ICMPv6 の echo の socket と transport への error の配達は段 c、PMTU の TCP への反映は段 c。

## 設計の変更（p001 §3.1 の案 A から）

案 A は kernel の中の IPv4 の address を全て 16 byte に替える案だった。transport の中で address を読む所は少ない（tcp.c 15 箇所、udp.c 6 箇所）ので、
**IPv4 の型と動きはそのまま残し**、`struct inet_socket` に family・IPv6 の local と remote・scope・`v6only`・`mapped`（v4-mapped の address で bind か connect した
AF_INET6 の socket は、その IPv4 の endpoint を今の `local_address`・`remote_address` に持ち、IPv4 の経路で送受する）を足す。IPv4 の回帰の幅が小さく、
dual stack（`IPV6_V6ONLY` = 0）も書ける。差は「IPv4 の socket と IPv6 の socket を同じ照合の関数で扱わず、照合に v4 の見方と v6 の見方を持つ」こと。

## 段

| 段 | 中身 | 状態 |
| --- | --- | --- |
| a | `inet_socket` の family と IPv6 の欄、`AF_INET6` の family の登録、`sockaddr_in6` の bind・connect・name、`IPV6_V6ONLY`、port の衝突（v4 と v6 の見方）、UDP の IPv6 の送受と dual stack の IPv4 の受信 | 実装済み（build のみ） |
| b | TCP の IPv6（endpoint の照合、checksum、出力、listener の子、RST、MSS 1440） | 実装済み（build のみ） |
| c | ICMPv6 の echo の socket（`ping6` は p005）、transport への ICMPv6 の error、PMTU を TCP に | 実装済み（build のみ） |
| d | 試験: `userland/tests/ipv6-probe` に UDP・TCP の `::1` と slirp の `fec0::2`、`plan/ws130/tests/ipv6-p003.sh`。IPv4 の回帰（既存の network の試験）。T1 | — |

## 段 a の結果（2026-10-06、P1）

- `include/kern/net/inet-socket.h`: `family`・`local6`・`remote6`・`scope6`・`v6only`・`mapped`、flag `INET_SOCKET_SOURCE_CHOSEN`、
  `inet_socket_accepts_ipv4`・`accepts_ipv6`・`speaks_ipv6`・`unmapped`・`peer_name`。
- `src/kern/net/inet-socket.c`: `AF_INET6` の family（段 a は `SOCK_DGRAM` だけ、他は `EPROTONOSUPPORT`）、`sockaddr_in6` の bind・connect・
  getsockname・getpeername（v4-mapped は IPv4 の経路、link-local は scope の interface）、`IPV6_V6ONLY`（bind・connect の前だけ）、
  port の衝突は v4 の見方（両方が IPv4 を受けるとき）と v6 の見方（両方が IPv6 を受けるとき）の重なりの大きい方で判定。AF_INET 同士は今の規則のまま。
- `src/kern/net/ipv6.c`: `ipv6_route_source`（ipv6_output と同じ選び方の source と path MTU。transport が checksum の前に要る）。
- `src/kern/net/udp.c`: IPv4 の送信を `udp_send_ipv4` に分け、`udp_send_ipv6`（checksum 必須、MTU を超えるものは `EMSGSIZE`）、`udp6_input`
  （checksum 0 は破棄、bound の interface の照合）、IPv4 の受信は IPv4 を受ける AF_INET6 の socket にも v4-mapped の名前で渡す。IPv6 の connect は
  source を選んで getsockname に出す（再 connect で選び直す）。既存の `udp_input` の pull 失敗時の socket の参照の漏れも直した。
- 確認: amd64・arm64（rpi4）の kernel、i386（pcat）の変えた 3 file の build（`-Werror`、warning 0）。QEMU の確認は段 d で T1 に依頼する。

## 段 b の結果（2026-10-06、P1）

- `src/kern/net/tcp.c`: 受信は `tcp_input`（IPv4）と `tcp6_input`（IPv6、group 宛ては破棄）が checksum を確かめて `tcp_segment_input` に
  `struct tcp_segment_addresses` を渡す。照合は `tcp_connection_matches`・`tcp_listener_matches`（segment の family で、その family を受ける socket だけ。
  IPv4 の segment は `IPV6_V6ONLY` でない `[::]` の listener にも合い、子は v4-mapped）。子は listener の family で作る（`tcp_child_addresses`）。
  送信は IPv6 の相手なら `tcp6_route`（bound の interface か link-local の相手の scope、source は bound か RFC 6724 の選択を保持）と `ipv6_output`、SYN の MSS は 1440。
  RST は segment の family で返す（`tcp_send_reset6`）。`IPPROTO_IPV6` の option（`IPV6_V6ONLY`）を inet の層に渡す。
- `src/kern/net/inet-socket.c`: `AF_INET6` の `SOCK_STREAM` を TCP で作る。
- 確認: amd64・arm64 の kernel、i386 の変えた 4 file の build（`-Werror`、warning 0）。

## 段 c の途中（2026-10-06、P1、q806 の割り込みで中断）

- `src/kern/net/icmp.c`: raw の ICMPv6 の socket（`AF_INET6`・`SOCK_RAW`・`IPPROTO_ICMPV6`、`icmp6_socket_create`）。受信は全ての ICMPv6 の message を IPv6 の header 無しで
  （RFC 3542）bound・connected の address が合う socket へ（`icmp6_raw_deliver`）、送信は `icmp6_send` が checksum を埋める。IPv4 の raw socket は AF_INET の物だけに配る。
- `src/kern/net/icmp6.c`: 入力で raw socket に写しを渡し、Destination Unreachable・Time Exceeded・Parameter Problem を transport に（`icmp6_transport_error`、
  errno は code 0 ENETUNREACH・1 EACCES・4 ECONNREFUSED・他 EHOSTUNREACH、Time Exceeded EHOSTUNREACH、Parameter Problem EPROTO）。
- `udp6_error`（connected の socket の error に）、`tcp6_error`（port unreachable は SYN_SENT の connect を ECONNREFUSED で終える、他は soft）。
- PMTU を TCP に: TCP の送る segment は `TCP_MSS` 1024 で、IPv6 の最小 MTU 1280 から header 60 を引いた 1220 より小さいので、Packet Too Big で縮める要が無い（UDP は `ipv6_route_source` の path MTU を使う）。
- 2026-10-06（q806 の後）: `inet6_create` の `SOCK_RAW` を `icmp6_socket_create` につないだ。amd64・arm64 の kernel、i386 の変えた 6 file の build（warning 0）。残りは段 d（試験）。
- 確認: amd64 の kernel の build（`-Werror`、warning 0）。
