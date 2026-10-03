# zedBSD GPU スタック p009 設計・実装レビュー (機能性)

対象: `cca12445..f62dc635` (p009 / q312)。`src/drivers/gpu/gpu.c` (+1206)、`venus/{transport,venus}.c`、`libvulkan/{queue,sync,objects,wsi-swapchain,wsi-display,context,external-fence}.c`、`include/{uapi/gpu-job.h,drivers/gpu.h}`、phase009 の契約資料。
方法: 差分の通読と、既存コード (p008 時点) との突き合わせ。実行はしていない (最終受入 8 VM は前回私が実行済み)。セキュリティ観点は対象外。

---

## 1. 設計の評価

責務分担は妥当で、review4 の回答書どおりに実装されている。

- **共通層 (gpu.c)**: open ごとの completion record、fence generation、予約/実行/停止の期限、device 全体の capacity sequence と waitq、session 単位の sticky error、stop 状態機械 (REQUESTED→PENDING→FINISHED)、device 1 本の monitor thread、fresh open 時の checked reset。
- **backend (Venus)**: try-reserve/commit/cancel/capacity、`stop_begin`/`stop_poll` (private SUBMIT_3D payload + CPU0 fence で host が context 内全 VkDevice の `DeviceWaitIdle` を確認)、`fault` (transport 全体の quarantine)、`reset`。
- **libvulkan**: native 準備 → capacity QUERY → 回収 → 非待機 RESERVE → EAGAIN なら queue/device/context mutex を全部外して WAIT (250 ms 上限) → 再試行。private fence の EMPTY/READY/PREPARING/INFLIGHT/TERMINAL と一括 reset。direct acquire の pipe + `ppoll`。error 公開 (`vulkan_context_error` / `vulkan_device_error`) の一元化と wake 登録。

R1 (submit の OOM) は解消しており、submit-load-005 (2 process × 576 submit、OOM 0) で実証されている。R3/R4/R5 も意図どおり。以下は、この設計のまま残る問題と、改善提案。

### D1 (中〜高): close 時の stop 期限が実行期限より短く、正常な長い job を持つ process の終了が device 全体を落とす

`gpu_close` は最初に `drv_gpu_report_session_error(device, backend, ENODEV)` を呼び、`gpu_session_fail_locked` が `stop_deadline = now + CONFIG_GPU_JOB_STOP_MS (10 s)` を設定する。その後 monitor が `stop_begin` → quiesce (host `DeviceWaitIdle`) → `stop_poll` を繰り返し、10 秒以内に ACK が来なければ action 3 で `fault` + `drv_gpu_report_error` (device 全体)。

したがって、**実行期限 60 秒の範囲内で正当に走っている 15 秒の compute job を持つ process が exit (または crash) すると、10 秒後に他の全 session が DEVICE_LOST になる**。p009 の試験がこれを検出しなかったのは、context-timeout/producer-stop が「実行期限 8 秒 + 停止 10 秒 = 18 秒 > 遅延 15 秒」の組合せだったため。既定設定 (60 秒) で「close 時に 15 秒の job が残る」ケースは試験されていない。

推奨: `gpu_session_fail_locked` で `job_deadline` を 0 にする前に、その session の pending job の最大 `job_deadline` を取り、`stop_deadline = max(now + STOP_MS, 最大 job_deadline + STOP_MS)` とする。つまり停止期限は「実行期限を過ぎてから 10 秒」。ETIMEDOUT で失敗した session は既に実行期限を過ぎているので従来どおり 10 秒。

### D2 (中): producer の close が admitted job を ERROR にする

同じ `gpu_session_fail_locked` は `backend_owned` の pending completion を全部 `completed=1, error=ENODEV` にし、bind 済み fence を ENODEV で signal する。p008 では U が signaler だったので producer 消失 = ERROR が必然だったが、p009 では K が job を最後まで監督して signal できる。Linux では process exit は GPU 仕事も dma-fence も取り消さず、fence は仕事の完了で signal され、compositor は最後のフレームをそのまま表示する。

