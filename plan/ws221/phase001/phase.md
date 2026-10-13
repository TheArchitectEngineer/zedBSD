<!-- awesome-plan project=zedbsd record=ws221-p001 -->
# ws221-p001: 調べと設計（Jumbo frame: USB の有線 LAN で MTU 9000、GENET は 1500 のまま）— 第 2 版

Status: in-progress（2026-10-13 P2: 第 2 版。review-1 の全 31 件（重大 8・中 11・軽微 12）に対応した。残り: design-reviewer の review-2、ユーザーの判断（§9）。cleared は Q1 の判定）
Parent: [WS221](../ws.md)
Queue: （Q1 が記入）
前の版: 第 1 版は git の履歴（commit `ecc1ef489` 以前の本 file）。review: [review-1.md](review-1.md)

## 第 1 版との差分（review-1 への対応の表）

行番号は merge 後の main `ecc1ef489`（2026-10-13、WS220 p002 の merge 後）の file で取り直した（L1）。

| review-1 | 対応 | 節 |
| --- | --- | --- |
| S1 MTU の変更と open の競合 | 推奨を「up のままでも変えられる」に変え（driver の buffer・URB が MTU に依存しなくなる S5・§3.5 の結果）、`device->mtu` の書き込みは `device_lock` の下、`ops->set_mtu` は ioctl と同じ admission（`ioctl_active`）で gone・shutdown_all が join する。down の時だけの案（B）は `mtu_changing` の印つきで残す | §3.2、§3.9、§9 Q1 |
| S2 sendmsg・recvmsg の上限の漏れ | `syscall.c` の 4 か所（1845・1961・2075・2245）を family ごとの上限 `socket_datagram_max()` に。ping の buffer を `-s` で動的に、受けた長さ＝送った長さを判定に | §3.1、§3.7、§8 |
| S3 lo0 の MSS が slot を超える | `tcp_device_mss` は「MTU − hdr」と「device の frame の class の上限（`net_device_frame_max`）− hdr − option 40」の小さい方。lo0 は標準の class。WS220 p006 への要件に入れた（ws220/ws.md 末尾の Q1 の記録と一致） | §3.4 |
| S4 IPv6 は path MTU で切る、IPv4 の black hole | IPv6: 送る segment の上限は送る時の `ipv6_route_source` の path MTU − 60（PTB の後は再送も切り直す）。IPv4: gateway 経由の宛先は MSS を min(1460, MTU − 40) に、on-link だけ device の MTU（`struct net_route.gateway`）。ICMP type 3 code 4 は Future Work。Q4 を書き直した | §3.3、§3.4、§9 Q4 |
| S5 ECM の受信 URB が壊れる | 受信 URB は常に attach の `frame_max` を bulk の max packet の倍数に切り上げ + 1 packet（ZLP を同じ URB で受ける）。MTU に依存しない。MTU 超えは core で落とす | §3.5 |
| S6 起動は reconcile を通らない | §3.7 を書き直した: `LAN_ENABLE` の record に `mtu N` を足す（語数 5 → 7）、`lan_policy_decode`・`struct networkd_lan_policy.mtu`、address の無い interface の扱い（制限）、`LAN_CONFIGURE`（lan-configure.c）の経路、`NETWORKD_OP_MTU` と policy の更新の関係 | §3.7 |
| S7 jumbo の slot の枯渇 | (a) driver の受けの予約 8 + (b) datagram socket ごとの jumbo の上限 2 + (d) lo0 は標準の class。(c) は p005 の任意。host 試験に「socket が使い切っても driver は取れる」 | §3.1、§9 Q2・Q13 |
| S8 実現性の確認と完了の条件 | ws.md の読み替え（Q1 2026-10-13）とユーザーの決定（GENET 1500、UGREEN 2.5GbE、switch 経由、5320 は対象外の案）を §0 に。descriptor の読みを p003 の最初の段に（手順 §4.1）。Q11〜Q13 を足した | §0、§4、§7、§9 |
| M1 下げる可否の矛盾 | 下げるのは全 device で許す（IP の層だけが使う。GENET は定数 `GENET_MAX_PACKET`）。`mtu_max` 未設定は `mtu_max = mtu` | §2、§3.2 |
| M2 p002 の試験が p004 の ping に依存 | `ping -s` と sendto の失敗の表示を p002 に移した（Q7） | §3.7、§7、§9 Q7 |
| M3 set_mtu が RA の MTU を忘れる | `ipv6_interface.ra_mtu` を持ち `link.mtu = ra_mtu ? min(device->mtu, ra_mtu) : device->mtu` | §3.3、§3.8 |
| M4 最小 MTU の穴 | 最小は常に 1280（Q3） | §3.2、§9 Q3 |
| M5 `no mtu` で戻さない食い違い | kernel が `mtu_default` を持ち `SIOCSIFMTU` の 0 は「既定に戻す」。reconcile は key が消えた interface に `MTU IF 0`（Q8） | §3.2、§3.7、§9 Q8 |
| M6 net.tcpstats は binary の UAPI | `struct tcp_stats_header` を 48 → 72 byte（jumbo の 4 欄 + 失敗の数）、読み手は `struct_size` で読む。WS220 の所有なので Q1 経由で順を決める。`packet_buf_in_use` に jumbo を含める | §3.1、§5 |
| M7 `tcp_device_mss` の所有が二重 | 作るのは WS220 p006。WS221 p002 は `net_device_frame_max()` を提供し、p005 は広告と受けの上限だけ | §3.4、§5 |
| M8 net.conf の互換の影響 | 全ての読み手（net・networkd・libkeiland-backend-zedbsd・monitor-probe）と rollback の影響を書いた。`mtu` は設定した時だけ書く | §3.7 |
| M9 QEMU で NCM の jumbo を流す代替 | host の Linux の gadget（dummy_hcd + f_ncm）を `usb-host` で渡す案を調べた（module の存在は確認、動作は未確認）。Q12 | §8.4、§9 Q12 |
| M10 DOWN は address を外す | S1 の変更で reconcile は `MTU` だけを出す（DOWN → UP を挟まない）ので接続は切れない。down が要る driver（今は無い）の時だけ EBUSY | §3.7、§8.2 |
| M11 検査の置き場 | `net_device_receive`（net-device.c:1155）。loopback を除く device で `length > 14 + mtu` を `rx_errors` に数える。WiFi は `wlan-l2.c:197` が A-MSDU を拒む（1500 を超える MSDU は来ない: 推測を p002 で確かめる） | §3.2 |
| L1 行番号 | 全部 `ecc1ef489` で取り直した | 全体 |
| L2 ncm.h の path と行 | `include/drivers/usb/usb-cdc-ncm.h:131〜138` | §1.6、§3.10 |
| L3 1547 は system event | MTU の変更で `KERN_SYSTEM_EVENT_CHANGE`（`include/uapi/system.h:334`）の event を `ifindex=%u mtu=%u` で出す（Settings の更新の契機） | §3.2 |
| L4 protocol.h の path、権限 | `userland/base/net/protocol.h`。`NETWORKD_OP_MTU = 21` は 3937 の範囲の外なので serialization の条件に明示に足す。権限は UP と同じ | §3.7 |
| L5 `.net_bss` | Q10 に注を足した（NOLOAD に移しても 16 MiB の予算は減らない、arm64 は `.vfs_bss`・`.kernel_heap` も同じ） | §3.1、§9 Q10 |
| L6 NCM の読み戻しと bit 3 の無い device | `GET_MAX_DATAGRAM_SIZE` で読み戻して `mtu_max` に。bit 3 が無ければ `wMaxSegmentSize`（≤ 65535）まで codec で受け、core で落とす | §3.6 |
| L7 alloc_for の失敗の区別 | `packet_buf_alloc_for()` は out 引数 `int *error`（EMSGSIZE / ENOBUFS）。UDP は EMSGSIZE を app に | §3.1 |
| L8 free の配列の選び方 | pointer の範囲で選ぶ（`pool_class` の欄は持たない） | §3.1 |
| L9 recv の staging | family ごと（AF_UNIX は 2048 のまま）。inet の datagram で 2048 を超える要求だけ 9216（page の確保になる。記録） | §3.1 |
| L10 NCM の 32 KB の coherent DMA | Pi 4 の確保の失敗・遅さは未確認。p003 の実機の log に `rx_rearm_failures` を出す。要れば 16 KB に戻す（jumbo 1 つ） | §3.6、§11 |
| L11 core が IPv6 に依存 | `ipv6_device_mtu_changed` は `SIOCSIFMTU` の handler（inet-socket.c）から呼ぶ。net-device.c は呼ばない | §3.2 |
| L12 slirp と ping | QEMU の ping の相手は tap の ue1 の host。SYN の MSS も tap で取る | §8.2 |

review の「§9 の推奨の妥当性」の欠落（Q11・Q12・Q13、5320 の扱い）は §9 に足した。

## 0. ユーザーの決定と前提

- 2026-10-13 ユーザー「GENETはMTU 1500までです。でもUSB LANではMTU 9000にしましょう。」→ Pi 4 の GENET（`src/drivers/platform/rpi4/rpi4-ethernet.c`）は 1500 のまま。USB の有線 LAN（CDC ECM・NCM）が MTU 9000 の目標。device の descriptor の上限が 9000 + 14 未満ならその上限。
- 2026-10-13 ユーザー「USB LANは型番不明の UGREEN 2.5GbE RTL8825BU です。MTU 9000はスイッチ経由です。」→ Pi 4 の USB LAN は UGREEN の 2.5GbE の adapter（chip はユーザーの言う RTL8825BU、型番は不明）。MTU 9000 の相手は jumbo frame を通す switch を経由する。混在の試験は同じ switch の上の 1500 の相手で行う。
  注（記憶、未確認）: Realtek の公開の USB 2.5GbE の controller として私が覚えているのは RTL8156・RTL8156B(G)（5GbE は RTL8157）で、「RTL8825BU」は覚えが無い。chip の同定は descriptor（`idVendor:idProduct`、configuration の class）で行い、名前は当てにしない（§4.1）。
- 5330 の `ue0` は **RTL8156** の USB LAN（2026-10-06 ユーザーの回答、[BUG-222](../../bugs/BUG-222.md) 67 行）。どちらの driver（NCM か ECM）が付いたかは実機で未確認（`src/drivers/usb/usb.c` は configuration ごとの match の点で選び、NCM 100 > ECM 80: BUG-222 q780 の P1 の読み）。
- 完了の条件の読み替え（[ws.md](../ws.md) の 2026-10-13 Q1 の記録、review S8）: 対象は USB の有線 LAN（CDC ECM・NCM）。QEMU では MTU の設定の口と回帰だけ。大きな frame の受け入れは実機（Pi 4・5330 に挿した USB LAN）だけ。5320 は対象の実機に含めない（**ユーザーの確認待ち**、§9 Q11）。断片化はしない（今のまま）。IPv4 の Path MTU discovery は §3.3 の緩和策（gateway 経由は 1460）を入れ、ICMP の読みは Future Work（§9 Q4）。
- WS220 との境（[ws220/ws.md](../../ws220/ws.md) 末尾の 2026-10-13 Q1 の 2 つの記録）: MSS は interface の MTU − 40/−60 から決め、packet の slot の大きさでも抑える。`tcp_device_mss` の持ち主は WS220 p006（WS221 p005 はそれを使う）。WS221 p005 は WS220 p006・p007 の後。
- zedBSD が扱う USB の Ethernet の driver は CDC ECM（`usb-cdc-ecm.c`）と CDC NCM（`usb-cdc-ncm.c` の codec と `usb-cdc-ncm-net.c` の binding）の **2 つだけ**。vendor 固有の driver（Realtek の vendor mode、ASIX、Microchip LAN78xx）は無い（`src/drivers/usb/` の一覧、`git grep -il "ax88\|rtl8152\|rtl8153\|lan78\|smsc95" src/drivers include` は CDC の file と `usb-rtl8822bu.c`（WiFi）だけ）。従って USB LAN の chip は **CDC ECM か NCM の configuration を出す物だけ**が動き、jumbo の上限は chip の表の値ではなく、その configuration の Ethernet Networking Functional Descriptor の `wMaxSegmentSize` と、NCM なら NTB parameter で決まる。
- RTL8822BU の WiFi は対象外（802.11 の上限。`usb-rtl8822bu.c` の `RTL8822BU_MTU`、変えない）。
- HAL（`include/hal/hal.h`・`src/hal/`）は変えない。

