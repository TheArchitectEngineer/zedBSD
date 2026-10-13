<!-- awesome-plan project=zedbsd record=ws221 -->

# WS221: Jumbo frame（Ethernet の L2 の大きな MTU）

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-13 Q1 作成。P2 が p001 を実行: 設計 [phase001/phase.md](phase001/phase.md) の第 2 版（review-1 の 31 件に対応）を書いた。design-reviewer の review-2 とユーザーの判断（phase.md §9 Q1〜Q13）待ち。WS220（TCP）と関係: MSS は MTU から決まる、`tcp_device_mss` は WS220 p006 の物、p005 は WS220 p006・p007 の後）
Resume point: p001 の review-2 → §9 の判断（特に Q1・Q4・Q11・Q12・Q13）→ p002（kernel + ifconfig + ping）と p004（設定）を Queue に、p003 の前に descriptor の読み（phase.md §4.1）
Primary Milestone: MG005
Related Milestones: なし
<!-- awesome-plan-current:end -->

## 原文（2026-10-13 ユーザー）

「オーケーです。あと、JUMBO FRAMEもお願いします。これはL2ですね。」（WS220 の TCP の Window Scale の話の後）

## 目的と完了の条件

1. 網の device ごとに MTU を 1500 より大きく（最大は device の上限、目標 9000）設定でき（`ifconfig <if> mtu N`、`net` の設定、再起動後も保つ）、受け・送りの両方で大きな frame を扱える。上限を超える値は断る。
2. 対象の driver の能力を調べて対応する（p001 で一覧）: Pi 4 の GENET（`src/drivers/platform/rpi4/rpi4-ethernet.c`、hardware の受けの buffer と最大の frame の長さ）、USB の CDC ECM・NCM（`src/drivers/usb/` の CDC ECM・NCM の driver（review の指摘で path を直した）、descriptor の wMaxSegmentSize）、amd64 の実機の有線（5320・5330 で使う物）、RTL8822BU の WiFi は対象外（802.11 の上限）。対応できない device は 1500 のまま、理由を記録。
3. IPv4・IPv6 の層が interface の MTU を使う（断片化・Path MTU、IPv6 の最小 1280）。TCP の MSS（WS220）が MTU から決まり、SYN で広告する。
4. 受け入れ: QEMU と実機（Pi 4 の GENET、5330）で MTU 9000（または device の上限）の相手と ping（DF 付きの大きな packet）と TCP の継続の転送が通り、1500 の相手とも混在して通る。build の warning 0、規約の全文の見直し。

## Phase

| Phase | 内容 | 依存 | Status |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 調べと設計: device ごとの上限、MTU の設定の口（ioctl・net の設定・保存）、IP・TCP の層の MTU の使い方、packet buffer の大きさ → design-reviewer | — | in-progress（2026-10-13 P2: 第 2 版（review-1 の 31 件に対応）。review-2 とユーザーの判断（phase.md §9）待ち） |
| p002（案） | kernel + ifconfig + ping: packet の pool を 2 つの class（標準 2 KB・jumbo 9 KB × 32、driver の受けの予約 8、64-bit だけ）に、`packet_buf_alloc_for`、datagram の上限を family ごとに（`syscall.c` の 4 か所）、socket ごとの jumbo の上限、`net_device.mtu_max`・`mtu_default`・`set_mtu`・`net_device_set_mtu`（up でも可）・`net_device_frame_max`、`net_device_receive` の MTU 超え、UAPI `SIOCSIFMTU`（0 = 既定）と `CHANGE` の event、IPv6 の `ra_mtu` と追従、net.tcpstats の header の jumbo の欄（WS220 の UAPI、順は Q1）、`ifconfig IF mtu N|default`、`ping -s` と sendto の失敗の表示 | 無し（sysctl.h は WS220 p002 の merge の後） | planning（p001 の review-2 と §9 Q1・Q2・Q3・Q8・Q13 の後） |
| p003（案） | driver: **最初に descriptor の読み**（phase.md §4.1: RTL8156 と UGREEN の `wMaxSegmentSize`・NTB parameter・caps。1514 なら止めて報告）→ CDC ECM（`frame_max`、MTU に依らない受信 URB（review S5）、`set_mtu`、log）、CDC NCM（codec の max datagram を可変に、NTB 32 KB/16 KB、`SET_MAX_DATAGRAM_SIZE` と読み戻しと STALL の fallback、bit 3 の無い device、ENOBUFS で続ける、log）。host の NCM の wire 試験 | p002、descriptor の読み | planning |
| p004（案） | 設定: net.conf の `mtu`、`net` の console の `mtu N`・`no mtu`、reconcile の `MTU IF N|0`（DOWN/UP を挟まない）、networkd の `NETWORKD_OP_MTU`・`FIELD_MTU`、`LAN_ENABLE` の record の末尾 `mtu N` と `lan_policy_decode`（語数 7）、policy の `mtu`、`lan_configure_steps` の up の前、`lan_configure_policy`（Settings の経路）、docs | p002（host 試験は独立） | planning（review S6・M5・M8・M10 で phase.md §3.7 を書き直し済み） |
| p005（案） | TCP: SYN の MSS を `tcp_device_mss`（WS220 p006 の物。MTU − 40/−60 と slot の上限の小さい方、IPv6 は path MTU、gateway 経由の IPv4 は 1460）に、受けの window の jumbo の上限 | p002、**WS220 p006・p007** が main に入って cleared | planning |
| p006（案） | 実機の受け入れ: 5330（RTL8156）と Pi 4（UGREEN 2.5GbE）で、jumbo の switch の上の 9000 の相手と 1500 の相手の混在、大きい ping（長さの一致）、TCP の継続の転送（両向き） | p003・p004・p005 | planning（ユーザーの機械と時期） |
| p007（案） | 規約の全文の見直し（WS の全ての C、試験を含む） | p002〜p005 | planning |