現状では、Wayland client が最後のフレームを commit して終了すると、compositor が持つ fence が ERROR になり、そのフレームは捨てられる。推奨: close 時は RESERVED (未 commit) の job だけ ERROR にし、COMMITTED の job は通常の completion に任せる (drain がどのみち待つ)。`producer-exit` 試験の期待値 (ERROR) はこの変更で「event 待ちの job は実行期限で ERROR」に変わる。

### D3 (中): 真に hang した job は依然として device 全体を落とす

context 単位の停止は「host の `DeviceWaitIdle` が返る」ことが前提なので、event 未設定などで永久に終わらない submission を持つ context は、実行期限 60 秒 → 停止期限 10 秒 → 全体 fault になる。Vulkan/Venus に context 単位の cancel が無い以上、これは設計上避けられない。ただし全体 fault は必須ではない。

推奨 (改善案): 停止期限切れの session を **device fault ではなく session quarantine** にする。session の資源 (descriptor slot、DMA、host context) は checked reset まで解放しない (leak) が、transport と他 session は継続させる。host 側は process worker が context ごとなので、stuck した `DeviceWaitIdle` thread は他 context に影響しない。全体 fault は「制御/decoder request が transport watchdog で timeout」「quarantine で slot が枯渇」のときだけにする。これで「1 つの rogue app が GPU を全員から奪う」状況が「その app の context 分の slot を失う」に縮小する。

### D4 (低〜中): monitor thread の起床が多すぎる

`gpu_session_leave` (全 ioctl の終了) と `gpu_capacity_changed_locked` (全 completion、全 slot 解放) が `monitor_waitq` を起こす。monitor は起きるたびに全 session × 64 record を走査する。libvulkan は 1 フレームに数十 ioctl を出すので、monitor はその回数だけ走る。正しさには影響しないが 2 vCPU の guest では無駄。

推奨: monitor を起こす条件を「job_deadline の登録/変更」「stop_state が REQUESTED/PENDING の session の busy 解除」「unregister」に限定する。`gpu_session_leave` は `session->stop_state != NONE` のときだけ、`capacity_changed_locked` は monitor を起こさない。

### D5 (低): capacity 待機は FIFO ではない

資料に明記されているとおり starvation-free ではない。複数 process の重負荷で片方が飢える可能性はあるが、現段階では許容範囲。将来 ticket 方式にするなら `capacity_sequence` をそのまま使える。

### D6 (低): 健全な close にも host 往復が入る

全 close が quiesce (CPU0 fenced request + host `DeviceWaitIdle`) を経るようになった。通常は数 ms だが、native command を一度も出していない open だけが省略対象で、それ以外は必ず 1 往復増える。許容範囲だが把握しておくこと。

---

## 2. 実装レビュー

### 要修正

- **B1 = D1** (`gpu.c` `gpu_session_fail_locked` / `gpu_close`): 上記。
- **B2 = D2** (`gpu.c` `gpu_session_fail_locked`): COMMITTED job まで ENODEV 終端している。

### 注意 (動作は正しいが条件付き)

- **B3** (`gpu_monitor_step`): `session->busy` が立っている間は stop_begin を呼ばないが、その間も `stop_deadline` は進む。同期 control (blob 作成など) は最悪 transport の 10 秒まで sleep するので、理論上 stop_begin を一度も呼ばずに全体 fault に至る。D1 の修正 (期限の再計算) と合わせて、busy 中は stop_deadline を延ばすか、busy 解除時点から数えるのが安全。
- **B4** (`venus_queue_collect`): `supervised` request (marker / quiesce) にエラー応答が来ると `UINT_MAX` → transport 全体 fail。strict host は marker にエラーを返さない契約なので現状は到達しないが、quiesce に対する host の明示的な refusal (ERR_UNSPEC 等) も device 全体になる。D3 の quarantine 方針を採るなら、ここは session error に落とす。
- **B5** (`drv_venus_transport_drain`): deadline を撤去し無期限待ちになった。共通層の監督 (実行期限 → stop → fault) が必ず callback を retire させる前提。D1/D3 を変えるときは、drain が「session quarantine」でも終わる (quarantine 時に callback を error 終端する) ことを保証する必要がある。
- **B6** (`drv_gpu_recovery_ready`): Venus から呼ばれなくなり未使用。削除するか、`gpu_recover_open` 内の条件と一本化する。

### 確認して問題なしと判断した点

