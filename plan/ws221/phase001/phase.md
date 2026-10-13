<!-- awesome-plan project=zedbsd record=ws221-p001 -->
# ws221-p001: 調べと設計（Jumbo frame: USB の有線 LAN で MTU 9000、GENET は 1500 のまま）

Status: in-progress（2026-10-13 P2: source の読みと設計は済み。残り: design-reviewer の review、ユーザーの判断（§9）。cleared は Q1 の判定）
Parent: [WS221](../ws.md)
Queue: （Q1 が記入）

## ゴール

- 対象の driver（USB の CDC ECM・NCM）と網の層（packet の pool、net-device、IPv4・IPv6、TCP の MSS）、設定の口（ifconfig・net・networkd・net.conf・Settings）を読み、
  MTU 9000 の frame を受け・送るのに要る変更を file:行つきで挙げる。
- 変更の設計（責務と境界、interface、状態と寿命、並行性、失敗、試験）と実装の段の分け・受け入れの条件を書く。
- 判断の要る点を推奨つきで挙げる。

行番号は main の `4f5cbfcdb`（2026-10-13）の file。ソースの事実と推測を分けて書く（推測には「推測」「記憶」と付ける）。

## 0. ユーザーの決定と前提

- 2026-10-13 ユーザー「GENETはMTU 1500までです。でもUSB LANではMTU 9000にしましょう。」→ Pi 4 の GENET（`src/drivers/platform/rpi4/rpi4-ethernet.c`）は 1500 のまま。
  USB の有線 LAN（CDC ECM・NCM）が MTU 9000 の目標。device の descriptor の上限が 9000 + 14 未満ならその上限。
- 5330 の `ue0` は **RTL8156** の USB LAN（2026-10-06 ユーザーの回答、[BUG-222](../../bugs/BUG-222.md) の「2026-10-06 ユーザーの回答」）。どちらの driver（NCM か ECM）が付いたかは実機で未確認
  （`src/drivers/usb/usb.c` は configuration ごとの match の点で選び、NCM 100 > ECM 80: BUG-222 q780 の P1 の読み）。
- Pi 4 に挿す USB LAN の adapter の型番はユーザーに質問中（未回答）。chip が分からない点は §4 に「要確認」と書く。
- zedBSD が扱う USB の Ethernet の driver は CDC ECM（`usb-cdc-ecm.c`）と CDC NCM（`usb-cdc-ncm.c` の codec と `usb-cdc-ncm-net.c` の binding）の **2 つだけ**。
  vendor 固有の driver（Realtek の vendor mode、ASIX、Microchip LAN78xx）は無い（`src/drivers/usb/` の一覧、`git grep -il "ax88\|rtl8152\|rtl8153\|lan78\|smsc95" src/drivers include` は CDC の file と `usb-rtl8822bu.c`（WiFi）だけ）。
  従って USB LAN の chip は、**CDC ECM か NCM の configuration を出す物だけ**が zedBSD で動き、jumbo の上限は chip の表の値ではなく、その configuration の
  Ethernet Networking Functional Descriptor の `wMaxSegmentSize` と、NCM なら NTB parameter で決まる。
- RTL8822BU の WiFi は対象外（802.11 の上限。`usb-rtl8822bu.c:8326` の `RTL8822BU_MTU`、変えない）。

## 1. 今の実装の記録（source の事実）

### 1.1 packet の pool（`include/kern/net/packet-buf.h`、`src/kern/net/packet-buf.c`）

| 項目 | 事実（file:行） |
| --- | --- |
| slot の大きさ | `PACKET_BUF_STORAGE_SIZE` = 2048（packet-buf.h:29）。slot は `struct packet_slot { packet; storage[2048]; used }`（packet-buf.c:33〜37）、1 つの静的な配列 `packet_pool[PACKET_BUF_POOL_COUNT]`（`.net_bss`、packet-buf.c:28・40） |
| 数 | amd64・arm64 で 256、他（i386・sparcv9・m68k・pc98）で 32（packet-buf.h:24〜28、BUG-222 q786 で 32 → 256） |
| alloc | `packet_buf_alloc(headroom)` は headroom ≤ 2048 を確かめ、最初の空き slot を線形に探し `capacity = 2048`（packet-buf.c:79〜128）。大きさの引数は無い |
| free | pointer で slot を線形に探す（packet-buf.c:168〜196）。lock は IRQ を止める spin（driver が割込みの文脈で free する） |
| copy | `packet_buf_copy` は `alloc(headroom)` → `append(length)`（packet-buf.c:331〜372）: 元が 2048 を超える前提は無い |
| 大きさに依存する他の場所 | `src/kern/syscall.c:1844〜1847`（SOCK_STREAM 以外の sendto は `args[2] > PACKET_BUF_STORAGE_SIZE` で EMSGSIZE）、`syscall.c:1961`（SOCK_STREAM 以外の recv の capacity を 2048 に切る）、`src/kern/net/unix-socket.c:1850`（AF_UNIX の datagram の上限）、`core.c:42`（入力 queue の長さ = pool の数）、`tcp.c:93・103`・`usb-cdc-ncm-net.c:56`（pool が 256 以上かで定数を変える） |
| 配置 | `.net_bss` は `platform/amd64/vmunix.ld:46〜52`・`platform/arm64/vmunix.ld:40〜47` の NOLOAD の `.bss` の入力の一覧（`.vfs_bss* .kernel_heap* .bss*`、arm64 は `.bss .bss.* COMMON`）に**合わない**ので orphan の PROGBITS になり、image を大きくする（BUG-222 q786 の P1 の観測: 256 個で amd64 の vmunix が約 500 KB 増えた）。pc98 の `stage2.ld:41` だけが `*(.net_bss)` を書く |

9000 byte の frame（14 + 9000 = 9014、headroom 64 を足して 9078）は今の slot（2048）に入らない。**pool の変更が最初の前提**。

### 1.2 net-device と MTU の口

| 項目 | 事実 |
| --- | --- |
| MTU の持ち方 | `struct net_device.mtu`（unsigned、`include/kern/net/net-device.h:58`）。driver が create の前に入れ、`net_device_create` は 0 を拒む（net-device.c:154）。**上限（driver が受けられる最大）を持つ欄は無い**。`net_device_ops` に MTU を変える callback は無い（net-device.h:30〜48: open・close・transmit・poll_receive・ioctl・release） |
| ioctl | `SIOCGIFMTU`（`include/uapi/netif.h:72`、inet-socket.c:673〜675）と `SIOCGIFSTATS` の `ifi_mtu` だけ。**`SIOCSIFMTU` は無い**（`0x8922` は未使用: `git grep 0x8922 include` は空）。書く ioctl は superuser（inet-socket.c の `inet_ioctl_is_query` の allow-list 1320〜1344、それ以外は `inet_ioctl_caller_is_superuser`） |
| 各 driver の値 | ECM `ECM_MTU` 1500（usb-cdc-ecm.c:46、1861）、NCM `DRV_USB_CDC_NCM_MTU` 1500（usb-cdc-ncm.h:18、usb-cdc-ncm-net.c:2098）、GENET 1500（rpi4-ethernet.c:431）、ne2000・lgy98 1500、WiFi は各自、lo0 65535（core.c:468） |
| ifconfig | `userland/base/ifconfig/main.c`: 動詞は `up`・`down`・`inet`・`ipv6`・`inet6`・`-inet6`・`broadcast`（92〜180）。表示は `SIOCGIFMTU` で `mtu %d`（197〜225）。**`mtu N` は無い** |
| Settings | 表示だけ（`userland/desktop/settings/page-network.c:1179〜1181` が `link->mtu` を描く。編集の関数は無い: 87〜106 の一覧）。値は libkeiland-backend-zedbsd が `SIOCGIFMTU` で読む（`network-link-zedbsd.c:316〜319`）→ compositor の `kl_system_network_v1` の `link` event の `uint mtu`（`kl-system-protocol.h:65`） |
| route socket | `RTM_IFINFO`（`include/uapi/route.h:134〜145`）に MTU の欄は無い（transition は carrier と removal・arrival） |

### 1.3 IPv4・IPv6・UDP・ICMP・ARP・ND

