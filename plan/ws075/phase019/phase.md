<!-- awesome-plan project=zedbsd record=ws075p019 -->

# ws075-p019: 性能 3: present mode と vsync（ws031-p027）

Phase ID: `ws075-p019`
Parent: [WS075](../ws.md)
Status: in-progress（2026-09-29、サブエージェント、worktree `.claude/worktrees/ws075-rps`。FIFO の先行（UAPI なし）を実装、MAILBOX・IMMEDIATE は UAPI の提案 [proposed/p019-present-mode.md](../proposed/p019-present-mode.md)）
Phase disposition: normal

## 範囲（正本は WS031 の ws.md の p027 の行と [phase018](../../ws031/phase018/phase.md) の 5）

present mode（FIFO・MAILBOX・IMMEDIATE）で vsync を選ぶ。今は build の option（`I915_PRESENT_NO_VSYNC`）だけで、既定は FIFO:
presentation は flip の latch まで presenting thread（compositor）を待たせる（`display/present.c` の `i915_present_shared()`、
2026-09-29 の実機の perf で present ごとに flip 約 5 ms）。UAPI で運べなければ変更を事前に提示する。

## 着手前に決めること

- compositor（`userland/desktop/wayland`）は直さない（WS075 の規則）。libvulkan の present mode から display の present の
  flag へ運ぶ道（`include/drivers/gpu.h` の `gpu_display_present`）。UAPI を変えるなら差分を `plan/ws075/proposed/` に置いて提示する。
- MAILBOX の「新しい frame が勝つ」の実装（今の NO_VSYNC の armed flip の buffer への描き直しは copy が latch と重なり tear する）。

## 依存

[ws075-p008](../phase008/phase.md)。
