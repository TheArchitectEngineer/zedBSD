# 担当の台帳

今の担当だけ（[運用](protocol.md)）。返却した担当の行は、次の担当を起動する時に Q1 が消す。2026-10-26 までの台帳は
[history/agents-registry-through-20261026.md](../history/agents-registry-through-20261026.md)。

| 担当 | 定義 | 仕事（Queue） | worktree / branch | 状態 | 最後の merge |
| --- | --- | --- | --- | --- | --- |
| P1 | phase-runner | WS299 の残り（q1657） | `/home/awe/zedBSD-worktrees/p1-ws299d` / `agent/p1-ws299d` | running（5330） | 8b250a81c（q1654） |
| P2 | phase-runner | WS291 UCLK の DPM（q1658） | `/home/awe/zedBSD-worktrees/p2-ws291h` / `agent/p2-ws291h` | running（10.0.10.3） | e1314f4ce（q1655） |
| P3 | phase-runner | WS290 p017 の残り（q1659、cleared） | `/home/awe/zedBSD-worktrees/p3-ws290h` / `agent/p3-ws290h` | returned・終了（2026-10-28 21:23 ユーザー「P3はいったん終了、再開は明示的な指示です。」） | 351040f44（q1659、merge b99e96139） |
| P4 | phase-runner | 記録の矛盾の解消（q1660） | `/home/awe/zedBSD-worktrees/p4-closeout` / `agent/p4-closeout` | running | 23c76dd28（WS307 p001） |

- 2026-10-28 21:23 から P3 を終了し、動くのは P1・P2。P3 の再開はユーザーの明示の指示（WS290 は FPGA 側の設計の完了待ち）。
- 同時に動かす数は 3（2026-10-28 ユーザー「N=3で作業します。」）。WS307 の仕様の間だけ P4 を足して 4、2026-10-28 ユーザー「P4は停止して、N=3に戻します。」で 3 に戻した。
- 試験の担当 T・ブラウザの B1 は今は立てていない（以前の試験の台帳は `plan/agents/T1/requests.md`・`T2/`・`T3/`）。