| 層 | 事実 |
| --- | --- |
| IPv4 の送り | `ipv4_output`（ipv4.c:259〜265）は `packet->length > device->mtu − 20` を EMSGSIZE で拒む。**断片化はしない**。header の fragment 欄は常に `0x4000`（**DF 常に立つ**、ipv4.c:346） |
| IPv4 の受け | 断片（offset か MF）は捨てる（ipv4.c:395〜409）。長さは packet の長さまで何でも受ける |
| ICMPv4 | echo だけ（icmp.c:37〜38、678）。**「Fragmentation needed」（type 3 code 4）は読まない**: IPv4 の Path MTU discovery は無い |
| IPv6 の送り | `ipv6_output` は `icmp6_path_mtu(destination, link.mtu)`（Packet Too Big で覚えた path の MTU、最小 1280、10 分）と比べ、超えると EMSGSIZE（ipv6.c:294〜303）。断片化しない |
| IPv6 の link MTU | interface ごとの `link.mtu` は作る時に `device->mtu`（ipv6.c:1629）。RA の MTU option は `IPV6_MINIMUM_MTU ≤ 値 ≤ device->mtu` の時だけ採る（ipv6.c:604〜605、nd6.c:512〜514・555）。**device の MTU が後で変わっても `link.mtu` は追わない**（変える口が無いので今は問題にならない） |
| IPv6 の受け | 断片 header は捨てる（ipv6.c:1350〜1351） |
| UDP | IPv4: `length > mtu − 20 − 8` を拒む（udp.c:682〜683）、IPv6: path MTU で（udp.c:796〜809）。packet は `packet_buf_alloc(64)` + append（udp.c:689・815）: slot に入らない大きさは append が失敗 |
| raw ICMP の送り | icmp.c:342〜348・833〜845（alloc + append、同じ） |
| ARP・ND・MLD | 小さい packet だけ（arp.c:379、nd6.c:272・332・822、mld6.c:134）。MTU と無関係 |
| Ethernet | `ethernet_output` は payload > mtu を拒む（ethernet.c:164〜168）。`ethernet_input` に上限は無い（driver が frame の長さを見る）。packet socket は mtu + 14 まで（packet-socket.c:258） |

### 1.4 TCP の MSS（WS220 との境）

- 送る segment は常に `TCP_MSS` = 1024（tcp.c:54、2244〜2245）。再送の経路は `uint8_t payload[TCP_MSS]` を stack に持つ（tcp.c:425）。
- SYN・SYN-ACK の MSS option は定数 `TCP_RECEIVE_MSS` 1460・IPv6 `TCP_RECEIVE_MSS6` 1440（tcp.c:62・65、1343〜1347）。相手の MSS option は読まない。
- 受けの window は「残り byte と残り packet 数 × 1460 の小さい方」（WS220 p001 §1、tcp.c:2456〜）。receive queue の packet の上限 44（`TCP_RECEIVE_PACKETS`、tcp.c:94）、reorder 16。
- WS220 の計画: p006 で「segment は min(相手の MSS, device の MTU − 40)、byte の送り buffer」、p007 で「受けの queue を byte で数える、window scale」（[ws220-p001 §4](../../ws220/phase001/phase.md)）。
  review-1 の S5 は「WS221 の TCP の部分は WS220 の p006・p007 の後、p006・p007 の設計は MTU を定数にしない」と書く（[review-1](../../ws220/phase001/review-1.md) S5）。

### 1.5 CDC ECM の driver（`src/drivers/usb/usb-cdc-ecm.c`）

| 項目 | 事実 |
| --- | --- |
| frame の上限 | `ECM_MTU` 1500、`ECM_FRAME_SIZE` 1514（46〜47）の定数 |
| descriptor | Ethernet Networking Functional Descriptor（subtype 0x0f、長さ 13）の `wMaxSegmentSize`（offset 8）を読み、**1514 未満なら binding を断る**（383〜388）。値は `binding->max_segment_size` に保ち（597）、attach の log に `segment=%u` と出す（1970）だけで、**frame の大きさには使わない** |
| buffer | `rx_buffer`・`tx_buffer` は `kern_malloc(ECM_FRAME_SIZE)`（1832〜1833）。1 つずつ、attach の時に取る |
| 受け | 受信の URB 1 本、長さ `ECM_FRAME_SIZE` で arm（1046〜1047、再 arm 1350）。完了で `length > ECM_FRAME_SIZE` を EMSGSIZE（1624〜1627）、`packet_buf_alloc(0)` + append + memcpy（1630〜1636） |
| 送り | `length > ECM_FRAME_SIZE` を EMSGSIZE（1101〜1107）。送信の URB 1 本、`tx_buffer` へ memcpy（1162〜1166）、bulk-OUT の max packet の倍数なら ZLP（1176〜1178）。queue 8（`ECM_TX_QUEUE_MAX`） |
| ops | open・close・transmit・poll_receive・release（182〜189）。ioctl は無い |
| net_device | `mtu = ECM_MTU`（1861） |

### 1.6 CDC NCM の driver（`usb-cdc-ncm.h`、`usb-cdc-ncm.c` の codec、`usb-cdc-ncm-net.c` の binding）

| 項目 | 事実 |
| --- | --- |
| datagram の上限 | `DRV_USB_CDC_NCM_MAX_DATAGRAM_SIZE` = 14 + 1500 = 1514（usb-cdc-ncm.h:18〜20）。codec の `profile_valid` は `max_datagram_size != 1514` を **EINVAL**（usb-cdc-ncm.c:441）、`negotiate` は candidate に 1514 を固定（140）、`build_ntb16` は frame > max_datagram_size を拒む（321〜322）、binding の受けは `length > 1514` を EINVAL（usb-cdc-ncm-net.c:1354〜1358） |
| descriptor | `wMaxSegmentSize < 1514` なら binding を断る（usb-cdc-ncm-net.c:407〜415）。**値は保たない** |
| NTB parameter | `GET_NTB_PARAMETERS`（28 byte）から `dwNtbInMaxSize`（offset 4）・`wNdpInDivisor`/`PayloadRemainder`/`Alignment`（8・10・12）・`dwNtbOutMaxSize`（16）・out の 3 つ（20・22・24）・`wNtbOutMaxDatagrams`（26）を読む（usb-cdc-ncm.c:111〜134）。device の値と driver の限度の小さい方、かつ NTH16 なので ≤ 65535 に clamp（`clamp_nth16_size` 412〜425）。1 つの max datagram の layout が in・out の NTB に入ることを確かめる（152〜176） |
| driver の限度 | 受けの NTB `NCM_NTB_IN_SIZE` 16384（pool 256 以上）/ 8192、受けの frame の queue 32 / 8、送りの NTB `NCM_NTB_BUFFER_SIZE` 8192、送りは 1 NTB に 1 datagram（`tx_max_datagrams` 1）（usb-cdc-ncm-net.c:47〜61、2195〜2199） |
| device への設定 | `SET_NTB_FORMAT`（NTB32 も持つ device にだけ）、`SET_NTB_INPUT_SIZE`（profile の in の大きさ）、**`SET_MAX_DATAGRAM_SIZE` = 1514**（`bmNetworkCapabilities` の bit 3 `NCM_CAP_MAX_DATAGRAM_SIZE` 0x08 を持つ device にだけ）、CRC off（715〜750、usb-cdc-ncm.c:196〜215）。今の driver は device の最大 datagram を **1514 に下げている** |
| buffer | `rx_buffer = kern_malloc(ntb_in_max_size)`、`tx_buffer = kern_malloc(ntb_out_max_size)`（2068〜2070） |
| 受け | 受信の URB 1 本（長さ ntb_in_max_size、1151〜1154・1514）。`parse_ntb16` が datagram ごとに `ncm_queue_datagram`（`packet_buf_alloc(0)` + append + memcpy、queue 32）、同じ poll で stack に渡して再 arm（BUG-222 の 2026-10-08 の直し） |
| 送り | `build_ntb16` で tx_buffer に 1 datagram の NTB を作り URB 1 本（1255〜1300） |
| ops・net_device | ECM と同じ構成。`mtu = DRV_USB_CDC_NCM_MTU`（2098） |
| host 試験 | codec の試験 `plan/ws004/tests/usb-cdc-ncm-wire-test.c`（`run-usb-cdc-ncm-wire-test.sh`、host の cc で `usb-cdc-ncm.c` と link、ASan/UBSan も） |

