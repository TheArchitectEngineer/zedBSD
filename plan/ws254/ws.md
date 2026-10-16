# WS254 networkd: DHCP の失敗の後に 1 分おきに取り直す

- ID: ws254、Master: [master.md](../master.md)、Primary Milestone: MG001
- Status: incomplete（p001 を q1226 で実行中）
- 起点（2026-10-16 ユーザー）:「また、networkdは、DHCPが失敗したあと、1分おきにDHCPを試みる修正を入れるのがいいと思います。別なサブエージェントで実施してOKです。」
- 背景: 5330・RDNA 機で DHCP を取れずに ssh が届かず、ユーザーの手で network を直すことが続いた（BUG-326 の dhcpc の temp の残りは修正済み）。169.254 の割り当ては 2026-10-16 に外した。

## 完了の条件

1. 有線（と管理する WiFi）の interface で DHCP が失敗したら、networkd が 1 分おきに取り直す。lease が取れたら止める。cable の抜き差し・`net up` の今の動きは保つ。
2. 静的な設定の interface には何もしない。
3. QEMU で、DHCP の server が無い間は 1 分おきに試し、server が現れた後に lease が取れることを確かめる（console/serial の log は使わない）。

## Phase

| Phase | 目的 | 状態 |
| --- | --- | --- |
| [p001](phase001/phase.md) | 1 分おきの取り直しの実装と QEMU の確認 | in-progress（q1226） |
| p002 | 規約の全文の見直し | planned |
