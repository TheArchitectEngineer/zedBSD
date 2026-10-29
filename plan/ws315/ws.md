<!-- awesome-plan project=zedbsd record=ws315 -->

# WS315 Realtek の USB の 2.5G・5G の NIC を、Realtek の独自の方式（vendor mode）の driver で速くする

- Master: [plan/master.md](../master.md)（2026-10-30 作成）。Primary Milestone: MG003。
- Status: planning（2026-10-30 Q1 が作成。着手は WS221 p014（xHCI の 1 endpoint に複数の要求）と NCM での速さの観察の後）
- 目標: 市場の USB の 2.5GbE・5GbE の NIC のほとんどが使う Realtek の chip（RTL8156・RTL8157 の類）を、汎用の CDC NCM ではなく
  Realtek の独自の方式で動かす driver を作り、Linux（r8152）と同じ程度の速さにする。

## ユーザーの決定（2026-10-30、原文）

「xHCIの改修は行いましょう。たとえ今回効果がなくても、価値がある実装だと考えます。
Realtek方式については、NCMでどこまで速度が上がるか観察してから、ぜひ実装しましょう。今のところUSB 2.5G NICはRealtekのチップのものしか持っていません。現在、市場にある2.5G USB LAN製品は、ほぼ100%がRealtekのチップのものだそうです。なので、このドライバを実装する価値が非常に大きいです。
5GbEのUSB NICについても、ほとんどがRealtekだそうです。すでに市場から消え始めていますが、Marvell（Aquantia）のチップもありました。AQCはUSB4で10GbEのものを1つ持っていますが、2.5/5GbEは入手が難しいですね。」

## 前提（Q1 の理解、設計で置き換える）

- 試験の機械: 5330 と RTL8156（2.5G）。5G の Realtek の NIC は手元に無い（未確認、ユーザーに聞く）。
- 比べる値: 5330 の Debian の r8152 で 2353 Mbit/s（向きは未記録、測り直す）。WS221 p013・p014 の NCM の値。
- 出典の候補: Linux の r8152 は GPL なので読むだけで写さない（独立実装）。FreeBSD・OpenBSD の `if_ure`（BSD）が RTL8152/8153/8156 を扱う。
  Realtek の公開の資料。license の境界は master-design-policy.md と Guardrail に従う。
- 範囲の外（記録だけ）: Marvell（Aquantia）AQC の USB の NIC。手元の AQC は USB4 の 10GbE（Thunderbolt・USB4 の PCIe tunnel の上の AQC107 の類か、未確認）。

## Phase

| Phase | 目的 | 状態 | 依存 |
| --- | --- | --- | --- |
| ws315-p001 | 設計: Realtek の vendor mode（device の切り替え、RX の集約・TX の詰め・checksum と TSO の offload、PHY・link）、NCM との切り替え、出典と license | planning | WS221 p014 |

## Resume point

- 2026-10-30 Q1: 作成。WS221 p014（xHCI）と NCM の観察の後に担当を割り当てる。
