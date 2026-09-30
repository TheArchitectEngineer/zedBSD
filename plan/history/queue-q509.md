<!-- awesome-plan project=zedbsd record=q509 -->

# q509（finished 2026-09-30）


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
| q509-i01 | [ws103-p002](ws103/phase002/phase.md) | uncleared | ws103-p001 cleared（design） | 最優先の WS103 の、依存の無い最初の実装の Phase。ユーザーが実行を指示 |

Dependency graph: `ws103-p001 (cleared) -> q509-i01/ws103-p002`。


## Outcome

- q509-i01 / ws103-p002: **uncleared**。compositor の `GPU_GET_INFO`・`GPU_DISPLAY_QUERY`・`GPU_DISPLAY_MODE`・`GPU_DISPLAY_CLAIM`・`GPU_DISPLAY_PRESENT`・`GPU_DISPLAY_RELEASE` と `--direct` を削除（commit `e27565f3` ほか）。
  build warning 0、QEMU の Venus で C1（2 件）・C2 PASS、boot test PASS。5330 の passthrough の smoke（`c5-hw.sh`）は FAIL だが、変更の前の compositor の image でも同じく FAIL（原因は p002 の外、未確認）。
- 再開の条件: [phase.md](../ws103/phase002/phase.md) の「再開の条件」。GitHub へは未公開。
