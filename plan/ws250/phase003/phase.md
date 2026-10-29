<!-- awesome-plan project=zedbsd record=ws250-p003 -->

# ws250-p003: 検出と取り消し

Status: in-progress（2026-10-29 P5、q1676-i01。実装済み・試験未実施（実機の S1〜S6 待ち）。cleared にしない）
Parent: [WS250](../ws.md)
Approval: p002 と同じ。2026-10-29 Q250-1「nvrtx の 2 s で STOP (推し)」。

## 範囲

[design.md](../design.md) 4.3〜4.6・4.8・6.1（scheduler の model の host 試験）。依存: p002。

## 受け入れ

build warning 0、host 試験 PASS。実機の S1〜S6 は p005 の後に Q1 の割当で。

## 実装（2026-10-29 P5、48cdd8025）

- recover.c: `nvrtx_recover_culprit`（期限を過ぎた run があれば、GR に載っている channel → 最後に始まった channel → submit の最も古い channel）、
  `nvrtx_recover_kill_state`、`nvrtx_recover_stopped`、STOP・RESET の param の組み立て。nvrtx-work.h に state の定義と `struct nvrtx_recover_view`。
- work.c: 全ての run に start stamp（ring の entry は 3、timed で 4）、run ごとの submit の時刻（64 個）、`nvrtx_work_channel_look`（最も古い終わらない
  run・始まったか・GPU の時刻）、`nvrtx_work_channel_resident`（0x409b00 と instance block の照合）、`_stop`（STOP_CHANNEL bImmediate=1）・`_reset`
  （RESET_CHANNEL）・`_free`。space の ready（`work->ready`）を driver の channel から分けた（共有の channel を止めても map は続く）。
- rpc.c: CHANNEL_ERROR の event を `gpu->channel_error` に渡す。
- runtime.c: reaper の thread（250 ms ごと・起こされた時、`channel_lock` の下で選んで止める、取り消しの唯一の実行者）、`runtime_kill`（STOP → 失敗なら
  RC を 500 ms 待つ → RESET → 証拠があれば FREE、state と `kill_end_us`）、RC の handler（spinlock だけ、type 79 は reaper から device の fault）、
  `stop_poll` に GPU の側（`runtime_session_stopped`）、`isolate`（quarantine、state だけで 0 か EIO）、reboot の stop は reaper の取り消しの終わりを待つ。
  `nvrtx.kill=0` は選んで LOST にするだけで止めない（以後は選ばない）。
- vk-node.c: 待ちの期限は `max(submit, kill_end) + 2 s`、過ぎたら reaper を起こし 50 ms ずつ待ち続け、channel が lost になるか submit から 8 s で EIO。
  submit も channel の lost を見る。FREED の channel の session は run が済まなくても memory を返す（`vk_node_quiet`、quarantine は除く）。
- design.md からの変更: `drv_gpu_report_session_error` は呼ばない（design.md 4.5 の 3 に理由）。STOP だけの probe の boot の語は作らない。

## 確かめた事（2026-10-29、host）

- `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws250/k build/ws250/k/vmunix` rc=0・warning 0。
- `plan/ws250/tests/nvrtx-recover-host-test.sh` 45 checks 0 failed（chid、kill の state、poll の表、STOP・RESET の byte、scheduler の model:
  犯人 1 つ・被害者が先に始まって GR を取られる・犯人 2 つ・共有の channel の犯人・1.5 s の有限の draw は止めない、を GR の照合あり・なしで）。
- `plan/tools/nvrtx/nvrtx-vk-host-test.sh` PASS 86 checks 0 failures。

## 未実施・残り

- 実機の S1〜S6（design.md 6.3、p004 の script、p005 の後にまとめて）。U1（STOP の返り）・U4（start stamp の意味）・U5（PTIMER）・U7（0x409b00）は実機で。
- p005 の前は executor の待ちが `node->lock` の中で延長に入る（design.md 7 節の注）。
