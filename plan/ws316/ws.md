<!-- awesome-plan project=zedbsd record=ws316 -->

# WS316 5330 の USB4 と、その先の Marvell（Aquantia）AQC の 10GbE を使えるようにする

- Master: [plan/master.md](../master.md)（2026-10-30 作成）。Primary Milestone: MG003。
- Status: planning（2026-10-30 Q1 が作成、担当は未割当）
- 目標: 5330（Alder Lake）の USB4（Thunderbolt 4 互換、CPU 内蔵の TCSS）に、ユーザーの USB4 の 10GbE（AQC）をつなぎ、zedBSD から 10GbE で通信する。

## ユーザーの決定（2026-10-30、原文）

1. 「USB4は5330についているのですが、これって対応するにはどういうことが必要ですか？どんなHCIなんでしょうか。」（Q1 の説明: NHI（class 0x0C0340）・TCSS の xHCI・PCIe の tunnel の root port・DP の tunnel、firmware の CM か software の CM、PCIe の hot-plug、security level と IOMMU、AQC107 の driver）
2. 「USB4+AQC 10GbEのWSを立てます。」
3. hot-plug の device の承認は deviced で（[WS317](../ws317/ws.md) の hotplugd、原文はそちら）。

## 段取りの案（Q1、2026-10-30）

1. 調べ（5330 の Debian）: BIOS がどちらの CM か（dmesg の thunderbolt、boltctl）、AQC が起動の時から PCIe に見えるか（pre-boot ACL）、IOMMU（VT-d）の設定、AQC の chip（AQC107/113 の類）と PCI ID。
2. AQC の NIC の driver（PCIe の device として、起動の時に見える場合）。出典は FreeBSD の aq(4)・OpenBSD の aq(4)（BSD）、Linux の atlantic は読むだけ。
3. PCIe の hot-plug と firmware の CM の NHI（mailbox での許可）。承認は WS317 の deviced。
4. software の CM（要る時）。

## Phase

| Phase | 目的 | 状態 | 依存 |
| --- | --- | --- | --- |
| ws316-p001 | 調べ: 5330 の USB4 の CM・AQC の見え方・IOMMU・PCI ID（Debian で） | planned | — |

## Resume point

- 2026-10-30 Q1: 作成。担当の割当の前。
