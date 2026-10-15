<!-- awesome-plan project=zedbsd record=ws250-p001 -->

# ws250-p001: nvrtx の取り消しと復帰の設計

Status: in-progress（2026-10-15、q1206 P4。design.md を書き、design-reviewer の review を受けている）
Parent: [WS250](../ws.md)
Approval: 2026-10-15 ユーザー「その場合のキャンセルと復帰を実装した方がよくない？device lostでプログラムがそれ以上動けなくなるのはOK。でもシステムとしては動き続けてほしいな。」

## 範囲

ws.md の「目的と完了の条件」を満たす設計（code は書かない）。design-reviewer を通す。

## 結果

- 設計: [design.md](../design.md)（2026-10-15 P4）。要点: session ごとの channel（vaspace は共有）、検出は executor の run の期限（`CONFIG_NVRTX_RUN_MS` 既定 2 s）、GPU の上の取り消しは GSP-RM の `STOP_CHANNEL`（bImmediate）→ `RESET_CHANNEL` → FREE（代替 `FIFO_DISABLE_CHANNELS`）、RC の event を session に結ぶ、`isolate` は channel の FREE を確かめた時だけ 0、`reset` は作らない（既定の案）、Phase は p002〜p006（必須 17〜22 LW）。
- 読んだ code: `include/drivers/gpu/gpu.h`、`src/drivers/gpu/gpu.c`（監視 7028〜7271 行、reset 7440〜7520 行、session の失敗 6744〜6850 行）、`src/drivers/gpu/i915/reset.c`、nvrtx の `work.c`・`runtime.c`・`rpc.c`・`channel.c`・`node.c`・`vk-node.c`・`vk-queue.c`・`vk-execute.c`・`vk-session.c`・`vk-pipeline.c`・`objects.c`・`gr.c`・`compute.c`、`userland/desktop/libvulkan/{context.c,sync.c}`、`userland/tests/{gpu-recovery,vkquad,keiland-shot}`。
- 読んだ公開の source（他の worktree の `plan/ws037/temp/` と、この WS の `plan/ws250/temp/ogkm-src/`（commit しない）: design.md 10 節の一覧）。
- 未確認（実機でしか決まらない、design.md 9 節）: STOP_CHANNEL が user の channel の無限 loop を止めるか、RESET_CHANNEL が同じ runlist の他の channel を壊さないか、chid 16〜 の USERD の page の割り当て、golden context と違う VA の own buffer の promote、GSP の firmware が自分で RC を送るか（と何秒か）。
- design-reviewer: [design-review.md](../design-review.md)（結果はこの節に追記）。
