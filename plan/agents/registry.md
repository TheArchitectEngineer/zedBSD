# 担当の台帳

今の担当だけ（[運用](protocol.md)）。返却した担当の行は、次の担当を起動する時に Q1 が消す。2026-10-26 までの台帳は
[history/agents-registry-through-20261026.md](../history/agents-registry-through-20261026.md)。

| 担当 | 定義 | 仕事（Queue） | worktree / branch | 状態 | 最後の merge |
| --- | --- | --- | --- | --- | --- |
| P1 | phase-runner | ws309 (q1662-i02) | `/home/awe/zedBSD-worktrees/p1-ws309b` / `agent/p1-ws309b` | running（i01 は返却、i02 起動） | ef99d4812（merge b9f358a5a） |
| P2 | phase-runner | ws291 (q1663、cleared) | `/home/awe/zedBSD-worktrees/p2-ws291j` / `agent/p2-ws291j` | returned（2026-10-29） | 919a05b78 |
| P3 | phase-runner | ws221 (q1664) | `/home/awe/zedBSD-worktrees/p3-ws221` / `agent/p3-ws221` | running（2026-10-29 01:37） | — |
| P4 | phase-runner | ws307 (q1665) | `/home/awe/zedBSD-worktrees/p4-ws307e` / `agent/p4-ws307e` | running（2026-10-29 01:37） | — |
| P5 | kernel-and-driver-designer | 詳細の決まっていない WS の設計 (q1666-i02) | `/home/awe/zedBSD-worktrees/p5-design-b` / `agent/p5-design-b` | running（i01 は返却、全部 merge 63386d8a4） | 9f74fd78e |
| P6 | phase-runner | WS296・WS282〜WS284・WS223 の実装 (q1667-i02) | `p6-ws296b`・`p6-ws282b`（`/home/awe/zedBSD-worktrees/`）/ `agent/` に同じ名前 | running（i01 は返却、merge しない） | — |

- 同時に動かす数は 6（2026-10-29 ユーザーの P1〜P6 の指示）。VF2（主）と 5330（従）は P3、10.0.10.3 は P2。
- P6 の branch（merge しない）: agent/p6-ws296→p6-ws296b（2d17bf9ed から）、agent/p6-ws282→p6-ws282b（1190b629c から）、agent/p6-ws243b（飛ばした記録 b13f3dae7）、agent/p6-ws223（未着手）。
- 試験の担当 T・ブラウザの B1 は今は立てていない。
