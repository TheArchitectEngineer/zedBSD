# WS254 networkd: DHCP の失敗の後に 1 分おきに取り直す

- ID: ws254、Master: [master.md](../master.md)、Primary Milestone: MG001
- Status: incomplete（p001 cleared 2026-10-16: QEMU で確認、main 5cb63ff2a。実機は未実施。p002 規約の見直しが残り）
- 起点（2026-10-16 ユーザー）:「また、networkdは、DHCPが失敗したあと、1分おきにDHCPを試みる修正を入れるのがいいと思います。別なサブエージェントで実施してOKです。」
- 背景: 5330・RDNA 機で DHCP を取れずに ssh が届かず、ユーザーの手で network を直すことが続いた（BUG-326 の dhcpc の temp の残りは修正済み）。169.254 の割り当ては 2026-10-16 に外した。

## 完了の条件

1. 有線（と管理する WiFi）の interface で DHCP が失敗したら、networkd が 1 分おきに取り直す。lease が取れたら止める。cable の抜き差し・`net up` の今の動きは保つ。
   取り直しは毎回 interface を down → up → DHCP の順に行う（2026-10-16 ユーザー、Q1 経由:「DHCP の 1 分おきの取り直しですが、インタフェースのdown->up->DHCPという手順にしてください。でないとインタフェースのリセットが必要なケースに対処できないです。」）。
2. 静的な設定の interface には何もしない。
3. QEMU で、DHCP の server が無い間は 1 分おきに（毎回 down/up して）試し、server が現れた後に lease が取れることを確かめる（console/serial の log は使わない。down/up は guest の記録で見る）。

## Phase

| Phase | 目的 | 状態 |
| --- | --- | --- |
| [p001](phase001/phase.md) | 1 分おきの取り直し（毎回 down → up → DHCP）の実装と QEMU の確認 | in-progress（q1226、QEMU PASS、Q1 の判定待ち） |
| p002 | 規約の全文の見直し | planned |
