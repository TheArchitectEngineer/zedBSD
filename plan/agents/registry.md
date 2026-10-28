# 担当の台帳

今の担当だけ（[運用](protocol.md)）。返却した担当の行は、次の担当を起動する時に Q1 が消す。2026-10-26 までの台帳は
[history/agents-registry-through-20261026.md](../history/agents-registry-through-20261026.md)。

| 担当 | 定義 | 仕事（Queue） | worktree / branch | 状態 | 最後の merge |
| --- | --- | --- | --- | --- | --- |
| P1 | phase-runner | WS299 5330 の NVMe、O_DIRECT と残りの受け入れ（q1646） | `/home/awe/zedBSD-worktrees/p1-ws299c` / `agent/p1-ws299c` | running（2026-10-28 08:3x〜、5330） | 0500994dd（q1640） |
| P2 | phase-runner | WS291 RDNA の描画の速さ、Linux 以上まで自律（q1645） | `/home/awe/zedBSD-worktrees/p2-ws291d` / `agent/p2-ws291d` | running（2026-10-28 08:5x〜、10.0.10.3） | dc0e46e09（q1639） |
| P3 | phase-runner | WS290 p005〜p012（q1644） | `/home/awe/zedBSD-worktrees/p3-ws290g` / `agent/p3-ws290g` | running（2026-10-28 08:xx〜） | bb020cf80（q1642） |
| P4 | phase-runner | WS307 p001 の J5 の反映（q1649） | `/home/awe/zedBSD-worktrees/p4-ws307d` / `agent/p4-ws307d` | running | e7e87b0f4（q1648） |

- 同時に動かす数は 3（2026-10-28 ユーザー「N=3で作業します。」）に、WS307 の仕様の P4 を足して 4（2026-10-28 ユーザー「P4を立てて…」）。
- 試験の担当 T・ブラウザの B1 は今は立てていない（以前の試験の台帳は `plan/agents/T1/requests.md`・`T2/`・`T3/`）。
