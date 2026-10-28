<!-- awesome-plan project=zedbsd record=ws221 -->

# WS221: Jumbo frame（Ethernet の L2 の大きな MTU）

<!-- awesome-plan-current:start -->
Status: incomplete（すぐ取り組む物。2026-10-27 ユーザー「Jumbo Frameはすぐやるものの一つにします。」）。旧: incomplete・保留（ユーザーの判断待ち）（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: p001 第 2 版の設計の後、design-reviewer の review-2 とユーザーの判断 §9 Q1〜Q13…
Resume point: 2026-10-29 05:20 q1664（P3）: 最新の commit は下の表の最後の行（write(2) の socket への pinned の送り）。VF2: recv 948.6・send 932〜935 Mbps（3 回）、send ≥ 940 まであと少し（書き手の CPU 律速、copy と checksum が残り）。VF2 の /boot/boot0/vmunix はその commit の build（前は vmunix.old、元は vmunix.p3orig と host の build/vf2-board-backup/）。5330 の enx6c1ff706148a の MTU は 1500 に戻した（Debian 側の一時の設定だけで、永続の設定は触っていない）。次: send の残り（候補: sendto(2) にも pinned、fused の copy と checksum、TX checksum の offload の可否）、p002 の残り（ws の p002 の行）、p003（RTL8156 の SET_MAX_DATAGRAM_SIZE）、p007（規約の見直し）。§9 は全部推しで決定（下）。旧: p001 の review-2 → §9 の判断（特に Q1・Q4・Q11・Q12・Q13）→ p002（kernel + ifconfig + ping）と p004（設定）を Queue に、p003 の前に descriptor の読み（phase.md §4.1）
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
| [p001](phase001/phase.md) | 調べと設計: device ごとの上限、MTU の設定の口（ioctl・net の設定・保存）、IP・TCP の層の MTU の使い方、packet buffer の大きさ → design-reviewer | — | cleared（2026-10-29 Q1: ユーザーの回答「全部 推しで (推し)」で §9 の Q1〜Q13 を推しの案で確定。review-2 は行わない） |
| [p002](phase002/phase.md) | kernel + ifconfig + ping: packet の pool を 2 つの class（標準 2 KB・jumbo 9 KB × 32、driver の受けの予約 8、64-bit だけ）に、`packet_buf_alloc_for`、datagram の上限を family ごとに（`syscall.c` の 4 か所）、socket ごとの jumbo の上限、`net_device.mtu_max`・`mtu_default`・`set_mtu`・`net_device_set_mtu`（up でも可）・`net_device_frame_max`、`net_device_receive` の MTU 超え、UAPI `SIOCSIFMTU`（0 = 既定）と `CHANGE` の event、IPv6 の `ra_mtu` と追従、net.tcpstats の header の jumbo の欄（WS220 の UAPI、順は Q1）、`ifconfig IF mtu N|default`、`ping -s` と sendto の失敗の表示 | 無し（sysctl.h は WS220 p002 の merge の後） | in-progress（2026-10-29 P3 q1664: pool の 2 class・alloc_for・MTU の口・SIOCSIFMTU・ifconfig mtu・IPv6 の追従は済み、残りは phase.md） |
| p003（案） | driver: **最初に descriptor の読み**（phase.md §4.1: RTL8156 と UGREEN の `wMaxSegmentSize`・NTB parameter・caps。1514 なら止めて報告）→ CDC ECM（`frame_max`、MTU に依らない受信 URB（review S5）、`set_mtu`、log）、CDC NCM（codec の max datagram を可変に、NTB 32 KB/16 KB、`SET_MAX_DATAGRAM_SIZE` と読み戻しと STALL の fallback、bit 3 の無い device、ENOBUFS で続ける、log）。host の NCM の wire 試験 | p002、descriptor の読み | planning |
| p004（案） | 設定: net.conf の `mtu`、`net` の console の `mtu N`・`no mtu`、reconcile の `MTU IF N|0`（DOWN/UP を挟まない）、networkd の `NETWORKD_OP_MTU`・`FIELD_MTU`、`LAN_ENABLE` の record の末尾 `mtu N` と `lan_policy_decode`（語数 7）、policy の `mtu`、`lan_configure_steps` の up の前、`lan_configure_policy`（Settings の経路）、docs | p002（host 試験は独立） | planning（review S6・M5・M8・M10 で phase.md §3.7 を書き直し済み） |
| p005（案） | TCP: SYN の MSS を `tcp_device_mss`（p008 の物、旧 WS220 p006。MTU − 40/−60 と slot の上限の小さい方、IPv6 は path MTU、gateway 経由の IPv4 は 1460）に、受けの window の jumbo の上限 | p002、**p008・p009**（旧 WS220 p006・p007）が main に入って cleared | planning |
| p006（案） | 実機の受け入れ: 5330（RTL8156）と Pi 4（UGREEN 2.5GbE）で、jumbo の switch の上の 9000 の相手と 1500 の相手の混在、大きい ping（長さの一致）、TCP の継続の転送（両向き） | p003・p004・p005 | planning（ユーザーの機械と時期） |
| p007（案） | 規約の全文の見直し（WS の全ての C、試験を含む） | p002〜p005・p008・p009 | planning |
| [p008](phase008/phase.md) | TCP の輻輳の制御（cwnd・ssthresh・IW 10）と byte の送りの buffer、相手の MSS と `tcp_device_mss`（slot の上限・IPv6 の path MTU・gateway 経由の IPv4）、Nagle（旧 WS220 p006） | WS220 p005（main） | in-progress（2026-10-29 P3 q1664: cwnd・相手の MSS・ring 128・複数 segment の write は済み、byte の buffer は ring の copy のまま、send ≥ 940 が残り） |
| [p009](phase009/phase.md) | TCP の受けの窓: 窓の約束（byte で数える）、受け側の SWS 回避、window scale、delayed ACK（旧 WS220 p007） | p008 | in-progress（2026-10-29 P3 q1664: delayed ACK・recv の複数 packet・受けの packet と reorder 128・相手の window scale は済み、受け 948 Mbps） |

