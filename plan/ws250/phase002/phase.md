<!-- awesome-plan project=zedbsd record=ws250-p002 -->

# ws250-p002: session ごとの channel と rpc_lock

Status: in-progress（2026-10-29 P5、q1676-i01。実装中・試験未実施）
Parent: [WS250](../ws.md)
Approval: 2026-10-27 ユーザー「すぐ取り組むものの一つです。」、2026-10-29 Q250-4「案 A を必須に」、2026-10-29（Q1 の中継）「作業の手が空いてしまったら、テストなしで実装だけ進める戦略で、残っているWSのphaseをまんべんなく進めましょう。」

## 範囲

[design.md](../design.md) 4.2（session の channel）・4.7（rpc_lock と drain）・6.1（chid の host 試験）。取り消し・判定は p003。

## 受け入れ

build（amd64 vmunix）warning 0、`plan/tools/nvrtx/nvrtx-vk-host-test.sh` PASS、`plan/ws250/tests/nvrtx-recover-host-test.sh` PASS。実機の S0（design.md 6.3）は Q1 の割当で後に。

## 実装（2026-10-29 P5）

- 72f15b880 rpc_lock（Q1 の指示で全ての RM の call に）: `nvrtx_rm_allocate_in`・`nvrtx_rm_control`・`nvrtx_rm_free_in` が自分で取る（呼び手が持てば
  `mutex_owned` で取らない）、reply を読む呼び手（display.c の `display_control`、perf.c の `perf_state`）は `nvrtx_rpc_lock` で囲む、drain は 20 ms の
  turn ごとに取って手放し、待つ者がいれば 1 tick 寝る。
- f535c24b1 session の channel: `struct nvrtx_work_channel`（nvrtx-gpu.h）に driver の channel（chid 4）と session の channel を一般化、
  `nvrtx_work_channel_open`・`_close`・`_submit`・`_passed`・`_wait`・`_wait_late`・`_run_time`（work.c）、session の arena と 2 MiB の区画
  （own の context buffer を privileged に、ring・semaphore・fence の page）、promote に VA の配列（gr.c、`nvrtx_mmu_gr_entries_privileged` を mmu.c に）、
  runtime.c の `runtime_channel`（最初の frame の前、`channel_lock` の下、失敗は共有の chid 4 へ退避）と `nvrtx_runtime_session_close`（node_close から）、
  vk-node.c は session の channel で submit・待ち・passed、`recover.c` の `nvrtx_recover_chid_next`（next-fit）、stage.c の `nvrtx.channels=0`・`nvrtx.kill=0`。
- build: `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws250/k build/ws250/k/vmunix` rc=0、warning 0（f535c24b1 の tree）。

## 残り

- design.md 第 6 版の直し（first fence を待たない、ENOSPC の計算）を code に入れる。
- host 試験 `plan/ws250/tests/nvrtx-recover-host-test.sh`（chid）と既存の vk の host 試験。
- 未実施: 実機（S0）、QEMU の boot の回帰。