## 1. 今の実装の記録（source の事実、`ecc1ef489`）

### 1.1 packet の pool（`include/kern/net/packet-buf.h`、`src/kern/net/packet-buf.c`）

| 項目 | 事実（file:行） |
| --- | --- |
| slot の大きさ | `PACKET_BUF_STORAGE_SIZE` = 2048（packet-buf.h:29）。slot は `struct packet_slot { packet; storage[2048]; used }`（packet-buf.c:33〜37）、1 つの静的な配列 `packet_pool[PACKET_BUF_POOL_COUNT]`（`.net_bss`） |
| 数 | amd64・arm64 で 256、他で 32（packet-buf.h:24〜28、BUG-222 q786 で 32 → 256） |
| alloc | `packet_buf_alloc(headroom)` は headroom ≤ 2048 を確かめ（packet-buf.c:91）、最初の空き slot を線形に探し `capacity = 2048`。大きさの引数は無い |
| free | pointer で slot を線形に探す。lock は IRQ を止める spin（driver が割込みの文脈で free する） |
| copy | `packet_buf_copy`・`packet_buf_copy_region` は `alloc(headroom)` → `append(length)`: 元が 2048 を超える前提は無い |
| 使用量 | `struct packet_buf_pool_usage { total; used; used_max; alloc_failures }`（packet-buf.h:40〜45）。`tcp.c:775〜778` が net.tcpstats の header に写す。`src/kern/resource.c:85` の `packet_buf_in_use()` は資源の snapshot（漏れの検出） |
| 大きさに依存する他の場所 | `src/kern/syscall.c:1845`（SOCK_STREAM 以外の sendto）、`:1961〜1962`（recvfrom の staging の capacity）、`:2075`（sendmsg）、`:2245〜2246`（recvmsg の staging）、`src/kern/net/unix-socket.c:1850`（AF_UNIX の datagram の上限）。staging は `kern_malloc` で、`KERNEL_LARGE_THRESHOLD`（2 page = 8 KB、`src/kern/entry.c:55`）を超えると page の確保になる |
| weak の hook | `hal_irq_disable`・`hal_irq_enable`・`net_device_ref`・`net_device_release` が weak（packet-buf.c:29〜32）で host 試験が link できる |
| 配置 | `.net_bss` は `platform/amd64/vmunix.ld`・`platform/arm64/vmunix.ld` の NOLOAD の `.bss` の入力の一覧に合わず orphan の PROGBITS（review L5 の確認: amd64 は `.data` と `.bss` の間の 0x8e000 byte、data の PT_LOAD に入る。arm64 は `.vfs_bss`（5 MB）・`.kernel_heap` も同じ orphan） |

9000 byte の frame（14 + 9000 = 9014、headroom 64 を足して 9078）は今の slot（2048）に入らない。**pool の変更が最初の前提**。

### 1.2 net-device と MTU の口

| 項目 | 事実 |
| --- | --- |
| MTU の持ち方 | `struct net_device.mtu`（unsigned、net-device.h:58）。driver が create の前に入れ、`net_device_create` は 0 を拒む（net-device.c:154）。**上限を持つ欄は無い**。`net_device_ops`（net-device.h:34〜50: open・close・transmit・poll_receive・ioctl・release）に MTU の callback は無い |
| open と ioctl の admission | `net_device_open`（net-device.c:852〜906）は `device_lock` の下で `opening`・`closing` を見て `opening = 1` を立て、lock の外で `ops->open`、戻って `open_count = 1`。`net_device_ioctl`（1083〜1113）は `opening`・`closing`・`registry_stopping` を見て `ioctl_active++`、lock の外で `ops->ioctl`。finalizer の条件（1329〜1337）と `device_wait_callbacks`（1425〜1436）は `ioctl_active`・`poll_active`・`opening`・`closing` を join する。**open は `ioctl_active` を見ない**（review S1 の根拠） |
| receive | `net_device_receive`（1155〜1204）: live なら `net_input_enqueue`、失敗は `rx_dropped`（queue が満杯の意味）。長さの検査は無い。loopback（core.c:450）・WiFi（`wifi/wlan.c:1082・1433`）も通る |
| event | `device_post_event`（1535〜1549）は add・remove で `KERN_SYSTEM_EVENT_NETWORK` の event を `ifindex=%u mtu=%u` の detail で出す。action は `KERN_SYSTEM_EVENT_ADD`/`REMOVE`/`CHANGE`（`include/uapi/system.h:332〜334`）。MTU の変更の event は無い（変える口が無い） |
| ioctl | `SIOCGIFMTU`（`include/uapi/netif.h:77`、inet-socket.c:673）だけ。**`SIOCSIFMTU` は無い**（`0x8922` は未使用）。`struct ifreq` の `ifr_mtu`（netif.h:56、int）。書く ioctl は superuser（`inet_ioctl_is_query` の allow-list 1320〜1344、それ以外は `inet_ioctl_caller_is_superuser` 593〜594）。`SIOCSIFFLAGS`（653〜664）は flag を見てから `net_device_open`/`close` を呼ぶ check-then-act。IPv6 の ioctl（`SIOCSIFINET6` netif.h:135）は `src/kern/net/ipv6-ioctl.c:44・93` |
| 各 driver の値 | ECM 1500（usb-cdc-ecm.c:46、1861）、NCM 1500（usb-cdc-ncm.h:18、usb-cdc-ncm-net.c:2098 付近）、GENET 1500、ne2000・lgy98 1500、WiFi は各自、lo0 65535（core.c:468） |
| ifconfig | `userland/base/ifconfig/main.c`: 動詞は `up`・`down`・`inet`・`ipv6`・`inet6`・`-inet6`・`broadcast`（92〜）。表示は `SIOCGIFMTU` で `mtu %d`。**`mtu N` は無い** |
| Settings | 表示だけ（`userland/desktop/settings/page-network.c` が `link->mtu` を描く）。値は libkeiland-backend-zedbsd が `SIOCGIFMTU` で読む（`network-link-zedbsd.c:316〜319`）→ compositor の `kl_system_network_v1` の `link` event |
| route socket | `RTM_IFINFO` に MTU の欄は無い |

### 1.3 IPv4・IPv6・UDP・ICMP・ARP・ND

| 層 | 事実 |
| --- | --- |
| IPv4 の送り | `ipv4_output` は `packet->length > device->mtu − 20` を EMSGSIZE。**断片化はしない**。DF は常に立つ。route は `struct net_route { network; netmask; gateway; device; flags }`（`include/kern/net/route.h:19〜25`）で、gateway があればそこへ（ipv4.c:300〜308）。TCP も `tcp_route`（tcp.c:1114〜）で `route_lookup_ref` を使うので **on-link か gateway 経由かは分かる** |
| IPv4 の受け | 断片は捨てる。長さは packet の長さまで何でも受ける |
| ICMPv4 | echo だけ（icmp.c:670〜）。**Destination unreachable（type 3）は読まない**（`git grep UNREACH src/kern/net/icmp.c` は空）: IPv4 の Path MTU discovery は無い |
| IPv6 の送り | `ipv6_output` は `icmp6_path_mtu(destination, link.mtu)`（ipv6.c:294）と比べ EMSGSIZE。`ipv6_route_source(device, destination, source, *mtu)`（ipv6.c:360〜419）は **path MTU を返す口を既に持つ**（UDP が udp.c:797 で使う） |
| path MTU の表 | `icmp6_path_mtu`（icmp6.c:290〜316、link の MTU と覚えた値の小さい方、期限つき）、`icmp6_path_mtu_purge`（321〜331、全部）。Packet Too Big の handler（385〜）は 1280 を下限に表へ |
| IPv6 の link MTU | interface ごとの `link.mtu` は作る時 `device->mtu`（ipv6.c:1629）、`enabled = 1` が既定（1627）。RA の MTU option は `1280 ≤ 値 ≤ device->mtu` の時だけ採る（604〜605、RA の値そのものは保たない）。**device の MTU が後で変わっても追わない** |
| IPv6 の受け | 断片 header は捨てる |
| UDP | IPv4: `length > mtu − 28` を EMSGSIZE（udp.c:683）、IPv6: path MTU で（809）。packet は `packet_buf_alloc(64)` + append（689・815）、失敗はどちらも ENOBUFS（691・699: review L7） |
| raw ICMP・ICMPv6 の echo | icmp.c:342・833、icmp6.c:259・347（alloc + append）。ICMPv4 の echo の応答は受けた packet をその場で書き換える（alloc しない）。raw の配送は `packet_buf_copy_region`（icmp.c:192・628） |
| Ethernet・packet socket | `ethernet_output` は payload > mtu を拒む。packet socket の送りは `mtu + 14` まで（packet-socket.c:258）、配送は `packet_buf_copy`（142） |
| datagram socket の受けの queue | 8 message（`SOCKET_RECEIVE_MESSAGES_MAX`、socket.h:44）、byte は `receive_hiwat_bytes`（既定 `SOCKET_BUFFER_DEFAULT`、socket.c:117、上限 1 MB socket.h:47）。満杯は ENOBUFS で落とす（socket.c:643〜652） |

### 1.4 TCP の MSS（WS220 との境）

- 送る segment は常に `TCP_MSS` = 1024（tcp.c:54、2504〜2505）。再送の経路は `uint8_t payload[TCP_MSS]` を stack に持つ（tcp.c:467）。
- SYN・SYN-ACK の MSS option は定数 `TCP_RECEIVE_MSS` 1460・IPv6 1440（tcp.c:62・65、1396〜1400）。相手の MSS option は読まない。
- 受けの window は「残り byte と残り packet 数 × 1460 の小さい方」（tcp.c:2726〜2733）。receive queue の packet の上限 44（`TCP_RECEIVE_PACKETS`、tcp.c:94、tcp.c:310 で socket に）、reorder は 64-bit で 44・32-bit で 16（tcp-socket.h:52〜55）。送りの写しは接続ごとに 8（`CONFIG_TCP_SEND_QUEUE_MAX`、tcp-socket.h:36〜39）。
- WS220 の計画: p006 で「segment は min(相手の MSS, device の MTU − 40)、byte の送り buffer」、p007 で「受けの queue を byte で数える、window scale」（[ws220-p001 §4](../../ws220/phase001/phase.md) 154〜155 行）。WS220 p002（正しさ）は実装済みで T1 の試験待ち。

### 1.5 CDC ECM の driver（`src/drivers/usb/usb-cdc-ecm.c`）

| 項目 | 事実 |
| --- | --- |
| frame の上限 | `ECM_MTU` 1500、`ECM_FRAME_SIZE` 1514（46〜47）の定数 |
| descriptor | Ethernet Networking Functional Descriptor の `wMaxSegmentSize`（offset 8）を読み、**1514 未満なら binding を断る**（383〜388）。値は `binding->max_segment_size`（73、597）に保ち、attach の log に `segment=%u`（1970）と出すだけで、frame の大きさには使わない |
| buffer | `rx_buffer`・`tx_buffer` は `kern_malloc(ECM_FRAME_SIZE)`（1832〜1833）。1 つずつ、attach の時 |
| 受け | 受信の URB 1 本、長さ `ECM_FRAME_SIZE` で arm（open 1046〜1047、再 arm 1350）。再 arm の失敗は 3 回まで retry（`ECM_REARM_RETRY_MAX` 57、1376）、それ以上は quarantine（carrier を落とす）。完了で `length > ECM_FRAME_SIZE` を EMSGSIZE（1624〜1627、`rx_errors`）、`packet_buf_alloc(0)` + append + memcpy（1630〜1640） |
| 送り | `length > ECM_FRAME_SIZE` を EMSGSIZE（1101）。bulk-OUT の max packet（`bulk_out_max_packet_size` 92、1921〜1922）の倍数なら ZLP（1177）。queue 8（`ECM_TX_QUEUE_MAX` 54） |
| ops・net_device | open・close・transmit・poll_receive・release。ioctl は無い。`mtu = ECM_MTU`（1861） |

### 1.6 CDC NCM の driver（`include/drivers/usb/usb-cdc-ncm.h`、`usb-cdc-ncm.c` の codec、`usb-cdc-ncm-net.c` の binding）

