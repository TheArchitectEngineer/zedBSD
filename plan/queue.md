<!-- awesome-plan project=zedbsd record=queue -->

# Queue

<!-- awesome-plan-current:start -->
Status: active（2026-09-30）
Active Queue: q512
Last finished Queue: [q511](history/queue-q511.md)（ws103-p004 cleared）
<!-- awesome-plan-current:end -->

## q512

- Purpose: WS103 の p005（libvulkan の WSI が、Wayland の target の present ごとに新しい fence を作って送る）。
- Timebox: この session。
- Focus: WS103（最優先）。
- Approval: current user, 2026-09-30 夜、「ws103完了まで自走してください。phaseごとにコミットしてください。」（p005・p006・p007 を順に実行する指示。
  Queue は Phase ごとに 1 つ作る）。
- Exact approved scope: [ws103-p005](ws103/phase005/phase.md) だけ（[design](ws103/design.md) §2.4 の WSI の側）。compositor（p006）、HAL、toolchain、kernel は範囲の外。
- Executor: メインのエージェント（Q1）。

| Attempt | Phase | Status | Dependency | Selection reason |
| --- | --- | --- | --- | --- |
| q512-i01 | [ws103-p005](ws103/phase005/phase.md) | in-progress | ws103-p001 cleared（design） | 設計の順。ユーザーの自走の指示 |

Dependency graph: `ws103-p001 (cleared) -> q512-i01/ws103-p005 -> (future) ws103-p006`。

## Upcoming Work Outlook

WS103 の p005（WSI の fence）→ p006（fence を poll に、UAPI を閉じる）→ p007（規約と回帰）。どれも Queue で承認が要る。