### 1.7 USB の host controller（xHCI）と転送の大きさ

- `drv_usb_urb_setup` に大きさの固定の上限は無い（`include/drivers/usb/usb.h:924〜931`）。xHCI は TD を 64 KiB の境界で TRB に分け chain する（`pci-xhci.c:3296〜3350`）。
  `pci-xhci.c:4236〜4239` に「`q` の経路では 64 KiB を超える長さや 64 KiB の境界を跨ぐ DMA を拒む」条件がある（`q` が何の経路かは未確認、**推測: 共有 staging の経路**）。
  本設計の NTB は最大 32 KB なのでどちらでも収まる。Pi 4 の USB は VL805 の xHCI（`rpi4-pcie.c:49〜50`）、5330 も xHCI。
- `DRV_USB_URB_RECLAIM_SAFE_MAX_SIZE` 8192 は `DRV_USB_URB_RECLAIM_SAFE` flag の時だけの上限で、ECM・NCM は使っていない（`git grep RECLAIM_SAFE src/drivers/usb/usb-cdc-*` は空）。

### 1.8 GENET（対象外、事実の記録）

`GENET_MAX_FRAME` の register に 1536（rpi4-ethernet.c:1039）、受けの DMA の slot は 2048 byte で `GENET_MAX_PACKET` 1514 を超える frame は捨てる（85〜86、1358〜1384）。
MTU は 1500 のまま。9000 の frame が LAN に流れても GENET は hardware で捨てる（1536 超）ので、混在の試験で Pi 4 の GENET の側は 1500 の相手として振る舞う。

### 1.9 設定の保存（net.conf・net・networkd）

- `/etc/net.conf` v1（`userland/base/net/netconf.c`）: interface の key は `type`・`enabled`・`parent`・`vlan-id`・`ipv4`（`dhcp`・`dhcp-timeout`・`addresses`）・`members`・`ipv6`（…）（1306〜1381、1434〜1457、1960〜2004）。
  **知らない key は parse の失敗**（1381 `unknown interface key`）。writer は 397〜545・2090〜2111。`struct netconf_interface`（netconf.h:85〜102）に MTU の欄は無い。
- `net` の console（`userland/base/net/main.c`）: `configure` → `interface NAME` → `enable`・`disable`・`dhcp [timeout S]`・`static ipv4 A prefix-length B`・`no ipv4`（3018〜3068、[docs/reference/network-console.md](../../../docs/reference/network-console.md) 80〜95）。
  commit は `reconcile.c` が `DOWN`・`UP`・`DHCP`・`STATIC`（126〜219）を networkd の request（opcode は main.c:805〜811 の表、`NETWORKD_OP_*` は `protocol.h:58〜97`、field は TLV `NETWORKD_FIELD_*` 184〜198）にして送る。
- networkd（`userland/base/networkd/main.c`）は `/sbin/ifconfig`・`/sbin/dhcpc`・`/sbin/route` の子を走らせる（1714・1779〜1826・4061〜4098）。cable の抜き差しの構成 `lan_configure_steps`（1765〜1830）は `ifconfig IF up` → static か dhcpc。policy は `struct networkd_lan_policy`（managed-lan.h:43〜49: interface・mode・address・netmask・dhcp_timeout）。
- dhcpc は DHCP の option 26（Interface MTU）を読まない（`git grep -i mtu userland/base/dhcpc` は空）。

### 1.10 QEMU の試験の網

- QEMU の網は全部 `usb-net`（CDC ECM、`plan/tools/guest/guest.py:193〜208`: slirp の ue0。`plan/ws220/tests/tcp-stream-series.sh`・`plan/ws033/tests/tcp-loss-speed.sh` は tap の ue1 を足す）。virtio-net の driver は無い（WS220 の 2026-10-13 ユーザーの決定: usb-net のまま）。
- QEMU の usb-net の ECM の descriptor は **`wMaxSegmentSize` = 1514**（[ws033/notes.md](../../ws033/notes.md) 137〜160 の attach の log `usb-cdc-ecm: ue0 ... segment=1514`）。
  `qemu-system-x86_64 -device usb-net,help`（QEMU 10.0.11、この host）の property は attached・bootindex・mac・msos-desc・netdev・pcap・port・serial で、**segment の大きさを変える口は無い**。
  → QEMU の usb-net では 1514 を超える frame を guest に入れられない（tap の MTU を 9000 にしても device の descriptor が 1514 で、ECM の driver（§5.2）は descriptor の値で上限を決める）。
  QEMU で確かめられるのは MTU の口（拒否・保存・再起動）、TCP の MSS の導出、pool の変更の回帰まで。**jumbo の data の経路の受け入れは実機だけ**（§8）。

## 2. 責務と境界

| 層 | 責務 | 変える物 |
| --- | --- | --- |
| packet の pool | 2 つの大きさの class（標準 2 KB、jumbo 9 KB）を持ち、要る大きさで alloc する | `packet-buf.h`・`packet-buf.c`、`syscall.c` の大きさの上限 |
| net-device | device の MTU の上限（`mtu_max`）と「MTU を変える」の口（`net_device_set_mtu`）、driver への `set_mtu` callback、IPv6 の link への通知 | `net-device.h`・`net-device.c`、`ipv6.c`（link.mtu の更新と path MTU の purge） |
| UAPI | `SIOCSIFMTU`（superuser、struct ifreq の `ifr_mtu`） | `include/uapi/netif.h`、`inet-socket.c` |
| IPv4・IPv6・UDP・ICMP・packet socket | 今の `device->mtu` の使い方のまま。packet の alloc を「要る大きさ」に | `udp.c`・`icmp.c`・`icmp6.c`・`packet-socket.c`（alloc の呼び方） |
| TCP | MSS = device の MTU − 40（IPv4）/ − 60（IPv6）を広告し、送る segment は min(相手の MSS, 自分の MSS)（**WS220 p006・p007 の後**） | `tcp.c`（p005） |
| CDC ECM | descriptor の `wMaxSegmentSize` から上限、buffer と URB を上限で、受け・送りの長さの検査を今の MTU で、`set_mtu` | `usb-cdc-ecm.c` |
| CDC NCM | codec の max datagram を可変に、NTB の大きさを jumbo が入る値に、`SET_MAX_DATAGRAM_SIZE` に negotiate の値、上限の導出、`set_mtu` | `usb-cdc-ncm.h`・`usb-cdc-ncm.c`・`usb-cdc-ncm-net.c` |
| ifconfig | `ifconfig IF mtu N` | `userland/base/ifconfig/main.c` |
| net.conf・net・networkd | interface の `mtu` の key、console の `mtu N`・`no mtu`、reconcile の `MTU`、networkd の op と policy（up の前に設定） | `netconf.[ch]`・`net/main.c`・`reconcile.c`・`protocol.h`・`networkd/main.c`・`managed-lan.[ch]` |
| ping | `-s SIZE`（大きい DF の ping の受け入れに要る。今は固定の長さ: `ping/main.c:235〜248`） | `userland/base/ping/main.c` |
| Settings | 表示だけ（今のまま）。編集の口は足さない（§9 Q5） | 無し |
| HAL | **変更なし**（`include/hal/hal.h` に触れない。pool の配置の linker の変更は platform/ で、任意: §3.1） | — |

GENET・WiFi・ne2000・lgy98 は `mtu_max` を持たない（= 今の MTU で固定）。`SIOCSIFMTU` は今の値以外を EINVAL で断る。

## 3. 設計

### 3.1 packet の pool: 2 つの class