| 項目 | 事実 |
| --- | --- |
| datagram の上限 | `DRV_USB_CDC_NCM_MAX_DATAGRAM_SIZE` = 14 + 1500 = 1514（usb-cdc-ncm.h:18〜20）。codec の `profile_valid` は `max_datagram_size != 1514` を EINVAL、`negotiate` は candidate に 1514 を固定、`build_ntb16` は frame > max_datagram_size を拒む、binding の受け `ncm_queue_datagram` は `length > 1514` を EINVAL（usb-cdc-ncm-net.c:1354〜1358）、alloc は `packet_buf_alloc(0)`（1361）、queue 32 |
| descriptor | `wMaxSegmentSize < 1514` なら binding を断る（usb-cdc-ncm-net.c:407〜415）。**値は保たない**（attach の log にも出ない: review S8） |
| NTB parameter | `GET_NTB_PARAMETERS` から in/out の最大・divisor・remainder・alignment・out の最大 datagram 数を読む。device の値と driver の限度の小さい方、NTH16 なので ≤ 65535 に clamp。1 つの max datagram の layout が in・out の NTB に入ることを確かめる |
| driver の限度 | 受けの NTB `NCM_NTB_IN_SIZE` 16384（pool 256 以上）/ 8192（47・57・60）、受けの frame の queue 32 / 8、送りの NTB 8192、送りは 1 NTB に 1 datagram（2195〜2197） |
| device への設定 | `ncm_program_profile`（716〜）: `SELECT_NTH16`、`SET_INPUT_SIZE`、**`SET_MAX_DATAGRAM_SIZE` = 1514**（`bmNetworkCapabilities` の bit 3 `NCM_CAP_MAX_DATAGRAM_SIZE` 0x08（80）を持つ device にだけ、726〜737）、CRC off。request の番号 `GET_MAX_DATAGRAM_SIZE` 0x87・`SET_` 0x88（usb-cdc-ncm.h:42〜43） |
| buffer | `rx_buffer = kern_malloc(ntb_in_max_size)`、`tx_buffer = kern_malloc(ntb_out_max_size)`（2069〜2070） |
| callback の約束 | 「NTB 全体を先に検査してから deliver を呼ぶ。callback の失敗は、既に渡した datagram の後の delivery を止める」（usb-cdc-ncm.h:131〜138）。`ncm_queue_datagram` は ENOBUFS・EMSGSIZE・EINVAL を返す（1354〜1384） |
| host 試験 | codec の試験 `plan/ws004/tests/usb-cdc-ncm-wire-test.c`（`run-usb-cdc-ncm-wire-test.sh`） |

### 1.7 USB の host controller（xHCI）と転送の大きさ

- `drv_usb_urb_setup` に大きさの固定の上限は無い。xHCI は TD を 64 KiB の境界で TRB に分け chain する。review が確かめた点: bulk は submit ごとの coherent の bounce か reservation（`pci-xhci.c:3640〜3750`）、64 KiB の条件（4236〜4247）は control transfer の経路。本設計の NTB は最大 32 KB。NCM の 32 KB の受けの URB は submit ごとに 32 KB の coherent DMA を取る（review L10、Pi 4 での失敗・遅さは**未確認**）。
- `DRV_USB_URB_RECLAIM_SAFE_MAX_SIZE` 8192 は flag の時だけで ECM・NCM は使っていない。

### 1.8 GENET（対象外、事実の記録）

`GENET_MAX_FRAME` の register に 1536、受けの DMA の slot は 2048 byte で `GENET_MAX_PACKET` 1514（rpi4-ethernet.c:84〜86）を超える frame は捨てる。MTU は定数で frame の大きさに使っていない（下げるのは IP の層だけで可: M1）。混在の試験で Pi 4 の GENET の側は 1500 の相手として振る舞う。

### 1.9 設定の保存と適用（net.conf・net・networkd・Settings）

- `/etc/net.conf` v1（`userland/base/net/netconf.c`）: `version: 1` が必須（115・336・1040〜1046）。interface の key は `type`・`enabled`・`parent`・`vlan-id`・`ipv4`・`members`・`ipv6`。**知らない key は file 全体の parse の失敗**（1381 `unknown interface key`）。`struct netconf_interface`（netconf.h:85〜102）に MTU の欄は無い。
  読み手（全部 `netconf_load`）: `net`（main.c:296・2130・2703・2894）、networkd（`networkd/main.c:9750`、`networkd/ipv6.c:613`、`lan-configure.c` が編集して書き戻す）、compositor の backend（`libkeiland-backend-zedbsd/network-link-zedbsd.c:455`）、`userland/tests/monitor-probe`。
- `net` の console の commit は `reconcile.c` が previous と target から op を emit する（`emit_interface` 176〜214: `DOWN` / `UP` → `emit_ipv6` → `DHCP` か `STATIC`）。op の名前は `net/main.c:805〜820` の表で opcode に（`NETWORKD_OP_*`、`userland/base/net/protocol.h:56〜163`: 1〜20 が有線、32〜41 WiFi、48〜50 LAN、64 subscribe、80〜81 sleep。**21 は空き**。field は 184〜198、**24 は空き**）。
- **起動は reconcile を通らない**（review S6）: `net startup`（`startup_command`）は `lan_send_policy`（main.c:2116〜2204）で interface ごとに text の record `"IF disabled"`・`"IF dhcp N"`・`"IF static A M"` を `NETWORKD_FIELD_OUTPUT` に並べ `NETWORKD_OP_LAN_ENABLE` で送る。address も dhcp も無い enabled の interface は record を送らない（2178〜2184「whoever writes one」）。lo0 は `net` が直に設定する。
- networkd は `lan_policy_decode`（`networkd/main.c:1421〜1509`、record 128 byte、`word[5]`、dhcp は 3 語・static は 4 語・disabled は 2 語、それ以外は **EINVAL で policy 全体を拒む**）で `struct networkd_lan_policy { interface; mode; address; netmask; dhcp_timeout }`（managed-lan.h:43〜49）にし、`networkd_lan_set_policy`（3863〜3865）に渡す。cable の抜き差しの構成 `lan_configure_steps`（1767〜1856）は `ifconfig IF up` → static か `dhcpc`。
- 管理者の `NETWORKD_OP_DOWN` は `lan_hold_down`（4056〜4080、BUG-293: address を外し policy が上げ直さない）、`UP` は `lan_release`（policy が構成し直す）。有線の変更の op は confirmed transaction と serialize（3937〜3940: opcode が `UP`〜`STATIC6_REMOVE`（`RELOAD` を除く）か `ROUTE6_REMOVE`）。構成中の interface を止める op の一覧（5672〜5676: UP・DOWN・DHCP・STATIC・LAN_CONFIGURE）。
- Settings の LAN の設定: `net lan set IF dhcp|static ...`（net/main.c:2391〜2472、field は INTERFACE・ADDRESS・NETMASK・GATEWAY・DNS）→ `NETWORKD_OP_LAN_CONFIGURE` → `networkd_lan_configure_edit`（`lan-configure.c:155〜262`: net.conf の item を編集。既存の item は type・enabled・dhcp・addresses だけを書き換え、他の欄は保つ）と `lan_configure_policy`（`networkd/main.c:9788〜9810`: request から policy を作り `networkd_lan_set_interface`）。
- dhcpc は DHCP の option 26（Interface MTU）を読まない。

### 1.10 QEMU の試験の網

- QEMU の網は全部 `usb-net`（CDC ECM、`plan/tools/guest/guest.py`: slirp の ue0。`plan/ws220/tests/tcp-stream-series.sh` は tap の ue1 を足す）。virtio-net の driver は無い（WS220 の 2026-10-13 ユーザーの決定: usb-net のまま）。
- QEMU の usb-net の `wMaxSegmentSize` は **1514**（[ws033/notes.md](../../ws033/notes.md) の attach の log `segment=1514`）。`-device usb-net,help`（QEMU 10.0.11）に segment の大きさの property は無い。→ QEMU の usb-net では 1514 を超える frame を guest に入れられない。QEMU で確かめられるのは MTU の口（拒否・保存・再起動）、TCP の MSS の導出、pool の変更の回帰まで。
- 代替（review M9、§8.4）: この host の Debian 13（6.12.101）には `dummy_hcd.ko.xz`・`usb_f_ncm.ko.xz`・`usb_f_ecm.ko.xz` がある（`/lib/modules/6.12.101+deb13-amd64/kernel/drivers/usb/gadget/`、2026-10-13 P2 が `ls` で確認、load はしていない）。

## 2. 責務と境界

| 層 | 責務 | 変える物 |
| --- | --- | --- |
| packet の pool | 2 つの大きさの class（標準 2 KB、jumbo 9 KB）、要る大きさで alloc、driver の受けの予約 | `packet-buf.h`・`packet-buf.c`、`syscall.c`・`socket.c`（datagram の上限、socket ごとの jumbo の上限） |
| net-device | `mtu_max`・`mtu_default`、`net_device_set_mtu`（up でも可）、driver への `set_mtu`、`net_device_frame_max`、receive の MTU 超えの検査、MTU の変更の system event | `net-device.h`・`net-device.c` |
| UAPI | `SIOCSIFMTU`（superuser、`ifr_mtu`、0 = 既定に戻す）、net.tcpstats の header の jumbo の欄 | `include/uapi/netif.h`、`include/uapi/sysctl.h`（WS220 の所有: Q1 経由）、`inet-socket.c`（handler から IPv6 の追従を呼ぶ） |
| IPv4・IPv6・UDP・ICMP・packet socket | IPv4 は今のまま。IPv6 は `ra_mtu` を持ち device の MTU の変更に追従。packet の alloc を「要る大きさ」に | `ipv6.c`・`udp.c`・`icmp.c`・`icmp6.c`・`packet-socket.c` |
| TCP | `tcp_device_mss`（WS220 p006 が作る）を SYN で広告、送る segment は min(相手の MSS, 自分の MSS, path MTU − hdr)、gateway 経由の IPv4 は 1460 まで。受けの上限を jumbo の数で | `tcp.c`（WS221 p005、WS220 p006・p007 の後） |
| CDC ECM | descriptor の `wMaxSegmentSize` から上限、buffer・URB を上限で（MTU に依らない）、`set_mtu`、log | `usb-cdc-ecm.c` |
| CDC NCM | codec の max datagram を可変に、NTB を jumbo が入る値に、`SET_MAX_DATAGRAM_SIZE` に negotiate の値と読み戻し、上限の導出、`set_mtu`、log | `usb-cdc-ncm.h`・`usb-cdc-ncm.c`・`usb-cdc-ncm-net.c` |
| ifconfig・ping | `ifconfig IF mtu N|default`、`ping -s SIZE` と sendto の失敗の表示（p002） | `ifconfig/main.c`・`ping/main.c` |
| net.conf・net・networkd | interface の `mtu` の key、console の `mtu N`・`no mtu`、reconcile の `MTU`、`LAN_ENABLE` の record の `mtu`、`NETWORKD_OP_MTU`、policy の `mtu`、`lan_configure_steps` の順、`LAN_CONFIGURE` の経路 | `netconf.[ch]`・`net/main.c`・`reconcile.c`・`protocol.h`・`networkd/main.c`・`managed-lan.[ch]`・`lan-configure.c` |
| Settings | 表示だけ（今のまま）。MTU の変更の event で表示が更新される | 無し |
| HAL | **変更なし** | — |

GENET・WiFi・ne2000・lgy98 は `mtu_max` を入れないので `net_device_create` が `mtu_max = mtu` にする。**下げる**変更（1280 以上）は全 device で通る（IP の層だけが使う。driver は frame の大きさを定数で持つ: M1）。上げるのは `mtu_max` まで。

## 3. 設計

### 3.1 packet の pool: 2 つの class と予約

