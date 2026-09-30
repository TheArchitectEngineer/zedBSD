<!-- awesome-plan project=zedbsd record=queue -->

# Queue

<!-- awesome-plan-current:start -->
Status: active（2026-09-30）
Active Queue: q510
Last finished Queue: [q509](history/queue-q509.md)（ws103-p002: attempt は uncleared、同日の追いの確かめで Phase は cleared）
<!-- awesome-plan-current:end -->

## q510

- Purpose: WS103 の p003（libvulkan: VK_KHR_dedicated_allocation と VK_KHR_get_memory_requirements2、image の capability の import での記述の照合、bind の守り）。
- Timebox: この session。
- Focus: WS103（最優先）。
- Approval: current user, 2026-09-30 夜、「消して作り直してOKです。次に進みましょう。」（Q1 が「独立して進められるのは p003 と p005、進める場合は Queue を作る」と示した後）。
  Q1 は p004 の前提の p003 を選んだ。
- Exact approved scope: [ws103-p003](ws103/phase003/phase.md) だけ（[design](ws103/design.md) §2.3 の libvulkan の側の 1〜5 と §3 の p003 の行）。compositor の変更（p004）、fence（p005）、HAL、toolchain は範囲の外。
- Executor: メインのエージェント（Q1）。

| Attempt | Phase | Status | Dependency | Selection reason |
| --- | --- | --- | --- | --- |
| q510-i01 | [ws103-p003](ws103/phase003/phase.md) | in-progress | ws103-p001 cleared（design） | p004 の前提。p002 と独立 |

Dependency graph: `ws103-p001 (cleared) -> q510-i01/ws103-p003 -> (future) ws103-p004`。

## Upcoming Work Outlook

WS103 の p005（WSI の fence）、p003 の後の p004。どれも Queue で承認が要る。
