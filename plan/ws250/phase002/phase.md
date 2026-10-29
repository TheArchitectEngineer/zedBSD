<!-- awesome-plan project=zedbsd record=ws250-p002 -->

# ws250-p002: session ごとの channel と rpc_lock

Status: cleared（2026-10-29 P5、q1676-i02。実機の S0 が通った）
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

- 5f684d746: session の channel の first fence を待たない（design 第 6 版 4.2、最後の ring entry の再利用はその fence の後）、host 試験
  `plan/ws250/tests/nvrtx-recover-host-test.sh`（chid の next-fit）。その後 runtime.c の `runtime_channel` に作成済みの速い道（atomic の load）。
- 確かめた事（2026-10-29 13:3x、host）: `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws250/k build/ws250/k/vmunix` rc=0・warning 0、
  `plan/ws250/tests/nvrtx-recover-host-test.sh` 14 checks 0 failed、`plan/tools/nvrtx/nvrtx-vk-host-test.sh` PASS 86 checks 0 failures。

## 残り

- 未実施: 実機（design.md 6.3 の S0: dmesg の `channel 16: session channel on runlist`、mview の fps が p011 の値から −5% 以内、desktop の回帰）、QEMU の boot の回帰。
- 実機で U3（chid 16〜 の USERD、別の VA の promote）が通らなければ、`channel %u: no session channel` の log で退避（共有）になる。GPU の側で働かない時は
  session の run が期限で喪失になる（逃げ道は boot の語 `nvrtx.channels=0`）。

## 実機（2026-10-29 P5、q1676-i02、[p004 の「実機」](../phase004/phase.md)）

- S0: dmesg に `channel 18〜20: session channel on runlist 0`（U3: chid 16〜 の USERD と、golden と違う VA の own の buffer の promote を RM が通し、GPU で動く）。
  mview scene-5000 49.50・49.33・49.17 fps（3 回、p011 の 49.1〜49.5 の内、−5% 以内）。`nvrtx.channels=0` の退避は要らなかった。
- rpc_lock と drain の 20 ms の turn: 実機の全 scenario で RPC の timeout・`rpc_unanswered` は出ていない。
