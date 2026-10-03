<!-- awesome-plan project=zedbsd record=q511 -->

# q511（finished 2026-09-30）


- Purpose: WS103 の p004（compositor を dedicated の import に切り替え、`GPU_RESOURCE_IMPORT`・`GPU_RESOURCE_DESTROY` を消す）。
- Timebox: この session。
- Focus: WS103（最優先）。
- Approval: current user, 2026-09-30 夜、「続けてください。」（Q1 の「p004 から進めますか」への返事）。
- Exact approved scope: [ws103-p004](ws103/phase004/phase.md) だけ（[design](ws103/design.md) §2.3 の compositor の側と §2.5 の buffer の部分、p003 から移した「dedicated の無い
  image の capability の import を拒む」、実行の前に見つけた「dedicated の import は image の capability だけ」の直し、偽の buffer の probe）。fence（p005・p006）、HAL、toolchain は範囲の外。
- Executor: メインのエージェント（Q1）。

| Attempt | Phase | Status | Dependency | Selection reason |
| --- | --- | --- | --- | --- |
| q511-i01 | [ws103-p004](ws103/phase004/phase.md) | cleared | ws103-p002・p003 cleared | 設計の順（p002・p003 の後） |

Dependency graph: `ws103-p002 (cleared), ws103-p003 (cleared) -> q511-i01/ws103-p004 -> (future) ws103-p006`。


## Outcome

- q511-i01 / ws103-p004: **cleared**。compositor は client の GPU buffer を dedicated の import で取り込み、`GPU_RESOURCE_IMPORT`・`GPU_RESOURCE_DESTROY` を削除。
  wire の値は backend（`gpu-zedbsd.c`）で確かめてから Vulkan へ。libvulkan は dedicated の無い image の capability の import と、allocation の capability の dedicated の import を拒む。
  偽の buffer の probe は断られ（QEMU の Venus）、C1・C2・boot test・5330 の passthrough の smoke PASS。GitHub へは未公開。
