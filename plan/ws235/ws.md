<!-- awesome-plan project=zedbsd record=ws235 -->

# WS235: Realtek RTL8127・8125・8126・8122・8121・8120 の共通の Ethernet driver（まず PCIe の RTL8127 10GbE）

<!-- awesome-plan-current:start -->
Status: planning（2026-10-14 起票。見積もりだけで、まだ実施しない）
Primary Milestone: MG006
Parent: [Master](../master.md)
<!-- awesome-plan-current:end -->

## ユーザーの指示（原文、2026-10-14）

「このホストには、RTL8127という10GbE NICがついています。RTL8127/8125/8126/8122/8121/8120に共通のドライバを作りつつ、PCIeの8127が動くようにしたいです。できそうですか？WS作成と見積もりをお願いします。まだ実施はしません。」

## 目標

- `src/drivers/ethernet/` に、Realtek の 2.5G〜10G の PCIe の NIC の系列（RTL8125・8126・8127・8122・8121・8120）で共通の driver を作る。chip の差は版の表と chip ごとの hook に閉じ込める。
- 試験機（Core i5-13400F の desktop、`ssh kei@10.0.30.5`、zedBSD が動く）の PCIe の RTL8127 で、link が上がり、IPv4 の通信と 10GbE に見合う転送ができる。
- 他の chip は版の判定と表まで（実機が無いので未確かめと明記）。
- server の build の config（`config/ci/config-amd64-server.mk`、ユーザーが作った物）で有効にする。

## Phase と見積もり（Q1、2026-10-14。1 LW ≈ 15〜25 分）

| ID | 内容 | 見積もり | 依存 |
| --- | --- | --- | --- |
| ws235-p001 | 調査と設計: chip の系列の差（MAC の版の判定・PHY・EEE・firmware の patch）、参考にできる source と license（Realtek の BSD の driver、FreeBSD・OpenBSD の re・rge は BSD。Linux の r8169 は GPL なので写さない）、firmware（linux-firmware の `rtl_nic/rtl8127a-*.fw` など、再配布できれば zedBSD-firmware へ）、zedBSD の network stack の口（ifnet・mbuf 相当・checksum の offload）、試験の道（実機、相手の 10GbE の機械） | 4 LW | — |
| ws235-p002 | 共通の骨格: PCI の ID の表、BAR の MMIO、reset、MAC address、MSI-X/MSI の割り込み、chip の版の判定、`CONFIG_DRIVER_PCI_RTL8125`（仮）、menuconfig の Drivers | 6 LW | p001 |
| ws235-p003 | RTL8127 の PHY と link: PHY の初期化の手順、firmware の patch の load、auto-negotiation、link の速さ・duplex の報告（**一番不確か**） | 10 LW | p002 |
| ws235-p004 | RX・TX の descriptor の ring、割り込みの処理、network stack への接続、checksum の offload | 8 LW | p002 |
| ws235-p005 | 実機の確かめ（link・DHCP・ping・ssh・大きな転送の速さ。相手が 10GbE でなければ相手の速さまで）と直し | 6 LW | p003・p004 |
| ws235-p006 | 他の chip（8125・8126・8122・8121・8120）の版の表と差の hook（実機は未確かめ） | 4 LW | p003 |
| ws235-p007 | 規約の全文の確認 | 2 LW | 全て |

合計 約 40 LW（実時間で約 10〜17 時間、P の 1 人なら 2〜3 日、並列にすれば短くなる）。p003 の PHY の初期化が資料どおりに行かないと延びる（+10 LW 程度）。
