<!-- awesome-plan project=zedbsd record=ws241 -->

# WS241: nvrtx（RTX）の描画の速さ

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-15 Q1 作成、ユーザー「RTXの最適化は独立WSにして、今から作業します。」）
Primary Milestone: MG006
Parent: [Master](../master.md)
Related: [WS037](../ws037/ws.md)（nvrtx の本体）、[WS242](../ws242/ws.md)（RTX 20/30/40 の対応）
<!-- awesome-plan-current:end -->

## ユーザーの指示（原文、2026-10-15）
「RTXでの描画が遅いです。RPi4と大差ないです。」「RTXの最適化は独立WSにして、今から作業します。」

## 単一目標
RTX 機（i5-13400F＋RTX 2070、方法 A の素の起動）の Keiland の desktop で、compositor の frame を vblank（60 fps）で回す。

## これまで（WS037 の phase007 の 9 節から移した要旨）
- T1-695（前）: draw 0.1 ms、frame は静止 12.9・Files 29.8・Terminal＋Files 19.5〜20.8 ms で、vkQueueSubmit・Present の待ちが律速。CPU は余っている。
- P4 の見立てと直し（main に merge 済み、WS037 の記録）: F1 present が vblank まで node->lock を持たない（38fa1c750）、F2 GPU の fence の待ちを最初の 2 ms spin（a811fe4f2）、計数の行 nvrtx.vk.perf=1、F3 揃わない upload を GPU の quad で（9e74e7be1）。F4（BAR1 の write-combining）は未。
- T1-696（後）: 静止 8.8・**Files 17.0 ms（ほぼ 60 fps）**・Terminal＋Files **27.8 ms（悪化）**。flips・lock の待ち・cpu copies 0 は目標内。**GPU の run の平均 3.8 ms**（目標は数十〜数百 µs）、Terminal＋Files で 5 秒に約 430 回。結果は plan/agents/T1/results-20261015-t1-696.md。

## Phase
- ws241-p001: run の 3.8 ms の原因（GPU の実行か、投入・待ちの道か）と Terminal＋Files の悪化の解析と直し（担当 P1）。
