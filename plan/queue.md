<!-- awesome-plan project=zedbsd record=queue -->

# Queue

<!-- awesome-plan-current:start -->
Status: active（2026-09-30）
Active Queue: q509
Last finished Queue: [q508](history/queue-q508.md)（ws103-p001 cleared。WS103 の設計 design.md 改訂 3）
<!-- awesome-plan-current:end -->

## q509

- Purpose: WS103 の p002（compositor の起動の問い合わせを VK_KHR_display へ、`--direct` の削除）。
- Timebox: この session。
- Focus: WS103（最優先）。
- Approval: current user, 2026-09-30 夜、「書き直してOKです。p002を実行してください。」
- Exact approved scope: [ws103-p002](ws103/phase002/phase.md) だけ（[design](ws103/design.md) §2.1・2.2 と §3 の p002 の行）。`userland/desktop/wayland/` の
  `GPU_GET_INFO`・`GPU_DISPLAY_QUERY`・`GPU_DISPLAY_MODE`・`GPU_DISPLAY_CLAIM`・`GPU_DISPLAY_PRESENT`・`GPU_DISPLAY_RELEASE` と `--direct` の道を消し、
  それを読む試験を直すか退役させる。buffer の import（`RESOURCE_IMPORT`）と fence（`FENCE_QUERY`）、libvulkan、HAL、toolchain は範囲の外。
- Executor: メインのエージェント（Q1）。

| Attempt | Phase | Status | Dependency | Selection reason |
| --- | --- | --- | --- | --- |
| q509-i01 | [ws103-p002](ws103/phase002/phase.md) | in-progress | ws103-p001 cleared（design） | 最優先の WS103 の、依存の無い最初の実装の Phase。ユーザーが実行を指示 |

Dependency graph: `ws103-p001 (cleared) -> q509-i01/ws103-p002`。

## Upcoming Work Outlook

WS103 の p003（libvulkan の import の照合）・p005（WSI の fence）、p002 の後の p004。どれも Queue で承認が要る。
