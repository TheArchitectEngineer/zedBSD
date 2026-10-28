<!-- awesome-plan project=zedbsd record=ws311 -->

# WS311: TCP の輻輳の制御に CUBIC を実装し、性能が良ければ既定を CUBIC に切り替える

Status: planning
Primary Milestone: MG005
Resume point: p001（調べと設計）。担当は P3、WS221（Jumbo frame と TCP）の続きとして。

## ユーザーの指示（原文、2026-10-29）

「なるほど。みんなCUBICに帯域を奪われて、たまりかねたんですかね……。CUBICを実装して、性能がよくなったらCUBICに切り替えるWSを作って、取り組んでほしいです。jumbo frameの続きでやってほしいです。」

（前の流れ: zedBSD の TCP は RFC 5681 の Reno 型、Linux・Windows・FreeBSD 14 以降の既定は CUBIC、と Q1 が説明した。）

## 目標（Q1 の解釈）

- zedBSD の TCP に CUBIC（RFC 9438）を、今の Reno 型と並べて実装し、接続ごと・system の既定を選べるようにする。
- Reno と CUBIC を同じ条件で測り（VF2 の 1GbE、10.0.10.3 の 10GbE、遅延と損失のある経路、CUBIC の相手との取り合い）、性能が良ければ既定を CUBIC に切り替える。良くなければ切り替えず、測った値と理由を残す。
- 順: WS221（Jumbo frame と TCP、p010 の 10GbE を含む）の続き。窓の前提（自分の window scale の広告、byte で数える受けの窓）は WS221 の p008・p009 の残りに依存。

## Phase（案、p001 で確定）

| Phase | 目的 | 状態 | 依存 |
| --- | --- | --- | --- |
| p001 | 調べと設計（RFC 9438、今の tcp.c の cwnd の形、選び方の口、測りの条件と「良くなった」の基準） | planning | — |
| p002 | CUBIC の実装と host の model の試験 | planning | p001、WS221 p008・p009 の残り |
| p003 | 実機の測り（Reno と CUBIC、取り合い）と既定の切り替えの判定 | planning | p002 |
| p004 | 規約の全文の見直し | planning | p002 |
