<!-- awesome-plan project=zedbsd record=queue-history q491 -->

# Queue q491: zdesktop-x11server の窓を Vulkan で表示（ws069-p011）

<!-- awesome-plan-current:start -->
Status: finished（2026-09-27）
Active Queue: q491
Executor: メインセッション（WS068・WS070・WS049・WS045・WS048・WS001 はユーザーの例外の許可でサブエージェントが並行）
<!-- awesome-plan-current:end -->

Approval: 2026-09-27 ユーザーの X server の判断（標準の Wayland と Vulkan）、「続けてください。」

| Order | Attempt | Phase | Status |
| --- | --- | --- | --- |
| 1 | q491-i01 | [ws069-p011](../ws069/phase011/phase.md) | cleared（X の窓を Vulkan の swapchain で表示、wl_shm は fallback。server の Wayland の dispatch を非 blocking に直した。Venus と実機の run1） |
