<!-- awesome-plan project=zedbsd record=queue -->

# Queue

<!-- awesome-plan-current:start -->
Status: idle（2026-10-01）
Active Queue: なし
Last finished Queue: [q514](history/queue-q514.md)（ws103-p007 cleared、WS103 完了）
<!-- awesome-plan-current:end -->

## q514

- Purpose: WS103 の p007（規約の全文で WS の全 source の変更を見直す、回帰、5330、V4 の性能の計測）。WS103 の最後の Phase。
- Timebox: この session。
- Focus: WS103（最優先）。
- Approval: current user, 2026-09-30 夜、「ws103完了まで自走してください。phaseごとにコミットしてください。」
- Exact approved scope: ws103-p007（[WS103](ws103/ws.md)、Phase の記録は完了の処理で削除） だけ。見直しで見つけた規約の違反の修正は範囲に入る。新しい機能、HAL、toolchain は範囲の外。
- Executor: メインのエージェント（Q1）。規約の見直しは読むだけの subagent に並行で頼む。

| Attempt | Phase | Status | Dependency | Selection reason |
| --- | --- | --- | --- | --- |
| q514-i01 | ws103-p007（[WS103](ws103/ws.md)、Phase の記録は完了の処理で削除） | cleared | ws103-p002〜p006 cleared | WS103 の最後の Phase。ユーザーの自走の指示 |

Dependency graph: `ws103-p002..p006 (cleared) -> q514-i01/ws103-p007`。

## Upcoming Work Outlook

WS103 は完了（2026-10-01）。次の Queue はユーザーの指示の後。
