<!-- awesome-plan project=zedbsd record=ws311 -->

# WS311: TCP の輻輳の制御に CUBIC を実装し、性能が良ければ既定を CUBIC に切り替える

Status: planning
Primary Milestone: MG005
Resume point: 2026-10-29 P3 q1664-i05: p001 の設計の第 2 版（design-reviewer の review-1 の致命的 2・重大 4 に答えた）。次: §8 の問い Q1〜Q9 の答え（Q5 は 5330 の ifb・netem の承認）→ p001 cleared → p002。

## ユーザーの指示（原文、2026-10-29）

「なるほど。みんなCUBICに帯域を奪われて、たまりかねたんですかね……。CUBICを実装して、性能がよくなったらCUBICに切り替えるWSを作って、取り組んでほしいです。jumbo frameの続きでやってほしいです。」

（前の流れ: zedBSD の TCP は RFC 5681 の Reno 型、Linux・Windows・FreeBSD 14 以降の既定は CUBIC、と Q1 が説明した。）

## 目標（Q1 の解釈）

- zedBSD の TCP に CUBIC（RFC 9438）を、今の Reno 型と並べて実装し、接続ごと・system の既定を選べるようにする。
- Reno と CUBIC を同じ条件で測り（VF2 の 1GbE、10.0.10.3 の 10GbE、遅延と損失のある経路、CUBIC の相手との取り合い）、性能が良ければ既定を CUBIC に切り替える。良くなければ切り替えず、測った値と理由を残す。
- 順: WS221（Jumbo frame と TCP、p010 の 10GbE を含む）の続き。窓の前提（自分の window scale の広告、byte で数える受けの窓）は WS221 の p008・p009 の残りに依存。

## Phase（p001 の第 2 版で改めた、[phase001/phase.md](phase001/phase.md) §7）

| Phase | 目的 | 状態 | 依存 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 調べと設計（RFC 9438、今の tcp.c の cwnd の形、選び方の口、測りの条件と「良くなった」の基準）→ design-reviewer | in-progress（第 2 版、review-1 に答えた、§8 の問い Q1〜Q9 の答え待ち） | — |
| p002 | cwnd を使っている時だけ増やす門（Reno も、WS221 p011 で見つけた不具合）と tcp-model の拡張（drop-tail の bottleneck、2 本目の接続、上限、新しい check） | planning | p001 |
| p003 | CUBIC の実装（TCP_CONGESTION・sysctl・net.tcpstats の UAPI、cc-*・fair-* の scenario） | planning | p002 |
| p004 | 実機の測り（Reno と CUBIC、遅延の経路、取り合い）と既定の切り替えの判定 | planning | p003 |
| p005 | 規約の全文の見直し | planning | p003（p004 の後） |