p001 の設計の要点（第 2 版）: QEMU の usb-net（CDC ECM）は descriptor の `wMaxSegmentSize` が 1514 で変えられないので、QEMU では MTU の口（拒否・保存・既定に戻す・再起動）と回帰だけを確かめ、jumbo の data の経路は実機だけ（phase.md §1.10・§8）。代替として host の Linux の USB gadget（dummy_hcd + f_ncm、module はこの host にある）を QEMU の `usb-host` で渡す案を §8.4 に書いた（可否は未確認、Q12）。
判断の要る点（推奨つき）は phase.md §9 の Q1〜Q13: MTU の変更は up のままでも可（第 1 版から変更、理由は review S5・M10）、jumbo 32 のうち予約 8、最小 MTU は常に 1280、IPv4 は gateway 経由の MSS を 1460 に抑え ICMP の読みは Future Work、`no mtu` は既定に戻す、descriptor の読みの手段と 1514 の時の扱い、5320 を対象外に、gadget の試し、枯渇の方針。

## ユーザーの決定（2026-10-13）

ユーザー（原文）:「GENETはMTU 1500までです。でもUSB LANではMTU 9000にしましょう。」→ Pi 4 の GENET は 1500 のまま（対象外、理由はユーザーの判断と hardware の上限）。USB の有線 LAN（CDC ECM・NCM と、zedBSD が扱う USB の Ethernet の chip）は MTU 9000 を目標にする（chip・descriptor の wMaxSegmentSize が 9000 未満ならその上限、p001 で一覧）。受け入れの実機は USB LAN（Pi 4 と 5330 に挿した物）。

依存（2026-10-13 Q1、WS220 review-1 S5）: WS220 の p006（相手の MSS・送りの buffer）と p007（受けの窓）は MTU から MSS を決めるので、この WS の MTU の口と IP の層の MTU の使い方に合わせる。どちらが先でも、MSS は interface の MTU − 40（IPv4）・−60（IPv6）で決める形に揃える。

2026-10-13 Q1: review-1（致命的 0、重大 8、中 11、軽微 12）。p002（kernel）は S1（MTU の変更と up の競合、device_lock の下の印）・S2（sendmsg・recvmsg の大きさの上限）・M1・M2 を phase.md で直し、S7（jumbo の slot 32 の枯渇: driver の受けの予約、socket ごとの上限、lo0 の datagram は標準の class）を Q2 と一緒に決めてから。p004（設定）は S6（起動は reconcile を通らず `lan_send_policy`／`lan_policy_decode` の固定の語数、Settings の lan-configure.c）で §3.7 を書き直してから。p003 の前に RTL8156 の descriptor の値を読む（S8）。S4（IPv6 の TCP は path MTU で segment を切る、IPv4 は DF と fragmentation needed を読まないので途中に 1500 の router があると止まる）で Q4（IPv4 の PMTU を Future Work）を考え直す。

完了の条件の読み替え（review S8、ユーザーの決定に合わせる）: 上の「目的と完了の条件」の 2 の GENET・amd64 の有線、3 の断片化、4 の QEMU で MTU 9000 の受け入れは、ユーザーの決定（GENET は 1500 のまま、USB の有線 LAN で 9000）と設計（QEMU の usb-net は 1514 固定）に合わせて、次に読み替える: 対象は USB の有線 LAN（CDC ECM・NCM）、QEMU では MTU の設定の口と回帰だけ、大きな frame の受け入れは実機（Pi 4・5330 に挿した USB LAN）だけ。5320 は対象の実機に含めない（ユーザーの確認待ち）。

2026-10-13 ユーザー（原文）:「USB LANは型番不明の UGREEN 2.5GbE RTL8825BU です。MTU 9000はスイッチ経由です。」→ Pi 4 の USB LAN は UGREEN の 2.5GbE の adapter（chip はユーザーの言う RTL8825BU、型番は不明）。MTU 9000 の相手は jumbo frame の switch を経由。p003 の前にこの adapter と 5330 の RTL8156 の descriptor（wMaxSegmentSize・NCM の NTB の値・CDC の class）を実機で読む。switch を経由するので、混在の試験は同じ switch の上の 1500 の相手で行う。
