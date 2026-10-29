# 担当の台帳

今の担当だけ（[運用](protocol.md)）。返却した担当の行は、次の担当を起動する時に Q1 が消す。2026-10-26 までの台帳は
[history/agents-registry-through-20261026.md](../history/agents-registry-through-20261026.md)。

| 担当 | 定義 | 仕事（Queue） | worktree / branch | 状態 | 最後の merge |
| --- | --- | --- | --- | --- | --- |
| P1 | phase-runner | ws309 (q1662-i06) | `/home/awe/zedBSD-worktrees/p1-ws309f` / `agent/p1-ws309f` | running | 429c6b2dc（merge ffad67106） |
| P2 | phase-runner | ws291 の NVIDIA の段 (q1670-i05) | `/home/awe/zedBSD-worktrees/p2-ws291q` / `agent/p2-ws291q` | running | 0b35eee90（merge 5aa6b291a） |
| P3 | phase-runner | ws311 (q1673-i01) | `/home/awe/zedBSD-worktrees/p3-ws311` / `agent/p3-ws311` | running | 642d812f3（merge 1fd28c4de） |
| P4 | phase-runner | ws307 (q1665-i08) | `/home/awe/zedBSD-worktrees/p4-ws307l` / `agent/p4-ws307l` | running | d6139052b（merge 6b115e9fd） |
| P5 | phase-runner | ws307 p015 (q1674-i01) | `/home/awe/zedBSD-worktrees/p5-ws307lock` / `agent/p5-ws307lock` | returned（2026-10-29。p5-ws307p002 は upload 待ちで残す） | 8755132b3（merge 478c88fdf） |
| P6 | phase-runner | ws312 (q1672-i01) | `/home/awe/zedBSD-worktrees/p6-ws312` / `agent/p6-ws312` | returned（2026-10-29、WS312 completed） | 930169a83（merge 45c65539d） |
| P7 | phase-runner | ws313 (q1675-i02) | `/home/awe/zedBSD-worktrees/p7-ws313b` / `agent/p7-ws313b` | running | 1f4acb4ee（merge 52256eecc） |

- 同時に動かす数は 6（2026-10-29 ユーザーの P1〜P6 の指示）。VF2（主）と 5330（従）は P3、10.0.10.3 は P2。
- P6 の branch（merge しない）: agent/p6-ws296→p6-ws296b（2d17bf9ed から）→p6-ws296c（f31a4954d から）、agent/p6-ws282→p6-ws282b（1190b629c から）→p6-ws282c（c60cdbd44 から）、WS296 は agent/p6-ws296c 1809873bd で全 Phase 実装済み、agent/p6-ws243b（飛ばした記録 b13f3dae7）、agent/p6-ws223（未着手）。
- 試験の担当 T・ブラウザの B1 は今は立てていない。

- 10.0.10.3 の時間の枠（plan/tools.md）: 最初の枠は P1（06:20〜07:05）、次は P2（07:05〜07:50）、以後交代（ネットワーク優先）。