```c
/* include/kern/net/packet-buf.h（案） */
#define PACKET_BUF_STORAGE_SIZE		2048U		/* 標準の class（今のまま） */
#if defined(HAL_ARCH_AMD64) || defined(HAL_ARCH_ARM64)
#define PACKET_BUF_POOL_COUNT		256U
#define PACKET_BUF_JUMBO_STORAGE_SIZE	9216U		/* 64 + 14 + 9000 = 9078 を 1 KB 単位に */
#define PACKET_BUF_JUMBO_COUNT		32U		/* §9 Q2 */
#else
#define PACKET_BUF_POOL_COUNT		32U
#define PACKET_BUF_JUMBO_STORAGE_SIZE	PACKET_BUF_STORAGE_SIZE
#define PACKET_BUF_JUMBO_COUNT		0U		/* 32-bit の board は jumbo を持たない */
#endif
/* stack 全体の 1 packet の最大の data（Ethernet の header を含む frame がこれに入る） */
#define PACKET_BUF_MAX_STORAGE_SIZE	PACKET_BUF_JUMBO_STORAGE_SIZE
/* device の MTU の kernel 側の上限: jumbo の slot に既定の headroom と Ethernet の header を引いた値を 9000 に丸める */
#define NET_MTU_MAX			(PACKET_BUF_JUMBO_COUNT != 0U ? 9000U : 1500U)

struct packet_buf *packet_buf_alloc(size_t headroom);			/* 今のまま: 標準の class */
struct packet_buf *packet_buf_alloc_for(size_t headroom, size_t length);	/* headroom + length が入る最小の class */
```

- `packet_slot` を 2 つの配列（`packet_pool[256]`、`packet_jumbo_pool[32]`、後者は storage が 9216）にし、`struct packet_buf` に `uint8_t pool_class` を足す（free で pointer を探す配列を選ぶ。
  今の線形探索のまま）。`capacity` は既に packet ごとなので、push・pull・append・tailroom・trim・各層の header の処理は**変更なし**。
- `packet_buf_alloc_for(headroom, length)`: `headroom + length ≤ 2048` なら標準、それ以外で `≤ 9216` かつ jumbo の slot があれば jumbo、無ければ NULL（`alloc_failures` を数える）。
  標準が尽きた時に jumbo へ**逃げない**（jumbo は 9000 の frame のために取っておく。逆も無し）。
- `packet_buf_copy`・`packet_buf_copy_region` は元の長さで `alloc_for`（今は `alloc(headroom)` なので 2048 を超える元の copy が失敗する。packet socket の配送 `packet_socket_deliver` と TCP の送りの写しが copy を使う）。
- `packet_buf_pool_usage` に jumbo の `total`・`used`・`used_max`・`alloc_failures` を足す（`struct packet_buf_pool_usage` は `include/kern/` の kernel 内の型。net.tcpstats の出力に行を足す。
  出力が sysctl の text なら末尾に足すだけ: p002 で確かめる）。
- 使う側の alloc の呼び方: 受け（ECM・NCM の受け、packet socket の送り）と UDP・raw ICMP・ICMPv6 の echo の応答（`icmp6.c:259・347` は元の長さの packet を作る）は `alloc_for(headroom, length)`。ARP・ND・MLD・route socket・TCP（p005 まで）は `alloc` のまま。
- `syscall.c:1844〜1847`（SOCK_STREAM 以外の sendto）と `:1961`（recv の capacity）の `PACKET_BUF_STORAGE_SIZE` は `PACKET_BUF_MAX_STORAGE_SIZE` に。`unix-socket.c:1850` は AF_UNIX 自身の上限なので **2048 のまま**（AF_UNIX の datagram は標準の class: 振る舞いを変えない）。
- 配置: 静的な配列（`.net_bss`）のまま。jumbo 32 × 9216 ≈ 295 KB（+ `struct packet_buf` 32 個）。`.net_bss` が orphan の PROGBITS（§1.1）なので amd64 の vmunix が約 300 KB 増える見込み
  （BUG-222 q786 の 500 KB と同程度。kernel の上限 16 MiB の内: BUG-222 の記録「12.9 MiB」）。**任意の改善**: `platform/amd64/vmunix.ld:49`・`platform/arm64/vmunix.ld:44` の NOLOAD の `.bss` に `*(.net_bss*)` を足すと PROGBITS でなくなり image が約 800 KB 縮む（既存の 256 × 2 KB の分も）。
  `platform/` は WS221 の所有の外なので Q1 の判断（一行の変更、boot の確認が要る）。本設計はこれに依存しない。
- 選ばなかった案: (a) 全 slot を 9 KB に（256 × 9 KB = 2.3 MB、image が 2.3 MB 増えて 16 MiB の上限に迫る、小さい packet の無駄） (b) scatter/chain（stack 全体が `packet->data` の連続を前提: header の push・checksum・copy）
  (c) `kern_malloc` で都度（driver が割込みの文脈で free する。heap の lock と断片化） (d) pool を heap や `kern_pmem_alloc` から boot で取る（配置が静的でなくなる利点は image の大きさだけで、linker の一行で同じ効果）。

### 3.2 net-device: MTU の上限と変更

```c
/* include/kern/net/net-device.h（案） */
struct net_device_ops {
	...
	void (*release)(void *driver_data);
	/*
	 * Optional.  Thread context, under the ioctl admission, only while the
	 * device is not open.  The driver checks the value against what the
	 * hardware takes, reprograms the hardware if it must (NCM: nothing until
	 * the next open), and returns 0 or an errno.  device->mtu still holds
	 * the old value when this is called.
	 */
	int (*set_mtu)(struct net_device *device, unsigned mtu);
};
struct net_device {
	...
	unsigned mtu;
	unsigned mtu_max;	/* the most the driver takes; 0 (not set) means mtu cannot change */
	...
};
int net_device_set_mtu(struct net_device *device, unsigned mtu);
```

- 全 driver は designated initializer（例 usb-cdc-ecm.c:182〜189）なので ops の末尾に欄を足しても変更は要らない。`mtu_max` を入れない driver は `net_device_create` が `mtu_max = mtu` に（= 変えられない）。
- `net_device_set_mtu(device, mtu)`（net-device.c、thread context）:
  1. `mtu < NET_MTU_MIN(device)` か `mtu > device->mtu_max` か `mtu > NET_MTU_MAX` → EINVAL。最小は IPv6 がその interface で有効なら 1280、そうでなければ 576（§9 Q3）。
  2. device が UP（`NET_DEVICE_UP`）なら **EBUSY**（§9 Q1）。down の時だけ変える: driver の受信 URB・buffer の長さ・NCM の device への `SET_MAX_DATAGRAM_SIZE` は open で決まるので、open と競合しない。
  3. `net_device_ioctl` と同じ admission（`ioctl_active`、close・gone・shutdown_all が join する: net-device.h:36〜41 の約束）で `ops->set_mtu` を呼ぶ（無ければ範囲の検査だけ）。
  4. 成功したら `device->mtu = mtu`（word の書き込み。down なので送りの経路に読み手はいない。`ipv6.c`・`inet-socket.c` の読みは値の遅れを許す）。
  5. `ipv6_device_mtu_changed(device)`: `interface->link.mtu = device->mtu`（ipv6_lock の下）、`icmp6_path_mtu_purge()`（全 path。device ごとの purge は無いので全部。10 分で消える物の再学習で害は無い）。
  6. `kern_log`（`ifindex=%u mtu=%u`、net-device.c:1547 と同じ形）。
- `SIOCSIFMTU`（`include/uapi/netif.h`: `#define SIOCSIFMTU 0x00008922UL`、struct ifreq の `ifr_mtu`、int）。`inet-socket.c` の ioctl の switch に足す（query の allow-list には入れない → superuser）。`ifr_mtu ≤ 0` は EINVAL。
  AF_INET6 の socket でも同じ経路（`inet6_ioctl` が `SIOCGIFMTU` を共有しているか p002 で確かめる: `inet-socket.c:1338` 付近）。
- 変更の通知: 無し（RTM_IFINFO に MTU の transition は足さない。Settings・networkd は `SIOCGIFMTU` で読む。管理者の MTU の変更は稀で、networkd が自分で変えるので知っている）。
  選ばなかった案: RTM_IFINFO に `rtm_transition = 5`（MTU_CHANGE）を足す（networkd の managed-lan の状態機械が知らない transition をどう扱うかの検査が要る。要れば後で足せる）。

### 3.3 IP の層

- IPv4: 変更なし（`device->mtu` を使う検査はそのまま、DF は常に立つ、断片化・PMTU は無し）。**制限として記録**: MTU 9000 の zedBSD が 1500 の経路（router の向こう、または同じ LAN の 1500 の相手）へ 1500 を超える IPv4 の datagram を送ると黙って落ちる（ICMP の Fragmentation needed を読まない）。
  TCP は相手の MSS（相手が 1500 なら 1460）で segment を切る（p005、WS220 p006）ので混在の LAN で困らない。UDP・raw は app の責任（Linux と同じ: `IP_PMTUDISC_DO` 相当が既定）。IPv4 の PMTU discovery は Future Work（§9 Q4）。
