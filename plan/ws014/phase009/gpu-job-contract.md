# GPU 共通 job・容量待機・停止契約（p009 / q312）

この資料は p009 の共通 GPU 層の実装と限定 fixture を記録する。Phase 状態と実 QEMU の最終受入は [phase.md](phase.md) に従う。実装は [gpu.c](../../../src/drivers/gpu/gpu.c)、公開ドライバ契約は [gpu.h](../../../include/drivers/gpu.h)、追加 UAPI は [gpu-job.h](../../../include/uapi/gpu-job.h)。

## 責務と互換性

[共通 GPU core](../../../src/drivers/gpu/gpu.c) が open ごとの状態、完了記録、fd の fence 世代、予約・実行期限、容量待機、局所停止の期限と最終 close barrier を所有する。[driver ops](../../../include/drivers/gpu.h) の内部版は 8。kern は引き続き汎用 handle/fd/refcount/poll/SCM_RIGHTS だけを扱う。

backend は原子的 try-reserve、実投入、実資源解放通知、native 完了、実停止と DMA 保持を担当する。GPU 共通層は Venus の timeline 0 / 63 を解釈しない。既存 `GPU_JOB_RESERVE` / `COMMIT` / `CANCEL` と `GPU_COMMAND_WAIT` の配置を保ち、能力 `GPU_CAP_JOB_CAPACITY=16384` と要求 37 / 38 を追加する。libvulkan が必要とする host 能力の選別は別の backend / U 契約である。

## 二段階の容量待機

| 要求 | 配置 | 契約 |
| --- | --- | --- |
| `GPU_JOB_CAPACITY` / QUERY | 48 bytes、ioctl 37、flags=1 | device 共通の非zero sequence と advisory `available` を返す。domain は backend が検証する。予約はしない。 |
| `GPU_JOB_CAPACITY` / WAIT | 同じ48 bytes、flags=0 | 観測 sequence が変わるか、backend slot と当該 open の記録が両方空くまで待つ。timeout=0 は EAGAIN、有限は ETIMEDOUT、UINT64_MAX は割込み可能待機。 |
| `GPU_JOB_POLICY` | 40 bytes、ioctl 38 | 設定した予約・実行・停止の最小間隔を nanoseconds で返す。変更権限は与えない。 |

U は native fence を一度準備してから **QUERY → terminal 記録の回収 → try RESERVE** を行う。EAGAIN の場合は queue/device/context mutex をすべて外して WAIT し、再取得後に health、共有世代、待機 payload を再検査する。WAIT の成功は予約権を保証しないため、再試行が再び EAGAIN になることは正常。ENOMEM は実際の割当失敗として分ける。

共通層の待機チャネルは device 全体で、backend の実 FREE/cancel と共通の terminal/最終 observer/回収遷移が sequence を進める。別 open の解放でも起床するため、自分の fd の POLLIN を容量通知として使わない。open の64記録が全て listed の場合も terminal 遷移で起床して自分で回収できる。既に terminal で observer が pin した記録だけを繰り返し ready としない。失敗した try-reserve の内部 rollback では、自分の retry token を進めない。

QUERY/WAIT は通常の session admission を保持しない。registry spinlock を外して backend snapshot を取り、同じ waitq sequence を再検査して atomic sleep へ入る。backend は容量 callback で待機せず、資源 lock を外して `drv_gpu_capacity_changed()` を呼ぶ。取消・割込み・copyout 失敗は job を受理せず容量を消費しない。

これは FIFO ticket 予約や starvation-free を保証する仕組みではない。待機者は scheduler と実資源の進行に従って競合し、control 経路を塞がず再試行する。将来の U 操作に依存する仕事について「待てば必ず成功する」とは主張しない。未設定 host event による故障注入も正常な Vulkan forward progress の例ではない。

## 共通の期限と実資源の寿命

| 設定 | 既定 | 開始点 |
| --- | --- | --- |
| `CONFIG_GPU_JOB_RESERVATION_MS` | 10000 ms | native 投稿より前に K が予約を所有した時点 |
| `CONFIG_GPU_JOB_EXECUTION_MS` | 60000 ms | COMMIT で当該 job の受理を確定した時点 |
| `CONFIG_GPU_JOB_STOP_MS` | 10000 ms | session が最初の局所 ERROR へ遷移した時点 |

make/userconfig と menuconfig の GPU policy 選択で設定する。単調 tick へ切り上げるため、`GPU_JOB_POLICY` が返す値は設定した最小間隔であり、tick 精度を越えた期限の保証ではない。予約期限と実行期限は U のスケジューリングや CPU wait の timeout に依存しない。Venus の通常 control 通信の watchdog は backend 側に残り、managed job の policy と分ける。

