<!-- awesome-plan project=zedbsd record=ws130 -->

# WS130: IPv6 の network stack

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。p001〜p008 cleared（p008 の 5330 は 2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」））
<!-- retained-after-completion --> Phase phase001 と tests/ はその場に残す: src/・userland/・platform/・Makefile・config/ の source か build の file が path を名指す（AGENTS.md §10 の source の comment の掃除の後に消す。2026-10-28 Q1 の方針、q1660）
Completed: 2026-10-27（q1608）
Primary Milestone: MG005
Related Milestones: MG002（POSIX の socket API）
Objectives: O1, O3
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

2026-10-03 ユーザー「WSを立てましょう。計画だけ行っていきます。ベータ2以降で実装します。dhcpc -6というコマンドを作るのがいいと思います。SLAACはどうすればいいかなあ。NDPもどうすれば。」

IPv4 だけだった kernel の network stack（`src/kern/net/`）に、IPv6 の本体・ICMPv6・NDP・SLAAC・`AF_INET6` の socket を足し、userland（`dhcpc -6`、`getaddrinfo` の IPv6、ping・ifconfig・route の IPv6、networkd の IPv6 の route と DNS）を揃える。

## ユーザーの決定

- 2026-10-03: DHCPv6 は既存の `dhcpc` に `-6` を足す。
- 2026-10-03「SLAACは、カーネルはRAを受け取る。networkdが解決する、でいいと思います。networkdはインタフェースを開いて通知を監視できますしね。」→ NDP は kernel、RA は kernel が受けて networkd へ通知し、SLAAC の address・route・DNS（RDNSS）は networkd が決める。
- 2026-10-03「IPv6について、net.confの記法を ipv4: と ipv6: に分離する必要がありますね。個別に無効にする設定も。」
- 2026-10-03「RFC 7217, RFC 8981は両方使いましょう。記録しておいてください。」→ 主の address は RFC 7217 の安定な address、外への接続には RFC 8981 の一時的な address を既定で有効。どちらも `net.conf` の `ipv6:` で切り替えられる。
- 2026-10-05: p001 の設計の判断 H1〜H8 をユーザーが承認（UAPI ほか）。
- 2026-10-08「規約の整形（各 WS の全文規約の Phase）はベータ3」→ p009 は WS の外の一括の見直しへ。
- 2026-10-13「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」→ p008。

## 結果

- kernel: UAPI、`ipv6.c`・ICMPv6・NDP・DAD・MLDv2・address と route の表、RS・RA と route socket（p002）。transport（16 byte の address、`AF_INET6` の UDP・TCP・ICMPv6、`IPV6_V6ONLY`、PMTU、source の選択、p003）。
- libc: `inet_pton`・`inet_ntop`・`getaddrinfo`・`getnameinfo`・resolver（p004）。
- 道具と `net.conf` の `ipv6:`（`net`・`ifconfig`・`route`・`ping`・`host`・`nslookup`、p005）、networkd の SLAAC（link-local・RFC 7217・RFC 8981・RA・既定の route・RDNSS・DNSSL、p006）、`dhcpc -6`（stateless・stateful、DUID、Renew、p007）。
- 証拠: QEMU の guest の試験 T1-206b・T1-243・T1-286・T1-290・T1-296・T1-300、slirp と tap+netns の dnsmasq の T1-316（p008）。5330 の DHCPv6 の UAT は 2026-10-13 の一括の cleared（個別の観測の記録は無い）。

## 制限・移管

- 規約の全文の見直し（p009）は WS の外の一括の見直しへ。
- 実機（5330）での IPv6 の個別の確認の記録は無い。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws130-p001 | 設計（kernel と userland の境界、UAPI、`net.conf`、段、試験） | cleared（2026-10-08 Q1 の判定） |
| ws130-p002 | kernel の核（UAPI・ipv6.c・ICMPv6・NDP・DAD・MLDv2・RA） | cleared（2026-10-06、T1-206b） |
| ws130-p003 | transport（`AF_INET6` の UDP・TCP・ICMPv6） | cleared（2026-10-06、T1-243） |
| ws130-p004 | libc（inet_pton・getaddrinfo・resolver） | cleared（2026-10-07、T1-286） |
| ws130-p005 | 道具と `net.conf` の `ipv6:` | cleared（2026-10-07、T1-290） |
| ws130-p006 | networkd の SLAAC | cleared（2026-10-07、T1-296） |
| ws130-p007 | `dhcpc -6` | cleared（2026-10-07、T1-300） |
| ws130-p008 | T1（slirp と dnsmasq）と 5330 の UAT | cleared（2026-10-13 ユーザー「UATの結果待ちのものは…clearedに…」、QEMU は T1-316） |
| ws130-p009 | 全文規約の見直し | WS の外の一括の見直しへ（Phase の directory なし） |

## 記録の所在

Phase の計画・結果（`phaseNNN/`）は削除する（Q1 の pipeline）。p001 の設計（`phase001/phase.md`、source の comment が節を引く）は移して残す案あり（Q1 の判断）。IPv6 の host・guest の試験（`tests/`）は plan/tools/ へ移す案（Q1 の判断、WS221 の p004 が `host-netconf6.c` の形を使う）。git の commit `fb1f6afee` の `plan/ws130/` にある。Queue の履歴は [plan/history](../history/index.md)。
