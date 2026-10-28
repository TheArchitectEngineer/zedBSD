# 担当の台帳

今の担当だけ（[運用](protocol.md)）。返却した担当の行は、次の担当を起動する時に Q1 が消す。2026-10-26 までの台帳は
[history/agents-registry-through-20261026.md](../history/agents-registry-through-20261026.md)。

| 担当 | 定義 | 仕事（Queue） | worktree / branch | 状態 | 最後の merge |
| --- | --- | --- | --- | --- | --- |
| P1 | phase-runner | ws309 (q1662-i04) | `/home/awe/zedBSD-worktrees/p1-ws309d` / `agent/p1-ws309d` | running | 30650bb3b（merge 2bf956f01） |
| P2 | phase-runner | ws291 (q1668-i02) | `/home/awe/zedBSD-worktrees/p2-ws291l` / `agent/p2-ws291l` | running | f5c01e255（merge 239e8ed7f） |
| P3 | phase-runner | ws221 (q1664) | `/home/awe/zedBSD-worktrees/p3-ws221` / `agent/p3-ws221` | running（2026-10-29 01:37） | — |
| P4 | phase-runner | ws307 (q1665-i04) | `/home/awe/zedBSD-worktrees/p4-ws307h` / `agent/p4-ws307h` | running | 1f487021a（merge 241c03f1d） |
| P5 | kernel-and-driver-designer | 設計 (q1666-i03、web の取得は禁止) | `/home/awe/zedBSD-worktrees/p5-design-c` / `agent/p5-design-c` | running | 6bddb1eda（merge d94ceac00） |
| P6 | phase-runner | WS282〜WS284・WS223 の実装 (q1667-i04) | `/home/awe/zedBSD-worktrees/p6-ws282c` / `agent/p6-ws282c` | running（merge しない） | — |

- 同時に動かす数は 6（2026-10-29 ユーザーの P1〜P6 の指示）。VF2（主）と 5330（従）は P3、10.0.10.3 は P2。
- P6 の branch（merge しない）: agent/p6-ws296→p6-ws296b（2d17bf9ed から）→p6-ws296c（f31a4954d から）、agent/p6-ws282→p6-ws282b（1190b629c から）→p6-ws282c（c60cdbd44 から）、WS296 は agent/p6-ws296c 1809873bd で全 Phase 実装済み、agent/p6-ws243b（飛ばした記録 b13f3dae7）、agent/p6-ws223（未着手）。
- 試験の担当 T・ブラウザの B1 は今は立てていない。
