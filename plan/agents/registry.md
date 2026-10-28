# 担当の台帳

今の担当だけ（[運用](protocol.md)）。返却した担当の行は、次の担当を起動する時に Q1 が消す。2026-10-26 までの台帳は
[history/agents-registry-through-20261026.md](../history/agents-registry-through-20261026.md)。

| 担当 | 定義 | 仕事（Queue） | worktree / branch | 状態 | 最後の merge |
| --- | --- | --- | --- | --- | --- |
| P1 | phase-runner | WS308（q1650 paused、13 時に再開） | `/home/awe/zedBSD-worktrees/p1-ws308` / `agent/p1-ws308` | returned（11:xx、待機） | f43be9280 |
| P2 | phase-runner | WS291（q1651 paused、13 時に再開） | `/home/awe/zedBSD-worktrees/p2-ws291e` / `agent/p2-ws291e` | returned（待機） | 07abbf57d |
| P3 | phase-runner | WS290 p005〜p012（q1644） | `/home/awe/zedBSD-worktrees/p3-ws290g` / `agent/p3-ws290g` | running（2026-10-28 08:xx〜） | bb020cf80（q1642） |
| P4 | phase-runner | WS307 p001（q1643〜q1649、済み）、BUG-347（resolved） | `/home/awe/zedBSD-worktrees/p4-ws307d` / `agent/p4-ws307d` | returned（2026-10-28） | 23c76dd28 |

- 同時に動かす数は 3（2026-10-28 ユーザー「N=3で作業します。」）。WS307 の仕様の間だけ P4 を足して 4、2026-10-28 ユーザー「P4は停止して、N=3に戻します。」で 3 に戻した。
- 試験の担当 T・ブラウザの B1 は今は立てていない（以前の試験の台帳は `plan/agents/T1/requests.md`・`T2/`・`T3/`）。