```c
/* include/kern/net/packet-buf.h（案） */
#define PACKET_BUF_STORAGE_SIZE		2048U		/* 標準の class（今のまま） */
#if defined(HAL_ARCH_AMD64) || defined(HAL_ARCH_ARM64)
#define PACKET_BUF_POOL_COUNT		256U
#define PACKET_BUF_JUMBO_STORAGE_SIZE	9216U		/* 64 + 14 + 9000 = 9078 を 1 KB 単位に */
#define PACKET_BUF_JUMBO_COUNT		32U		/* §9 Q2 */
#define PACKET_BUF_JUMBO_RESERVE	8U		/* driver の受けだけが使える残り（§9 Q13） */
#else
#define PACKET_BUF_POOL_COUNT		32U
#define PACKET_BUF_JUMBO_STORAGE_SIZE	PACKET_BUF_STORAGE_SIZE
#define PACKET_BUF_JUMBO_COUNT		0U		/* 32-bit の board は jumbo を持たない */
#define PACKET_BUF_JUMBO_RESERVE	0U
#endif
#define PACKET_BUF_MAX_STORAGE_SIZE	PACKET_BUF_JUMBO_STORAGE_SIZE
#define NET_MTU_MIN			1280U		/* §9 Q3 */
#define NET_MTU_MAX			(PACKET_BUF_JUMBO_COUNT != 0U ? 9000U : 1500U)

#define PACKET_BUF_ALLOC_RECEIVE	0x1U		/* driver の受け: 予約を使える */

struct packet_buf *packet_buf_alloc(size_t headroom);				/* 今のまま: 標準の class */
struct packet_buf *packet_buf_alloc_for(size_t headroom, size_t length, unsigned flags, int *error);
```

- slot の配列を 2 つ（`packet_pool[256]`、`packet_jumbo_pool[32]`、後者は storage 9216）にする。`struct packet_buf` に欄は足さず、free は **pointer の範囲**（どちらの配列の中か）で配列を選ぶ（review L8: memset 済みの packet や壊れた値に強い）。`capacity` は既に packet ごとなので push・pull・append・tailroom・trim・各層の header の処理は**変更なし**。
- `packet_buf_alloc_for(headroom, length, flags, error)`: `headroom + length ≤ 2048` なら標準、それ以外で `≤ 9216` なら jumbo。jumbo の空きが `PACKET_BUF_JUMBO_RESERVE` 以下の時、`PACKET_BUF_ALLOC_RECEIVE` の無い呼び手（UDP・raw・packet socket の送り、copy）は NULL（**予約**: S7 (a)）。`*error` は EMSGSIZE（どの class にも入らない）か ENOBUFS（尽きた）（review L7）。標準が尽きた時に jumbo へ**逃げない**（逆も無し）。`alloc_failures` は class ごとに数える。
- socket ごとの上限（S7 (b)）: `struct socket` に `receive_jumbo` の数を持ち、`socket_enqueue`（socket.c:643〜）が jumbo の packet を `SOCKET_RECEIVE_JUMBO_MAX`（2）を超えて積まない（ENOBUFS、driver の `rx_dropped` と同じ扱い）。TCP の受けは p005 で window の計算に入れる（§3.4）。送りの写し（接続ごと 8）は WS220 p006 の byte の buffer で無くなる。
- lo0（S7 (d)）: `net_device_frame_max(device)` = loopback なら `PACKET_BUF_STORAGE_SIZE − PACKET_BUF_DEFAULT_HEADROOM`（1984）、それ以外は `min(14 + device->mtu, PACKET_BUF_MAX_STORAGE_SIZE − PACKET_BUF_DEFAULT_HEADROOM)`。UDP・raw・packet socket の送りは `length + hdr > net_device_frame_max − 14` を EMSGSIZE にしてから `alloc_for`。lo0 の UDP の上限は今（1984 付近で alloc が失敗して ENOBUFS）と実質同じで、誤りが EMSGSIZE になる。
- `packet_buf_copy`・`packet_buf_copy_region` は元の長さで `alloc_for`（flags 0: 予約は使わない。packet socket の配送と raw の配送が失敗しても driver の受けは続く）。
- 使う側の alloc の呼び方: ECM・NCM の受けは `alloc_for(0, length, PACKET_BUF_ALLOC_RECEIVE, &error)`。UDP・raw ICMP・ICMPv6 の echo の応答（icmp6.c:259・347）・packet socket の送りは `alloc_for(64, length, 0, &error)`。ARP・ND・MLD・route socket・TCP（p005 まで）は `alloc` のまま。
- datagram の大きさの上限（S2・L9）: `socket_datagram_max(socket)` を socket.c に足し、AF_UNIX は 2048、それ以外は `PACKET_BUF_MAX_STORAGE_SIZE`。`syscall.c` の 4 か所（1845 sendto・2075 sendmsg は EMSGSIZE の判定、1961 recvfrom・2245 recvmsg は staging の capacity）が全部これを使う。`unix-socket.c:1850` は AF_UNIX 自身の上限のまま。recv の staging は要求が 2048 以下なら今まで通り（AF_UNIX の syslog などは変わらない）、inet の datagram で 2048 を超える要求だけ 9216 の `kern_malloc`（`KERNEL_LARGE_THRESHOLD` 8 KB を超えるので page の確保になる。塞がって待つ間ずっと持つ。**記録**: 2048 を超える buffer で recv する app は今は ping（`-s` の後）だけ。遅ければ p002 で staging を使わず packet から直に copyout する改善を別に考える）。
- `packet_buf_pool_usage` に jumbo の `jumbo_total`・`jumbo_used`・`jumbo_used_max`・`jumbo_reserve`・`jumbo_alloc_failures` を足す。`packet_buf_in_use()`（resource.c:85）は両方の合計。
- net.tcpstats（M6、UAPI は WS220 の所有）: `struct tcp_stats_header` を 48 → **72 byte**（`uint32_t jumbo_total, jumbo_used, jumbo_used_max, jumbo_reserve; uint64_t jumbo_alloc_failures;` を末尾に）。`version` は 1 のまま、読み手（`sysctl/main.c:783・802`、`tcp-model/model.c:1036`）は `struct_size` で entry の位置を取るので古い読み手も壊れない。`_Static_assert`（sysctl.h:443）、`uapi-abi-layout-check`（Makefile:655）、sysctl の表示の行、tcp-model の表示（972〜977）を同時に直す。**順は Q1**: WS220 p002 が test-wait なので、その merge の後に WS221 p002 が触る（または WS220 の次の Phase に相乗り）。
- 配置: 静的な配列（`.net_bss`）のまま。jumbo 32 × 9216 ≈ 295 KB。`.net_bss` が orphan の PROGBITS なので image が約 300 KB 増える（kernel の上限 16 MiB の内。review L5: NOLOAD に移しても `__kernel_phys_end` で数える 16 MiB の予算は減らず、file の大きさだけ減る）。Q10 は任意のまま。
- 選ばなかった案: (a) 全 slot を 9 KB（256 × 9 KB = 2.3 MB、小さい packet の無駄） (b) scatter/chain（stack 全体が `packet->data` の連続を前提） (c) `kern_malloc` で都度（割込みの文脈の free、heap の断片化） (d) 予約の代わりに jumbo を 64 に（数を増やしても socket が溜める物は溜まる。上限の方針が先）。

### 3.2 net-device: MTU の上限・既定と変更

```c
/* include/kern/net/net-device.h（案） */
struct net_device_ops {
	...
	void (*release)(void *driver_data);
	/*
	 * Optional.  Thread context, admitted like ioctl (joined by close,
	 * gone and shutdown_all), with or without the device open.  The driver
	 * checks the value against what the hardware takes and returns 0 or an
	 * errno (EBUSY when it needs the device closed first).  device->mtu
	 * still holds the old value when this is called; nothing the driver
	 * sized at open may depend on device->mtu.
	 */
	int (*set_mtu)(struct net_device *device, unsigned mtu);
};
struct net_device {
	...
	unsigned mtu;
	unsigned mtu_max;	/* the most the driver takes; 0 at create means "mtu" (cannot grow) */
	unsigned mtu_default;	/* the value at create; SIOCSIFMTU 0 restores it */
	...
};
int net_device_set_mtu(struct net_device *device, unsigned mtu);	/* 0 = mtu_default */
size_t net_device_frame_max(const struct net_device *device);	/* §3.1 */
```

- 全 driver は designated initializer なので ops の末尾に欄を足しても変更は要らない。`net_device_create` は `mtu_max == 0` なら `mtu_max = mtu`、`mtu_default = mtu`。
- `net_device_set_mtu(device, mtu)`（net-device.c、thread context、S1 の直し）:
  1. `mtu == 0` なら `mtu = device->mtu_default`。`mtu < NET_MTU_MIN`（1280、M4・Q3）か `mtu > device->mtu_max` か `mtu > NET_MTU_MAX` → EINVAL。
  2. `device_lock` の下: live でなければ ENODEV、`opening`・`closing`・`registry_stopping` なら EBUSY（ioctl と同じ）。`mtu == device->mtu` なら 0（冪等。cable の抜き差しで networkd が同じ値を毎回入れる）。`ioctl_active++`（ioctl と同じ admission: finalizer と `device_wait_callbacks` が既に join する欄を使うので、gone・shutdown_all・close の変更は要らない）。
  3. lock の外で `ops->set_mtu(device, mtu)`（無ければ 0）。
  4. `device_lock` の下で `ioctl_active--`、成功なら `device->mtu = mtu`（**lock の下の word の書き込み**。送りの経路の読み手は lock 無しで読む: 新旧どちらの値で検査しても frame は driver の `frame_max` 以下なので安全。S1 の「open が古い MTU で URB を決める」は、S5・§3.6 で driver の buffer・URB が MTU に依存しなくなるので起きない）。
  5. `device_post_event(device, ifindex, KERN_SYSTEM_EVENT_CHANGE)`（L3: Settings の backend が `SIOCGIFMTU` を読み直す契機。backend 側の購読は WS221 の外、既に event を受けていれば表示が更新される）と `kern_log`。
- **up のまま変えられる**（Q1 の推奨を第 1 版から変えた）。理由: (1) S5 の直しで ECM・NCM の受信 URB・buffer は `frame_max`（attach の時の値）で決まり MTU に依らない、(2) GENET・WiFi・ne2000 は frame の大きさを定数で持つ（下げるだけ）、(3) down を要求すると networkd の `DOWN` が address を外し全接続（SSH）が落ちる（M10）。down が要る driver が将来できたら `set_mtu` が EBUSY を返す（ifconfig は「bring the interface down first」）。代案 B（down の時だけ、`mtu_changing` の印を `device_lock` の下で立て `net_device_open` がその間 EBUSY）は review S1 の通り作れる。選ばない理由は M10。
- `SIOCSIFMTU`（`include/uapi/netif.h`: `#define SIOCSIFMTU 0x00008922UL`、`ifr_mtu`、int）。`inet-socket.c` の switch に足し（query の allow-list には入れない → superuser）、`ifr_mtu < 0` は EINVAL、0 は既定に戻す（M5・Q8）。handler が `net_device_set_mtu` の成功の後に `ipv6_device_mtu_changed(device)` を呼ぶ（L11: net-device.c は IPv6 を呼ばない）。AF_INET6 の socket の ioctl の経路（`ipv6-ioctl.c`）が `SIOCGIFMTU` を共有しているかは p002 で確かめ、同じ所に足す。
- `net_device_receive`（M11）: loopback でない device で `packet->length > 14 + device->mtu` なら落として `rx_errors++`（`rx_dropped` は queue が満杯の意味なので分ける）。WiFi は `wlan-l2.c:197` が A-MSDU を拒むので 1500 を超える MSDU は来ない（**推測**。p002 で `wlan-l2.c` の frame の長さの上限を読んで確かめる。超える物があれば検査を Ethernet の device（`NET_DEVICE_*` の種類の flag、無ければ ops の有無）に限る）。
- 通知: route socket の `RTM_IFINFO` には足さない（system event で足りる。要れば後で）。

### 3.3 IP の層

