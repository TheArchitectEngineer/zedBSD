<!-- awesome-plan project=zedbsd record=ws130-p007 -->
# ws130-p007: `dhcpc -6`

Phase ID: `ws130-p007`
Parent: [WS130](../ws.md)
Status: test-wait（q832、P1、2026-10-07: 正常系を実装、host の試験 PASS、T1 の試験待ち）
設計: [p001](../phase001/phase.md) §5、H7（DUID-UUID）・H5（DNS の順）（2026-10-05 ユーザー決定）
依存: [p006](../phase006/phase.md)（networkd の RA の処理、T1-289/290）

## 実装（2026-10-07、P1）

| 部分 | file | 中身 |
| --- | --- | --- |
| 電文 | `userland/base/net/dhcp6.c`・`.h` | RFC 8415 の client の Solicit・Request・Renew・Information-Request を作る（CLIENTID、Request・Renew は SERVERID、IA_NA（IAADDR を付けて）、ORO: 23・24 と Solicit は 82・Information-Request は 32・83、ELAPSED_TIME）。Advertise・Reply を読む（xid の 24 bit、CLIENTID の一致、SERVERID（Advertise は必須、Reply は無くてよい: QEMU の slirp の Reply には無い）、PREFERENCE、STATUS_CODE、IA_NA の T1・T2・status と IAADDR（preferred > valid は取らない）、DNS（3 つまで）、DOMAIN_LIST（英数・`-`・`_` の label だけ）、INFORMATION_REFRESH_TIME）。DUID-UUID（RFC 6355、version 4）、IAID は interface の名前の FNV-1a（index は変わり得る、MAC を出さない） |
| `dhcpc -6` | `userland/base/dhcpc/inet6.c`・`.h`、`main.c`、`Makefile` | `dhcpc -6 [-i] [-n] [-v] [-t s] [IF]`。UDP の `AF_INET6` socket（`SO_BINDTODEVICE`、[::]:546）から `ff02::1:2`:547 へ（scope は interface）。送り元が無い間（link-local の DAD）は 0.2 秒ごとに送り直す。各交換は 1・2・4…8 秒の round で送り直す。`-i`: Information-Request。無し: `/var/db/dhcpc/IF.dhcp6` に stateful の lease があれば Renew（時間の半分まで）、駄目なら Solicit（round の中で preference の最も高い Advertise、255 は即）→ Request。address は /128・`IN6_IFF_DHCP`・lease の寿命（valid 0 は消す）。DNS は `resolv.conf` の今の server の後に（H5、3 つまで、link-local は `%IF`、他の行と先頭の comment は保つ、search が無ければ足す）。`-n` は resolver を書かない（net.conf の DNS が static の時）。記録 `IF.dhcp6`: `mode`・`renew`（T1、0 なら preferred の半分。stateless は refresh、既定 86400・最小 600。無限は 0）・`server`・`address`。DUID は `/var/db/dhcpc/duid`（初回に作る、18 byte） |
| networkd | `userland/base/networkd/ipv6.c`・`.h`、`main.c` | RA を受けて、net.conf の `dhcp`: auto は M なら stateful、O で RDNSS が無ければ stateless。stateless・stateful はその通り、false は無し。同じ mode で走らせた interface は予定に任せる。`dhcpc -6 [-i] [-n] -t 10 IF` を走らせ（15 秒まで）、記録の `renew` 秒の後に走らせ直す（Renew）。失敗は 60 秒から倍に 3600 秒まで。interface が上がり直したら次の RA から。poll の待ちと `run_due_work` に入れた（confirmed の間は走らせない）。`networkd_run_command` は `run_command_until` の薄い包み |
| 試験 | `plan/ws130/tests/host-dhcp6.sh`・`.c`、`plan/ws130/tests/ipv6-p007.sh` | host: 34 checks（DUID・IAID、3 種の電文を byte ごと、拒否 3 つ、Advertise・Reply の読み、拒否 5 つ、status、寿命の矛盾、refresh、改行の label）。guest: 下の T1 の依頼 |
| p006 の修正 | `userland/base/networkd/slaac.c` | DNSSL の label を英数・`-`・`_` だけに（改行などが `resolv.conf` に入らないように）。`host-slaac.sh` PASS |

QEMU の slirp の DHCPv6 は Information-Request だけに答える（libslirp の `src/dhcpv6.c`、2026-10-07 に確かめた: Reply は CLIENTID を返し、ORO に 23 があれば DNS、SERVERID・refresh は無い）。
stateful（M=1）の確かめは p008 の tap と network namespace の dnsmasq で行う。

## 確認（host、2026-10-07）

| 確認 | 結果 |
| --- | --- |
| `make -j16 disk-image`（`-Werror`） | exit 0、自前の warning 0 |
| i386・aarch64 の clang（`include/libc`）で `dhcpc/inet6.c`・`main.c`、`net/dhcp6.c`、networkd の `ipv6.c`・`main.c` | warning 0 |
| `sh plan/ws130/tests/host-dhcp6.sh`（ASan・UBSan） | PASS（34 checks） |
| `sh plan/ws130/tests/host-slaac.sh` | PASS |

未実施: QEMU（T1: `ipv6-p007.sh`）、stateful と networkd の M・O（p008 の dnsmasq）、5330 の実機（p008）、規約の見直し（p009）。

## T1 への依頼（文面）

config-amd64-ipv6.mk を p007 を含む main から作り、`files-guest.sh start` で起動し、`plan/ws130/tests/ipv6-p007.sh build/<dir>/p007` を流す。PASS は最後の行 `ipv6-p007: PASS`。

## 積み残し

[WS177 の一覧](../../ws177/backlog-p1.md) の ws130-p007 の行。
