# 担当の台帳

今の担当だけ（[運用](protocol.md)）。返却した担当の行は、次の担当を起動する時に Q1 が消す。2026-10-26 までの台帳は
[history/agents-registry-through-20261026.md](../history/agents-registry-through-20261026.md)。

| 担当 | 定義 | 仕事（Queue） | worktree / branch | 状態 | 最後の merge |
| --- | --- | --- | --- | --- | --- |
| P1 | phase-runner | ws309 (q1662-i08) | `/home/awe/zedBSD-worktrees/p1-ws309h` / `agent/p1-ws309h` | running | abf15895b（merge d441dcafb） |
| P2 | phase-runner | ws242 (q1678-i01) | `/home/awe/zedBSD-worktrees/p2-ws242` / `agent/p2-ws242` | running（実装だけ、merge は試験の後） | fdca948fe（merge ae2e1283c） |
| P3 | phase-runner | ws221 p010 (q1679-i01) | `/home/awe/zedBSD-worktrees/p3-ws221f` / `agent/p3-ws221f` | running | cc2ad2075（merge 3edf82c60） |
| P4 | phase-runner | ws307 (q1665-i08) | `/home/awe/zedBSD-worktrees/p4-ws307l` / `agent/p4-ws307l` | running | d6139052b（merge 6b115e9fd） |
| P5 | phase-runner | ws250 (q1676-i01) | `/home/awe/zedBSD-worktrees/p5-ws250` / `agent/p5-ws250` | running（実装だけ、merge は試験の後） | 8755132b3（merge 478c88fdf） |
| P6 | phase-runner | ws243 (q1677-i01) | `/home/awe/zedBSD-worktrees/p6-ws243c` / `agent/p6-ws243c` | running（実装だけ、merge は試験の後） | 930169a83（merge 45c65539d） |
| P7 | phase-runner | ws313 (q1675-i02) | `/home/awe/zedBSD-worktrees/p7-ws313b` / `agent/p7-ws313b` | running | 1f4acb4ee（merge 52256eecc） |

- 同時に動かす数は 6（2026-10-29 ユーザーの P1〜P6 の指示）。VF2（主）と 5330（従）は P3、10.0.10.3 は P2。
- P6 の branch（merge しない）: agent/p6-ws296→p6-ws296b（2d17bf9ed から）→p6-ws296c（f31a4954d から）、agent/p6-ws282→p6-ws282b（1190b629c から）→p6-ws282c（c60cdbd44 から）、WS296 は agent/p6-ws296c 1809873bd で全 Phase 実装済み、agent/p6-ws243b（飛ばした記録 b13f3dae7）、agent/p6-ws223（未着手）。
- 試験の担当 T・ブラウザの B1 は今は立てていない。

- 10.0.10.3 の時間の枠（plan/tools.md）: 最初の枠は P1（06:20〜07:05）、次は P2（07:05〜07:50）、以後交代（ネットワーク優先）。