- IPv4: 送りの検査（`device->mtu`）はそのまま、DF は常に立つ、断片化・ICMP の読みは無し。**制限と緩和（S4）**: MTU 9000 の zedBSD から 1500 の経路（別 subnet の router の向こう）へ 1500 を超える IPv4 の datagram は黙って落ち、ICMP の Fragmentation needed を読まないので分からない。緩和: TCP は gateway 経由の宛先に対して MSS を min(1460, MTU − 40) にする（§3.4、`struct net_route.gateway != 0` で分かる）。UDP・raw は app の責任（Linux の `IP_PMTUDISC_DO` 相当が既定）。同じ switch の上の 1500 の相手へは、相手の MSS（TCP）で避け、ping・UDP の大きい物は「正しい失敗」。IPv4 の Path MTU discovery（ICMP type 3 code 4 を読み path の表を持つ）は Future Work（Q4）。
- IPv6（M3）: `struct ipv6_interface` に `ra_mtu`（0 = RA で受けていない）を足す。RA の処理（ipv6.c:604〜605）は `values->mtu ≥ 1280` なら `ra_mtu = values->mtu` に保ち（device の MTU より大きい値も保つ: device が後で上がった時に使う）、`link.mtu = ra_mtu ? min(device->mtu, ra_mtu) : device->mtu` を計算する。`ipv6_device_mtu_changed(device)` は `ipv6_lock` の下で同じ式で `link.mtu` を計算し直し、`icmp6_path_mtu_purge()`（全 path。10 分で消える物の再学習で害は無い）。RA の MTU option が 1500 を広告する link では IPv6 は 1500 で送る（RFC 4861 6.3.4 の通り）。IPv6 の jumbo の受け入れは「RA の MTU option が無い、または 9000 を広告する link」で行う。
- ARP・ND: 変更なし。

### 3.4 TCP（WS221 p005、WS220 p006・p007 の後）

所有（M7）: `tcp_device_mss()` と「送る segment の大きさ」は **WS220 p006 が作る**（ws220/ws.md 末尾の 2026-10-13 Q1 の記録）。WS221 p002 は `net_device_frame_max()` を提供し、p005 は SYN の広告と受けの上限だけを扱う。WS220 p006 への要件（Q1 経由で ws220 の p006 の phase.md に写す）:

1. `tcp_device_mss(device, family)` = min(`device->mtu` − hdr, `net_device_frame_max(device)` − 14 − hdr − 40)（hdr は IPv4 40・IPv6 60、40 は TCP の option の最大）。**slot の上限を入れる**（S3: lo0 の 65535 で segment が slot を超え、自分の機械への TCP が止まる）。WS220 p006 が WS221 p002 より先なら、`net_device_frame_max` が無い間は `PACKET_BUF_STORAGE_SIZE − PACKET_BUF_DEFAULT_HEADROOM` を使い、p002 が置き換える。
2. 送る segment の大きさ = min(相手の SYN の MSS option（無ければ 536 / 1220）, `tcp_device_mss`, **path の上限**): IPv6 は送る時に `ipv6_route_source(..., &mtu)` の path MTU − 60（S4 (a): RA で下がった link や PTB の後に 8940 の segment を作り続けて EMSGSIZE で止まらない）。PTB を受けた後の再送も byte の buffer から切り直すので同じ式で小さくなる（p006 の「再送は buffer から作る」が前提）。IPv4 は `tcp_route` の `route.gateway != 0` なら min(…, 1460)（S4 (b): router の向こうの 1500 の経路の black hole を避ける。古い BSD と同じ考え）、on-link なら device の値。
3. 定数 `TCP_MSS`・`TCP_RECEIVE_MSS`・`TCP_RECEIVE_MSS6` を広告・切り方に使わない。

WS221 p005 の範囲: SYN・SYN-ACK の MSS option を `tcp_device_mss`（route の device、listener は受けた packet の device）にする（tcp.c:1396〜1400 の定数を置き換える）。受けの window（WS220 p007 が byte で数える前提）に jumbo の pool の上限を入れる: 1 接続の受けで 44 packet × 9 KB が jumbo 32 を超える（S7）ので、window = min(byte の残り, 標準の空き × 1460 + (jumbo の空き − 予約) × MSS) の形（p007 の設計次第、p005 で確定）。任意（S7 (c)）: jumbo の空きが予約に近い時は標準の slot に入る大きさ（1984 − 14 − hdr）で切る（MSS 以下ならいつでも合法）。

### 3.5 CDC ECM

- attach: `frame_max = min(wMaxSegmentSize, 14 + NET_MTU_MAX)`（descriptor の値は既に `binding->max_segment_size`）。`device->mtu = 1500`、`device->mtu_max = frame_max − 14`。
- **受信の URB と buffer は MTU に依らない**（S5）: `rx_length = round_up(frame_max, bulk_in_max_packet) + bulk_in_max_packet`（frame_max が max packet の倍数の frame の後の ZLP を同じ URB で受ける。倍数でない frame は short packet で完了する: 今の 1514 と同じ）。`rx_buffer = kern_malloc(rx_length)`（9014・512 なら 9728、1024 なら 10240）、`tx_buffer = kern_malloc(frame_max)`。open・再 arm（1046〜1047・1350）は `rx_length` で arm。`bulk_in_max_packet_size` を `bulk_out_max_packet_size`（92）と同じく attach で取る。
  今の 1514 の arm で、device が 1514 を超える frame（descriptor が 9014 の device で LAN に 9000 の相手がいる時）を送ると babble か境の崩れが起きうる（review S5、**推測を含む**: 既存の不具合の可能性。p003 の実機で `rx_errors` を見る）。
- 検査: 受け `length < 14 || length > frame_max` → EMSGSIZE（`rx_errors`、今と同じ形）。MTU を超えるが frame_max 以下の frame は driver は通し、`net_device_receive` が落とす（§3.2）。alloc は `packet_buf_alloc_for(0, length, PACKET_BUF_ALLOC_RECEIVE, &error)`、ENOBUFS は `rx_dropped`。送りは `length > 14 + device->mtu` → EMSGSIZE（`ethernet_output` が先に落とすので二重の検査）。
- `set_mtu`: `mtu ≤ mtu_max` の検査だけ（hardware への設定は無い。ECM には max datagram の request が無い）。up でも可。
- QEMU の usb-net（1514）: `mtu_max` = 1500 → `ifconfig ue0 mtu 9000` は EINVAL、`mtu 1400` は通る（§8.2）。
- log: `segment=%u` に加えて `mtu_max=%u rx_urb=%u` を出す。

### 3.6 CDC NCM

- codec（`usb-cdc-ncm.h`・`usb-cdc-ncm.c`）: `struct drv_usb_cdc_ncm_limits` に `uint16_t max_datagram_size`（driver が望む上限）を足し、`negotiate_nth16` は candidate の `max_datagram_size` を「limits の値以下で、1 つの datagram の layout が in・out の両方の NTB（clamp 後）に入る最大」に**導出**する（`make_layout` を上限から試し、入らなければ 1514 まで下げる。1514 でも入らなければ今と同じ EMSGSIZE）。`profile_valid` は `1514 ≤ max_datagram_size ≤ 65535`。`DRV_USB_CDC_NCM_MAX_DATAGRAM_SIZE` は「最小の上限」の意味で `DRV_USB_CDC_NCM_MIN_MAX_DATAGRAM_SIZE` に改名する。
- binding: descriptor の `wMaxSegmentSize` を `binding` に保つ（今は検査だけ: 407〜415）。limits: `max_datagram_size = min(wMaxSegmentSize, 14 + NET_MTU_MAX)`。`ntb_in_max_size` を jumbo のある build で **32768**（16 KB だと 9014 の datagram は 1 つしか入らない。32 KB なら jumbo 3 つ、標準 21）、`ntb_out_max_size` を **16384**（8 KB には 9014 の NTB が入らない）、`rx_max_datagrams` 32 のまま。32-bit の build は今の値のまま。L10: 32 KB の受けの URB は submit ごとに 32 KB の coherent DMA を取る。Pi 4 での確保の失敗・遅さは未確認なので、p003 は `rx_rearm_failures` を log（と `ifconfig` の `rx_dropped`）で見える形にし、実機で問題なら 16 KB（jumbo 1 つ）に戻す。
- bit 3 の無い device（L6）: `SET_MAX_DATAGRAM_SIZE` を送れないので device の最大 datagram は `wMaxSegmentSize` のまま（CDC NCM 1.0 の SetMaxDatagramSize の節の記憶、**推測**）。codec は NTB 全体を先に検査するので、上限超えの datagram 1 つで NTB 全体を捨てる → bit 3 が無ければ codec の `max_datagram_size` は `min(wMaxSegmentSize, 65535)` まで受け（layout が入る範囲で）、MTU を超える frame は `net_device_receive` で落とす。bit 3 のある device: `SET_MAX_DATAGRAM_SIZE` に導出の値、続けて **`GET_MAX_DATAGRAM_SIZE`（0x87）で読み戻し**、device が黙って小さくした値を profile と `mtu_max` に使う。STALL なら 1514 に下げてやり直す（capability bit を立てながら拒む device への備え）。
- `device->mtu_max = profile.max_datagram_size − 14`（≤ 9000）。`rx_buffer = kern_malloc(ntb_in_max_size)` は今と同じ（MTU に依らない）。
- 受け: `ncm_queue_datagram` の検査を `length > profile.max_datagram_size`、alloc は `alloc_for(0, length, PACKET_BUF_ALLOC_RECEIVE, &error)`。ENOBUFS は「この datagram を飛ばして続ける」（`rx_dropped` を数えて 0 を返す。callback の約束 usb-cdc-ncm.h:131〜138 は「失敗で止める」なので、止めずに続けるのは binding の中で吸収する）。
- 送り: `build_ntb16` の上限が profile の値になる。`ntb_capacity = ntb_out_max_size`（16 KB）。1 NTB に 1 datagram のまま。
- `set_mtu`: `mtu ≤ mtu_max`。hardware の設定は変えない（device には negotiate の最大を既に設定してあり、それ以下の frame は常に受ける）。up でも可。
- log: attach で `segment=%u ntb_in=%u ntb_out=%u max_datagram=%u(read back %u) caps=0x%02x mtu_max=%u` を出す（**この log が descriptor の読みの手段の一つ**: §4.1）。
- host 試験: `plan/ws004/tests/usb-cdc-ncm-wire-test.c` は WS004（incomplete）の物なので触らず、`plan/ws221/tests/ncm-jumbo-wire-test.c`（同じ作り、`run-ncm-jumbo-wire-test.sh`）で (a) `dwNtbInMaxSize` 65536・32768・16384・8192 と divisor 4/remainder 0/alignment 4、divisor 4/remainder 2、divisor 1 の各組で導出の `max_datagram_size`、(b) 9014 の datagram の build と parse（3 つの jumbo を 1 NTB に）、(c) 上限超えの拒否、(d) 1514 の非退行、(e) bit 3 の無い limits（65535 まで受ける）。完了の時に回帰に残す物は master.md の Tools に。

### 3.7 ifconfig・ping・net.conf・net・networkd（S6・M5・M8・M10・L4 で書き直し）

**ifconfig（p002）**: `ifconfig IF mtu N`、`ifconfig IF mtu default`（`ifr_mtu = 0`）。EINVAL は「mtu N is outside 1280..the interface's limit」、EBUSY は「the driver needs the interface down first」、EPERM はそのまま。usage の行を直す。上限を読む UAPI（`SIOCGIFMTUMAX`）は足さない（Q6）。

**ping（p002、M2・S2）**: `-s SIZE`（ICMP の data の長さ、既定 56、上限は `PACKET_BUF_MAX_STORAGE_SIZE − 64 − 14 − 20 − 8`（9110）付近で kernel が EMSGSIZE）。`echo[64]`・`packet[2048]`（main.c:60・431〜432）を `-s` に合わせて malloc し、**受けた長さが送った長さと一致する時だけ応答と数える**（一致しなければ `truncated` と出す。S2: recvmsg の上限の漏れを試験で検出できるように）。sendto の失敗を `ping: sendto: Message too long` のように表示する（今は出さない: 246〜256・494）。IPv4 は DF が常に立つので「DF 付き」は既定。IPv6 も断片化しない。

**net.conf（p004、M8）**: interface の key `mtu: N`（省略可。無ければ driver の既定）。`struct netconf_interface` に `unsigned mtu; int mtu_set;`。validate は `1280 ≤ N ≤ 65535`（device の上限は適用の時に分かる）。writer は `mtu_set` の時だけ書く。version は 1 のまま（前例: ws130 の `ipv6` の節）。**互換の影響**: 読み手は全部 `netconf_load`（§1.9: net・networkd の main と ipv6・lan-configure・libkeiland-backend-zedbsd・monitor-probe）で、未知の key は file 全体の失敗なので、`mtu` を書いた file を**古い image に戻すと網の設定全体が読めない**（networkd は policy 無し、Settings は link の一覧だけ）。`mtu` を設定しなければ file は変わらない。記録する。