一 GPU に一つの common monitor を最初の job で開始する。raw-only session も final close の停止確認前には monitor を開始する。期限も停止処理もない間は無期限の通知待ちで、idle な固定周期起床を追加しない。active な局所停止確認だけ100 ms 間隔で再照会し、停止期限を監督する。登録解除で worker を起床・join し、backend の借用寿命を終了する。

論理 ERROR と backend callback の退役は別。期限切れは当該 session の新規受付を拒否し、完了記録と共有 fence の正確な世代へ ERROR を公開するが、`backend_owned` を落とさない。利用者が ERROR を CONSUME しても、native callback または observer が参照する記録は再利用しない。後着の `drv_gpu_complete(..., 0)` は最初の ERROR を成功に変えず、実 callback の hold だけを退役する。

## 局所停止、global quarantine、close と reset

`drv_gpu_recovery_ops` が存在する場合、`fault` は必須。`stop_begin` と `stop_poll` は任意の対で、共通層の通常 ioctl と job action が退いた後、registry lock を保持せず session pin の下で呼ぶ。begin は新規 native 受理を止め、poll は **その context の native access、descriptor、callback の全てが退いた場合だけ0** を返す。単に完了記録が空、CTX_DESTROY を送った、ERROR を公開した、という条件は実停止の証拠にならない。

未確認なら EAGAIN、非対応や失敗・停止期限超過なら `fault` で全体を隔離する。`fault` は冪等で、新規投入を止め、destroy/close が uncertain DMA を保持できる状態を先に確立する。`drv_gpu_report_error(nonzero)` はその後の通知であり、物理停止命令や物理停止成功の通知ではない。callback drain と checked reset は別の barrier。common unregister も先に `fault` を実行してから global error を公開する。

final close は session ERROR と共有 producer 権限終了を先に公開し、callback drain 中も monitor を生存させる。続いて native 停止確認または global quarantine を待ち、monitor pin が0になってから resource/backend/session を破棄する。raw-only session の callback ledger が空でも、この停止確認を省略しない。monitor の割当失敗も quarantine を経由する。close 自身は worker と同じ `gpu_monitor_step()` を補助でき、session pin が重複した stop callback を防ぐ。

callback の消失を raw native work の停止証拠として用いないため、Venus の実停止証明は backend の交渉済み host 契約に依存する。能力がなく確認できない場合に局所成功を返すことは認めず、全体隔離へ進める。共有 image/allocation、mapping、scanout の独立所有者は各参照を保ち、隔離された native backing は checked reset 前に再利用しない。

reset は全旧 session が閉じ、共有 allocation と mapping の参照が退いた後の sole fresh open だけが要求できる。backend reset 成功時にも開始時の `fault_epoch` を照合し、reset callback 中に IRQ が同じ errno を再通知した場合を含め、新しい故障を0で上書きしない。旧 VkDevice や旧 fd が復活する契約ではない。

## 限定検証

[verification.json](gpu-core-verification/verification.json) に実行、source hash、小ログを保存した。supervision fixture は実 common core/fence/fd を使い、明示的な scheduler boundary で別 owner の callback/observer 退役を起こす。4 slot の peer による確実な飽和と open64記録の自己回収、observer pin、domain77、timeout/EINTR/copyout 非消費、11秒の正常実行、60秒の局所失敗と別 open 継続、遅延成功 callback、10秒予約+停止 fallback、close/raw/withdrawal/割当失敗、reset epoch を通常と ASan/UBSan で確認した。

fixture は同時 kernel thread 実行や実 GPU の DeviceWaitIdle 成功を代用しない。native host の停止と実 QEMU の描画・負荷・回復は Phase の統合証拠で判断する。検出した reset-time IRQ 上書き、raw close の早期破棄、withdrawal の隔離前通知は、各修正前 FAIL と修正後 PASS を同ディレクトリに残した。既存 framework/job/fence/fence-close/fence-reuse/sharing/topology/scanout/placement/handle-fd も通常と sanitizer で通過した。

UAPI は ILP32 / LP64 の配置を compile assertion で確認。6 platform の GPU y/n、計12 make 入力を確認し、さらに別 build directory で [GPU なし amd64 kernel](gpu-core-verification/nogpu-amd64.json) を実 build した。ELF と object に GPU core/fence/Venus がなく、`handle_get/put`、`fd_object_get/put`、`filedesc_commit_objects` が残る。全6 platform の実 build を行ったという意味ではない。
