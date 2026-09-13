# GPU 監督契約の p010 差分（q313）

p009 の [共通契約](../phase009/gpu-job-contract.md) と [transport 契約](../phase009/transport-sync.md) に対する変更点だけを記す。実装は [gpu.c](../../../src/drivers/gpu/gpu.c)、契約は [gpu.h](../../../include/drivers/gpu.h)（内部ops版9）、Venus は [venus.c](../../../src/drivers/gpu/venus/venus.c) / [transport.c](../../../src/drivers/gpu/venus/transport.c)。UAPI（ioctl番号・layout・size）は変更していない。

## 停止期限の起点（S1）

| 項目 | p009 | p010 |
| --- | --- | --- |
| `stop_deadline` の設定時点 | session が最初の局所 ERROR になった時点 | 共通monitorが `stop_begin` を実際に呼び、REQUESTED→PENDING へ遷移した時点 |
| `stop_begin` を呼ぶ条件 | REQUESTED かつ admitted ioctl/job action なし | 上に加え、backend 所有で `job_deadline > now` の未完了 job が無いこと |
| escalation の条件 | `now >= stop_deadline`（REQUESTED でも） | `stop_state == PENDING && now >= stop_deadline`、または recovery/stop_begin が無い |

実行期限内の job は自分の期限だけで監督され、その退役または期限切れの後に初めて停止 interval が始まる。busy 中は期限が進まない。

## graceful close と commit 済み job（S2）

`gpu_session_fail_locked(session, error, hard)`。hard=1（期限切れ、fault cancel、device loss）は従来どおり backend 所有の全 generation を終端し全 binding を ERROR にする。hard=0 は `gpu_close` だけが使い、`GPU_JOB_COMMITTED` の記録と、その sequence に bound された fence を残す。残した job の結果は `drv_gpu_complete` が公開する（成功を含む）。RESERVED と ordinary command（`GPU_JOB_NONE`）は close 時に終端し、その fence は ENODEV。`gpu_close` は `drain` の後に `gpu_fence_retire` を呼び、完了しなかった binding だけを ENODEV にする。

close は残した job の退役まで `drain` で待つ。Linux と異なり process の exit がその分遅れる。実行期限（既定60秒）まで完了しない job は期限で ERROR になり、停止確認へ進む。

## 共通層へ移した判断（S3）

| 判断 | p009（Venus） | p010（共通層） |
| --- | --- | --- |
| native 仕事の有無 | `native_commands_submitted` を command/submit で立て、`stop_begin` で idle shortcut | `session->native_submitted` を GPU_COMMAND / GPU_COMMAND_SUBMIT / GPU_JOB_COMMIT の直前に立てる。`native_submitted == 0` かつ backend 所有記録なしなら `stop_begin` を呼ばず FINISHED |
| fault cancel 後の session 失敗 | `venus_job_cancel(fault)` が `drv_gpu_report_session_error(EIO)` | `gpu_job_action_ioctl` が cancel(fault) 成功後（commit 失敗時の fault cancel を含む）に `gpu_session_fail_locked(EIO, hard)` |
| 未 commit 予約の回収 | quiesce ACK 後に Venus が RESERVED slot を ENODEV callback で終端 | `stop_poll` が 0 を返した後、共通層が `reservation_pending` の記録へ `jobs->cancel(fault=0)` を呼び、成功したものを `drv_gpu_complete(ECANCELED)` で退役（先の ERROR は保持） |
| 新規受付の拒否 | `session->stopping` を command/submit/reserve/capacity で検査 | 共通層の sticky error（`gpu_session_error_locked`）。Venus の `stopping` は stop_poll の妥当性検査だけに残す |
| control 通信の期限 | transport 内定数 10000 ms | `CONFIG_GPU_CONTROL_MS`（既定 10000、make/menuconfig）。`VENUS_WAIT_MILLISECONDS` はこれを参照 |

`stop_poll` の契約は「native access の停止と、投稿済み descriptor と callback の退役」。RESERVED slot は idle を妨げない（Venus の `drv_venus_transport_idle` は RESERVED を無視する）。quiesce 能力の無い host では `stop_begin` が ENOTSUP を返し、共通層が escalation する。

## monitor 起床（S4）と B6

`gpu_session_leave` は `stop_state` が REQUESTED/PENDING のときだけ monitor を起こす。`gpu_capacity_changed_locked` の起床は、graceful close が job 退役を検知するために保持した。`drv_gpu_recovery_ready` は削除した。

## session 隔離（S5）

`drv_gpu_recovery_ops.isolate(device, session)`（任意、`stop_begin` がある backend のみ有効）。停止 interval 超過、`stop_begin`/`stop_poll` の EAGAIN 以外の失敗、または stop 契約の欠如で escalation するとき、`isolate` があればそれを呼ぶ。成功すると session は `GPU_STOP_QUARANTINED`、`device->quarantined` を増やし、device の `error` は 0 のまま。失敗または `isolate` が無ければ従来どおり `fault` + `drv_gpu_report_error`。

隔離後も共通層は通常どおり `drain`、`resource_destroy`、`close` を呼ぶ。backend は隔離 context の callback を `isolate` の中で全て終端し（drain は待たない）、destroy/close では host へ何も送らず資源を controller に保持する。Venus: `drv_venus_transport_isolate` がその context の POSTED/RESERVED chain を QUARANTINED にして callback を EIO で公開し、`venus_resource_destroy`/`venus_close` は `session->quarantined` で早期に返る。表示 lease の記録は `drv_venus_display_forget_locked` で host 送信なしに外す（scanout の実状態は reset まで残る）。device から遅れて返った隔離 chain は `venus_queue_collect` が FREE に戻し、容量通知を出す（callback が未公開なら通常完了経路で退役）。

回収: `gpu_recover_open` は `device->error != 0 || device->quarantined != 0` で、他の open/share が無い sole fresh open のとき checked reset を行い、成功で両方を 0 にする。隔離中に容量が減った分は次の idle open まで戻らない。自動 escalation（隔離が続いて容量が尽きた場合）は入れていない。

B4: `venus_queue_collect` は validate の EIO（framing/identity 不一致）だけを transport 全体の失敗にし、host の定義済み refusal（EINVAL/ENOMEM 等）は当該 request の error として公開する。supervised marker の refusal は job の ERROR、quiesce の refusal は `stop_poll` の EIO → 隔離になる。

## 限定 fixture

[core-verification/](core-verification/) と各 fixture の puts 行を参照。gpu-supervision に S1/S2（既存の 60 秒継続・遅延成功）、S3（metadata-only close の shortcut、予約回収）、S5（隔離、peer 継続、idle open での reset 回収）を追加。gpu-job に committed-job-survives-close と reserved-terminates-at-close。venus-backend に隔離 context の destroy/close/reset、venus-transport に per-context quarantine・peer 継続・遅延返却の回収・supervised refusal の局所化。
