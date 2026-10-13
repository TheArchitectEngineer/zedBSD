<!-- awesome-plan project=zedbsd record=ws221 -->

# WS221: Jumbo frame（Ethernet の L2 の大きな MTU）

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-13 Q1 作成。P2 が p001 を実行: 設計 [phase001/phase.md](phase001/phase.md) を書いた。design-reviewer とユーザーの判断（phase.md §9）待ち。WS220（TCP）と関係: MSS は MTU から決まる、p005 は WS220 p006・p007 の後）
Resume point: p001 の review → §9 の判断 → p002（kernel）と p004（設定）を Queue に
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
| [p001](phase001/phase.md) | 調べと設計: device ごとの上限、MTU の設定の口（ioctl・net の設定・保存）、IP・TCP の層の MTU の使い方、packet buffer の大きさ → design-reviewer | — | in-progress（2026-10-13 P2: 設計は書いた。design-reviewer とユーザーの判断（phase.md §9）待ち） |
| p002（案） | kernel: packet の pool を 2 つの class（標準 2 KB・jumbo 9 KB、64-bit だけ）に、`net_device.mtu_max`・`set_mtu`・`net_device_set_mtu`、UAPI `SIOCSIFMTU`、IPv6 の link MTU の追従、`syscall.c` の大きさの上限、UDP・ICMP・packet socket の alloc、`ifconfig IF mtu N` | — | planning（p001 の review の後） |
| p003（案） | driver: CDC ECM（descriptor の `wMaxSegmentSize` から上限、buffer・URB・検査、`set_mtu`）、CDC NCM（codec の max datagram を可変に、NTB 32 KB/16 KB、`SET_MAX_DATAGRAM_SIZE` の値、log）。host の NCM の wire 試験 | p002 | planning |
| p004（案） | 設定: net.conf の `mtu`、`net` の console の `mtu N`・`no mtu`、reconcile の `MTU`、networkd の op・policy（up の前に設定）、ping の `-s` | p002（host 試験は独立） | planning |
| p005（案） | TCP: MSS を device の MTU − 40 / − 60 から（SYN の広告、送る segment は min(相手の MSS, 自分)）、受けの packet の上限の見直し | p002、**WS220 p006・p007** が main に入って cleared | planning |
| p006（案） | 実機の受け入れ: 5330（RTL8156）と Pi 4（USB LAN、型番は質問中）で 9000 の相手と 1500 の相手の混在、DF の大きい ping、TCP の継続の転送（両向き） | p003・p004・p005 | planning（ユーザーの機械と時期） |
| p007（案） | 規約の全文の見直し（WS の全ての C、試験を含む） | p002〜p005 | planning |

p001 の設計の要点: QEMU の usb-net（CDC ECM）は descriptor の `wMaxSegmentSize` が 1514 で変えられないので、QEMU では MTU の口（拒否・EBUSY・保存・再起動）と回帰だけを確かめ、jumbo の data の経路は実機だけ（phase.md §1.10・§8）。
判断の要る点（推奨つき）は phase.md §9（MTU の変更は down の時だけ、jumbo の slot 32、最小 MTU、IPv4 の PMTU は Future Work、Settings は表示だけ、ping の `-s`、`.net_bss` の linker の一行）。

## ユーザーの決定（2026-10-13）

ユーザー（原文）:「GENETはMTU 1500までです。でもUSB LANではMTU 9000にしましょう。」→ Pi 4 の GENET は 1500 のまま（対象外、理由はユーザーの判断と hardware の上限）。USB の有線 LAN（CDC ECM・NCM と、zedBSD が扱う USB の Ethernet の chip）は MTU 9000 を目標にする（chip・descriptor の wMaxSegmentSize が 9000 未満ならその上限、p001 で一覧）。受け入れの実機は USB LAN（Pi 4 と 5330 に挿した物）。

依存（2026-10-13 Q1、WS220 review-1 S5）: WS220 の p006（相手の MSS・送りの buffer）と p007（受けの窓）は MTU から MSS を決めるので、この WS の MTU の口と IP の層の MTU の使い方に合わせる。どちらが先でも、MSS は interface の MTU − 40（IPv4）・−60（IPv6）で決める形に揃える。
