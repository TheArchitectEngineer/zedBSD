<!-- awesome-plan project=zedbsd record=ws250-p003 -->

# ws250-p003: 検出と取り消し

Status: cleared（2026-10-29 P5、q1676-i02。実機の S1〜S5 が通った（直し 1 つ）。S6 は stuck が出ず条件に当たらない）
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

## 実機（2026-10-29 P5、q1676-i02、[p004 の「実機」](../phase004/phase.md)）

- S2〜S5: 犯人だけが DEVICE_LOST（2.0〜2.1 s。S4 の 2 つ目は 6.3 s）、mview・compositor・後の vkhang --loops は動き続けた。`stuck` 0。
- 見つけて直した（3ad5d28fc、vk-node.c・nvrtx-node.h）: S4 で mview が `executor fail: the work of fence 203 did not finish (8018 ms)` で巻き添えの喪失になった。
  被害者の待ちの上限（8 s）が submit から数えていたため、犯人 2 つ（2 s＋STOP 2.3 s を 2 回）の後ろでは足りなかった。上限を max(submit, 最後の取り消しの終わり)
  から数え、reaper が取り消している間（`reaper_busy`）は越えても待つ形にした（design.md 4.4 を直した）。直した後の r3 で mview は完走（S4 12.90 fps）。
- 診断の log を足した（runtime.c）: `channel %u: stop answered %d after %u ms`、`channel %u: reset answered %d after %u ms`。

## U1〜U7（実機の観測、2026-10-29）

| # | 結果 |
| --- | --- |
| U1 STOP_CHANNEL（bImmediate=1）の結果 | 多くは 0 を 2.29〜3.72 s で返し、その間に GSP の RC（type 109、ctxsw の timeout）が来る。時々 status 0x65（NV_ERR_TIMEOUT）で約 4.0 s で失敗し、その後の RESET_CHANNEL が約 22 ms で 0、RC type 43 が来る。どちらも FREED。1 回の取り消しは 2.3〜4.0 s（設計の見込みの「数十 ms〜1 s」より長い） |
| U2 RESET_CHANNEL が他を壊すか | RESET の道（r1 の S3 で 2 回、S5 の probe で 1 回）の後も、control の vkhang --loops・desktop は動いた（他の channel の context は失われていない）。mview が同時に走る時の RESET は未観測 |
| U3 chid 16〜 の USERD と別の VA の promote | 通る（chid 16〜34 で描けた） |
| U4 start stamp の意味 | 直接は未確認: 全ての判定で GR の照合（resident 1）が先に当たり、「最後に始まった」の規則は使われなかった。被害者（mview）が犯人に選ばれたことは 0 回 |
| U5 PTIMER | 実装は PTIMER を読まない（始まりの時刻は semaphore の release の timestamp、経過は CPU の時刻）。この点は確かめる対象が無い |
| U6 GSP の ctxsw の RC が 2 s より先に来るか | 来ない: 全ての回で nvrtx の 2.0〜2.1 s の喪失が先、RC type 109 は STOP の中。`nvrtx.kill=0`（S1）では GSP が 10 s の内に自分で RC type 109 を出した（正確な秒は未計測） |
| U7 0x409b00 を CPU から読めるか | 読める: 実機の全ての喪失（r1〜r3 と probe、約 20 回）で `resident 1`（GR の current context が犯人の channel の instance block と一致） |

- 判断の点（Q1 へ、推しで記録）: GR の停滞は 2 s（期限）＋STOP の 2.3〜4 s で 4.3〜6 s。RESET_CHANNEL は STOP の後で約 22 ms だった。STOP を飛ばして RESET を先に
  呼べば停滞を約 2 s に縮められる見込みがあるが、Q250-1（「nvrtx の 2 s で STOP」）の決定から外れるので変えていない。縮めるなら別の Phase で RESET 単独の時間を実機で量る。