- `gpu_capacity_ioctl`: registry lock 下で `observed = waitq_sequence` を取ってから backend snapshot を lock 外で取り、再 lock 後に同じ sequence で atomic sleep。backend 解放と common 回収の両方の race が閉じている。QUERY (flags=1) は immediate。
- `gpu_job_reserve_ioctl`: `job_deadline` は bind 前に設定、backend 拒否時は `gpu_completion_discard` で 0 に戻る。setup 中の observer pin と `job_action` で action/consume との競合を防いでいる。
- `drv_gpu_complete`: 先行する logical error を上書きせず、`backend_owned` だけ落として hold を退役。`listed==0 && observers==0` のときだけ sequence を再利用可能にする。
- `gpu_completion_observer_leave`: `backend_owned` が残る record は sequence を保持。`unpublished` の rollback では capacity を起こさない (自分の retry を無限に起こさない)。
- `gpu_monitor_close`: `observed` を step より前に取り、step 後に同 sequence で sleep。`monitor_pins` で close と worker の二重 stop callback を排除し、pins が 0 になるまで backend を破棄しない。
- `gpu_recover_open`: sole fresh open のみ、`fault_epoch` で reset 中の再故障を検出。`drv_gpu_report_error(device, 0)` を backend から呼ばなくなり、clear は common 側だけ。
- Venus `drv_venus_transport_quiesce`: pending request は exact 24 byte / `OK_NODATA` / matching fence+context のみ ACK。ACK 後は RESERVED のみ ENODEV 終端し、POSTED は実返却を待つ。全 slot 使用中は EAGAIN で再試行。`supervised` request は transport watchdog の対象外。
- libvulkan `queue_enqueue` / `queue_attempt`: EAGAIN のときだけ `capacity_sequence` を公開して NOT_READY、それ以外の NOT_READY は DEVICE_LOST。retry ごとに software wait を再符号化、native reset は `prepared` で一度だけ。`locked` 呼出し (present) も待機中は queue mutex を外す (VkQueue の外部同期要件で問題なし)。
- `queue_private_fence` / `queue_reset_completed`: READY を優先し、INFLIGHT は `vulkan_sync_job_status(sync, 0)` (ioctl のみ、host 往復なし) で TERMINAL 判定してから最大 64 本を 1 回の `vkResetFences`。PREPARING は待機中も専有。
- `vulkan_wake_*`: 登録/解除は wake mutex のみ、通知は wake mutex 下で nonblocking write。`swapchain_notify_locked` (swapchain mutex 下) → wake mutex の順のみで逆順なし。解除時は list から外してから close するので fd 再利用への誤 write が無い。
- `swapchain_acquire`: drain → progress → error 観測 → 状態検査 → (登録なしなら登録して再観測) → `ppoll(GPU fd, display fd POLLPRI, pipe)`。登録後の全 predicate 再検査があるので取りこぼしなし。Wayland は従来の 10 ms 進行を維持。
- `present_worker_main` の fence 待ちを UINT64_MAX にし、期限は K の job 監督に一本化。session fail → bound fence ERROR → `vulkan_external_fence_wait` が ERROR を返す経路も通る。
- `device_validate` / `instance_add_context`: flags7 + `GPU_CAP_JOB|GPU_CAP_JOB_CAPACITY` を native instance 生成前に要求し、非対応 node は列挙から除外 (physical device 0 件は許容)。

---

## 3. 優先順位

| 順 | 項目 | 規模 |
|---|---|---|
| 1 | D1/B1: close 時の stop 期限を pending job の実行期限以降にする (+B3 の busy 中延長) | 小 (gpu.c 数十行) + 試験 1 件 (close with 15 s job、既定 60 s config) |
| 2 | D2/B2: close で COMMITTED job を ERROR にしない | 小〜中 (gpu.c + producer-exit 試験の期待値変更) |
| 3 | D4: monitor 起床条件の限定 | 小 |
| 4 | D3: 停止期限切れを device fault ではなく session quarantine にする (B4/B5 を含む) | 中 (gpu.c の action 3、venus の quarantine 単位、drain の終了条件) |
| 5 | B6 の整理 | 小 |

1〜3 は互いに独立で、いずれも UAPI を変えない。4 は設計変更なので、方針を決めてから着手する。