- IPv6: `ipv6_device_mtu_changed` で `link.mtu` を追う。RA の MTU option が 1500 を広告する router（Linux の radvd・多くの家庭の router の既定、**推測**）の link では `link.mtu` が 1500 に下がり、IPv6 は device の MTU が 9000 でも 1500 で送る
  （RFC 4861 6.3.4 の通り）。IPv6 の jumbo の受け入れは「RA の MTU option が無い、または 9000 を広告する link」で行う（§8）。
- ARP・ND: 変更なし。

### 3.4 TCP（p005、WS220 の p006・p007 の後）

- `tcp_device_mss(device, family)` = `device->mtu − 40`（IPv4）/ `− 60`（IPv6）。SYN・SYN-ACK の MSS option はこれ（`TCP_RECEIVE_MSS`・`TCP_RECEIVE_MSS6` の定数を置き換える）。
  SYN は route の device が決まってから作る（tcp.c:1343〜1350 で route の後に mss を決めているので挿す場所は同じ）。listener の SYN-ACK は受けた packet の device。
- 送る segment の大きさ = min(相手の SYN の MSS option（無ければ 536 / 1220）, `tcp_device_mss`)。これは WS220 p006 の設計そのもの（相手の MSS を読む、byte の送り buffer から segment を切る）。WS221 は p006 が「定数でなく device から取る」ことを要件にするだけ。
- 受けの window: WS220 p007 が byte で数えるので segment の大きさに依らない。p007 の前に MSS 8960 を広告すると、receive queue の 44 packet × 9 KB が jumbo の pool（32）を超える（1 接続で pool を使い切り `receive_refused`）。
  **p005 は p007 の後**（依存）。p005 では receive queue の packet の上限も「jumbo の pool の数の内」に見直す（例: window = min(byte の残り, 標準の空き × 1460 + jumbo の空き × MSS)。p007 の設計次第）。
- 再送の `payload[TCP_MSS]`（tcp.c:425）は p006 の byte の buffer で無くなる前提。無くならない場合は p005 で segment ごとの packet の写し（`packet_buf_copy`）に替える。
- WS220 側への依頼（Q1 経由）: p006・p007 の phase.md に「MSS・segment の大きさは `tcp_device_mss(device)` から、定数にしない」「受けの queue の上限は pool の class を考慮」を書く。

### 3.5 CDC ECM

- attach: `frame_max = min(wMaxSegmentSize, 14 + NET_MTU_MAX)`（descriptor の値は既に `binding->max_segment_size` にある）。`device->mtu = 1500`、`device->mtu_max = frame_max − 14`。
  `rx_buffer`・`tx_buffer` を `kern_malloc(frame_max)`（attach で 1 回。9 KB × 2 は heap 4 MiB（`src/kern/entry.c:51`）の内）。
- 受信の URB は open で `14 + device->mtu` の長さで arm（QEMU の 1514 なら今と同じ。device がそれより短い frame を送れば bulk の短い packet で完了し `actual_length` が長さ: 今と同じ）。
- 検査: 受け `length > 14 + device->mtu` → EMSGSIZE（今の `ECM_FRAME_SIZE` を `adapter->frame_size` に）。送り同様。alloc は `packet_buf_alloc_for(0, length)`。
- `set_mtu`: `mtu ≤ mtu_max` の検査だけ（hardware への設定は無い: ECM には max datagram の request が無い）。open で `frame_size = 14 + device->mtu` を取り直す。
- QEMU の usb-net（1514）: `mtu_max` = 1500 → `ifconfig ue0 mtu 9000` は EINVAL（QEMU の試験 §8.2）。
- log: `segment=%u` に加えて `mtu_max=%u` を出す。

### 3.6 CDC NCM

- codec（`usb-cdc-ncm.h`・`usb-cdc-ncm.c`）: `struct drv_usb_cdc_ncm_limits` に `uint16_t max_datagram_size`（driver が望む上限: `min(wMaxSegmentSize, 14 + NET_MTU_MAX)`）を足し、`negotiate_nth16` は
  candidate の `max_datagram_size` を「limits の値以下で、1 つの datagram の layout が in・out の両方の NTB（clamp 後）に入る最大」に**導出**する（今は 1514 固定で layout が入らなければ EMSGSIZE。
  導出は `make_layout` を上限から試し、入らなければ 1514 まで下げる。1514 でも入らなければ今と同じ EMSGSIZE）。`profile_valid` は `1514 ≤ max_datagram_size ≤ 65535`。
  `DRV_USB_CDC_NCM_MAX_DATAGRAM_SIZE` は「最小の上限」の意味で残す（名前は `DRV_USB_CDC_NCM_MIN_MAX_DATAGRAM_SIZE` に改名して誤用を防ぐ）。
- binding: descriptor の `wMaxSegmentSize` を `binding` に保つ（今は検査だけ）。limits: `ntb_in_max_size` を jumbo のある build で **32768**（16 KB だと 9014 の datagram は 1 つしか入らず、1514 の frame の集約も 10 → 変わらない。
  32 KB なら jumbo 3 つ、標準 21。rx_buffer の `kern_malloc(32 KB)` は heap の内）、`ntb_out_max_size` を **16384**（8 KB には 9014 の NTB が入らない）、`rx_max_datagrams` 32 のまま（NTB 1 つに 32 以上の小さい frame は今と同じ扱い）。
  32-bit の build は今の値のまま（`PACKET_BUF_JUMBO_COUNT == 0`）。
- `device->mtu_max = profile.max_datagram_size − 14`。`SET_MAX_DATAGRAM_SIZE` には `profile.max_datagram_size`（capability bit 3 の device だけ。bit 3 の無い device の最大 datagram は `wMaxSegmentSize` のまま: CDC NCM 1.0 の
  SetMaxDatagramSize の節の記憶、**推測**。その device では `wMaxSegmentSize` が上限なので導出の値と一致する）。
- 受け: `ncm_queue_datagram` の検査を `length > profile.max_datagram_size`、alloc は `alloc_for(0, length)`。stack に渡す時は `net_device_receive` が `14 + device->mtu` を超える frame を落とす方が簡単（driver は profile の上限で受け、MTU より大きい物は core で落とす）→ **core.c の `net_device_receive` に 1 つの検査を足す**（全 driver に共通。`rx_dropped` を数える）。
- 送り: `build_ntb16` の上限が profile の値になる。`ntb_capacity = ntb_out_max_size`（16 KB）。送りは今まで通り 1 NTB に 1 datagram。
- `set_mtu`: `mtu ≤ mtu_max`。hardware の設定は変えない（device には negotiate の最大を既に設定してあり、それ以下の frame は常に受ける）。
- log: attach で `segment=%u ntb_in=%u ntb_out=%u max_datagram=%u mtu_max=%u` を出す（5330 の RTL8156 の descriptor の値は **この log で初めて分かる**: §4）。
- host 試験: `plan/ws004/tests/usb-cdc-ncm-wire-test.c` は WS004（incomplete）の物なので触らず、WS221 の `plan/ws221/tests/ncm-jumbo-wire-test.c`（同じ作り、`run-ncm-jumbo-wire-test.sh`）で
  (a) `dwNtbInMaxSize` 65536・32768・16384・8192 と divisor 4/remainder 0/alignment 4、divisor 4/remainder 2（Realtek 風、**推測**）、divisor 1 の各組で導出の `max_datagram_size`、
  (b) 9014 の datagram の build と parse（3 つの jumbo を 1 NTB に）、(c) 上限超えの拒否、(d) 1514 の build/parse の非退行。完了の時に回帰に残す物は master.md の Tools に（WS004 の試験と統合するかは Q1）。

### 3.7 ifconfig・net.conf・net・networkd・ping

- `ifconfig IF mtu N`（`argc == 4 && strcmp(argv[2], "mtu") == 0`、`SIOCSIFMTU`）。EBUSY は「interface is up: ifconfig IF down first」、EINVAL は「mtu N is outside 576..MAX」（MAX は `SIOCGIFSTATS` に無いので「the interface's limit」と出す。
  上限を UAPI で読む口（`SIOCGIFMTUMAX`）は足さない: `net` と Settings が要るようになったら足す。§9 Q6）。usage の行を直す。
