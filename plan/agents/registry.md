# 担当の台帳

今の担当だけ（[運用](protocol.md)）。返却した担当の行は、次の担当を起動する時に Q1 が消す。2026-10-26 までの台帳は
[history/agents-registry-through-20261026.md](../history/agents-registry-through-20261026.md)。

| 担当 | 定義 | 仕事（Queue） | worktree / branch | 状態 | 最後の merge |
| --- | --- | --- | --- | --- | --- |
| P1 | phase-runner | WS299 5330 の NVMe の速さ、fstrim の後に測り直して受け入れまで（q1640） | `/home/awe/zedBSD-worktrees/p1-ws299b` / `agent/p1-ws299b` | running（2026-10-28 06:0x〜、5330） | cc1869b7c（q1634） |
| P2 | phase-runner | WS291 RDNA の描画の速さ、Linux 以上まで自律（q1639） | `/home/awe/zedBSD-worktrees/p2-ws291c` / `agent/p2-ws291c` | running（2026-10-28 06:xx〜、10.0.10.3） | cd763014d（q1635） |
| P3 | phase-runner | WS290 p005〜p012（q1644） | `/home/awe/zedBSD-worktrees/p3-ws290g` / `agent/p3-ws290g` | running（2026-10-28 08:xx〜） | bb020cf80（q1642） |
| P4 | phase-runner | WS307 インストーラの仕様の確定（q1643） | `/home/awe/zedBSD-worktrees/p4-ws307` / `agent/p4-ws307` | running（2026-10-28 07:1x〜） | 0a3f17258（q1610） |

- 同時に動かす数は 3（2026-10-28 ユーザー「N=3で作業します。」）に、WS307 の仕様の P4 を足して 4（2026-10-28 ユーザー「P4を立てて…」）。
- 試験の担当 T・ブラウザの B1 は今は立てていない（以前の試験の台帳は `plan/agents/T1/requests.md`・`T2/`・`T3/`）。
