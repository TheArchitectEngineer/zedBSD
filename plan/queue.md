<!-- awesome-plan project=zedbsd record=queue -->

# Queue

<!-- awesome-plan-current:start -->
Status: active（2026-09-30）
Active Queue: q508
Last finished Queue: [q507](history/queue-q507.md)（ws074-p099 cleared。Acid2 100.00% exact pixel match）
<!-- awesome-plan-current:end -->

## q508

- Purpose: WS103（compositor を libvulkan だけにする）の調査と設計。
- Timebox: この session。
- Focus: WS103（2026-09-30 夜 ユーザーが最優先に）。
- Approval: current user, 2026-09-30 夜、「では、このqueueをメインエージェントで実行してください。」（Q1 が示した q508 の案に対して）。
- Exact approved scope: [ws103-p001](ws103/phase001/phase.md) だけ。調査と設計文書（ioctl ごとの置き換え、fence の世代の照合と i915 の native の external fence、
  OS の backend の境界の形、macro を外した build の確かめ方、buffer ごとの import の費用、p002 以降の Phase の分け方）と design-reviewer のレビュー。
  code の変更、HAL（`hal.h`）、toolchain、Linux・FreeBSD の backend、evdev は範囲の外。libvulkan への追加は設計として書くだけ。
- Executor: メインのエージェント（Q1）。

| Attempt | Phase | Status | Dependency | Selection reason |
| --- | --- | --- | --- | --- |
| q508-i01 | [ws103-p001](ws103/phase001/phase.md) | in-progress | なし（D1〜D3 は 2026-09-30 夜に決定） | 最優先の WS103 の最初の Phase。ユーザーが実行を指示 |

Dependency graph: `q508-i01/ws103-p001`（外部の前提なし）。

## Upcoming Work Outlook

p001 の設計で決める ws103-p002 以降（例: 起動の問い合わせ → `--direct` の削除 → 記述の照合 → fence → UAPI の型の隔離 → 規約と回帰）。承認は別。