**`net` の console（p004）**: `net(config-if:NAME)> mtu N`、`no mtu`（候補から key を消す）。`show candidate`・`running-config`・`startup-config` に `mtu` を出す。`docs/reference/network-console.md` の 82〜95 の一覧に足す。

**reconcile（p004、M5・M10）**: `emit_interface` の前に、previous と target で `mtu`（`mtu_set` と値）が違う interface に `MTU IF N`（target に無ければ `MTU IF 0` = 既定に戻す）を emit する。**DOWN → UP は挟まない**（§3.2: up のまま変えられる。driver が EBUSY を返す時は networkd の答えが失敗になり `net` が報告する。接続は切れない）。disabled の interface（`DOWN` を出す物）には `MTU` を出さない。

**networkd の protocol（p004、L4）**: `NETWORKD_OP_MTU = 21`（`protocol.h:76` の 20 の次）、field `NETWORKD_FIELD_MTU = 24`（uint の text、22 PATH・23 TOKEN の次）。`net/main.c:805〜820` の表に `MTU`。networkd の handler は `/sbin/ifconfig IF mtu N`（N = 0 は `default`）を `run_command_until`（UP/DOWN の 4056〜 と同じ形）。3937〜3940 の serialization の条件（`UP`〜`STATIC6_REMOVE` の範囲の外なので `ROUTE6_REMOVE` と同じく明示に足す）。権限の分類（operation_name の表、superuser の判定）は `UP` と同じ行に。5672〜5676（構成中の interface を止める op）には**入れない**（address を決めない）。handler は成功の後に managed-lan の policy の `mtu` も書き換える（`networkd_lan_set_interface` の mtu の欄だけ）ので、次の cable の抜き差しでも同じ値になる。

**起動の経路（p004、S6）**: `lan_send_policy`（net/main.c:2116〜2204）の record に **任意の末尾 `mtu N`** を足す: `"IF dhcp 10 mtu 9000"`（5 語）、`"IF static A M mtu 9000"`（6 語）。`"IF disabled"` には付けない。`lan_policy_decode`（networkd/main.c:1421〜1509）は `word[5]` → `word[7]`、dhcp は 3 か 5 語、static は 4 か 6 語で、5/6 語目が `mtu` でないか値が `1280..65535` でなければ EINVAL（今と同じく policy 全体を拒む: 送り手は同じ system の `net`）。`struct networkd_lan_policy` に `unsigned mtu`（0 = 変えない）。record 128 byte は足りる（最長 `"ue0 static 192.168.100.100 255.255.255.0 mtu 9000"` = 48）。**制限**: address も dhcp も無い enabled の interface は今 record を送らない（2178〜2184）ので、`mtu` だけを書いた interface には起動で MTU が付かない（手で `ifconfig` する運用の interface。文書に書く。policy の外の interface に `ifconfig mtu` を打つ新しい口は足さない）。
`lan_configure_steps`（1767〜）: `ifconfig IF up` の**前**に `policy.mtu != 0` なら `ifconfig IF mtu N`（cable の抜き差しで up し直す時に毎回。同じ値なら kernel が 0 を返す。失敗は syslog して続ける: MTU が付かなくても通信はできる）。
`NETWORKD_OP_RELOAD`（`net reload`）は同じ record を送り直すので同じ経路。

**Settings の経路（p004、S6 (4)）**: `net lan set` と `NETWORKD_OP_LAN_CONFIGURE` に MTU の operand は足さない（Q5: Settings は表示だけ）。`networkd_lan_configure_edit`（lan-configure.c:155〜262）は既存の item の `mtu`・`mtu_set` を保つ（type・enabled・dhcp・addresses だけを書き換える作りなので変更は要らないが、host 試験で確かめる）。`lan_configure_policy`（9788〜9810）は request からだけ policy を作るので、**編集後の configuration の item の `mtu` を policy に写す**引数を足す（そうしないと Settings で DHCP/static を変えた直後の再構成で MTU が消える）。

**host 試験（p004）**: `plan/ws221/tests/host-netconf-mtu.c`（`plan/ws130/tests/host-netconf6.c` と同じ作りで netconf.c・reconcile.c を host の cc で link）: `mtu` の parse・validate・write の往復、reconcile の emit（変更あり → `MTU IF N`、key が消えた → `MTU IF 0`、変更なし → 出さない、disabled → 出さない）。`plan/ws221/tests/host-lan-policy-mtu.c`: `lan_send_policy` の record の形（net/main.c の record の生成を関数に切り出して link）と `lan_policy_decode` の往復（3/5 語・4/6 語・不正な語）、`networkd_lan_configure_edit` が `mtu` を保つこと。既存の `managed-lan-host-test`（Makefile:464〜470、`plan/ws033/tests/managed-lan-host-test.c`: policy の表と状態）の非退行。`phase20-net-config-host-test` は Makefile の check の一覧（662）にあるが rule が無いので使わない（L1）。

**dhcpc**: option 26 は読まない（今のまま）。

### 3.8 状態遷移と寿命

- device の MTU: `create`（`mtu` = driver の既定、`mtu_max`、`mtu_default`）→ `set_mtu`（up・down どちらでも。`ioctl_active` の admission。EINVAL/EBUSY/ENODEV）→ `device->mtu` は `device_lock` の下で書く → `KERN_SYSTEM_EVENT_CHANGE`。`gone`/`shutdown_all` は `ioctl_active` を join する（既存）。
- packet: class は alloc で決まり free まで変わらない（pointer の範囲）。copy は元の長さで class を選ぶ。予約は alloc の時だけ効く（queue に入った後の jumbo は socket ごとの上限と TCP の window が抑える）。
- IPv6 link.mtu: create 時 device の MTU → RA で `ra_mtu` を保ち `min(device, ra)` → `set_mtu` で同じ式を計算し直す（RA の値は忘れない: M3）→ PTB は path の表（link とは別）。
- networkd の policy の `mtu`: `LAN_ENABLE`（起動・reload）で record から、`NETWORKD_OP_MTU`（commit）で handler が書き換え、`LAN_CONFIGURE`（Settings）で編集後の net.conf から。適用は `lan_configure_steps` の up の前と、commit の `MTU` op。

### 3.9 並行性と lock

- pool: 今の 1 つの spin lock（IRQ を止める）で 2 つの配列と予約の数を守る。jumbo の線形探索は 32 個。
- `device->mtu`: 書くのは `net_device_set_mtu` が `device_lock` の下で 1 回。読み手（`ipv4_output`・`ethernet_output`・`udp`・`packet-socket`・`net_device_receive`・`ipv6_device_mtu_changed`）は lock 無しの word の読み（今と同じ）。driver は open・URB・buffer で `device->mtu` を読まない（§3.5・§3.6）ので値の遅れは検査の結果（EMSGSIZE か通すか）だけに効く。
- `ops->set_mtu` は `ioctl_active` の下（非再入、同じ device の close・gone・shutdown_all を同期に呼ばない: net-device.h:36〜41 の ioctl の約束と同じ）。driver の `set_mtu` は adapter の lock の下で値を見るだけ。
- `ipv6_device_mtu_changed` は `ipv6_lock` の下で `link.mtu` を書く。`icmp6_path_mtu_purge` は自分の lock。
- socket の `receive_jumbo` は `socket->lock` の下（`socket_enqueue` と dequeue）。

### 3.10 失敗と回復

| 失敗 | 振る舞い |
| --- | --- |
| jumbo の slot が尽きる | 受け: driver は ENOBUFS で frame を落とし `rx_dropped`。NCM は NTB の残りの datagram を続ける（§3.6）。送り: UDP・raw は ENOBUFS を app に（socket 側は予約を取れないので driver の受けは続く）、TCP は WS220 p003 の再試行 |
| 標準の slot が尽きる | 今と同じ（jumbo へ逃げない） |
| socket が jumbo を溜める | socket ごとに 2 つまで。3 つ目は落とす（ENOBUFS、`rx_dropped`）。読まない app が pool を奪えない |
| `set_mtu` が driver で失敗 | `device->mtu` は変わらない。ifconfig は errno を出す。networkd は syslog して up を続ける |
| device の descriptor が 9014 未満 | `mtu_max` がその値。`ifconfig mtu 9000` は EINVAL。net.conf の `mtu: 9000` は起動のたびに syslog（通信は 1500 で続く） |
| NCM の device が negotiate の値を拒む（STALL） | 1514 に下げてやり直す。それでも失敗なら attach は失敗（今と同じ） |
| NCM の device が読み戻しで小さい値を返す | その値を `mtu_max` に |
| 相手が 1500 で zedBSD が 9000（同じ switch） | TCP: 相手の MSS で切る（p005）。`ping -s 8972`: 相手か switch が落として timeout（期待の振る舞い、§8.3 で「正しい失敗」として記録） |
| router の向こうの 1500 の経路 | TCP: gateway 経由は MSS 1460 で切る（p005、S4）。UDP・raw の大きい物は黙って落ちる（制限、Q4） |
| RA が MTU 1500 を広告 | IPv6 は 1500 で送る（正しい）。受けは 9000 まで受ける |
| IPv6 の PTB | path の表に入り、TCP は次の segment から path MTU − 60 で切る（WS220 p006 の要件 2） |
| 古い image に戻す | `mtu` のある net.conf は読めない（網の設定全体）。`mtu` を消すか file を直す。記録 |

## 4. device ごとの上限の一覧と descriptor の読み

| device | 事実 / 推測 | zedBSD での上限 |
| --- | --- | --- |
| QEMU usb-net（CDC ECM） | `wMaxSegmentSize` 1514（事実）。property で変えられない（事実） | 1500。jumbo は不可 |
| RTL8156（5330 の ue0、ユーザー 2026-10-06） | CDC NCM と ECM の両方の configuration を持つ（BUG-222 のユーザーの見込みと P1 の読み。zedBSD は NCM を選ぶ: 事実は実機の log で）。Realtek の製品概要は RTL8156/8156B の jumbo を「9K」と書いていると記憶する（**記憶、未確認**）。CDC の configuration の `wMaxSegmentSize`・NTB parameter・`bmNetworkCapabilities` は**未確認** | 導出（§3.6）。期待 9000 |
| UGREEN 2.5GbE（Pi 4、ユーザー「RTL8825BU」、型番不明） | chip の名前は当てにしない（§0 の注）。2.5GbE の UGREEN の adapter は RTL8156 系の個体が多いと**記憶**する。CDC ECM/NCM の configuration を出さなければ zedBSD で動かない（vendor の driver は無い） | descriptor 次第。CDC を出さなければ対象外（ユーザーに報告） |
| GENET（Pi 4 内蔵） | hardware 1536 / 1514（事実）。ユーザーの決定で 1500 | 1500（`mtu_max` = 1500、上げられない） |
| RTL8822BU（WiFi） | 802.11 | 対象外 |
| lo0 | 65535（事実）。packet の class は標準（§3.1 (d)） | 変えない（`mtu_max` = 65535 のまま、frame は 1984 まで） |

### 4.1 descriptor の読み（p003 の最初の段、S8）

p003 の実装の前に、2 つの adapter（5330 の RTL8156、Pi 4 の UGREEN）の次を実機で読んで `plan/ws221/phase003/descriptors.md` に記録する。どちらの手段でも可（Q11 で決める）:

1. **Linux で読む（zedBSD の code の変更なし）**: adapter を挿した Linux（5330 の host 10.0.30.3 は Linux。Pi 4 の adapter は Linux の PC に挿す）で `lsusb -v -d <VID:PID>`（root で）。全 configuration の descriptor が出るので、configuration ごとの interface の class（`02/06` ECM、`02/0d` NCM、`ff` vendor）、Ethernet Networking Functional Descriptor の `wMaxSegmentSize`、NCM Functional Descriptor の `bmNetworkCapabilities` を写す。NTB parameter（`GET_NTB_PARAMETERS`）は lsusb では出ない。Linux が `cdc_ncm` を当てていれば `/sys/class/net/<if>/cdc_ncm/` の属性（`dwNtbInMaxSize` など、**記憶**: Linux の `sysfs-class-net-cdc_ncm` の ABI 文書）で読める。`r8152`（vendor の driver）が当たっていれば NTB parameter は読めない（2 の手段で）。
2. **zedBSD で読む**: p003 の最初の commit で NCM の attach の log に `segment=%u ntb_in=%u ntb_out=%u caps=0x%02x` を出す一行を入れ（ECM は既に `segment=` を出す）、T1/UAT の実機の起動（Pi 4 と 5330 の USB boot）で `dmesg` を SSH で読む。
3. 値が 1514 なら: その adapter の CDC の mode では 9000 は届かない。vendor の driver（r8152 相当を GPL を読まずに書く）は新しい範囲なので、ユーザーの判断（Q11）。

