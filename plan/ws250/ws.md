<!-- awesome-plan project=zedbsd record=ws250 -->

# WS250: nvrtx の終わらない GPU の仕事の取り消しと復帰（system は動き続ける）

<!-- awesome-plan-current:start -->
Status: incomplete（2026-10-15 作成、p001 を P4 が q1206 で設計中）
Primary Milestone: MG006
Parent: [Master](../master.md)
Related: [WS037](../ws037/ws.md)（nvrtx）、[WS241](../ws241/ws.md)（RTX の描画の速さ、work channel を触る）、`src/drivers/gpu/gpu.c`（共通の GPU の層の監視と drv_gpu_recovery_ops）、`src/drivers/gpu/i915/reset.c`（isolate・reset を持つ手本）
Resume point: 2026-10-15 夜 ユーザーの即時中断で止めた。design.md の草稿は main（64f05b2c7）にあり、止めた時の未 commit の書き足し（+288/-243 行）は worktree p4 の plan/ws250/temp/design-partial-20261015.patch（git に入れない、64f05b2c7 に当たる）。同じ worktree の未追跡の plan/ws250/design-review.md（止める前に design-reviewer が書いた物）も残っている。再開はユーザーの指示で。以前: q1206（P4）で p001。design.md を書いた（2026-10-15）。design-reviewer の review（design-review.md）を受けて blocker・major を直し、Phase の表（p002〜p006）を確定する。
<!-- awesome-plan-current:end -->

## ユーザーの指示（原文、2026-10-15）

（外部の質問「A verifier can catch malformed SPIR-V, but not a valid shader that loops forever. Without DRM's reset path, is a watchdog in your driver the recovery story for now?」への Q1 の説明の後）
「その場合のキャンセルと復帰を実装した方がよくない？device lostでプログラムがそれ以上動けなくなるのはOK。でもシステムとしては動き続けてほしいな。」

## 今の振る舞い（Q1 の調べ、2026-10-15、main 5027043ab）

- executor は投入ごとに driver の fence を 2 秒待ち（`vk-node.c` の `VK_NODE_RUN_US`）、来なければ EIO → app に `VK_ERROR_DEVICE_LOST`（`vk-queue.c` の `queue_result`）。走り続ける shader は止めない。
- nvrtx の work channel は GPU に 1 本（`struct nvrtx_work_state`、全 session が共有）。詰まると後の投入（compositor を含む）も ring の空き待ち 2 秒で失敗する。
- 共通の層（`gpu.c` の監視）: job の期限（予約 10 秒・実行 60 秒・停止 10 秒）→ `stop_begin`/`stop_poll` → 確かめられなければ `isolate`（nvrtx は NULL）→ device 全体の `fault`（`runtime_fault` が node を faulted にし以後 EIO）。`reset` も NULL なので回復は reboot（GSP の unload）だけ。
- GSP の channel error の通知（`rpc.c` の `NVRTX_EVENT_CHANNEL_ERROR`）は log に出すだけ。
- 未確認: GSP の firmware が user の channel を自分で見張って止めるか。

## 目的と完了の条件

- 終わらない shader（または GPU の fault）を出した app は `VK_ERROR_DEVICE_LOST` で止まってよい。**他の app と compositor（desktop）は動き続ける**。
- 期限を過ぎた仕事を GPU の上で取り消す（channel の停止・作り直し、GSP の RM の口を使う）。
- 取り消しの後に GPU を使える状態に戻す（共通の層の `isolate`（その session だけ隔離）と、必要なら `reset`（device の作り直し）を実装する）。
- compositor の仕事が app の詰まった仕事の後ろで待たされない形（session ごとの channel か、取り消しで後ろの仕事を流し直す）を設計で決める。
- 試験: host 試験（期限・状態機械）と、RTX 機（10.0.30.5）の実機で「無限 loop の compute shader を出す試験の app → その app だけ DEVICE_LOST、desktop は動き続け、別の app が描ける」。

## Phase（案、p001 で確定）

| Phase | 内容 | 状態 |
| --- | --- | --- |
| [p001](phase001/phase.md) | 設計: GSP の RM の channel の停止・作り直しの口の調べ、session ごとの channel か共有の channel の作り直しか、isolate・reset の実装の形、期限の値、compositor の扱い、試験の道。design-reviewer を通す | in-progress（2026-10-18 Q1: phase.md に合わせた。旧: planned） |
| p002〜 | 実装（p001 で分割） | planned |
