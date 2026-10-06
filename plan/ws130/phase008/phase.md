<!-- awesome-plan project=zedbsd record=ws130-p008 -->
# ws130-p008: T1（QEMU の slirp と tap＋netns の dnsmasq）と 5330 の UAT

Phase ID: `ws130-p008`
Parent: [WS130](../ws.md)
Status: test-wait（q832、P1、2026-10-07: dnsmasq の試験の script を書いた、T1 待ち。5330 の UAT は 5330 に届かないので保留）
設計: [p001](../phase001/phase.md) §8 の p008
依存: [p005](../phase005/phase.md)・[p006](../phase006/phase.md)・[p007](../phase007/phase.md)（T1-292 の IPv6 の束）

## 範囲

| 部分 | 試験 | 状態 |
| --- | --- | --- |
| QEMU の slirp（link-local・SLAAC の `fec0::/64`・RDNSS・information-request・`ping fec0::2`） | `ipv6-p005.sh`・`ipv6-p006.sh`・`ipv6-p007.sh`（T1-292） | T1 待ち |
| host の tap と network namespace の dnsmasq（M=1 の stateful、O=1、networkd の M flag、Renew、T1 で走らせ直す） | `plan/ws130/tests/ipv6-p008-dnsmasq.sh` | T1 待ち |
| host への TCP（IPv6） | p001 §8 にあるが、slirp の hostfwd は IPv4 の口。今回の束には入れない（下の積み残し） | 未 |
| 5330 の UAT（家の router の IPv6、Wi-Fi） | 手順は下 | 保留（2026-10-07: 5330 に届かない。届いたら U2 経由で T1） |

## dnsmasq の試験（2026-10-07、P1）

`ipv6-p008-dnsmasq.sh IMAGE [OUTDIR]`: host 側を sudo で作り（namespace `zb6`、bridge `zbbr0` に tap `zbtap0` と veth `zbv0`、相手の `zbv1` が namespace の中で `fd00:6::1/64`、root の namespace の側は IPv6 無効）、
namespace の中で dnsmasq（`dnsmasq-base`、host に導入済み 2.91）: RA 10 秒ごと、router lifetime 0（slirp の既定の route と争わない）、
`fd00:6::100-1ff`・`slaac`（A も立てる: zedBSD の kernel は prefix へ address の connected route で届くので、/128 の DHCPv6 の address だけでは `fd00:6::1` に届かない）・
lease 2 分（T1 60 秒）、DNS `fd00:6::53`・search `zb6.test`、DNS の口は閉じ、lease の file は書かない。guest は自分の runtime（`build/ws130-p008-run`）で、
2 つ目の usb-net（`52:54:00:33:00:02`、port 5）を tap に。終わり（trap）に guest・dnsmasq・link・namespace を下ろす（rm は使わない）。

判定は script の頭の 8 項目。QEMU の起動・host の sudo は T1 が行う。

## T1-293 の結果と修正（2026-10-07）

1 回目（/var/db の修正の前の image）と 2 回目（後）。2 回目は FAIL 2 行（`networkd on the M flag`・`T1`）だけ。dnsmasq の log では、networkd の Solicit・Request（03:02:31）、試験の手での Renew（03:02:52）、networkd の T1 の Renew（03:03:32、最初から 60 秒）が揃っていて、動きは正しい。
原因は試験: networkd の行は console に出て `/var/log/networkd.log`・`messages` に無い（console の log は判定に使わない）。修正: 手順 3・7 を host 側の dnsmasq の log（DHCPSOLICIT・DHCPREPLY、DHCPRENEW が 2 つ）で判定する。

## 5330 の UAT（保留中の手順）

5330 に届いたら: 家の router の RA（SLAAC・RDNSS）で、有線と Wi-Fi のそれぞれで `ifconfig`（link-local・安定・一時的）、`route -6 show`（既定の route）、
`ping -6` で外の IPv6 の host、`host -t AAAA` と `fetch` で IPv6 の URL、`resolv.conf`。

## 積み残し

- host への IPv6 の TCP（slirp の hostfwd は IPv4 の口）: dnsmasq の segment の namespace に listener を置く形で後で足せる。今は未。
