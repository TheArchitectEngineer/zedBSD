<!-- awesome-plan project=zedbsd record=queue-history q488 -->

# Queue q488: zdesktop-x11server（ws069-p008）

<!-- awesome-plan-current:start -->
Status: finished（2026-09-27）
Active Queue: q488
Executor: メインセッション（サブエージェントは使わない）
<!-- awesome-plan-current:end -->

Approval: 2026-09-27 ユーザーの X server の判断（単体、rootless、組み込める形）と「続けてください。」

| Order | Attempt | Phase | Status |
| --- | --- | --- | --- |
| 1 | q488-i01 | [ws069-p008](../ws069/phase008/phase.md) | uncleared（zdesktop-x11server は Venus と実機で動く。x11-p005 の frame 数が BUG-057 で未達。BUG-057 は kernel の unix socket の送り手が起きない所まで特定） |

依存: ws069-p005、ws035-p073・p074（cleared）。
