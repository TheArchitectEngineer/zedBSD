<!-- awesome-plan project=zedbsd record=ws110 -->

# WS110: コンポジタの通常起動を既定にし試験modeを明示

Status: incomplete
Primary Milestone: MG006
Related Milestones: MG001
Parent: [Master](../master.md)
Queue: none /実装未承認
Resume point: p001・p002 は cleared（T1-168）。p003 は全文規約の見直しと build が済み、Q1 の判定待ち。その後 WS の完了の判定。

## Objective / scope / acceptance

通常利用に--sessionが必要な既定の有限起動を廃止し、試験用有限起動を--testingで明示。3OS共通compositorと関連test scripts/docs/CI callersを整合させる。FreeBSDのgraphicallogin/sessiond/認証backend移植、renderer/OSauthority/toolchain変更は含めない。

T1 引数なし通常sessionが期限なしで動きLog Out/desktop表示が成立。T2 --testingのみ明示的有限試験、期限/frame/矛盾/option順序契約を検証。T3 全現役compositor testsのmode/cleanup/外部watchdogと3OS同じ動作。T4 final source全文規約・該当build/tests/最後のboot/doc検証。これらは将来の実装acceptanceであり今回は未実施。

## Standards / dependencies / decisions

[Guardrail](../guardrail.md)、[全文C規約](../coding-style.md)、[automation](../standards/automation.md)。移動style例外なし。WS109 completed/実機acceptanceは前提evidence、WS105/WS108の現行callersを読む。--session alias/timeout opt gating/auth test組合せはdesignの推奨案、まだ実装仕様の承認ではない。

| ID | Purpose | Goal | Status | Dependencies |
| --- | --- | --- | --- | --- |
| [ws110p001](phase001/phase.md) | 通常/testing roleと引数契約 | cleared（2026-10-05 Q1） | in-progress（実装済み、T1 待ち） | user implementation instruction + contract |
| [ws110p002](phase002/phase.md) | test/launcher/docs整合 | cleared（2026-10-05 Q1） | in-progress（置き換え済み、T1 待ち） | p001 API/mode output |
| [ws110p003](phase003/phase.md) | 近final全文規約/回帰 | cleared（2026-10-05 Q1） | in-progress（見直し・build 済み、判定は Q1） | p001/p002 actual final outputs |

Phase詳細は実装指示・仕様決定後、有限scopeに分けて作成。Queue membership無し。現存codeのstyleをpolicyとしない。

## Event history

2026-10-02 current user「検討だけまずは」: WS109完了後の別の起動仕様目標として新規計画。source/test changesは無し、未順位/未実行。推奨案と影響inventoryを保存、chatの説明にリンクする。Issue/Project同期は保留。
