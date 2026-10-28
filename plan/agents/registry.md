# 担当の台帳

今の担当だけ（[運用](protocol.md)）。返却した担当の行は、次の担当を起動する時に Q1 が消す。2026-10-26 までの台帳は
[history/agents-registry-through-20261026.md](../history/agents-registry-through-20261026.md)。

| 担当 | 定義 | 仕事（Queue） | worktree / branch | 状態 | 最後の merge |
| --- | --- | --- | --- | --- | --- |
| P1 | phase-runner | WS308 の続き（q1652） | `/home/awe/zedBSD-worktrees/p1-ws308b` / `agent/p1-ws308b` | running（13:00〜） | f43be9280（q1650） |
| P2 | phase-runner | WS291 の続き（q1653） | `/home/awe/zedBSD-worktrees/p2-ws291f` / `agent/p2-ws291f` | running（13:00〜、10.0.10.3） | 07abbf57d（q1651） |
| P3 | phase-runner | WS290 p009〜（q1644、13 時に resume） | `/home/awe/zedBSD-worktrees/p3-ws290g` / `agent/p3-ws290g` | running（13:00〜） | 1e41a9a86 |
| P4 | phase-runner | WS307 p001（q1643〜q1649、済み）、BUG-347（resolved） | `/home/awe/zedBSD-worktrees/p4-ws307d` / `agent/p4-ws307d` | returned（2026-10-28） | 23c76dd28 |

- 同時に動かす数は 3（2026-10-28 ユーザー「N=3で作業します。」）。WS307 の仕様の間だけ P4 を足して 4、2026-10-28 ユーザー「P4は停止して、N=3に戻します。」で 3 に戻した。
- 試験の担当 T・ブラウザの B1 は今は立てていない（以前の試験の台帳は `plan/agents/T1/requests.md`・`T2/`・`T3/`）。
