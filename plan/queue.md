<!-- awesome-plan project=zedbsd record=queue -->

# Queue

<!-- awesome-plan-current:start -->
Status: active（2026-09-30）
Active Queue: q511
Last finished Queue: [q510](history/queue-q510.md)（ws103-p003 cleared）
<!-- awesome-plan-current:end -->

## q511

- Purpose: WS103 の p004（compositor を dedicated の import に切り替え、`GPU_RESOURCE_IMPORT`・`GPU_RESOURCE_DESTROY` を消す）。
- Timebox: この session。
- Focus: WS103（最優先）。
- Approval: current user, 2026-09-30 夜、「続けてください。」（Q1 の「p004 から進めますか」への返事）。
- Exact approved scope: [ws103-p004](ws103/phase004/phase.md) だけ（[design](ws103/design.md) §2.3 の compositor の側と §2.5 の buffer の部分、p003 から移した「dedicated の無い
  image の capability の import を拒む」、実行の前に見つけた「dedicated の import は image の capability だけ」の直し、偽の buffer の probe）。fence（p005・p006）、HAL、toolchain は範囲の外。
- Executor: メインのエージェント（Q1）。

| Attempt | Phase | Status | Dependency | Selection reason |
| --- | --- | --- | --- | --- |
| q511-i01 | [ws103-p004](ws103/phase004/phase.md) | in-progress | ws103-p002・p003 cleared | 設計の順（p002・p003 の後） |

Dependency graph: `ws103-p002 (cleared), ws103-p003 (cleared) -> q511-i01/ws103-p004 -> (future) ws103-p006`。

## Upcoming Work Outlook

WS103 の独立の p005（WSI の fence）、p004・p005 の後の p006。どれも Queue で承認が要る。
