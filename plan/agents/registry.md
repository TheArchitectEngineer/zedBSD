# 担当の台帳

今の担当だけ（[運用](protocol.md)）。返却した担当の行は、次の担当を起動する時に Q1 が消す。2026-10-26 までの台帳は
[history/agents-registry-through-20261026.md](../history/agents-registry-through-20261026.md)。

| 担当 | 定義 | 仕事（Queue） | worktree / branch | 状態 | 最後の merge |
| --- | --- | --- | --- | --- | --- |
| P1 | phase-runner | WS299 5330 の NVMe の速さ、fstrim の後に測り直して受け入れまで（q1640） | `/home/awe/zedBSD-worktrees/p1-ws299b` / `agent/p1-ws299b` | running（2026-10-28 06:0x〜、5330） | cc1869b7c（q1634） |
| P2 | phase-runner | WS291 RDNA の描画の速さ、Linux 以上まで自律（q1639） | `/home/awe/zedBSD-worktrees/p2-ws291c` / `agent/p2-ws291c` | running（2026-10-28 06:xx〜、10.0.10.3） | cd763014d（q1635） |
| P3 | phase-runner | WS290 仕様担当とのやり取りと p003〜（q1641） | `/home/awe/zedBSD-worktrees/p3-ws290e` / `agent/p3-ws290e` | running（2026-10-28 06:2x〜） | b289a76dc（q1638） |

- P4 は返却（q1610、最後の merge 0a3f17258）。同時に動かす数は 3（2026-10-28 ユーザー「N=3で作業します。」）。
- 試験の担当 T・ブラウザの B1 は今は立てていない（以前の試験の台帳は `plan/agents/T1/requests.md`・`T2/`・`T3/`）。
