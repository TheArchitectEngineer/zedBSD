# WS014 p010 結果: GPU監督の共通化仕上げと局所隔離（q313）

状態: 実装中。S1/S2/S4/B6を実装し限定fixtureと実kernel buildで確認した。S3（Venus→framework移管）とS5（局所隔離、ops版9）、および実QEMU受入は未着手。q313-i01 / p010をclearedにしない。

前提はp009自己レビュー [gpu-stack-review5.md](../gpu-stack-review5.md)。段階と全体像は [phase.md](phase.md)。実装は [gpu.c](../../../src/drivers/gpu/gpu.c)、契約は [gpu.h](../../../include/drivers/gpu.h)。source/patch/資料のgit add/commit/pushはユーザーが行う。

## 完了した段階（2026-09-14）

### S1: 停止期限の起点を stop_begin の実呼出しへ（D1 / B3）

`gpu_session_fail_locked` は失敗時に `stop_deadline` を設定しなくなった。共通monitorは、そのsessionにbackend所有で実行期限内（`job_deadline > now`）の未完了jobが残る間は `stop_begin` を呼ばず、jobの実行/予約期限だけで監督する。全jobが退役または期限切れになった時点で `stop_begin` を呼び、その瞬間に `stop_deadline = now + CONFIG_GPU_JOB_STOP_MS` を arm する。escalation（action 3）は `stop_state == GPU_STOP_PENDING && now >= stop_deadline` のときだけ発火する。

効果: 既定60秒の実行期限内で正当に走る長いjobを持つprocessが終了しても、10秒後にdevice全体がfaultしなくなった。busy中も `stop_deadline` は進まない。

### S2: close時のcommit済みjob監督継続（D2）

`gpu_session_fail_locked` に `hard` 引数を追加した。hard=1（期限切れ・fault・device loss）は従来どおり全backend所有generationを終端する。hard=0（graceful close、`gpu_close` から `ENODEV`）は `GPU_JOB_COMMITTED` のjobを終端せず監督を続け、その fence は実完了時に `drv_gpu_complete` が本来の結果（成功を含む）を公開する。未commitの `GPU_JOB_RESERVED` と ordinary command（`GPU_JOB_NONE`）は従来どおり close で終端する。`gpu_close` は `drv_gpu_report_session_error` の代わりに graceful fail を直接呼び、`gpu_fence_retire` を drain の後へ移して、完了しなかった binding だけを `ENODEV` にする。

効果: 最後のframeをcommitして終了したclientの fence が、compositor側で本来の成功を受け取る。event待ちで未完了のjobは実行期限で ERROR。

### S4（部分）: monitor起床の限定 / B6: 未使用API除去

`gpu_session_leave` は、そのsessionが停止処理中（`stop_state` が REQUESTED/PENDING）のときだけ monitor を起こす。健全なioctl終了では起こさない。`gpu_capacity_changed_locked` の起床は、graceful closeのjob退役検知に必要なため今回は保持した（完全なD4は継続）。未使用の `drv_gpu_recovery_ready` を `gpu.c` と `gpu.h` から除去した。

## 限定検証

実 production の `src/drivers/gpu/gpu.c` / `gpu-fence.c` を include する GPU core fixture を通常と ASan/UBSan で実行し、全て PASS。

| fixture | 確認 |
| --- | --- |
| gpu-supervision | 4slot飽和・64record自己回収・observer pin・domain77・timeout/EINTR・**60秒実行期限の正常継続**・局所失敗と別open継続・**遅延成功callback**・10秒予約+停止fallback・**global fallback**・reset epoch。停止期限の起点変更後も local_starts/local_polls/global_faults の不変を確認 |
| gpu-job | **committed jobがproducer close後も生存し実成功で fence SIGNALED**（S2）、**未commit予約はclose時 ENODEV 終端**（新規sub-case追加）、rollback/admission/no-alloc commit/consume race/fault isolation |
| gpu-fence-close | ordinary command は close 時 ENODEV（drain前）で late success 排除（S2下でも不変） |
| gpu-fence / fence-reuse / framework / sharing / topology / scanout / placement / handle-fd | 回帰 PASS |
| venus-backend / venus-transport | 回帰 PASS（S3未着手のためVenus側は不変） |
| gpu build selection（6platform × Venus有無） | PASS |

実 kernel build: `make -j16 ZEDBSD_CONFIG=plan/ws014/tests/config-wayland-amd64.mk vmunix` が `gpu.o`/`gpu-fence.o` を含めて成功し、`amd64 vmunix check: PASS`。`git diff --check` OK。

変更ファイル SHA256:

| ファイル | SHA256 |
| --- | --- |
| src/drivers/gpu/gpu.c | `15c40d6894e1250574295bfaad0d61cb24c35f65657f0eb0fe9d26ad336a605c` |
| include/drivers/gpu.h | `00a9dede8cca49aeba230c6c94f1d582ed3d46b93ba08130595b0debd17caf19` |
| plan/ws014/tests/gpu-job.c | `123e22703d67e84f3fe456fa5228df0ba8ba7bc9cfb5197e4ba7728cf0f86430` |

## 残作業

- **S3**: Venus backend の判断（`native_commands_submitted` による停止shortcut、`venus_job_cancel(fault)` 後の `drv_gpu_report_session_error`、quiesce ACK後のRESERVED終端loop、`stopping` flag、control 10秒watchdog定数）を共通層へ移す。venus-backend/transport fixture の追従が必要。
- **S5**: 停止未確認contextをdevice faultではなくsession隔離にする（`recovery->isolate`、ops版9）。B4（supervised markerエラーをsession error化）、B5（隔離sessionでdrainを呼ばない）を含む。fixture（gpu-supervision に隔離、venus-backend/transport に isolate/collect/quarantine）追加。
- **実QEMU受入**: 既存7件の回帰 + `producer-exit-delayed` / `producer-exit-hang` の新設、`producer-exit` 期待値更新。private host awe@10.0.10.25、isolated q312-quiesce / q312-quiesce-delay pair。
- **同期**: q312完了とq313開始のGitHub Issues/Project公開はこのsessionでは自動承認レビューにより保留し、outbox/draftに記録した。