- net.conf: interface の key `mtu: N`（省略可、無ければ driver の既定 = 1500）。`struct netconf_interface` に `unsigned mtu; int mtu_set;`。validate は `576 ≤ N ≤ 65535`（device の上限は適用の時に分かる）。
  writer は `mtu_set` の時だけ書く。version は 1 のまま（省略可の key。**古い `net` は `mtu` のある file を parse できない**（1381）が、file は同じ system の `net` が書くので許容。記録しておく）。
- `net(config-if:NAME)> mtu N` と `no mtu`。`show running-config`・`startup-config` に出す。
- reconcile: MTU が変わる interface は `DOWN` → `MTU IF N` → 元の `UP`/`DHCP`/`STATIC` の順で emit（`emit_interface` 180〜219）。初回（起動）は interface が down なので `MTU` → `UP` の順。`mtu_set` でない interface は何も出さない（driver の既定に**戻さない**。
  戻すなら `mtu 1500` を書く。§9 Q8）。
- networkd: `NETWORKD_OP_MTU = 21`（`protocol.h:58〜97` の空き）、field `NETWORKD_FIELD_MTU = 24`（uint）。handler は `/sbin/ifconfig IF mtu N` を `run_command_until`（4040〜4110 の `UP`/`STATIC` と同じ形）。
  `struct networkd_lan_policy` に `unsigned mtu`（0 = 変えない）を足し、`lan_configure_steps`（1765〜1830）で `ifconfig IF up` の**前**に `ifconfig IF mtu N`（cable の抜き差しで up し直す時に毎回。失敗は log して続ける: MTU が付かなくても通信はできる）。
  policy は net.conf の `mtu` から（`net startup`/`reload` → networkd の policy の読み込みの経路、p004 で場所を確かめる）。
- ping: `-s SIZE`（ICMP の data の長さ、既定 56、上限は `PACKET_BUF_MAX_STORAGE_SIZE − 28 − 64` 程度で kernel が EMSGSIZE）。IPv4 は DF が常に立つ（§1.3）ので「DF 付き」は既定。IPv6 も断片化しない。
- Settings: 変えない（表示は既にある）。
- dhcpc: option 26 は読まない（今のまま。Linux の dhclient も既定では MTU を当てない、**記憶**）。

### 3.8 状態遷移と寿命

- device の MTU: `create`（driver の既定 1500、`mtu_max`）→ `set_mtu`（down の時だけ、EBUSY/EINVAL）→ `open`（driver が `14 + mtu` で URB・検査の長さを固定）→ `close` → `set_mtu` 可。`gone`/`shutdown_all` の間は `net_device_ioctl` と同じ gate で `set_mtu` を待つか断る（live でなければ ENODEV）。
- packet: class は alloc で決まり free まで変わらない。copy は元の長さで class を選ぶ。
- IPv6 link.mtu: create 時 device の MTU → RA で下げ（上限 device の MTU）→ `set_mtu` で device の MTU に戻す（RA が次に来れば再び下がる）。

### 3.9 並行性と lock

- pool: 今の 1 つの spin lock（IRQ を止める）で 2 つの配列を守る。jumbo の線形探索は 32 個。
- `device->mtu` の書き換えは down の時だけで、`set_mtu` は thread context。読み手（`ipv4_output`・`ethernet_output`・`udp`・`packet-socket`・driver の open）は lock 無しで読む（今と同じ）。
- `ipv6_device_mtu_changed` は `ipv6_lock` の下で `link.mtu` を書く。`icmp6_path_mtu_purge` は自分の lock。
- driver の `set_mtu` は adapter の lock の下で `opened == 0` を確かめて値を保つ（open の `opened = 1` と同じ lock）。

### 3.10 失敗と回復

| 失敗 | 振る舞い |
| --- | --- |
| jumbo の slot が尽きる | 受け: driver は ENOBUFS で frame を落とし `rx_dropped`（今の標準の class と同じ）。NCM は NTB の残りの datagram は続ける（callback の失敗は delivery を止める約束（usb-cdc-ncm.h:103〜109）なので、**ENOBUFS は「この datagram を飛ばして続ける」に**: p003 で `ncm_queue_datagram` が ENOBUFS を数えて 0 を返す）。送り: UDP・raw は ENOBUFS を app に、TCP は WS220 の p003 の再試行 |
| `set_mtu` が driver で失敗 | `device->mtu` は変わらない。ifconfig は errno を出す。networkd は log して up を続ける |
| device の descriptor が 9014 未満 | `mtu_max` がその値。`ifconfig mtu 9000` は EINVAL。net.conf の `mtu: 9000` は起動のたびに log に残る（通信は 1500 で続く） |
| NCM の device が negotiate の値を拒む（`SET_MAX_DATAGRAM_SIZE` が STALL） | attach は失敗（今と同じ: `ncm_program_profile` の失敗で attach が止まる）。**改善**: STALL なら 1514 に下げてやり直す（p003 で入れる。capability bit を立てながら拒む device への備え） |
| 相手が 1500 で zedBSD が 9000 | TCP: 相手の MSS で切る（p005）。ping -s 8972: 相手か switch が落として timeout（期待の振る舞い。§8.3 の混在の試験はこれを「正しい失敗」として記録） |
| RA が MTU 1500 を広告 | IPv6 は 1500 で送る（正しい）。受けは 9000 まで受ける |

## 4. device ごとの上限の一覧

| device | 事実 / 推測 | zedBSD での上限 |
| --- | --- | --- |
| QEMU usb-net（CDC ECM、RNDIS は driver が無い） | `wMaxSegmentSize` 1514（ws033/notes.md の attach の log、事実）。property で変えられない（事実） | 1500。jumbo は不可 |
| RTL8156（5330 の ue0、ユーザー 2026-10-06） | CDC NCM と ECM の両方の configuration を持つ（BUG-222 のユーザーの見込みと P1 の読み。zedBSD は NCM を選ぶ: 事実は実機の log で）。Realtek の公開の製品概要は RTL8156/8156B の jumbo を「9K」と書いていると記憶する（**記憶、未確認**）。descriptor の `wMaxSegmentSize` と NTB parameter の値は**未確認**（p003 の attach の log で読む） | 導出（§3.6）。期待 9000 |
| Pi 4 に挿す USB LAN（型番は質問中） | 不明。候補と記憶: RTL8153（CDC ECM/NCM の configuration を持つ個体がある、jumbo 9K の製品概要の記憶）、AX88179（vendor 固有が主で CDC ECM の configuration を持つ firmware もある、jumbo の記憶は曖昧）、LAN7800（Microchip、CDC ECM 無しの記憶）。**全部推測**。CDC ECM/NCM を出さない chip は zedBSD で動かない（driver が無い） | descriptor 次第。CDC を出さなければ対象外 |
| GENET（Pi 4 内蔵） | hardware の `MAX_FRAME` 1536、slot 1514（事実）。ユーザーの決定で 1500 | 1500（`mtu_max` = 1500、変更不可） |
| RTL8822BU（WiFi） | 802.11 | 対象外 |
| lo0 | 65535（事実）。packet の class は標準か jumbo（最大 9216）なので実際の datagram は 9 KB まで | 変えない |

## 5. 公開・内部 interface のまとめ

- UAPI: `SIOCSIFMTU 0x00008922UL`（struct ifreq、`ifr_mtu`）。layout は既存の `ifreq` のまま。superuser。EINVAL（範囲外・上限超え）、EBUSY（up）、ENODEV。
- kernel 内: `packet_buf_alloc_for`、`packet_buf.pool_class`、`PACKET_BUF_JUMBO_*`・`PACKET_BUF_MAX_STORAGE_SIZE`・`NET_MTU_MAX`、`net_device.mtu_max`、`net_device_ops.set_mtu`、`net_device_set_mtu`、`ipv6_device_mtu_changed`、`tcp_device_mss`（p005）。
- NCM codec: `drv_usb_cdc_ncm_limits.max_datagram_size`、profile の `max_datagram_size` が可変。
- networkd の protocol: `NETWORKD_OP_MTU = 21`、`NETWORKD_FIELD_MTU = 24`。
- net.conf: interface の `mtu: N`。`net` の console: `mtu N`・`no mtu`。ifconfig: `mtu N`。ping: `-s SIZE`。
- HAL: 変更なし。

