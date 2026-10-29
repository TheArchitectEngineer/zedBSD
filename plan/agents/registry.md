# 担当の台帳

今の担当だけ（[運用](protocol.md)）。返却した担当の行は、次の担当を起動する時に Q1 が消す。2026-10-26 までの台帳は
[history/agents-registry-through-20261026.md](../history/agents-registry-through-20261026.md)。

| 担当 | 定義 | 仕事（Queue） | worktree / branch | 状態 | 最後の merge |
| --- | --- | --- | --- | --- | --- |
| P1 | phase-runner | ws309 (q1662-i09) | `/home/awe/zedBSD-worktrees/p1-ws309i` / `agent/p1-ws309i` | running | 8429b0efa |
| P2 | phase-runner | ws068 (q1686-i01) | `/home/awe/zedBSD-worktrees/p2-ws068` / `agent/p2-ws068` | running（試験待ちの branch: agent/p2-ws242 fda52e406、agent/p2-ws217 fb1b270d2） | 45aa87120（merge fe49f1d60） |
| P3 | phase-runner | ws221 p010 (q1679-i02) | `/home/awe/zedBSD-worktrees/p3-ws221g` / `agent/p3-ws221g` | running | bffc05d7d（merge 4b39403c5） |
| P4 | phase-runner | ws307 (q1665-i10) | `/home/awe/zedBSD-worktrees/p4-ws307n` / `agent/p4-ws307n` | running（試験待ちの branch: agent/p4-ws283 b7fd8d23e） | 580a117e3（merge ee83a6eda） |
| P5 | phase-runner | ws243 (q1677-i02) | `/home/awe/zedBSD-worktrees/p5-ws243` / `agent/p5-ws243` | running | 005871516（merge fbe855149） |
| P6 | phase-runner | ws282 (q1667-i05) | `/home/awe/zedBSD-worktrees/p6-ws282d` / `agent/p6-ws282d` | running（実装だけ。試験待ちの branch: agent/p6-ws243c（P5 が合わせ直し中）、agent/p6-ws299、agent/p6-ws300、agent/p6-ws052） | 930169a83（merge 45c65539d） |
| P7 | phase-runner | ws313 (q1675-i05) | `/home/awe/zedBSD-worktrees/p7-ws313e` / `agent/p7-ws313e` | running | 5a6b4aa4f（merge 8771f10f1） |

- 同時に動かす数は 6（2026-10-29 ユーザーの P1〜P6 の指示）。VF2（主）と 5330（従）は P3、10.0.10.3 は P2。
- P6 の branch（merge しない）: agent/p6-ws296→p6-ws296b（2d17bf9ed から）→p6-ws296c（f31a4954d から）、agent/p6-ws282→p6-ws282b（1190b629c から）→p6-ws282c（c60cdbd44 から）、WS296 は agent/p6-ws296c 1809873bd で全 Phase 実装済み、agent/p6-ws243b（飛ばした記録 b13f3dae7）、agent/p6-ws223（未着手）。
- 試験の担当 T・ブラウザの B1 は今は立てていない。

- 10.0.10.3 の時間の枠（plan/tools.md）: 最初の枠は P1（06:20〜07:05）、次は P2（07:05〜07:50）、以後交代（ネットワーク優先）。
