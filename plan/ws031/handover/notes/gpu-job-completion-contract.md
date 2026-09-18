# GPU job の完了契約: 未完了／成功／失敗／取消（zedBSD GPU core と libvulkan の対応表）

WS031 E-109（2026-09-19）。読取りだけの調査。path は `agent-1:~/zedBSD/` 基点、C = `src/drivers/gpu/gpu.c`、U = `userland/base/libvulkan/`。
目的: i915 backend が何を実装すれば既存の契約を満たすかを決める。**Linux の dma-fence や新しい UAPI を持ち込む話ではない**（既存 core と libvulkan の契約に合わせる）。

## 0. 先に訂正
E-107 の報告で capset の STRICT_QUEUE を「fence は成功時だけ完了」と要約したが、これは **「成功という通知は、その仕事が実際に終わったときだけ出す」** の意味であって、「失敗した job は永遠に未完了のまま」ではない。core は失敗・取消でも待機者を status つきで戻す（下表）。以後この表の語を使う。

## 1. 四つの結末

| | (1) 未完了 | (2) 成功で完了 | (3) 失敗として終了 | (4) 取消として終了 |
|---|---|---|---|---|
| core の記録 | `completed=0`、`job_state` = RESERVED（C:4152）→ commit で COMMITTED（C:4479）、`backend_owned=1`、期限 = 予約 10 s → 実行 60 s（C:4153, 4481） | `completed=1, error=0, FINISHED`（C:406-411）。**commit されていない job の成功は拒否**（RESERVED で error==0 → EIO、C:402-403） | `completed=1, error=<errno>, FINISHED`。出所 = backend の errno、watchdog の ETIMEDOUT（C:6060）、session／device 喪失（C:5785-5791, 4301）。`CANCEL_FAULT` は記録を終端 ERROR として残す（uapi/gpu-job.h:52-53） | (a) 通常の `GPU_JOB_CANCEL`: `listed=0, error=ECANCELED, completed=1, unpublished=1`（C:4854-4859）＝**記録は見つからなくなる**。(b) 停止が証明された後の未公開予約の回収: 記録は残り `drv_gpu_complete(…, ECANCELED)`（C:6380） |
| 待機者への通知（`GPU_COMMAND_WAIT`） | `timeout_ns==0` → EAGAIN、期限 → ETIMEDOUT、signal → EINTR、device 喪失 → ENODEV（C:4925-4945）。`status` は触らない | ioctl 0、`status=0`（C:4780）。`GPU_WAIT_CONSUME` は copyout 成功後に記録を退役（C:4783-4787） | **ioctl 0、`status=正の errno`**（C:4780）。session／device 喪失は fd に POLLERR も立つ（C:1666-1667）＝**待機者は必ず戻る** | (a) 後からの WAIT は ENOENT（C:4898, 4909-4912）。(b) ioctl 0、`status=ECANCELED` |
| backend → core | `jobs->reserve` が token を返し `jobs->commit` が公開（drivers/gpu.h:51-52） | `drv_gpu_complete(completion, 0)`（drivers/gpu.h:169）— **実際に退役した後だけ**。i915: breadcrumb seqno 到達で `error=0`（i915/request.c:207-215, 306） | `drv_gpu_complete(completion, errno)`。device 全体 = `drv_gpu_report_error`、session 単位 = `drv_gpu_report_session_error`（drivers/gpu.h:181, 186） | `jobs->cancel(fault=0)` は callback を外して 0 を返す（**完了をでっち上げない**、drivers/gpu.h:42-43）。`fault=1` は callback を保持し core が session 喪失を公開（C:4293-4306） |
| libvulkan の VkResult | `VK_NOT_READY`／`VK_TIMEOUT`（sync.c:1044-1045, 1226-1240, 1401-1402） | `VK_SUCCESS` | **0 以外の status は全部 `VK_ERROR_DEVICE_LOST`**（context に sticky、sync.c:1053-1056, 1312-1315, 1409-1412） | 「取消」を表す VkResult は無い。plain CANCEL を選ぶのは「未受理かつ native status が OOM」のときだけで、その OOM を返す（sync.c:672-682, 719-720）。それ以外の取消は DEVICE_LOST |
| 資源 | completion slot を pin、fence binding 保持、backend token 保持（C:4180-4182, 4378-4383） | bound fence を error 0 で signal（**監督下の job だけが成功を証明できる**、C:425-431）、slot 解放、capacity 通知 | fence を **error つきで signal**。資源／DMA は意図的に**保持**（quarantine を先に arm、drivers/gpu.h:172-180。i915 は reset まで解放しない、i915.c:1642-1657） | (a) fence は **unsignal のまま unbind**（C:4406-4422）、slot は backend の capacity へ返す |