## 6. ライセンスと転記

- zedBSD の code は Zlib（各 file の SPDX）。本 Phase で新しく書く code・試験も Zlib。
- 参照は USB-IF の公開の仕様（CDC ECM 1.2 の Ethernet Networking Functional Descriptor、CDC NCM 1.0 の NTB parameter・SetMaxDatagramSize）と RFC（791・1122・4861・8200・9293）。descriptor の offset と request の番号は仕様の事実で、既存の code が既に持つ（usb-cdc-ncm.h:38〜47）。
- Linux（GPL）の cdc_ncm・cdc_ether・r8152 の source は**読まない**（AGENTS の規則）。chip の jumbo の能力は公開の製品概要の記憶で、descriptor の値で置き換える。転記は無い。
- QEMU の usb-net の descriptor の値は QEMU の source でなく zedBSD の attach の log（ws033/notes.md）から。

## 7. 実装の段と受け入れの条件

| Phase | 内容 | 依存 | 受け入れ |
| --- | --- | --- | --- |
| p002 kernel: pool の 2 class、net-device の `mtu_max`・`set_mtu`、`SIOCSIFMTU`、IPv6 の link.mtu の追従、`syscall.c` の上限、alloc_for の適用（UDP・ICMP・ICMPv6・packet socket・copy）、`net_device_receive` の MTU 超えの検査、ifconfig `mtu N`、net.tcpstats の jumbo の行 | 無し | build: amd64・rpi4（arm64）・i386 の vmunix warning 0。host: 新 `plan/ws221/tests/packet-buf-host-test.c`（class の選択、copy の大きさ、尽きた時の NULL と counter、32-bit 相当の `JUMBO_COUNT 0` の build）PASS。WS220 の `tcp-model/run.sh` の非退行。QEMU（T1、§8.2 の (1)〜(4)） |
| p003 driver: ECM（descriptor からの上限、buffer・URB・検査、set_mtu、log）、NCM（codec の可変 max datagram と導出、limits の 32 KB/16 KB、`SET_MAX_DATAGRAM_SIZE` の値と STALL の fallback、ENOBUFS で続ける、log） | p002 | build warning 0。host: `plan/ws221/tests/run-ncm-jumbo-wire-test.sh` PASS、`plan/ws004/tests/run-usb-cdc-ncm-wire-test.sh` の非退行。QEMU（T1）: `tcp-receive-speed.sh`・`lan-hotplug.sh`・`lan-link-down.sh` の非退行、§8.2 の (5) |
| p004 設定: net.conf の `mtu`、`net` の console、reconcile の `MTU`、networkd の op・policy・`lan_configure_steps`、ping `-s` | p002 | host: 新 `plan/ws221/tests/host-netconf-mtu.c`（`plan/ws130/tests/host-netconf6.c` と同じ作りで netconf.c・reconcile.c を host の cc で link。`mtu` の parse・validate・write・reconcile の emit の順）PASS、`managed-lan-host-test`（Makefile:471〜478）の非退行。注: `Makefile:662` の `phase20-net-config-host-test` は check の一覧にあるが rule が無い（`git grep` で定義が見つからない）ので使わない。QEMU（T1）: §8.2 の (6)〜(7) |
| p005 TCP: `tcp_device_mss`、SYN の MSS、送る segment の上限、receive の packet の上限の見直し | p002、**WS220 p006・p007 が main に入り cleared** | tcp-model の scenario（MTU 9000 の device の模型で MSS 8960 の広告と 8960 の segment、1500 の相手との混在）PASS。QEMU: SYN の MSS 1460（usb-net）の非退行（§8.2 (8)） |
| p006 実機の受け入れ | p003・p004・p005 | §8.3。5330（RTL8156）と Pi 4（adapter 未定）で、9000 の相手と 1500 の相手の混在、DF の大きい ping、TCP の継続の転送（両向き） |
| p007 規約の全文の見直し | p002〜p005 | `plan/coding-style.md` の全文に対する WS の全 C（試験を含む）の見直し、build warning 0 |

p002 と p004 は独立（p004 は `ifconfig mtu` の子を呼ぶだけで、kernel の口が無い間は EINVAL になるが host 試験は通る）。p003 は p002 の alloc_for と `mtu_max` を使う。p005 は WS220 の後。p006 は実機でユーザーの時期。

## 8. 試験計画

### 8.1 host

- `packet-buf-host-test.c`（p002）: `packet-buf.c` を host の cc で（HAL の hook は weak なので今の WS220 の tcp-model と同じ作り）。
- `ncm-jumbo-wire-test.c`（p003、§3.6）。
- `host-netconf-mtu.c`（p004）: `mtu` の key の parse・validate（範囲）・write の往復、reconcile の emit の順（`DOWN`→`MTU`→`UP`、初回は `MTU`→`UP`、`mtu_set` でない interface は `MTU` を出さない）。
- WS220 の `tcp-model`（p005）。

### 8.2 QEMU（T1 に依頼、1 つの image で続けて）

image: `plan/ws221/tests/config-amd64-jumbo.mk`（SSH の guest、`config-amd64-tcp.mk`（WS220）を元に）。全部 usb-net（ECM、1514）なので jumbo の data は流れない。
1. `ifconfig ue0 mtu 9000` → 失敗（EINVAL、「the interface's limit」）、`ifconfig ue0` の mtu は 1500 のまま。
2. up のまま `ifconfig ue0 mtu 1400` → EBUSY。`ifconfig ue0 down; ifconfig ue0 mtu 1400; ifconfig ue0 up` → `ifconfig ue0` に `mtu 1400`、SSH が続く（再接続）。
3. 1400 で `ping -s 1372 10.0.2.2` が通り、`ping -s 1373` が EMSGSIZE（IPv4 は DF、kernel の MTU の検査）。
4. 非 root の `ifconfig ue0 mtu 1400` → EPERM。`sysctl net.tcpstats`（名前は WS220）の header に jumbo の行。
5. （p003）`tcp-receive-speed.sh`・`tcp-loss-speed.sh`・`lan-hotplug.sh`・`lan-link-down.sh` が従来どおり PASS（NCM は QEMU に無いので ECM の経路の非退行と NCM の host 試験）。attach の log に `mtu_max=1500` を SSH の `dmesg` で読む（判定は ifconfig の値で）。
6. （p004）`net configure` → `interface ue0` → `mtu 1400` → `commit` → `cat /etc/net.conf` に `mtu: 1400`、`ifconfig ue0` に 1400。`no mtu` → 保存から消える（MTU は 1400 のまま: §9 Q8）。`mtu 9000` の commit → networkd の答えに失敗、通信は続く。
7. （p004）再起動の後に `ifconfig ue0` が 1400（`net startup` の経路と、cable の抜き差し `lan-hotplug.sh` の後も 1400）。
8. （p005）`tcpdump` を host の tap で取り SYN の MSS が 1460（1500 の device）、1360（1400 の時）。
QEMU の限界: usb-net は 1514 固定（§1.10）。QEMU で 9000 を流す代替（QEMU の patch、virtio-net の driver）は選ばない（WS220 の決定と、本 WS の範囲の外）。

### 8.3 実機（p006、T1/UAT。ユーザーの機械と時期）

