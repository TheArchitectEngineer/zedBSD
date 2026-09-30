<!-- awesome-plan project=zedbsd record=queue -->

# Queue

<!-- awesome-plan-current:start -->
Status: active（2026-09-30）
Active Queue: q513
Last finished Queue: [q512](history/queue-q512.md)（ws103-p005 cleared）
<!-- awesome-plan-current:end -->

## q513

- Purpose: WS103 の p006（compositor の fence を poll だけに、`/dev/gpu0` と `--gpu` の削除、GPU の UAPI を `gpu-zedbsd.c` に閉じる、V1 の確かめ）。
- Timebox: この session。
- Focus: WS103（最優先）。
- Approval: current user, 2026-09-30 夜、「ws103完了まで自走してください。phaseごとにコミットしてください。」
- Exact approved scope: [ws103-p006](ws103/phase006/phase.md) だけ（[design](ws103/design.md) §2.4 の compositor の側・§2.5・§2.6）。libvulkan、HAL、toolchain、kernel、evdev は範囲の外。
- Executor: メインのエージェント（Q1）。

| Attempt | Phase | Status | Dependency | Selection reason |
| --- | --- | --- | --- | --- |
| q513-i01 | [ws103-p006](ws103/phase006/phase.md) | in-progress | ws103-p004・p005 cleared | 設計の順。ユーザーの自走の指示 |

Dependency graph: `ws103-p004 (cleared), ws103-p005 (cleared) -> q513-i01/ws103-p006 -> (future) ws103-p007`。

## Upcoming Work Outlook

WS103 の p005（WSI の fence）→ p006（fence を poll に、UAPI を閉じる）→ p007（規約と回帰）。どれも Queue で承認が要る。