## 2. ioctl（`include/uapi/gpu-job.h`、`gpu.h`、`gpu-fence.h`）
- `GPU_JOB_RESERVE`（'G',34、`struct gpu_job_reserve`: fd=-1 なら generation=0、`timeline` は backend 定義、`sequence` が出力）／`GPU_JOB_COMMIT`（35）／`GPU_JOB_CANCEL`（36、flags ∈ {0, `GPU_JOB_CANCEL_FAULT`=1}）／`GPU_JOB_CAPACITY`（37）／`GPU_JOB_POLICY`（38: 予約 10 s、実行 60 s、停止 10 s）。
- `GPU_COMMAND_SUBMIT`（12、`GPU_COMMAND_CONTEXT_FENCE`）／`GPU_COMMAND_WAIT`（13、`status` = 正の errno か 0、`GPU_WAIT_CONSUME`）。
- 登録時の強制: `GPU_CAP_JOB` ⇒ `GPU_CAP_NOTIFICATION`＋`jobs->{reserve,commit,cancel}`＋`recovery->fault`、`GPU_CAP_JOB_CAPACITY` ⇒ `jobs->capacity`（C:824-853）。
- `include/kern/fence.h~` は `include/drivers/gpu-fence.h` の古い backup（tilde 無しは存在しない）。

## 3. i915 backend（legacy 側 `src/drivers/gpu/i915/`）の現状
**実装済み**: `jobs->reserve/commit/cancel/capacity`（i915.c:1297-1513）、`commands->submit/drain`、`recovery->{stop_begin,stop_poll,fault,reset,isolate}`、完了配送（i915/request.c:282-333、成功は breadcrumb 到達時だけ）、capability = RESOURCE|TRANSFER|COMMAND|NOTIFICATION|JOB|JOB_CAPACITY|CAPSET|BLOB|MAPPING。
→ **ただしこれは legacy 初期化の上の実装**。parity の request／engine／reset へ付け替えるのが VK-1 の作業で、legacy 初期化と混ぜない。

**不足**:
1. `GPU_CAP_FENCE` 無し → fd つきの RESERVE は不可（sequence だけの job は可）。
2. capset が 156 byte で byte 0 だけ（`vk/vk.c:174-176`）→ libvulkan は open 時に拒否（E-107 の依存表 §0-1）。
3. **VK executor の仕事に completion が付いていない**: `i915_vk_queue_submit` は completion=NULL で request を作り（`vk/cmdbuf.c:814`）、`i915_command_submit` は native でない stream に対して **decode 時点で `drv_gpu_complete(…, 0)`**（i915.c:1237-1243）。つまり GPU 完了の証明は監督下の job marker だけで、それは batch を持たない marker request。RCS0 の VK batch と、`timeline_index` で選ばれた engine 上の marker の間に順序保証が無い（同一 engine の FIFO だけ）。
4. `I915_REQUEST_RETAINED` は fault／isolate 以外に退役経路が無く、`cancel(fault=1)` は `pending_requests` を減らさない → `stop_poll` は core の期限切れまで EAGAIN（意図か欠落かは code から判定不能）。
5. present／display／share／scanout／blob_create_placed は NULL（完了契約の外）。

**契約の参照実装**: virtio／Venus backend（`src/drivers/gpu/venus/transport.c`: reserve :149-259、commit :265-317、cancel :323-384、capacity :390-420、完了配送 :2293-2343）。

## 4. comment と code の食い違い・曖昧さ（事実だけ）
- 通常 cancel は「完了をでっち上げない」と書いてあるが、core は停止後の回収で `drv_gpu_complete(…, ECANCELED)` を呼ぶ（C:6363 と C:6380）。first-writer-wins（C:400）と「遅れた callback として退役」（C:6373）で整合。結果、同じ ECANCELED が (a) では観測不能、(b) では WAIT で観測可能。
- `status` は「正の errno」と書いてあるが core は符号を検査しない（C:4780）。
- `i915_engine_for_timeline` は timeline 0 を受けるが job ops は EINVAL（i915.c:1757-1760 と 1319-1320）。`i915_job_commit` の engine 走査は RCS0→BCS0 固定（engine 数 2 前提）。
- libvulkan は (3) の errno を区別せず全部 DEVICE_LOST にする（kernel は区別している）。
- **STRICT_QUEUE／QUIESCE の意味は libvulkan の comment 2 文だけ**（context.c:176-184: 「native fence の success-only completion」「他 session を失敗させずに raw native work を退役できる」）。STRICT_QUEUE は OPAQUE と同時でないと無視される。168 byte の vendor capset を**書く側は tree に存在しない**（Venus は host の値を転送、i915 は 156 byte）→ 適合 host が何を保証すべきかはこの tree からは確定できない。**capset を立てる前に、プロジェクト側で意味を確定する必要がある**（レビュー事項）。

## 5. parity へ接続するときの対応表（案）
| 契約 | parity 側で対応させるもの |
|---|---|
| 成功通知は実完了時だけ | parity request の完了（breadcrumb／user interrupt → park）を `drv_gpu_complete(…, 0)` へ。decode 時点の成功通知はやめ、VK batch の request 自体に completion を付ける |
| 失敗として終了し待機者を戻す | hang 検出 → `eu_hang_dump_reset` 相当 → request に errno → `drv_gpu_complete(…, errno)`。fence は error つき signal、資源は reset まで保持 |
| 取消 | 未公開の予約は callback を外して slot を返す（完了を作らない） |
| 実際の GPU 利用停止 | `recovery->stop_begin/stop_poll/fault/reset/isolate` を parity の engine stop／reset へ |
| WAIT からの復帰 | core が持つ（backend は上の通知を正しく出すだけ） |