p001 の設計の要点（第 2 版）: QEMU の usb-net（CDC ECM）は descriptor の `wMaxSegmentSize` が 1514 で変えられないので、QEMU では MTU の口（拒否・保存・既定に戻す・再起動）と回帰だけを確かめ、jumbo の data の経路は実機だけ（phase.md §1.10・§8）。代替として host の Linux の USB gadget（dummy_hcd + f_ncm、module はこの host にある）を QEMU の `usb-host` で渡す案を §8.4 に書いた（可否は未確認、Q12）。
判断の要る点（推奨つき）は phase.md §9 の Q1〜Q13: MTU の変更は up のままでも可（第 1 版から変更、理由は review S5・M10）、jumbo 32 のうち予約 8、最小 MTU は常に 1280、IPv4 は gateway 経由の MSS を 1460 に抑え ICMP の読みは Future Work、`no mtu` は既定に戻す、descriptor の読みの手段と 1514 の時の扱い、5320 を対象外に、gadget の試し、枯渇の方針。

## WS220 から移した Phase（2026-10-28）

ユーザーの回答（クリック、2026-10-28）「WS221 へ移す (推し)」→ WS220（completed）の未着手の p006（輻輳の制御と送りの buffer、`tcp_device_mss` の持ち主）と p007（受けの窓）を、この WS の [p008](phase008/phase.md)・[p009](phase009/phase.md) にした（P4、q1660）。設計の案と 2026-10-13 の記録は各 phase.md に写した。p005（TCP の MSS）は p008・p009 の後。

## ユーザーの決定（2026-10-13）

ユーザー（原文）:「GENETはMTU 1500までです。でもUSB LANではMTU 9000にしましょう。」→ Pi 4 の GENET は 1500 のまま（対象外、理由はユーザーの判断と hardware の上限）。USB の有線 LAN（CDC ECM・NCM と、zedBSD が扱う USB の Ethernet の chip）は MTU 9000 を目標にする（chip・descriptor の wMaxSegmentSize が 9000 未満ならその上限、p001 で一覧）。受け入れの実機は USB LAN（Pi 4 と 5330 に挿した物）。

