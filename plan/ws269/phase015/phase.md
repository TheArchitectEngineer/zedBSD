<!-- awesome-plan project=zedbsd record=ws269-p015 -->

# ws269-p015 安定性（板の連続実行・電源の入れ直し・GPU の reset からの回復）

- 親: [WS269](../ws.md)、Status: in-progress（2026-10-21 Q1。短い確かめ (1)(2) は済み。(3) reset からの回復と (4) 長時間は未実施）
- 起点: [proposal-next-phases.md](../proposal-next-phases.md) の N2。ユーザー 2026-10-21「続けてください。」
- 板の kernel: build/uat/vf2-gpu/vmunix（main 06f1ba788、板の /boot/boot0/vmunix と cmp で一致）。

## 範囲と結果

| 確かめ | 手順 | 結果（2026-10-21、板の実機） |
| --- | --- | --- |
| (1) 1 回の起動で連続 10 回 | SSH で `sudo sh -c "gpu-selftest; vkmatrix; vkcoherency"` を 10 回（約 36 s/回） | 10 回とも gpu-selftest 13 ok / 0 fail / 0 skip、vkmatrix 15/15、vkcoherency PASS。dmesg に context reset・device lost・lockup 0 行 |
| (2) 電源の入れ直し 3 回 | `VF2_HOST=… VF2_PASSWORD=… plan/tools/vf2/run.sh build/uat/vf2-gpu/vmunix build/uat/vf2-gpu/runs/stabN`、各回の後に (1) の 3 つ | 3 回とも stages 全部 ok（present は -）、gpu-selftest 13/13、vkmatrix 15/15、vkcoherency PASS、reset 0。deploy から render node まで 38 s |
| (3) GPU の reset（意図的な lockup）からの回復 | 未実施。lockup を起こす試験の job（例: 終わらない compute の kernel）が要る | 未実施 |
| (4) 長時間（vkdemo 30 分など） | 負荷・耐久の試験はユーザーに確かめてから夜間（AGENTS.md） | 未実施（ユーザーの確認待ち） |

## 残件

- (3): 終わらない kernel を投げて、時間切れ → context の reset → 次の job が通るかを板で。実装の担当に試験の program を依頼する。
- (4): ユーザーの確認の後。