## 5. 公開・内部 interface のまとめ

- UAPI: `SIOCSIFMTU 0x00008922UL`（`struct ifreq`、`ifr_mtu`。0 = 既定に戻す）。superuser。EINVAL（1280 未満・上限超え）、EBUSY（driver が down を要る時）、ENODEV。`struct tcp_stats_header` を 72 byte に（jumbo の 5 欄。WS220 の所有、Q1 経由）。`KERN_SYSTEM_EVENT_NETWORK` の `CHANGE` の event（detail `ifindex=%u mtu=%u`）。
- kernel 内: `packet_buf_alloc_for(headroom, length, flags, *error)`、`PACKET_BUF_ALLOC_RECEIVE`、`PACKET_BUF_JUMBO_*`・`PACKET_BUF_MAX_STORAGE_SIZE`・`NET_MTU_MIN`・`NET_MTU_MAX`、`packet_buf_pool_usage` の jumbo の欄、`socket_datagram_max()`、`SOCKET_RECEIVE_JUMBO_MAX`、`net_device.mtu_max`・`mtu_default`、`net_device_ops.set_mtu`、`net_device_set_mtu`、`net_device_frame_max`、`ipv6_device_mtu_changed`、`ipv6_interface.ra_mtu`。`tcp_device_mss` は WS220 p006 の物。
- NCM codec: `drv_usb_cdc_ncm_limits.max_datagram_size`、profile の `max_datagram_size` が可変、`DRV_USB_CDC_NCM_MIN_MAX_DATAGRAM_SIZE`。
- networkd の protocol: `NETWORKD_OP_MTU = 21`、`NETWORKD_FIELD_MTU = 24`、`LAN_ENABLE` の record の末尾 `mtu N`、`struct networkd_lan_policy.mtu`。
- net.conf: interface の `mtu: N`。`net` の console: `mtu N`・`no mtu`。ifconfig: `mtu N|default`。ping: `-s SIZE`。
- HAL: 変更なし。

## 6. ライセンスと転記

- zedBSD の code は Zlib（各 file の SPDX）。本 Phase で新しく書く code・試験も Zlib。
- 参照は USB-IF の公開の仕様（CDC ECM 1.2 の Ethernet Networking Functional Descriptor、CDC NCM 1.0 の NTB parameter・SetMaxDatagramSize・GetMaxDatagramSize）と RFC（791・1122・1191・4861・8200・8201・9293）。descriptor の offset と request の番号は仕様の事実で、既存の code が既に持つ（usb-cdc-ncm.h:38〜47）。
- Linux（GPL）の cdc_ncm・cdc_ether・r8152・f_ncm・dummy_hcd の source は**読まない**（AGENTS の規則）。Linux の gadget の configfs の属性の名前は公開の ABI 文書（`Documentation/ABI/testing/`）の記憶で、host の `/sys/kernel/config/` を見て確かめる（§8.4）。chip の jumbo の能力は公開の製品概要の記憶で、descriptor の値で置き換える。転記は無い。
- QEMU の usb-net の descriptor の値は QEMU の source でなく zedBSD の attach の log（ws033/notes.md）から。

## 7. 実装の段と受け入れの条件

| Phase | 内容 | 依存 | 受け入れ |
| --- | --- | --- | --- |
| p002 kernel + ifconfig + ping: pool の 2 class と予約、`alloc_for`（UDP・ICMP・ICMPv6・packet socket・copy）、`socket_datagram_max`（syscall.c の 4 か所）、socket ごとの jumbo の上限、`net_device.mtu_max`・`mtu_default`・`set_mtu`・`net_device_set_mtu`・`net_device_frame_max`、`net_device_receive` の MTU 超え、`SIOCSIFMTU`（0 = 既定）と `CHANGE` の event、IPv6 の `ra_mtu` と追従、net.tcpstats の header の jumbo の欄（Q1 が WS220 と順を決める）、`ifconfig mtu N|default`、`ping -s` と sendto の失敗の表示と長さの判定 | 無し（UAPI の sysctl.h は WS220 p002 の merge の後） | build: amd64・rpi4（arm64）の vmunix と i386 の vmunix（`JUMBO_COUNT 0`）warning 0。host: 新 `plan/ws221/tests/packet-buf-host-test.c`（class の選択、copy の大きさ、予約: socket 側が使い切っても `ALLOC_RECEIVE` は取れる、尽きた時の `*error` と counter、`JUMBO_COUNT 0` の build）PASS。WS220 の `tcp-model/run.sh` の非退行（header の大きさの変更を含む）。QEMU（T1）: §8.2 の (1)〜(4) |
| p003 driver: descriptor の読み（§4.1、最初の段。結果が 1514 なら止めて Q1 に返す）→ ECM（`frame_max`、MTU に依らない URB、`set_mtu`、log）、NCM（codec の可変 max datagram と導出、limits 32 KB/16 KB、`SET_MAX_DATAGRAM_SIZE` と読み戻しと STALL の fallback、bit 3 の無い device、ENOBUFS で続ける、log） | p002、descriptor の読み | build warning 0。host: `plan/ws221/tests/run-ncm-jumbo-wire-test.sh` PASS、`plan/ws004/tests/run-usb-cdc-ncm-wire-test.sh` の非退行。QEMU（T1）: `tcp-receive-speed.sh`・`lan-hotplug.sh`・`lan-link-down.sh` の非退行、§8.2 の (5)。Q12 が可なら §8.4 の gadget で jumbo の data の経路 |
| p004 設定: net.conf の `mtu`、`net` の console、reconcile の `MTU`、networkd の op・field・policy・`lan_policy_decode`・`lan_send_policy` の record・`lan_configure_steps`・`lan_configure_policy`、docs | p002（host 試験は独立） | host: `host-netconf-mtu.c`・`host-lan-policy-mtu.c` PASS、`managed-lan-host-test` の非退行。QEMU（T1）: §8.2 の (6)〜(7) |
| p005 TCP: SYN の MSS を `tcp_device_mss` に、受けの window の jumbo の上限、（任意）jumbo が少ない時の小さい segment | p002、**WS220 p006・p007 が main に入り cleared** | tcp-model の scenario（MTU 9000 の device の模型で MSS 8960 の広告、1500 の相手との混在、lo0 65535 で segment が slot を超えない、gateway 経由の IPv4 で 1460、IPv6 の PTB の後に小さくなる）PASS。QEMU: SYN の MSS 1460（usb-net）・1360（MTU 1400）の非退行（§8.2 (8)） |
| p006 実機の受け入れ | p003・p004・p005 | §8.3。5330（RTL8156）と Pi 4（UGREEN）で、同じ jumbo の switch の上の 9000 の相手と 1500 の相手の混在、大きい ping、TCP の継続の転送（両向き） |
| p007 規約の全文の見直し | p002〜p005 | `plan/coding-style.md` の全文に対する WS の全 C（試験を含む）の見直し、build warning 0 |

p002 と p004 は独立（p004 は `ifconfig mtu` の子を呼ぶだけで、kernel の口が無い間は EINVAL になるが host 試験は通る）。p003 は p002 の `alloc_for`・`mtu_max` を使う。p005 は WS220 の後。p006 は実機でユーザーの時期。WS220 p006 の進みが遅ければ、p005 を「SYN の MSS の広告だけ（送りは 1024 のまま）」に絞る案がある（受けの上限の見直しは同じ）。

## 8. 試験計画

### 8.1 host

- `packet-buf-host-test.c`（p002）: `packet-buf.c` を host の cc で（HAL の hook は weak。WS220 の tcp-model と同じ作り）。
- `ncm-jumbo-wire-test.c`（p003、§3.6）。
- `host-netconf-mtu.c`・`host-lan-policy-mtu.c`（p004、§3.7）。
- WS220 の `tcp-model`（p005）。

### 8.2 QEMU（T1 に依頼、1 つの image で続けて）

image: `plan/ws221/tests/config-amd64-jumbo.mk`（SSH の guest、`config-amd64-tcp.mk`（WS220）を元に、tap の ue1 を足す: ping の相手と capture は tap の host 側。L12）。全部 usb-net（ECM、1514）なので jumbo の data は流れない。判定は SSH の出力と `ifconfig` の値（console・serial の log では判定しない）。
1. `ifconfig ue0 mtu 9000` → 失敗（EINVAL、「the interface's limit」）、`ifconfig ue0` の mtu は 1500 のまま。`ifconfig ue0 mtu 1279` → EINVAL。
2. up のまま `ifconfig ue1 mtu 1400` → 成功、`ifconfig ue1` に `mtu 1400`、SSH（ue0）は続く。`ifconfig ue1 mtu default` → 1500。
3. ue1 を 1400 にして tap の host へ `ping -s 1372` が通り（受けた長さ = 送った長さ）、`ping -s 1373` が `sendto: Message too long`（IPv4 は DF、kernel の MTU の検査）。IPv6 があれば `ping6 -s 1352` が通り 1353 が失敗。
4. 非 root の `ifconfig ue1 mtu 1400` → EPERM。`sysctl net.tcpstats` の header に jumbo の行（total 32、reserve 8）。
5. （p003）`tcp-receive-speed.sh`・`tcp-loss-speed.sh`・`lan-hotplug.sh`・`lan-link-down.sh` が従来どおり PASS。attach の log に `mtu_max=1500 rx_urb=2048` を SSH の `dmesg` で読む（判定は ifconfig の値で）。
6. （p004）`net configure` → `interface ue1` → `mtu 1400` → `commit` → `cat /etc/net.conf` に `mtu: 1400`、`ifconfig ue1` に 1400、SSH は切れない（M10）。`no mtu` → commit → 保存から消え、`ifconfig ue1` は 1500（既定に戻る: Q8）。`mtu 9000` の commit → networkd の答えに失敗、通信は続く。
7. （p004）`mtu 1400` で再起動の後に `ifconfig ue1` が 1400（`net startup` の record の経路）。`lan-hotplug.sh`（cable の抜き差し）の後も 1400。
8. （p005）`tcpdump` を tap の host で取り SYN の MSS が 1460（1500）、1360（1400）。
QEMU の限界: usb-net は 1514 固定（§1.10）。QEMU の patch・virtio-net の driver は選ばない（WS220 の決定と、本 WS の範囲の外）。

### 8.3 実機（p006、T1/UAT。ユーザーの機械と時期）

準備（ユーザー）: jumbo を通す switch、9000 の相手（Linux の PC、`ip link set DEV mtu 9000`）、同じ switch の上の 1500 の相手（別の PC か Pi 4 の GENET）。
1. 5330 + RTL8156: attach の log（NCM か ECM か、`segment=`・`max_datagram=`・`mtu_max=`）。`ifconfig ue0 mtu 9000`（up のまま、または net.conf）。
2. 大きい ping: Linux から `ping -M do -s 8972 ZEDBSD`、zedBSD から `ping -s 8972 LINUX`。両方向で応答し、長さが一致。Linux 側の `tcpdump -i DEV -nn 'greater 1514'` に 9014 byte の frame（証拠は Linux 側の file）。
3. 混在: 1500 の相手へ `ping -s 1472` は通り、`ping -s 8972` は失敗（正しい失敗）。1500 の相手との TCP（scp・fetch）が今までどおり通る。
4. TCP の継続: Linux ↔ zedBSD で 256 MB を両向き（`fetch`/`python3 -m http.server`、scp）。`tcp-stream-series.sh`（WS220）の実機版で時系列。Linux 側の tcpdump で segment が 8960 近く（zedBSD → Linux）。`net.tcpstats` の `jumbo_used_max`・`jumbo_alloc_failures`。速さの目標は WS220 の基線の後（Q1）。
5. Pi 4 + UGREEN: 1〜4 を同じく。GENET（eth0）は 1500 の相手として 3 に参加。
6. IPv6: RA の MTU option が無い link（または 9000）で `ping6 -s 8952`（長さの一致）。RA が 1500 なら 1500 までで正しい（記録）。
証拠: zedBSD の `ifconfig`（counter）、Linux の tcpdump の抜粋、`net.tcpstats` の jumbo の欄。QEMU の証拠と分けて書く。