依存（2026-10-13 Q1、WS220 review-1 S5）: WS220 の p006（相手の MSS・送りの buffer）と p007（受けの窓）は MTU から MSS を決めるので、この WS の MTU の口と IP の層の MTU の使い方に合わせる。どちらが先でも、MSS は interface の MTU − 40（IPv4）・−60（IPv6）で決める形に揃える。

2026-10-13 Q1: review-1（致命的 0、重大 8、中 11、軽微 12）。p002（kernel）は S1（MTU の変更と up の競合、device_lock の下の印）・S2（sendmsg・recvmsg の大きさの上限）・M1・M2 を phase.md で直し、S7（jumbo の slot 32 の枯渇: driver の受けの予約、socket ごとの上限、lo0 の datagram は標準の class）を Q2 と一緒に決めてから。p004（設定）は S6（起動は reconcile を通らず `lan_send_policy`／`lan_policy_decode` の固定の語数、Settings の lan-configure.c）で §3.7 を書き直してから。p003 の前に RTL8156 の descriptor の値を読む（S8）。S4（IPv6 の TCP は path MTU で segment を切る、IPv4 は DF と fragmentation needed を読まないので途中に 1500 の router があると止まる）で Q4（IPv4 の PMTU を Future Work）を考え直す。

完了の条件の読み替え（review S8、ユーザーの決定に合わせる）: 上の「目的と完了の条件」の 2 の GENET・amd64 の有線、3 の断片化、4 の QEMU で MTU 9000 の受け入れは、ユーザーの決定（GENET は 1500 のまま、USB の有線 LAN で 9000）と設計（QEMU の usb-net は 1514 固定）に合わせて、次に読み替える: 対象は USB の有線 LAN（CDC ECM・NCM）、QEMU では MTU の設定の口と回帰だけ、大きな frame の受け入れは実機（Pi 4・5330 に挿した USB LAN）だけ。5320 は対象の実機に含めない（ユーザーの確認待ち）。

2026-10-13 ユーザー（原文）:「USB LANは型番不明の UGREEN 2.5GbE RTL8825BU です。MTU 9000はスイッチ経由です。」→ Pi 4 の USB LAN は UGREEN の 2.5GbE の adapter（chip はユーザーの言う RTL8825BU、型番は不明）。MTU 9000 の相手は jumbo frame の switch を経由。p003 の前にこの adapter と 5330 の RTL8156 の descriptor（wMaxSegmentSize・NCM の NTB の値・CDC の class）を実機で読む。switch を経由するので、混在の試験は同じ switch の上の 1500 の相手で行う。

## ユーザーの決定（2026-10-29）

- 原文: 「P3にWS221と、ついでに同じWS内で、1GbEの性能を使い切れるまでTCPスタックの最適化、5330を利用。」→ この WS に目標を足す: 5330 で 1GbE の速さを使い切るまで TCP の stack を最適化する（p008・p009 の輻輳の制御・受けの窓を含む）。相手は Q1 の Debian の host（10.0.10.2）を想定、P3 が link の速さを確かめる。
- p001 §9 の Q1〜Q13: ユーザーの回答（クリック）「全部 推しで (推し)」→ 各問いの推しの案で確定。
- 2026-10-29 ユーザー（原文）「P3は5330の代わりにVF2を使えませんか？こちらはjumbo frameが使えるかも？」→ Q1: 主の実機を VisionFive 2（10.0.30.2、JH7110 の GMAC、dw-eqos、1GbE）に。dw-eqos の jumbo を p001 の対象と p003 の driver に足す。5330（USB の RTL8156、CDC では descriptor の上限 1518、link 2.5G）は従で、CDC の SET_MAX_DATAGRAM_SIZE の確かめと比較だけ。