準備（ユーザー）: 9000 の相手（Linux の PC、`ip link set DEV mtu 9000`）と直結か jumbo を通す switch。1500 の相手（別の PC か Pi 4 の GENET）。
1. 5330 + RTL8156: attach の log（NCM か ECM か、`segment=`・`max_datagram=`・`mtu_max=`）。`ifconfig ue0 down; ifconfig ue0 mtu 9000; ifconfig ue0 up`（または net.conf）。
2. 大きい DF の ping: Linux から `ping -M do -s 8972 ZEDBSD`、zedBSD から `ping -s 8972 LINUX`。両方向で応答。Linux 側の `tcpdump -i DEV -nn 'greater 1514'` に 9014 byte の frame（証拠の PNG/log は Linux 側の file）。
3. 混在: 同じ LAN の 1500 の相手へ `ping -s 1472` は通り、`ping -s 8972` は失敗（正しい失敗）。1500 の相手との TCP（scp・fetch）が今までどおり通る。
4. TCP の継続: Linux ↔ zedBSD で 256 MB を両向き（`fetch`/`python3 -m http.server`、scp）。`tcp-stream-series.sh`（WS220）の実機版で時系列。Linux 側の tcpdump で segment が 8960 近く（zedBSD → Linux）。速さの目標は WS220 の基線の後（Q1）。
5. Pi 4 + USB LAN（型番が分かってから）: 1〜4 を同じく。GENET（eth0）は 1500 の相手として 3 に参加。
6. IPv6: RA の MTU option が無い link（または 9000）で `ping6 -s 8952`。RA が 1500 なら 1500 までで正しい（記録）。
証拠: zedBSD の `ifconfig`（counter）、Linux の tcpdump の抜粋、`net.tcpstats` の jumbo の used_max。QEMU の証拠と分けて書く。

## 9. 決めてほしい点（推奨つき）

| # | 問い | 推奨 | 他の案と理由 |
| --- | --- | --- | --- |
| Q1 | MTU の変更は interface が down の時だけ（up なら EBUSY） | **down の時だけ** | up のまま変えるには driver が受信の URB を cancel・drain して長さを変える経路が要る（ECM・NCM とも URB 1 本の作りで、再 arm の retry と絡む）。networkd は up の前に設定するので運用で困らない。後で足せる |
| Q2 | jumbo の slot の数（64-bit） | **32**（9216 byte × 32 ≈ 295 KB） | 64 なら 590 KB。1 接続の受けの window（64 KB ≈ 7 segment）と NCM の NTB（3 つ）と送りの写しで 32 で足りる見込み。net.tcpstats の `used_max` で測り、足りなければ上げる |
| Q3 | 最小の MTU | **IPv6 が有効なら 1280、無効なら 576** | Linux は 68。576 は RFC 791 の host の最小の datagram。小さい値を許す理由が無い |
| Q4 | IPv4 の Path MTU discovery（ICMP の Fragmentation needed を読む） | **本 WS の外、Future Work に** | 1500 の経路を越える jumbo の IPv4 は TCP では相手の MSS で避けられる。UDP の app は無い。要るなら icmp6 の path の表と同じ作りで別 WS |
| Q5 | Settings に MTU の編集を出す | **出さない（表示だけ）** | Settings の Ethernet の card は読み取り専用で、編集の仕組み（compositor の protocol の request、networkd の経路）を新しく足すことになる。`net` の console で十分。要るなら WS089/WS131 の範囲 |
| Q6 | `SIOCGIFMTUMAX`（上限を読む UAPI） | **足さない** | ifconfig は EINVAL の文で足りる。`net` が候補の検証に使いたくなったら足す |
| Q7 | ping に `-s` を足す（本 WS） | **足す** | 受け入れの「DF 付きの大きな ping」に要る。小さい変更 |
| Q8 | net.conf から `mtu` を消した時に 1500 に戻すか | **戻さない（明示の `mtu 1500` で戻す）** | reconcile は「書いてある事を適用する」作り。「無い＝既定に戻す」は driver の既定を net が知る必要がある。戻したい時は値を書く |
| Q9 | Pi 4 の USB LAN の型番と、9000 の相手の環境（直結か switch か） | （質問中） | chip が CDC ECM/NCM を出さなければ対象外 |
| Q10 | `.net_bss` を NOLOAD の `.bss` に入れる linker の一行（`platform/amd64/vmunix.ld`・`arm64`） | **入れる（Q1 の判断、別の小さな変更として）** | image が約 800 KB 縮む。boot-test の確認が要る。本設計は依存しない |

## 10. 敵対的レビュー（自分で）

- **pool の class を跨ぐ copy**: `packet_buf_copy` を直さないと 9 KB の packet の packet socket への配送と TCP の写しが落ちる → §3.1 で直す。試験 (packet-buf-host-test) に入れる。
- **jumbo の pool の枯渇で標準の通信が止まるか**: 標準と jumbo を分けるので、jumbo が尽きても標準の frame は流れる。逆に標準が尽きても jumbo へ逃げないので、9000 の設定が小さい frame の在庫を奪わない。
- **TCP が p005 の前に 9000 の MTU を見るか**: p002〜p004 の間、TCP は 1460 を広告し 1024 を送る（定数）。MTU 9000 は UDP・ping・raw だけが使う。受けは相手が 1500 の MSS の範囲で送るので問題ない。
  ただし **WS220 p006 が「segment = min(相手の MSS, MTU − 40)」を先に入れ、p007 の byte の window が後**だと、相手が 9000 の時に 8960 の segment を送り、相手（zedBSD）の受けの「1 packet = 1460」の約束が逆向きに破れる（1 packet に 8960 が入るので安全側）。pool の面では、送りの写し 8 個 × jumbo = 8 slot/接続。許容。
- **NCM の `tx_max_datagrams = 1` のまま**: 9014 の NTB を 1 つずつ送る。送りの効率は WS220 の範囲。
- **ECM の受信 URB を 9014 で arm して device が 1514 の frame を送る**: bulk の短い packet で完了するので `actual_length` が frame の長さ。ただし frame の長さが bulk の max packet（512/1024）の倍数の時、ECM の device は ZLP を送る（仕様）ので URB は終わる。QEMU の usb-net がこれを守るかは今の 1514 の arm でも同じ条件（1514 は 512 の倍数でない。1024・1536 の frame では問題になりうる。**推測**: 今も同じ）。
- **`net_device_receive` の MTU 超えの検査**: GENET や WiFi の driver が MTU より大きい frame を渡していた場合に落とすようになる（今は通していた）。GENET は 1514 で切っているので変化無し。WiFi は 802.11 の frame を Ethernet に直して渡す（`wlan-l2.c`、未読）ので p002 で上限を確かめる（1500 を超える A-MSDU があれば検査を `14 + mtu` でなく driver に任せる）。
- **`ifconfig IF mtu N` の EBUSY と networkd の順**: networkd の `lan_configure_steps` は毎回 `ifconfig up` から始めるので、その前に `mtu` を入れる。cable の抜き差しで carrier が落ちても device は up のままなので、MTU の再設定は EBUSY になる → 「既に同じ値なら 0 を返す」を `net_device_set_mtu` に入れる（冪等）。
- **再起動の後の順**: `net startup` → networkd の policy → `mtu` → `up` → dhcp。DHCP の relay や server が 9000 の frame を要求することは無い（DHCP は 576 以内）。
- **IPv6 の RA**: 多くの router は MTU option で 1500 を広告する（推測）。IPv6 の jumbo は環境依存。受け入れは IPv4 を主にする。
- **32-bit の board**: `JUMBO_COUNT 0` の build で `alloc_for` が 2048 を超える長さに NULL を返し、`NET_MTU_MAX 1500` で `set_mtu` が 9000 を拒む。`ifconfig mtu 1400` は通る。i386 の build を p002 の確認に入れる。
- **試験で検出できない誤り**: 実機の NCM の device が `SET_MAX_DATAGRAM_SIZE 9014` を受けても実際には 9014 を送れない個体（firmware）。tcpdump の frame の大きさと zedBSD の `rx_errors`・`rx_dropped` で見る。QEMU では NCM が無いので codec の host 試験だけが NCM の保証。
- **Phase の依存の誤りの可能性**: p005 が WS220 の p006・p007 を待つ間、p006（実機）は「ping と UDP の jumbo」までしか受け入れられない。ユーザーの受け入れ（TCP の継続の転送）は p005 の後。WS220 の進みが遅ければ、p005 を「SYN の MSS の広告だけを MTU から（送りは 1024 のまま）」に絞る案がある（受けの packet の上限を jumbo の数で見直す必要は同じ）。

## 11. 残課題・未実施

- 実機の descriptor の値（RTL8156、Pi 4 の adapter）は未確認。p003 の log で読む。
- QEMU では jumbo の data の経路を流せない（§1.10）。
- WS220 の p006・p007 への依頼（§3.4）は Q1 経由。
- docs（`docs/reference/network-console.md` の console の文法、ifconfig の文書は無い）の更新は p004 で（docs は目標の設計なので p004 の実装と一緒に）。