### 8.4 QEMU で NCM の jumbo を流す代替（M9、Q12。可否は未確認）

案: host の Linux で `modprobe dummy_hcd; modprobe libcomposite` → configfs で gadget（`/sys/kernel/config/usb_gadget/g1/`、function `ncm.usb0`、`max_segment_size` を 9014（**記憶**: Linux 6.8 で f_ncm に足された configfs の属性。この host は 6.12.101 なのである見込み。`ls .../functions/ncm.usb0/` で確かめる））→ UDC `dummy_udc.0` に bind → host に USB の NCM の device と netdev `usb0` が現れる → `ip link set usb0 mtu 9000` → QEMU の `-device usb-host,vendorid=0x1d6b,productid=0x0104`（gadget の ID は自分で決める）で guest の xHCI に渡す。guest の zedBSD は NCM の driver で attach し、host の `usb0` と jumbo の data（ping・TCP）を流せる。
確かめた事: module の file がある（§1.10）。確かめていない事: `dummy_hcd` の load と gadget の bind が sudo で通るか、`usb-host` が dummy の device を掴めるか（libusb、udev の権限）、f_ncm の `max_segment_size` の属性の有無、NTB parameter の既定（f_ncm は `dwNtbInMaxSize` 16384 と**記憶**: zedBSD の 32 KB の limits と min で決まる）。
運用: sudo は許可済み（AGENTS）。module の load は host の状態を変えるので、Q1 がユーザーに確かめてから T1 が 1〜2 時間で試す（QEMU は T1）。可なら p003 の受け入れに「gadget の NCM で `ping -s 8972` と TCP が通る」を足す。不可なら jumbo の data は実機だけ（今の計画）。

## 9. 決めてほしい点（推奨つき）

| # | 問い | 推奨 | 他の案と理由 |
| --- | --- | --- | --- |
| Q1 | MTU の変更を up のままでも許すか（第 1 版は down の時だけ） | **up のままでも許す**（driver が down を要る時だけ EBUSY） | S5 の直しで driver の URB・buffer が MTU に依らなくなり、down を要する技術の理由が消えた。down を要求すると networkd の `DOWN` が address を外し SSH を含む全接続が切れる（M10）。代案 B: down の時だけ + `mtu_changing` の印（S1）。B を選ぶなら reconcile は `DOWN → MTU → UP` で接続が切れることを利用者に示す |
| Q2 | jumbo の slot の数（64-bit） | **32、うち予約 8**（9216 × 32 ≈ 295 KB） | 64 なら 590 KB。数より予約と socket ごとの上限（Q13）が枯渇を抑える。`net.tcpstats` の `jumbo_used_max`・`jumbo_alloc_failures` で測り、足りなければ上げる |
| Q3 | 最小の MTU | **常に 1280** | 第 1 版の「IPv6 が無効なら 576」は後から IPv6 を有効にした時に 1280 未満になる穴（M4）。Linux は 68 だが小さい値を許す理由が無い |
| Q4 | IPv4 の Path MTU（ICMP type 3 code 4） | **緩和策を p005 に入れ、ICMP の読みは Future Work**: gateway 経由の宛先は MSS を min(1460, MTU − 40)、on-link は device の MTU | S4: router の向こうの 1500 の経路へ 8960 の segment を送ると黙って止まる。緩和策は `tcp_route` の `route.gateway` で分かる小さい変更。ICMP を読む案（icmp6 の path の表と同じ作り）は別 WS。UDP・raw の大きい物は app の責任（制限として記録）。代案: 緩和策も入れず制限だけ記録（jumbo の LAN の中だけで使う前提） |
| Q5 | Settings に MTU の編集を出す | **出さない（表示だけ）** | 編集の仕組み（compositor の protocol の request、networkd の経路）を新しく足すことになる。`net` の console で十分。要るなら WS089/WS131 の範囲 |
| Q6 | `SIOCGIFMTUMAX`（上限を読む UAPI） | **足さない** | ifconfig は EINVAL の文で足りる |
| Q7 | ping に `-s` と sendto の失敗の表示を足す（p002） | **足す（p002）** | p002 の QEMU の試験に要る（M2）。受けた長さの判定も（S2） |
| Q8 | net.conf から `mtu` を消した時に既定に戻すか | **戻す**（kernel が `mtu_default` を持ち `SIOCSIFMTU 0`、reconcile は `MTU IF 0`） | 第 1 版の「戻さない」は動いている状態と設定が食い違う（M5）。net は driver の既定を知らなくてよい |
| Q9 | Pi 4 の USB LAN と 9000 の相手の環境 | （回答済み: UGREEN 2.5GbE、switch 経由） | 残り: descriptor の読み（Q11） |
| Q10 | `.net_bss` を NOLOAD の `.bss` に入れる linker の一行 | **入れる（Q1 の判断、別の小さな変更として）** | image の file が約 800 KB 縮む（16 MiB の予算は減らない: L5）。arm64 は `.vfs_bss`・`.kernel_heap` も同じ orphan なので一緒に。boot-test の確認が要る。本設計は依存しない |
| Q11 | descriptor の読みの手段と、1514 だった時の扱い。5320 を対象から外す確認 | **Linux の `lsusb -v`（§4.1 の 1）を先に、NTB parameter は p003 の log で。1514 なら止めて報告（vendor の driver は書かない）。5320 は対象外** | ユーザーの手元の Linux で 10 分で分かる。vendor の driver は GPL を読まずに作る大きな範囲で、本 WS の目標を超える |
| Q12 | host の Linux の gadget（dummy_hcd + f_ncm）で QEMU の NCM の jumbo を試すか | **T1 に 1〜2 時間の bounded の試しを頼む（host の module の load の許可つき）** | 可なら p003 の受け入れに QEMU の jumbo の data の経路が加わり、実機の前に codec・binding の誤りが取れる。不可なら今の計画（実機だけ）。host の状態（module）を変えるので許可が要る |
| Q13 | jumbo の枯渇の方針 | **(a) driver の受けの予約 8 + (b) datagram socket ごとに 2 + (d) lo0 は標準の class。(c)（TCP が少ない時に小さく切る）は p005 の任意** | (a) だけでは受けた packet が socket に溜まるのを防げない。(b) だけでは送り側（UDP の送り、lo0）が奪える。(d) は lo0 の振る舞いを変えない（今も 1984 付近が上限）。host 試験で確かめる |

## 10. 敵対的レビュー（自分で、第 2 版）

- **up のまま MTU を上げた瞬間の受け**: driver は `frame_max` まで受けて `net_device_receive` の検査に渡す。検査が新しい MTU を読めば通り、古い値を読めば落とす（1 packet の損失、再送で回復）。送りは `ethernet_output` が新しい MTU で検査し、driver は `frame_max` で検査するので矛盾しない。
- **up のまま下げた瞬間の送り**: 既に queue に入った大きい packet は driver が `frame_max` で受け付けるので送られる（相手には届く）。IPv6 の `link.mtu` は `ipv6_device_mtu_changed` で下がる。TCP の segment の大きさは p006 の送る時の計算で次から小さくなる。
- **予約が効かない経路**: `packet_buf_copy`（packet socket の配送、raw の配送）は予約を使わないので、jumbo が予約だけになると配送が落ちる（正しい側）。TCP の送りの写し（p006 の前）は `packet_buf_alloc`（標準）で 1024 を送るので jumbo を使わない。
- **socket ごとの上限 2 と TCP**: TCP の受けの queue は `socket_enqueue_stream` 経由で、数の上限は `receive_packet_limit`（44）。jumbo の上限 2 を TCP に当てると 1 接続の受けが 18 KB で止まるので、TCP は上限の対象外にし、p005 の window の計算で抑える（p005 の前は相手が 1460 で送るので jumbo は来ない）。
- **lo0 の TCP**: `tcp_device_mss` は lo0 で `1984 − 14 − 40 − 40 = 1890`（IPv4）。今の 1024 より大きく、slot に入る。WS220 p006 の要件 1 に入っている。
- **`socket_datagram_max` と AF_PACKET**: packet socket の送りは `mtu + 14` まで（packet-socket.c:258）で、lo0 では 65549 を通して `alloc_for` が EMSGSIZE（1984 超）→ `net_device_frame_max` の検査を先に入れる（§3.1）。
- **ECM の URB を `+ 1 packet` で arm する副作用**: device が frame_max ちょうど（倍数）を送り ZLP を送らない個体（仕様違反）では URB が終わらず次の frame が続きに入る。今の 1514 の arm でも同じ種類の問題（1024・1536 の frame）があり、実機で見る。`rx_errors` と `dmesg` で分かる形にする。
- **NCM の bit 3 の無い device で 65535 まで受ける**: 1 つの NTB の parse で 60 KB の datagram を `alloc_for` に出すと EMSGSIZE で落とす（`rx_errors`）。NTB 全体は捨てない（codec が datagram ごとに callback を呼ぶ作り）。
- **`LAN_ENABLE` の record の互換**: 新しい `net` が `mtu N` を付けた record を古い networkd に送る組は無い（同じ image）。逆（古い net と新しい networkd）は語数が少ないので通る。
- **reconcile の `MTU` と policy の `mtu` の二重の持ち主**: commit は `MTU` op で kernel と policy の両方を変え、起動・reload は record で policy を作る。net.conf が唯一の正本で、両方ともそこから来る。食い違うのは commit の失敗（driver の EBUSY）の時で、net が失敗を報告し net.conf は書かれない（confirmed commit の通常の経路）。
- **Settings で DHCP/static を変えた時**: `lan_configure_policy` に編集後の item の `mtu` を渡す（§3.7）。渡し忘れると cable の抜き差しで MTU が既定に戻る（host 試験で検出）。
- **32-bit の board**: `JUMBO_COUNT 0`・`RESERVE 0` の build で `alloc_for` が 2048 を超える長さに EMSGSIZE を返し、`NET_MTU_MAX 1500` で `set_mtu` が 9000 を拒む。`ifconfig mtu 1400` は通る。i386 の build を p002 の確認に入れる（build だけ: 2026-10-02 の「i386 は試験しない」の範囲で）。
- **試験で検出できない誤り**: 実機の NCM の device が `SET_MAX_DATAGRAM_SIZE 9014` を受けて読み戻しも 9014 なのに実際には送れない個体（firmware）。tcpdump の frame の大きさと zedBSD の `rx_errors`・`rx_dropped` で見る。QEMU では NCM が無いので codec の host 試験（と Q12 の gadget）が NCM の保証。
- **WS220 との順の誤りの可能性**: WS220 p006 が `tcp_device_mss` を `PACKET_BUF_STORAGE_SIZE` の cap で先に入れ、WS221 p002 が `net_device_frame_max` に置き換える。置き換えを忘れると MTU 9000 でも MSS が 1890 で止まる（p005 の tcp-model の scenario「MSS 8960 の広告」で検出）。

## 11. 残課題・未実施

- 実機の descriptor の値（RTL8156、UGREEN）は未確認（§4.1、Q11）。
- QEMU では jumbo の data の経路を流せない（§1.10）。gadget の代替は可否が未確認（§8.4、Q12）。
- NCM の 32 KB の coherent DMA が Pi 4 で取れるか・遅くないかは未確認（L10）。
- `wlan-l2.c` の frame の長さの上限（`net_device_receive` の検査の影響）は p002 で読む。`ipv6-ioctl.c` が `SIOCGIFMTU` を共有しているかも p002 で。
- WS220 の p006・p007 への要件（§3.4 の 1〜3）は Q1 経由で ws220 の phase.md に写す。net.tcpstats の header の変更（M6）の順も Q1。
- docs（`docs/reference/network-console.md` の console の文法、net.conf の `mtu`、rollback の制限）の更新は p004 で。
- CDC NCM 1.0・ECM 1.2 の仕様の文言（ZLP、SetMaxDatagramSize と bit 3、GetMaxDatagramSize）は記憶で書いた。p003 の実装の前に仕様の該当の節で確かめる。
- 試験・build・QEMU は走らせていない（設計だけ）。host の確認は `ls /lib/modules/.../gadget/` と `qemu-system-x86_64 --version` だけ。