## 性能の受け入れ（2026-10-29 P3 が書いた、Q1 へ送る）

ユーザーの目標「1GbE の性能を使い切れるまで TCP スタックの最適化」（Q1 の読み: 両向き ≥ 940 Mbps を必達、その上は Linux の値に近づける）。主の実機は VF2（Q1 2026-10-29: 5330 から切り替え）。

| 項目 | 基準 | 測り方 |
| --- | --- | --- |
| 受け（VF2 が受ける） | 30 秒の平均 ≥ 940 Mbps、最遅の 1 秒 ≥ 900 Mbps、errors・dropped 0 | `plan/ws221/tests/vf2-tcp.sh --runs recv`（相手 centris 10.0.10.2、tcp-perf と perf-peer.py） |
| 送り（VF2 が送る） | 同じ（3 回の run の全部） | `vf2-tcp.sh --runs send,send,send` |
| 理論値 | 1500 MTU・timestamp なしの TCP の payload の上限 1460/1538 × 1000 = 949 Mbps | — |
| 比べる値 | 5330 の Debian（r8152、2.5G）は同じ道具で 2353 Mbps（道具が天井でない確かめ）。VF2 の Linux の値は無い（Q1: centris と理論値で比べてよい） | — |
| 回帰 | WS220 の tcp-model PASS、`qemu-tcp.sh` PASS、5 platform の vmunix の build warning 0 | — |

## 結果（2026-10-29、P3 q1664、VF2 実機、ファン有り、30 秒）

| commit | 受け Mbps | 送り Mbps | 変えた事 |
| --- | --- | --- | --- |
| main 61630bb0b（基線） | 198.5 | 59.2 | — （ファン無しは 198.5・58.8） |
| a0dc13377 | 904.8 | 338.8 | 送りの複数 segment・MSS 1460・cwnd・ring 128、受けの複数 packet・delayed ACK・packet と reorder 128、poll の通知を poll された socket だけ、WiFi の空の slot を lock なしで |
| fbfe26836 | 947.3 | 684.1 | kern_memcpy の非整列の語の copy、checksum を語で |
| e94e0040f | 948.0 | 732.7 | 送りの ring の copy を初回の送信に使う |
| 3c99abd3d | 948.3 | 765.5・838.6 | route の lookup の ref、tcp_route の無駄な lookup、source を ipv4 に渡す |
| 7d99bf2d9 | 948.4 | 900.1・784.9 | frame を語の境界に、TX の reclaim を ring が半分の時だけ |
| 970652255 | 948.5 | 886.7・918.4 | TCP が next hop と source を route の世代つきで持つ |
| 6d45f0a87 | 948.6 | 822.9 | pool を起動の時に確保（amd64 の上限の余りのため） |
| f22ba6a27 | 948.4 | 915.8・923.5・923.8 | 送りの ring 128 → 256、書き手を起こすのを ring の半分が空いてから（write の 64 KB の bounce が丸ごと取られる） |
| 09e15688b | 948.5 | 923.0・886.4 | dw-eqos の TX を pool の packet から直に DMA（pool を物理 page から取り、packet_buf_physical） |
| 83d44f3e3 | 948.6 | 932.2・935.4・932.9 | write(2) の stream socket は bounce を使わず pin した user の page から TCP の ring の copy へ（send_pinned） |

- 受けは基準を満たした（948 Mbps、理論値 949）。送りは 785〜918 Mbps で未達。書き手の thread が 1 CPU の 95 % 前後を使い、どの CPU に載るか（eqos の割り込みの CPU と同じか）で run ごとに揺れる。
- 測りの道具: sched の tick で中断の PC と kernel thread の入口を数える一時の差分（commit しない、build/tmp/*.patch）。送りの書き手の内訳（7d99bf2d9 の時）: memcpy（copyin の 64 KB の bounce と ring の copy）・eqos_transmit（DMA の buffer への copy と MMIO）・checksum・route と device の ref。
- 見つけた別件: BUG-350（usb_hub_worker の 100 %、f3d89900a で直した）。
