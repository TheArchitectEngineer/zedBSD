<!-- awesome-plan project=zedbsd record=ws250-design -->

# WS250 設計: nvrtx の終わらない GPU の仕事の取り消しと復帰（system は動き続ける）

作成: 2026-10-15、P4（q1206、ws250-p001）。code は書かない。読んだ code は main `f8bbc1bf2` の tree（worktree `agent/p4-ws250`）。

ユーザーの決定（原文、2026-10-15）:「その場合のキャンセルと復帰を実装した方がよくない？device lostでプログラムがそれ以上動けなくなるのはOK。でもシステムとしては動き続けてほしいな。」

## 0. 要約（この設計が決めたこと）

| # | 決めたこと | 根拠の節 |
| --- | --- | --- |
| 1 | **GPU の channel を session（`/dev/gpu0` の open）ごとに作る**。今は GPU に 1 本の work channel（chid 4）を全 session が共有している。session ごとの channel なら、詰まった app の仕事は自分の channel の中に閉じ、GPU の scheduler（runlist の timeslice）が compositor の channel を回し続ける。vaspace（page table）は今の driver の 1 本を共有したまま（nouveau と同じ: client の vmm を全 channel が共有する）。1 channel の VRAM の費用は約 1 MiB（golden context の「own buffers」0xf5000 byte）＋ instance block・USERD 8 KiB、system memory 約 50 KiB | 3.5、4.1 |
| 2 | **検出は executor の run ごとの fence の待ち（2 s）のまま**。共通の層（gpu.c）の期限（予約 10 s・実行 60 s・停止 10 s）は supervised job だけに効き、vkQueueSubmit の frame には効かない（事実）。2 s を越えた run は「その session の喪失」とし、他の session の期限とは混ぜない。長い正当な compute は、今の executor が submission ごとに同期で待つ作り（vk-execute.c の冒頭）の中では 2 s 以内が前提。期限は kernel の config `CONFIG_NVRTX_RUN_MS`（既定 2000）に出す | 4.2 |
| 3 | **GPU の上での取り消しは GSP-RM の口**: その channel に `NVA06F_CTRL_CMD_STOP_CHANNEL`（0xa06f0112、bImmediate=1、runlist から強制に preempt）→ `NV906F_CTRL_CMD_RESET_CHANNEL`（0x906f0102、engine GR）→ schedule off → 3D・compute の object と channel の FREE。どれも RM の kernel 側の実装が GSP client では GSP へ RPC で転送する（事実: open-gpu-kernel-modules 570.144 の `kernel_channel.c`）ので、nvrtx の RPC の control でそのまま届く。**実機で未確認**なので p003 の最初の run で確かめ、効かなければ `NV2080_CTRL_CMD_FIFO_DISABLE_CHANNELS`（0x2080110b、subdevice、channel の handle の一覧を preempt）に切り替える | 3.2、4.3 |
| 4 | **GSP の RC の event（`NVRTX_EVENT_CHANNEL_ERROR` 0x1004、`rpc_rc_triggered_v17_02`）を session に結びつける**: chid → session の表を node が持ち、event を受けた drain が session を喪失にする。GSP の firmware が user の channel を自分で見張るかは **source から確定できない**（RM の「RC watchdog」は CPU 側の kernel RM が自分の channel を作って回す物で、nouveau にも nvrtx にも無い。FECS の hardware の watchdog は GSP-RM の設定が不明）→ p003 の probe で観測する | 3.3、4.4 |
| 5 | **共通の層の ops**: `isolate` を実装する（channel の停止と FREE が RM で確かめられた session だけ 0 を返す。確かめられなければ失敗 → 今までどおり device 全体の fault）。`reset`（device の作り直し＝GSP の停止と再起動、display の作り直し）は **この WS では実装しない**（既定の案。理由: WS037 の判断 14・17 と同じく reboot に任せる。必要なら p005 の選択肢） | 4.6 |
| 6 | **compositor が詰まった仕事の後ろで止まらない形**: (1) session ごとの channel で GPU の側の順番待ちを無くす。(2) kernel の側は renderer の worker が 1 本（runtime.c）のままなので、詰まった run の検出の 2 s の間は全 session の frame が待つ（1 回だけ）。その後は喪失した session の frame を GPU に出さず EIO で返すので、desktop は動き続ける。2 s の 1 回の停滞も無くす形（session ごとの worker、node の lock を GPU の待ちの外に出す）は p005 の選択肢（WS241 の描画の速さにも効く） | 4.7 |
| 7 | **WS241 との関係**: WS241 は work.c の submit・fence・timed stamp・boost を触っている。p002 は work.c の「1 本の channel」を「channel の構造体の配列」に一般化する変更で、WS241 の次の round（boost の既定、rec submit）と同じ file に当たる。**順は WS241 の今の round の merge の後に p002**。p002 は submit・wait の中身（timed の 3 entry、spin の 2 ms）を変えない | 4.13 |
| 8 | Phase: p002 channel の一般化（6〜8 LW）、p003 検出と取り消し（6〜8 LW）、p004 試験の app と実機の scenario（3〜4 LW）、p005 選択肢（2 s の停滞を無くす worker の分離、または `reset`）（8〜12 LW、ユーザーの判断）、p006 規約の全文と出典の監査（2 LW）。必須は p002〜p004・p006 で 17〜22 LW | 7 |

## 1. 今の振る舞い（code の事実、file と行）

| 事実 | 所在 |
| --- | --- |
| executor は 1 submission（vkQueueSubmit の command buffer の列）を records → push buffer に直し、push buffer が満ちた時・CPU の copy の前・終わりに work channel で run し、**run ごとに fence を同期で待つ**。待ちは `VK_NODE_RUN_US` 2 s、越えたら EIO | `vk-execute.c` 9〜30 行（冒頭の説明）、`nvrtx_vk_execute` 216〜265 行、`execute_flush` 474〜490 行、`vk-node.c` 31 行・`nvrtx_vk_gpu_run` 267〜314 行 |
| EIO・ETIMEDOUT の submission は reply に `VK_ERROR_DEVICE_LOST` を書くだけ。**session は喪失にならず、次の submission も GPU に出る** | `vk-queue.c` `queue_submit` 140〜167 行、`queue_result` 245〜260 行 |
| work channel は GPU に 1 本（`struct nvrtx_work_state`、chid 4、graphics runlist、3D と compute の object、ring 512 entry、semaphore 1 page、fence の push 512 slot）。submission は segment＋fence の 2 entry（timed なら stamp を足して 3）。**ring の entry の再利用はその entry の前の fence が過ぎるまで待ち、待ちは 2 s（`WORK_ENTRY_WAIT_US`）**。詰まると後の全 session の submit が 2 s 待って ETIMEDOUT | `work.c` 9〜31・109〜120・500〜598・1098〜1119 行、`nvrtx-gpu.h` 359〜380 行 |
| vaspace は driver の 1 本（`NVRTX_HANDLE_WORK_SPACE`、512 MiB、context buffer を golden context の VA に privileged、driver の page、残りを user の run に heap で配る）。run の map・unmap は PRAMIN への PTE の書きと TLB の invalidate（hardware への access） | `work.c` 320〜487・1065〜1096 行、`nvrtx-work.h` |
| renderer の worker は node に 1 本。frame は `node->lock` を持って executor を走らせる（GPU の待ちも lock の中）。queue は全 session で 1 本の FIFO | `runtime.c` 8〜22・674〜744・751〜791 行 |
| 共通の層の監視（gpu.c）は **supervised job（`GPU_JOB_RESERVE`/`COMMIT`）だけ**に期限を付ける（予約 `CONFIG_GPU_JOB_RESERVATION_MS` 10 s、実行 `CONFIG_GPU_JOB_EXECUTION_MS` 60 s、停止 `CONFIG_GPU_JOB_STOP_MS` 10 s）。`GPU_COMMAND_SUBMIT` の frame（libvulkan の vkQueueSubmit の道、`context.c` 849〜857 行）には期限が無い。期限切れ → `gpu_session_fail_locked` → `stop_begin`/`stop_poll` → 確かめられなければ `isolate` → 無ければ `fault`＋`drv_gpu_report_error` | `gpu.c` 53〜61・5159〜5160・5486〜5488・7028〜7271 行、`config/kernel-options.list` 8〜10 行 |
| libvulkan は supervised job を `sync.c` の fence の export（timeline の marker）に使う。vkQueueSubmit そのものは `GPU_COMMAND_SUBMIT`（`GPU_COMMAND_CONTEXT_FENCE`）で queue に入れ、reply を `GPU_COMMAND_WAIT` で待つ | `userland/desktop/libvulkan/context.c` 830〜870 行、`sync.c` 600〜720 行 |
| kernel の error（EIO・ETIMEDOUT・ENODEV など、ENOMEM・ENOSPC 以外）を libvulkan は `VK_ERROR_DEVICE_LOST` にし、context を喪失にする | `context.c` `vulkan_kernel_error` 563〜589 行 |
| nvrtx の recovery の ops は `stop_begin`（session の `stopping`）・`stop_poll`（pending が 0 か）・`fault`（node の `faulted`）。**`reset`・`isolate` は NULL** | `runtime.c` 95〜102・573〜667 行 |
| GSP の channel error の event（0x1004）は `nvrtx_rpc_wait` が log に出して捨てる。runtime は node の drain thread が 1 s の turn で message を読み続ける（`nvrtx_rpc_wait` を `NODE_DRAIN_NO_FUNCTION` で） | `rpc.c` 248〜261 行、`node.c` 853〜884 行 |
| RPC の call・reply の buffer（`gpu->call`・`gpu->reply`）は 1 組で、「worker だけが 1 度に 1 つ」の前提。runtime に RM の control を送る道は無い（WS241 の M3 は drain の前に送った） | `rpc.c` 8〜18 行、`nvrtx-gpu.h` 616〜624 行、[ws241-p001](../ws241/phase001/phase.md) 1.2 の C |
| nvrtx の executor は compute pipeline を **拒む**（compute の仕事は走らない）。shader の compiler は loop（BSSY・BRA）を lower できる | `vk-pipeline.c` 758〜770 行、`compiler/sm75.c` 2866〜2990 行 |
| display の lease を持つ session（compositor）は `node->owner` | `present.c` 423〜465 行 |
| i915 の手本: `isolate` は session を quarantine にし engine を recover、`reset` は GT の full reset → engine の作り直し → quarantine の解放、`fault` は全 request を EIO に | `src/drivers/gpu/i915/reset.c` 650〜919 行 |

## 2. 責務と境界

| 層 | 責務（この設計の後） | 触らない物 |
| --- | --- | --- |
| 共通の GPU の層（`gpu.c`、`gpu.h`） | session の喪失の公開（`drv_gpu_report_session_error`）、close の時の stop の確認、isolate・fault・reset の呼び出し、supervised job の期限 | **変えない**（`drv_gpu_recovery_ops` の契約は gpu.h 59〜88 行のまま。UAPI も不変） |
| nvrtx の renderer（`runtime.c`、`vk-*.c`、`vk-node.c`） | session ごとの channel の open・close、run の期限の判定、喪失した session の frame を GPU に出さない、isolate の実装 | queue の形（1 worker・1 FIFO）は p002〜p004 では不変 |
| nvrtx の channel（`work.c` → 一般化） | channel の作成・停止・取り消し・FREE、submit・wait、RM の control の口 | vaspace の構造、PTE の形式、submit の entry の形（WS241 の timed の 3 entry を含む） |
| nvrtx の GSP の RPC（`rpc.c`、`node.c` の drain） | runtime の RM の control の直列化（`rpc_lock`）、RC の event の配送（chid → node の callback） | RPC の形式・sequence の扱い |
| GSP-RM（firmware） | channel の preempt・runlist の管理・RC の検出と channel の無効化・engine の reset | — |
| HAL | 変更なし | — |
| libvulkan・compositor | 変更なし（DEVICE_LOST は今の道で app に届く） | — |

## 3. 調べた事実（公開の source）

正本と license は [ws037-license-audit](../ws037/nvrtx-license-audit.md) の表のとおり: Linux v6.19 nouveau（大部分 MIT）、open-gpu-kernel-modules 570.144（MIT、SDK の header と kernel の source）、Mesa 25.3.6（MIT）。読んだ file は他の worktree の `plan/ws037/temp/`（commit されない）と、この WS の `plan/ws250/temp/ogkm-src/`（GitHub の tag 570.144 から取った kernel の source、**commit しない**、`.gitignore` の temp の規則）。実装では値・手順を block ごとに「Value(s) referenced:」「Procedure referenced:」の複数行の comment で示し、code・comment・構造・symbol の名前は写さない（Guardrail「MIT・GPL の出典のデータの引用」）。

### 3.1 RC（channel の error）の event と nouveau の扱い

- event `NV_VGPU_MSG_EVENT_RC_TRIGGERED` = 0x1004（nvrtx の `NVRTX_EVENT_CHANNEL_ERROR`）。中身 `rpc_rc_triggered_v17_02`: `nv2080EngineType`、`chid`、`gfid`、`exceptLevel`、`exceptType`、`scope`、`partitionAttributionId`(u16)、`mmuFaultAddrLo/Hi`、`mmuFaultType`、`bCallbackNeeded`、`rcJournalBufferSize`、`rcJournalBuffer[]`（Values referenced: open-gpu-kernel-modules 570.144 `src/nvidia/generated/g_rpc-structures.h` 1439〜1454 行）。nvrtx の `rpc.c` 93〜99 行の offset はこの構造の物。
- `exceptType` は Xid の番号（Values referenced: 同 `src/common/sdk/nvidia/inc/nverror.h`）: 13 GR exception、31 MMU fault、32 PBDMA error、44 GR fault during ctxsw、45 preemptive removal、69 GR class error、79 fallen off the bus。
- nouveau（Procedure referenced: Linux v6.19 `nvkm/subdev/gsp/rm/r535/fifo.c` `r535_fifo_rc_triggered`・`r535_fifo_rc_chid`、`nvkm/engine/fifo/chan.c` `nvkm_chan_error`）: event の chid から channel を引き、1 回目だけ「errored - disabling channel」として software で block し（GSP の道では `chan->func->stop` は空、preempt=false）、KILLED の event を DRM に上げる → `nouveau_channel_kill`: 未完了の fence を -ENODEV で終え、以後の EXEC は -ENODEV（Procedure referenced: `nouveau_chan.c` 44〜60 行、`nouveau_exec.c` 181〜193・379 行）。**GSP の道では channel の停止（runlist から外す）は RM が RC の中でやる**（推論、nouveau の stop/start・runl block/allow が空であることから）。
- nouveau の DRM の scheduler の job の timeout は 10 s（`nouveau_sched.c` `NOUVEAU_SCHED_JOB_TIMEOUT_MS`）、fence の待ちは 15 s（`nouveau_fence.c`）。timeout の時は channel を kill する（RM に止めさせる call は **しない**: `nouveau_exec_job_timeout` は `nouveau_channel_kill` だけ）。つまり nouveau は「自分で止める」口を使わず、GPU の上の仕事は RM の RC か次の context switch に任せる。
- Mesa の NVK: VkQueue ごとに DRM の context（channel）を作り（`nvkmd_nouveau_ctx.c` `nouveau_ws_context_create`）、submit が失敗したら `vk_queue_set_lost`（`nvk_queue.c` 288〜310 行）。**app ごと（queue ごと）の channel が普通の形**。

### 3.2 channel を止める・reset する RM の口（全部 GSP へ転送される）

| control | 番号 | object | 引数 | kernel RM の実装の事実（open-gpu-kernel-modules 570.144） |
| --- | --- | --- | --- | --- |
| `NVA06F_CTRL_CMD_STOP_CHANNEL` | 0xa06f0112 | channel | `NvBool bImmediate`（1 byte）: NV_FALSE は RM の既定の timeout まで idle を待つ、NV_TRUE は待たず、idle でなければ **runlist から強制に preempt** | `kernel_channel.c` `kchannelCtrlCmdStopChannel_IMPL` 1995〜2032 行: `IS_GSP_CLIENT` なら `NV_RM_RPC_CONTROL` で GSP へ、その後 `kchannelNotifyRc_HAL` |
| `NV906F_CTRL_CMD_RESET_CHANNEL` | 0x906f0102 | channel | `engineID`(u32)、`subdeviceInstance`(u32)、`resetReason`(u32: 0 DEFAULT、1 VERIF、2 MMU_FLT)、`bIsRcPending`(NvBool) | 同 3146〜3192 行: `resetReason < ENUM_MAX` を検査、`bIsRcPending` を kernel の記録で埋め、GSP client なら RPC で転送 |
| `NVA06F_CTRL_CMD_RESTART_RUNLIST` | 0xa06f0111 | channel | `bForceRestart`、`bBypassWait` | exported（`g_kernel_channel_nvoc.h` 263 行）、実装は読んでいない |
| `NV2080_CTRL_CMD_FIFO_DISABLE_CHANNELS` | 0x2080110b | subdevice | `bDisable`、`numChannels`、`bOnlyDisableScheduling`、`bRewindGpPut`、`pRunlistPreemptEvent`(NvP64、kernel client 以外は NULL)、`hClientList[64]`、`hChannelList[64]`（Values referenced: `ctrl2080fifo.h` 339〜355 行）。「bDisable かつ bOnlyDisableScheduling=false なら、列挙した channel が hardware で走っていない状態にし、enable まで走らせない」 | `kernel_fifo_ctrl.c` `subdeviceCtrlCmdFifoDisableChannels_IMPL` 713〜750 行: GSP client なら RPC で転送、それ以外は NOT_SUPPORTED |
| `NVA06C_CTRL_CMD_PREEMPT` | 0xa06c0105 | channel group（TSG、class 0xa06c） | `bWait`、`bManualTimeout`、`timeoutUs`（最大 1 s） | TSG を自分で作る時だけ使える。nouveau は TSG を作らない（[ws037 の調べ](../ws037/temp/channel-mmu-display.md) 2.1、「RM が暗黙に TSG を作る」は推論） |
| `NVA06F_CTRL_CMD_GPFIFO_SCHEDULE` | 0xa06f0103 | channel | `bEnable` | nvrtx が既に使う（`NVRTX_CTL_SCHEDULE`）。bEnable=0 は「scheduler から外す」で、走っている仕事を止める保証の記述は無い |

`NV906F_CTRL_CMD_GET_MMU_FAULT_INFO`（0x906f0106）は RC の後の fault の番地と shader の VA を返す（log の補い、任意）。

### 3.3 GSP の firmware が user の channel を自分で見張るか

- RM の「RC watchdog」（`kernel_rc_watchdog.c`・`kernel_rc_watchdog_callback.c`）は **CPU 側の kernel RM** の仕組み: 自分の client と channel（TWOD の class）を作り、1 Hz の timer で notifier 付きの仕事を出し、`timeoutSecs` の間 notifier が書かれなければ「GPU is probably locked」として `NV2080_CTRL_CMD_INTERNAL_RC_WATCHDOG_TIMEOUT` を physical RM（GSP）へ送って回復させる（`kernel_rc_watchdog_callback.c` 240〜372 行）。既定は disabled で、X が enable、CUDA が disable を求める（`kernel_rc_watchdog.c` 115〜160 行の説明）。**nouveau も nvrtx もこの channel を作らないので、この watchdog は無い**。
- hardware の FECS の watchdog: nouveau は GSP 無しの道で FECS に watchdog の timeout を書き（`gf100.c` `gf100_gr_fecs_set_watchdog_timeout`、起動で 0x7fffffff＝事実上無効）、「FECS watchdog timeout」の割り込みを log する（1593 行）。GSP-RM の firmware が FECS の watchdog と、timeslice の切れで要求した context switch が終わらない時（WFI の preemption では長い draw は切れない）の扱い（RC の種類、timeout の長さ）は **公開の source に無い**（GSP-RM の physical 側は firmware の中）。
- 結論: 「GSP が自分で止めて RC を送ってくる」可能性はあるが、その有無・時間・種類は **実機の観測でしか決まらない**。設計は両方に備える: (a) nvrtx の 2 s の期限で自分から止める（主の道）、(b) RC の event が先に来たらそれで session を喪失にする（従の道）。p003 の probe（6.3 の S1）で、無限 loop の fragment shader の間に event が来るか・何秒か・compositor の channel が進むか（＝GPU の timeslice で切れているか）を見る。

### 3.4 preemption の mode

`NV2080_CTRL_CMD_GR_SET_CTXSW_PREEMPTION_MODE`（0x20801210）で GFX は WFI/GFXP/GFXP_POOL、compute は WFI/CTA/CILP（Values referenced: `ctrl2080gr.h` 805〜833 行）。GFXP・CILP には追加の context buffer（BETA_CB・PAGEPOOL・SPILL・RTV_CB・SETUP、`kernel_graphics.c` 155〜159 行）の確保と promote が要り、nouveau は使わない。**この WS では既定（WFI）のまま**: 長い draw の中では GPU は channel を切り替えられず、compositor の channel は次の draw の境（WFI）まで待つ。無限 loop の 1 draw では WFI が来ないので、GPU の上の取り消し（3.2）が要る理由がこれ。GFXP は Future Work の候補（7 節）。

### 3.5 channel の共有と session ごとの channel

- nouveau の GSP の道: client（DRM の open）ごとに vmm（vaspace）1 本、channel は `nouveau_channel_new` ごとに RM の channel を 1 本（`r535_chan_alloc`: `hVASpace` は client の vmm、`hObjectError` 無し、`internalFlags` は ERROR_NOTIFIER_TYPE NONE・PRIVILEGE USER/ADMIN、USERD の index と page は chid から、CHID_PER_USERD 8）。複数の channel が 1 つの vaspace を使うのは通常の形。nvrtx の `nvrtx_channel_params`（`objects.c` 471〜510 行）は同じ欄を埋める。
- channel ごとに要る物（nvrtx の work channel の事実から）: instance block 4 KiB と USERD 4 KiB（VRAM）、ring 4 KiB・semaphore 4 KiB・fence の push 32 KiB・method buffer（RM が答えた大きさ、実機で 8192 byte）（system memory）、**own の context buffer 0xf5000 byte（約 1 MiB、VRAM）**（実機: `P9w arena at …, own buffers f5000 bytes`、[T1-658](../agents/T1/requests.md)）。global の context buffer（golden context の物）は共有。
- chid: golden 1、scrubber 2、compute-test 3、work 4、copy 8（別の runlist）。session の channel は **16 から**（RM の channel 数は Turing で 4096 の見込み、未確認。実装は `NVRTX_SESSION_CHANNELS_MAX` 32 の bitmap から配り、尽きたら open を EBUSY）。

## 4. 設計

### 4.1 構成: session ごとの channel（案 A、採用）と 共有の channel の作り直し（案 B、不採用）

案 A: `node_open` で session の channel を作り（`nvrtx_channel_open`）、`node_close` で止めて FREE（`nvrtx_channel_close`）。executor の `nvrtx_vk_gpu_run` は session の channel に submit する。vaspace・page table・run の map は今の driver の 1 本（`work->va` の heap）を共有。

| 観点 | A: session ごとの channel | B: 共有の channel を止めて作り直し、後ろの仕事を流し直す |
| --- | --- | --- |
| compositor の仕事が詰まった仕事の後ろで待つか | GPU の側では待たない（runlist の timeslice。ただし 3.4 の WFI の制限） | 待つ。取り消しの間（STOP → RESET → FREE → 作り直し → first fence、RPC が 6〜8 回、各 ms〜数十 ms）全 session が止まる |
| 流し直し | 不要（喪失した session 以外の仕事は自分の channel で続いている） | 要る: 詰まった entry より後の他 session の entry を新 channel に積み直す。entry は push buffer の VA と長さだけなので積み直せるが、executor は run を同期で待ち push buffer を再利用するので、「後ろの仕事」は実際には worker が 1 本の今は最大 1 つ。流し直しの価値が小さい |
| 誰の仕事が詰まったかの特定 | channel = session で自明。RC の event の chid でも分かる | entry の fence の値から辿る表が要る |
| 資源 | channel ごと約 1 MiB の VRAM＋50 KiB。session 32 で 32 MiB | 増えない |
| RM の口の数 | open・close ごとに allocate・promote・bind・schedule・free（今の work channel の列と同じ、`work_channel`・`work_objects`・`nvrtx_work_stop`） | 取り消しの時だけ |
| 危険 | runtime に RM の control を送る道（4.5 の `rpc_lock`）が要る。open が RM の失敗で落ちる道が増える | 同じく runtime の RM の control が要る |

B は「取り消しの後に GPU を使える状態に戻す」を満たすが、「compositor が詰まった仕事の後ろで待たされない」を満たさない（止めている間は全員止まる）。A を採る。A でも取り消しの RM の control は要るので、B の仕事の大半は A に含まれる。

**channel の共有の例外**: display（cursor・window の channel）と copy channel は今のまま。

### 4.2 検出（期限）

- **run の期限** `NVRTX_RUN_MS`（kernel の config `CONFIG_NVRTX_RUN_MS`、既定 2000、choice 1000/2000/5000/10000、`config/kernel-options.list` に 1 行）。`nvrtx_vk_gpu_run` の fence の待ちと、submit の ring の entry の待ち（`WORK_ENTRY_WAIT_US`）の両方に使う。session ごとの channel では ring の entry の待ちは自分の前の run だけなので、実質 run の待ちと同じ。
- **期限を越えた run の意味**: その session の喪失（4.3 へ）。**他の session の期限とは独立**（共通の層の session ごとの監視と同じ考え）。
- **共通の層の期限との関係**: supervised job の実行の期限 60 s は、libvulkan の fence の export の marker（timeline）に付く。marker は frame の後ろに並ぶので、frame が 2 s で EIO になれば marker もその後すぐ完了（EIO）し、60 s には届かない。60 s は「nvrtx の検出が効かなかった時の最後の網」として残す（その時は `stop_begin`/`stop_poll` → `isolate`（4.6）→ fault の段）。値は変えない。
- **長い正当な compute を殺さないか**: nvrtx の executor は compute pipeline を拒む（`vk-pipeline.c` 758 行）ので、今の対象は draw と transfer だけ。desktop の draw は数百 µs〜数 ms（WS241: boost ありで run 平均 0.16 ms、無しで 2.5 ms）。2 s は 2 桁の余裕。boost が無い実機で run が 3.8 ms だった時期もあり（T1-696）、期限を 2 s より短くする理由は無い。将来 compute を通す時に「1 dispatch が 2 s を越える正当な仕事」が出たら、config で延ばすか、submission を分けて待つ executor の作りを変える（Future Work、7 節）。期限の値はユーザーの判断（8 節 D2）。
- **GPU の fault（MMU fault・GR exception）**: RM が RC の event を送る（3.1）。期限を待たずに event で喪失にする（4.4）。

### 4.3 取り消し（GPU の上で止める）の手順

session の channel の状態（4.8）が READY で、run が期限を越えた時（worker の thread の中、`node->lock` を持っている）:

1. `channel->state = LOST`、`session->lost = reason`（ETIMEDOUT）。queue_lock の下で `session->stopping` と同じ扱いの `session->lost` を立て、以後の `runtime_submit`・`runtime_command`・`runtime_reserve` は EIO。
2. `drv_gpu_report_session_error(node->device, session, EIO)`: 共通の層がその session を喪失に公開し（`gpu_session_fail_locked`、`stop_state = REQUESTED`）、poll と fence の観測者を起こす。libvulkan の次の ioctl は EIO → `VK_ERROR_DEVICE_LOST`（1 節）。
3. GPU の上で止める（`nvrtx_channel_kill`、`rpc_lock` の下、4.5）:
   - 3a. `STOP_CHANNEL`（bImmediate=1）をその channel に。RM は idle を待たず runlist から preempt する（3.2）。reply の status を見る。
   - 3b. `RESET_CHANNEL`（engineID = graphics の engine type（`NVRTX_ENGINE_GRAPHICS`）、subdeviceInstance 0、resetReason 0 DEFAULT、bIsRcPending は RC の event を受けていれば 1）。GR の engine の状態（走っていた warp）を RM に片付けさせる。
   - 3c. `SCHEDULE`（bEnable=0）→ 3D・compute の object の FREE → channel の FREE（`nvrtx_work_stop` と同じ順。Procedure referenced: nouveau `r535_chan_stop`・`nvkm_chan_del` の順、[work.c](../../src/drivers/gpu/nvrtx/work.c) 212〜292 行が既にその形）。
   - 3d. 各 call の status を集める。**全部 0 なら `state = FREED`**（GPU はこの channel の memory をもう読まない）。1 つでも失敗（status ≠ 0、ETIMEDOUT）なら `state = STUCK`（確かめられない）。
4. STUCK の時は channel の資源（ring・semaphore・fence・method buffer・instance・USERD・own の context buffer）を **返さない**（device が持ったまま、GSP の停止まで）。FREED なら close の時に返す。
5. 喪失した session の run の memory（executor の run、blob の VRAM）は、FREED なら session の close の通常の道で返す（`nvrtx_work_unmap` は PTE の書きと TLB の invalidate で、channel の有無に関わらず安全）。STUCK なら `isolate`（4.6）の契約により、close と resource_destroy で hardware に触らず、VA も VRAM も返さない（leak、reset まで）。

**STOP_CHANNEL が効かない時の代替**（p003 の probe で決める）: `FIFO_DISABLE_CHANNELS`（subdevice、`hClientList[0]` = 自分の client、`hChannelList[0]` = channel の handle、`bDisable=1`、`bOnlyDisableScheduling=0`、`bRewindGpPut=1`）。RM が preempt して hardware から外す。その後は 3b 以降が同じ。両方が効かなければ STUCK。

**worker の中で RPC を待つ時間**: STOP（preempt の完了を RM が待つ、既定の timeout は RM の物、未知）＋RESET＋FREE 4 回。`NVRTX_CALL_TIMEOUT_US` 5 s を各 call に当てると最悪 30 s。kill の call には短い timeout（1 s）を使い、`rpc_unanswered` は立てない（kill の失敗は device の喪失ではない）。実機で測って p003 の記録に書く。

### 4.4 RC の event の道

- `nvrtx_rpc_wait` の `NVRTX_EVENT_CHANNEL_ERROR` の枝は log に加えて `gpu->channel_error`（関数 pointer、node が start で設定）を chid・engine・exceptType・fault の番地で呼ぶ。rpc.c は node を知らないので pointer で結ぶ。
- node の handler: `node->channels[]`（chid → session）を `queue_lock` の下で引き、session があれば 4.3 の 1〜2 を行い、`channel->rc_seen = exceptType`、取り消しは **worker に頼む**（drain の thread で RM の control を送らない: drain は `rpc_lock` を持っているが、kill の手順が長い間 message の読み出しが止まるのを避ける）。worker は次の turn で `state = LOST` の channel を見つけて 4.3 の 3〜5 を行う。run を待っている最中の worker は、fence の待ちの loop で `channel->state` を見て早く抜ける（期限の前に RC が来た時は待たない）。
- chid が driver の channel（golden・scrubber・work・compute-test・copy）なら今までどおり log だけ（device 全体の問題として `runtime_fault` に繋ぐかは p003 で決める。既定は log）。
- exceptType ごとの扱いは区別しない（全部その channel の喪失）。`ROBUST_CHANNEL_GPU_HAS_FALLEN_OFF_THE_BUS`（79）だけは device 全体の fault（`runtime_fault` → `drv_gpu_report_error`）。

### 4.5 runtime の RM の control の直列化（`rpc_lock`）

- `struct nvrtx_gpu` に `struct mutex rpc_lock`。`nvrtx_rm_allocate_in`・`nvrtx_rm_control`・`nvrtx_rm_free_in`（`rpc_exchange`）と drain の `nvrtx_rpc_wait` の turn が取る。起動の worker は 1 thread なので lock の費用は無視できる。
- drain の turn を `NODE_DRAIN_TURN_US` 1 s から **50 ms** に縮める（lock を持つ時間 = turn の長さ。kill と channel の open が drain の turn を待つのは最大 50 ms）。turn の間に来た event の処理は `nvrtx_rpc_wait` の中で今までどおり。
- kill・open・close の RM の call は `nvrtx_rpc_wait` を自分の function で呼ぶので、待ちの間に来た event（RC を含む）は同じ loop で処理される（`channel_error` の callback が drain 以外の thread からも呼ばれる → handler は `queue_lock` だけで動く形にする）。
- reboot の stop（`nvrtx_node_stop`）は drain を止めてから RM を呼ぶ今の形のまま（`drain_stop` → `drain_running` 0 の待ち）。stop の後は lock を取らずに今の順で流す。
- lock の順: `node->lock` → `rpc_lock`。`work->lock`（channel ごと）と `rpc_lock` は同時に持たない（submit は RM を呼ばない）。`queue_lock`（spin）の中で RM を呼ばない。

### 4.6 共通の層の ops

| op | 実装（nvrtx） |
| --- | --- |
| `stop_begin(session, error)` | 今のまま（`stopping`）。加えて `channel->state == READY` なら LOST にして worker に kill を頼む（close の時に run が走っていれば、その run の期限を待たずに止める。正常な close は `runtime_drain` の後に来るので run は無い。期限切れ（共通の層の 60 s）で来た時だけ効く） |
| `stop_poll(session)` | 今のまま（pending が 0 か）。**加えて `channel->state` が LOST・STOPPING なら EAGAIN**（GPU の上の停止が済むまで「止まった」と言わない） |
| `isolate(session)` | **`channel->state == FREED` または `NONE`（channel を作らなかった）なら 0**（GPU はこの session の memory を読まない: RM が channel を free した）。`STUCK` なら **失敗**（EIO）→ 共通の層は device 全体の fault へ。isolate が 0 の後の `resource_destroy`・`close` は hardware に触らない（gpu.h の契約）: run の unmap（PTE の書き）は PRAMIN への access なので **行わず**、VA と VRAM を `node->quarantined` の list に積む（reset まで返さない。今は reset が無いので GSP の停止まで）。display の lease を持つ session が isolate された時は `nvrtx_present_session_close` の console への復帰は display の channel の仕事なので行ってよい（channel の違い） |
| `fault(error)` | 今のまま（node の `faulted`） |
| `reset()` | **NULL のまま**（この WS の既定）。実装するなら p005 の選択肢 b: 全 session の close の後、`nvrtx_gsp_shutdown` の teardown（P7）→ GSP の起動（P1〜P5）→ display（P8）→ node の作り直し。display の lease・console の表示・`hw.gpu.attaching` の再掲が絡み、WS037 の判断 17「reset は display も落とす」のとおり desktop の session が切れる。reboot と同じ結果に近いので、価値は「reboot より速い」だけ |

共通の層の判定の順（`gpu_monitor_step` 7190〜7240 行）に合わせると: session の run の期限 → `drv_gpu_report_session_error` → `stop_state = REQUESTED` → monitor が `stop_begin` → `stop_poll`（EAGAIN while LOST/STOPPING）→ FREED になれば 0（FINISHED）、STUCK のまま `CONFIG_GPU_JOB_STOP_MS` 10 s が過ぎれば `isolate` → 失敗 → fault。**kill の手順は 10 s より十分短い**（4.3 の最悪 30 s は call の timeout を 1 s にすれば 6 s。p003 で測る）。

### 4.7 compositor（と他の app）が止まらない形

- GPU の側: session ごとの channel（4.1）。無限 loop の draw の channel は WFI に来ないので、GPU の host（PFIFO）の timeslice で runlist の次の channel に切り替えようとし、GR が切り替えられず… の扱いは firmware の物（3.3）。**取り消し（STOP_CHANNEL の preempt）が成功すれば compositor の channel は進む**。成功しなければ（STUCK）device 全体の fault（今と同じ結末、reboot）。
- kernel の側: worker が 1 本なので、詰まった run の 2 s の待ちの間は全 session の frame が queue で待つ。**1 回の 2 s の停滞**（検出）＋kill の RM の call の時間（数十 ms〜数 s）の後、喪失した session の frame は EIO で即返り、desktop は動く。compositor は frame の reply を待つので、その間 1 frame が 2 s 遅れる（画面が 2 s 止まって見える）。
- 2 s の停滞も無くす形（p005 の選択肢 a）: session ごとに worker を持ち、executor の GPU の待ちを `node->lock` の外で行う。lock の下で触る物（objects の registry、blob の list、backing の refs、display の状態）と GPU の待ちを分けられるかは vk-execute.c の run の memory（session の run、constants、headers）が session 専有であることから可能と見るが、`nvrtx_vk_memory_detach`「every submission waited for its work」の前提（他 session の import した backing の解放）を refs で守る必要があり、WS241 の rec submit の改善と同じ所に手が入る。**この WS の必須には入れない**（ユーザーの判断 D3）。
- compositor の channel を優先する（TSG の timeslice・interleave level）は、TSG を自分で作らない限り口が無い。不採用（Future Work）。

### 4.8 状態遷移

session の channel（`struct nvrtx_channel`、session ごと）:

```
NONE ──open ok──▶ READY ──run の期限 / RC の event / stop_begin──▶ LOST
  │                                                                │ worker が kill
  └─open 失敗（EBUSY/ENOMEM/RM の status）: session の open が失敗   ▼
                                                               STOPPING ──STOP+RESET+FREE 全部 ok──▶ FREED ──close──▶ (資源を返す)
                                                                  │
                                                                  └─どれか失敗──▶ STUCK ──close──▶ (資源を返さない。isolate 失敗 → device の fault)
```

- READY → LOST は `queue_lock` の下で 1 回だけ（最初の理由を `lost_reason` に残す: ETIMEDOUT / RC の exceptType / stop）。
- LOST の channel の submit は `nvrtx_channel_submit` が ENODEV（ring に積まない）。
- FREED・STUCK の session の frame は worker が EIO で完了（GPU に触らない）。
- close（`node_close`）: READY なら `nvrtx_channel_close`（schedule off → FREE、今の `nvrtx_work_stop` の順）で FREED にしてから資源を返す。LOST なら kill を先に（同じ thread で）行う。STUCK は資源を返さない。

device（node）: 今のまま `faulted`（fault の後は全部 EIO）。

### 4.9 資源の所有権と寿命

| 資源 | 所有 | 作る | 返す |
| --- | --- | --- | --- |
| channel の instance block・USERD（VRAM 8 KiB）、own の context buffer（VRAM 約 1 MiB）、ring・semaphore・fence・method buffer（system memory） | `struct nvrtx_channel`（session） | `node_open` | `node_close` で FREED の時だけ。STUCK は GSP の停止（reboot）まで device の物 |
| vaspace（`NVRTX_HANDLE_WORK_SPACE`）、page table、`work->va` の heap | gpu（driver）、全 channel が共有 | 起動（`nvrtx_work_start`） | 停止（今のまま） |
| golden context の global の buffer | gpu | 起動 | 停止 |
| executor の run・blob の VRAM・VA | session（executor） | 今のまま | 今のまま。isolate の後は `quarantined` の list（返さない） |
| RM の handle: channel 0xf1f00010+n、3D 0x97000010+n、compute 0xc5c00010+n（n = chid − 16） | gpu | open | close / kill |

### 4.10 並行性と lock

| lock | 守る物 | 取る順 |
| --- | --- | --- |
| `node->lock`（mutex） | executor の objects、blob の list、backing の refs、display の状態（今のまま）。**GPU の待ちも中**（p002〜p004） | 最初 |
| `channel->lock`（mutex、work->lock の一般化） | その channel の ring・put・entry_fence・fence | node->lock の中、または単独 |
| `gpu->rpc_lock`（mutex、新） | `gpu->call`・`gpu->reply`・RPC の queue（4.5） | node->lock の中、channel->lock と同時に持たない |
| `node->queue_lock`（spin） | queue、markers、pending、faulted、**`session->lost`、`channel->state`、`channels[]`（chid → session）** | 最後、中で sleep も RM も呼ばない |
| `gpu->window_lock` | PRAMIN・TLB の invalidate（今のまま） | channel->lock の中（`work_entries_write` の道） |

thread: renderer の worker（frame の実行、kill の実行）、drain（event の受け取り、RC の handler は queue_lock だけ）、session の ioctl の thread（open・close: node->lock → rpc_lock で channel の作成・FREE）、共通の層の monitor の thread（`stop_begin`/`stop_poll`/`isolate`: queue_lock だけで状態を読む・書く。`isolate` は hardware に触らない）。

### 4.11 割り込みと DMA

- nvrtx は割り込みを使わない（fence は polling、RPC は polling、display は vblank の polling の drain）。この設計も polling のまま。
- DMA（GPU が読む system memory）: channel の ring・fence の push・method buffer は RM が channel を FREE するまで GPU が読みうる。FREED を RM の status で確かめてから返す。STUCK は返さない（4.9）。executor の run と blob（VRAM）は GPU の中だが、同じ扱い（FREED か isolate の quarantine）。
- GSP の RPC の queue は `rpc_lock` で直列化（4.5）。

### 4.12 失敗と回復の段（上げ方）

| 段 | 事象 | 対応 | 誰が動き続けるか |
| --- | --- | --- | --- |
| 0 | run が期限（2 s）内に終わる | 通常 | 全員 |
| 1 | run が期限を越える、または RC の event | その session を喪失（DEVICE_LOST）、channel を STOP+RESET+FREE | その session 以外（2 s の 1 回の停滞の後） |
| 2 | STOP/RESET/FREE のどれかが失敗（STUCK） | session の資源は device 持ち。共通の層の `stop_poll` が EAGAIN のまま 10 s → `isolate` が失敗 → `fault` → device の喪失（全 session EIO） | 誰も（今と同じ: reboot。display は console に戻る道が残る） |
| 3 | RPC が返らない（`rpc_unanswered`） | 今のまま: 以後の call は待たない。GSP が答えないので device の fault | 誰も（reboot） |
| 4 | `ROBUST_CHANNEL_GPU_HAS_FALLEN_OFF_THE_BUS` | device の fault | 誰も |

段 2 で `isolate` を「資源を返さずに 0」として session だけの喪失にする選択肢もある（gpu.h の isolate の契約「descriptors and allocations device-owned until checked reset」「refuses further host traffic」は満たせる）が、**GPU が本当に止まっているか分からない**（STOP が失敗した channel の warp は走り続けうる）ので、「他の context は動き続ける」の約束が怪しい。i915 の isolate は engine の reset を成功させた時だけ 0 を返す（`i915_isolate` 842〜874 行）。同じ基準にする。

### 4.13 WS241（描画の速さ）との関係

- WS241 p001 は work.c の submit（timed の stamp の entry、`nvrtx_work_run_time`）、`nvrtx_work_wait` の 2 ms の spin、perf.c の boost を入れた（main に merge 済み）。次の round（boost を既定に、rec submit、streams の計数）は未着手。
- p002 の一般化は work.c の `gpu->work` を `struct nvrtx_channel` にし、`nvrtx_work_submit(gpu, …)` を `nvrtx_channel_submit(channel, …)` にする。**submit・wait・run_time・timed の形は保つ**（関数の第 1 引数が変わるだけ）。perf の counts は node の物のまま。
- 衝突を避ける順: WS241 の次の round が work.c を触るなら、その merge の後に p002 を始める（Q1 の調整）。逆の順なら WS241 が p002 の形に追従する。

### 4.14 選ばなかった案

| 案 | 理由 |
| --- | --- |
| 共有の channel の作り直しと流し直し（案 B） | 4.1 の表。取り消しの間に全員が止まる |
| TSG を自分で作り `NVA06C_CTRL_CMD_PREEMPT`・`SET_TIMESLICE` を使う | nouveau が通っていない道（RM が暗黙に TSG を作るかも未確認）。STOP_CHANNEL で足りる見込み。効かなければ p003 で再考 |
| GFXP・CILP の preemption で無限 loop を GPU に切らせる | 追加の context buffer の確保・promote が要り、nouveau に手本が無い。Future Work |
| 共通の層の job の期限（60 s）を短くして frame にも付ける | gpu.c と UAPI の変更。frame に期限を付けるのは backend の仕事で、i915 も自分の request の timeout を持つ |
| 期限で device 全体を fault（今の作りの延長） | ユーザーの決定に反する |
| `reset` の実装（GSP の再起動） | 4.6。価値が「reboot より速い」だけで、display の作り直しが絡む。p005 の選択肢 b |
| RC の handler の中で RM の control（drain の thread で kill） | drain が長く止まり、event の受け取りが遅れる。worker に頼む（4.4） |

## 5. インタフェース

UAPI（`include/uapi/gpu*.h`）、`include/drivers/gpu/gpu.h`、HAL は **変えない**。libvulkan・compositor も変えない。

### 5.1 nvrtx の内部（案。名前は実装の Phase で coding-style に合わせて決める）

```c
/* nvrtx-work.h: one channel on the graphics runlist, the work channel generalized (work.c). */
struct nvrtx_channel {
	struct nvrtx_gpu *gpu;
	uint32_t chid;			/* 4 for the driver's own, 16.. for sessions */
	uint32_t handle_channel;	/* RM handles of the channel and its two objects */
	uint32_t handle_threed;
	uint32_t handle_compute;
	struct nvrtx_bo arena;		/* instance block, USERD, own context buffers */
	uint64_t physical[NVRTX_GR_BUFFER_MAX];
	struct nvrtx_buffer ring;
	struct nvrtx_buffer semaphore;
	struct nvrtx_buffer fences;
	struct nvrtx_buffer methods;
	struct mutex lock;
	uint32_t put;
	uint32_t fence;
	uint32_t entry_fence[NVRTX_WORK_RING_ENTRIES];
	int arena_made, channel_made, threed_made, compute_made, scheduled, ready, timed;
	uint32_t state;			/* NONE, READY, LOST, STOPPING, FREED, STUCK (under the node's queue_lock) */
	int lost_reason;		/* ETIMEDOUT, or the RC exceptType + 1000, or ECANCELED for a stop */
	uint32_t rc_type;		/* the RC event's exceptType when one came */
};

int  nvrtx_channel_open(struct nvrtx_gpu *gpu, uint32_t chid, struct nvrtx_channel *channel);   /* arena, instance, channel, promote, objects, bind, schedule, first fence */
void nvrtx_channel_close(struct nvrtx_gpu *gpu, struct nvrtx_channel *channel);                 /* schedule off, free objects, free channel; the arena when FREED */
int  nvrtx_channel_kill(struct nvrtx_gpu *gpu, struct nvrtx_channel *channel);                  /* STOP_CHANNEL, RESET_CHANNEL, then close's RM calls; 0 only when every status is 0 */
int  nvrtx_channel_submit(struct nvrtx_channel *channel, uint64_t push_va, size_t length, uint32_t *fence); /* ENODEV once LOST */
int  nvrtx_channel_wait(struct nvrtx_channel *channel, uint32_t fence, uint32_t timeout_us);   /* ECANCELED when the channel became LOST meanwhile */
int  nvrtx_channel_run_time(struct nvrtx_channel *channel, uint32_t fence, uint64_t *gpu_ns);

/* The space (vaspace, page tables, the heap of user runs) stays the gpu's: nvrtx_work_map_vram / map_system / unmap unchanged. */

/* nvrtx-gsp.h: the controls to add. */
#define NVRTX_CTL_STOP_CHANNEL		0xa06f0112U	/* params: 1 byte bImmediate (4 with padding) */
#define NVRTX_CTL_RESET_CHANNEL		0x906f0102U	/* params: u32 engineID, u32 subdeviceInstance, u32 resetReason, u8 bIsRcPending (16 bytes) */
#define NVRTX_CTL_DISABLE_CHANNELS	0x2080110bU	/* params: NV2080_CTRL_FIFO_DISABLE_CHANNELS_PARAMS (alternative) */
#define NVRTX_RC_FALLEN_OFF_THE_BUS	79U

/* nvrtx-gpu.h */
struct nvrtx_gpu {
	...
	struct mutex rpc_lock;						/* 4.5 */
	void (*channel_error)(void *, uint32_t chid, uint32_t engine, uint32_t type, uint64_t fault, uint32_t fault_kind);
	void *channel_error_argument;
};

/* nvrtx-node.h */
#define NVRTX_SESSION_CHID_FIRST	16U
#define NVRTX_SESSION_CHANNELS_MAX	32U
struct nvrtx_session {
	...
	struct nvrtx_channel channel;
	int lost;				/* under queue_lock: EIO for every new frame, command, reservation */
};
struct nvrtx_node {
	...
	struct nvrtx_session *channels[NVRTX_SESSION_CHANNELS_MAX];	/* chid - FIRST -> session, under queue_lock */
	uint32_t channel_bitmap;
	struct nvrtx_quarantine *quarantined;				/* VA and VRAM of isolated sessions, never returned */
};
```

- `nvrtx_vk_gpu_run`（vk-node.c）: `nvrtx_work_submit(node->gpu, …)` → `nvrtx_channel_submit(&session->channel, …)`、待ちは `CONFIG_NVRTX_RUN_MS`、ETIMEDOUT・ECANCELED は EIO のまま返す。期限の時に `nvrtx_runtime_session_lost(node, session, ETIMEDOUT)`（runtime.c、4.3 の 1〜2）を呼ぶ。
- `runtime_worker`: entry を取った時に `entry->session->lost` なら frame を GPU に出さず EIO。frame の後に `channel->state == LOST` の session があれば `nvrtx_channel_kill`（node->lock → rpc_lock）。
- RM の param の layout は `records.c`・`objects.c` の既存の形（`nvrtx_put32` で byte offset）に合わせ、block ごとに「Values referenced: NVIDIA open-gpu-kernel-modules 570.144 src/common/sdk/nvidia/inc/ctrl/ctrla06f/ctrla06fgpfifo.h (NVA06F_CTRL_STOP_CHANNEL_PARAMS)」などを付ける。

### 5.2 log の行（段の印、`nvrtx_stage_mark`）

開発の管理情報（WS・Phase の番号）は入れない（Guardrail）。

| 行 | 意味 |
| --- | --- |
| `channel %u: session channel on runlist %u` | open |
| `channel %u lost: run of fence %u did not finish in %u ms` | 期限 |
| `channel %u lost: rc type %u eng %x fault %llx/%u` | RC の event |
| `channel %u stopped and freed (%u ms)` | kill の成功 |
| `channel %u stuck: %s status %08x` | kill の失敗（どの call か） |
| `session isolated: %u runs and %u blobs retained` | isolate |

## 6. 試験計画

### 6.1 host 試験（QEMU も実機も要らない）

`plan/ws250/tests/nvrtx-channel-host-test.sh`（`plan/ws037/tests/nvrtx-vk-host-test.sh` と同じ作り: kernel の C を host の cc で、stub を test の C に）:

1. channel の状態機械: READY → LOST（1 回だけ、最初の理由）、LOST の submit は ENODEV、wait は ECANCELED、kill の各 call の status の組み合わせ（全部 0 → FREED、どれか失敗 → STUCK）、close の資源の扱い（STUCK は返さない）。RM の call は stub（status を script で与える）。
2. RM の param の layout: STOP_CHANNEL（bImmediate の offset）、RESET_CHANNEL の 4 欄、DISABLE_CHANNELS の欄の offset と大きさ（header から計算した値と一致）。
3. runtime: lost の session の frame が GPU に出ず EIO で完了する、他の session の frame が続く、RC の handler の chid → session の表。
4. 期限: `nvrtx_channel_wait` の timeout の計測（`nvrtx_now_us` を stub）、config の値の反映。
5. 共通の層との結合は `src/drivers/gpu/gpu.c` の host 試験が無い（見つからなかった）ので、ops の返す値の表（stop_poll が LOST/STOPPING で EAGAIN、isolate が FREED で 0・STUCK で EIO）を test で確かめる。

### 6.2 QEMU

GPU が無いので nvrtx の道は通らない。boot の回帰（`plan/tools/boot-test.sh`、既定の image）だけ。p002 の config の追加（`CONFIG_NVRTX_RUN_MS`）が既定の image を変えないことの確認。

### 6.3 実機（RTX 2070、10.0.30.5、方法 A の素の起動、T1 に依頼）

image: `plan/ws037/tests/config-amd64-nvrtx.mk` の RAk（方法 A、`nvrtx.vk=1`）に `vkhang`・`vkquad`・`keiland-shot` を足した config（`plan/ws250/tests/config-amd64-nvrtx-hang.mk`、`include` で ws037 の物を読み、`ZEDBSD_USER_PROGRAMS += vkhang vkquad keiland-shot`、`ZEDBSD_TEST_SCREEN_CAPTURE=y`）。kernel の差し替えと reboot、SSH での観測は T1-684・T1-692 の手順（`/boot/boot0/vmunix` の複写、dmesg の `nvrtx` の行、sessiond.log、ps）。

**試験の app `vkhang`**（新、`userland/tests/vkhang/`、p004）: Vulkan 1.0 の API だけで、vkquad と同じ道（offscreen の 64x64 の target）に **fragment shader が無限 loop**（`while (x != 0) x = x * 3 + 1;` の形で uniform から来る値、compiler が消せない）の quad を 1 つ描き、vkQueueSubmit → vkWaitForFences の結果を 1 行で出す: `VKHANG lost after=<ms> result=<VkResult>`（DEVICE_LOST が期待）、`VKHANG fail <step>`。option `--loops N` で有限の loop（正常に終わる対照）、`--draws N` で連続の submit。compute pipeline は executor が拒むので使わない（fragment の loop でも GPU の上の挙動は同じ「WFI に来ない draw」）。

| scenario | 手順 | 合格 | 証拠 |
| --- | --- | --- | --- |
| S0 対照 | desktop の session の中で `vkhang --loops 1000` | `VKHANG ok`、desktop は変化なし | 出力、`keiland-shot` の PNG |
| S1 probe（p003 の最初） | `vkhang`（無限）。kernel は **取り消しを行わない build**（`nvrtx.kill=0` の boot の語） | 観測だけ: RC の event が来るか（`channel %u lost: rc …` の行）、来るなら何秒後か、その間 compositor の frame が進むか（`keiland-shot` が答えるか、sessiond の frame の log）、GSP の firmware の error log の行 | dmesg、PNG、時刻 |
| S2 取り消し | `vkhang`（無限）、既定の build | ≤ 2 s＋kill の時間で `VKHANG lost … result=-4`（DEVICE_LOST）、dmesg に `channel %u lost: run …` と `channel %u stopped and freed`、**desktop が動き続ける**（`keiland-shot` の PNG が kill の後に新しい frame を写す、Files の窓を動かせる）、その後 `vkquad` が `VKQUAD ok` | 出力、dmesg、PNG 2 枚（前後） |
| S3 繰り返し | S2 を 5 回（vkhang を 5 回起動） | 5 回とも S2 の合格、chid が再利用される（`channel 16` が毎回）、VRAM の allocator の残りが戻る（perf の行か `nvrtx.vk.perf=1`） | 同上 |
| S4 同時 | `vkhang` 2 つを同時に、`vkquad` を間に | 2 つとも DEVICE_LOST、vkquad ok、desktop 継続 | 同上 |
| S5 close の途中 | `vkhang` を起動して 0.5 s で kill -9（run の最中に close） | `stop_begin` → kill → FREED、process が終わる（close が返る）、desktop 継続 | dmesg、ps |
| S6 段 2 の確認（STOP が失敗する形を作れないので観測のみ） | S1〜S5 で `channel %u stuck` が出た時 | 共通の層が 10 s 後に `isolate` → fault（dmesg の `renderer: the renderer faulted`）、console に戻る | dmesg |

実機の証拠と QEMU の証拠は分けて書く。実機の時間は T1 の lock（host 全体で 1 つの RTX 機）の中。

## 7. Phase の分割と LW（1 LW ≈ 実時間 15〜25 分）

| Phase | 内容 | 受け入れ条件 | LW | 依存 |
| --- | --- | --- | --- | --- |
| p002 channel の一般化 | work.c の `gpu->work` を `struct nvrtx_channel` に（driver の channel は chid 4 のまま 1 本）、session ごとの channel の open・close（chid 16〜、bitmap）、executor が session の channel に submit、`rpc_lock` と drain の turn の短縮、`CONFIG_NVRTX_RUN_MS`、host 試験 6.1 の 1・4。**取り消しはまだ無い**（期限は今と同じ EIO） | build warning 0（nvrtx=y と CI の config）、host 試験 PASS、desktop の回帰（T1: RAk の R9k 相当: session が上がり Files が描ける、`channel 16: session channel` の行）、`nvrtx.vk.perf=1` の run の時間が WS241 の値から悪化しない（run avg ±20%） | 6〜8 | WS241 の今の round の merge（4.13） |
| p003 検出と取り消し | 期限 → session の喪失 → `drv_gpu_report_session_error` → kill（STOP+RESET+FREE、代替の DISABLE_CHANNELS）、RC の event の道、`stop_begin`/`stop_poll`/`isolate` の実装、quarantine、段の印。host 試験 6.1 の 2・3・5。S1 の probe → 結果で STOP/DISABLE を決めて S2 | build warning 0、host 試験 PASS、**実機 S1 の観測が記録にある**、S2 PASS（app だけ DEVICE_LOST、desktop 継続、vkquad ok） | 6〜8 | p002、p004 の vkhang（S1 に要るので p004 の app の部分を先に） |
| p004 試験の app と scenario | `vkhang`（package、Makefile、1 行の出力）、`config-amd64-nvrtx-hang.mk`、T1 の依頼の文（S0〜S5）、結果の記録 | S0・S2〜S5 PASS（実機）、S6 は観測のみ | 3〜4 | p002（app 自体は p002 と並行に作れる） |
| p005 選択肢（ユーザーの判断） | a: session ごとの worker と GPU の待ちの lock の外出し（2 s の停滞を無くす）。b: `reset`（GSP の再起動）。両方・片方・無し | a: S2 で compositor の frame が 2 s 止まらない（sessiond の frame の log、PNG）。b: device の fault の後に全 session の close → 新しい open で `P8c ok` 相当の再登録、desktop の session の再起動 | a 8〜12、b 10〜14 | p003 |
| p006 規約の全文と出典の監査 | coding-style の全文で p002〜p004 の C を見直す、「Value(s)/Procedure referenced:」の block の点検、`plan/ws250/temp/` を commit しない確認、ws.md を完了の形に | 指摘 0、build warning 0 | 2 | 全部 |

必須（p002〜p004・p006）: 17〜22 LW。p005 は別に判断。

## 8. ユーザーに決めてほしい点（既定の案と理由）

| # | 問い | 既定の案 | 理由 |
| --- | --- | --- | --- |
| D1 | session ごとの channel にするか（案 A） | **する** | 4.1 の表。compositor が GPU の側で待たない唯一の形。費用は session ごと約 1 MiB |
| D2 | run の期限の値と config | **2 s、`CONFIG_NVRTX_RUN_MS`（choice 1000/2000/5000/10000）** | desktop の run は ms 台（WS241）。compute は今の executor が拒むので長い正当な仕事は今は無い。config に出すのは実機で調整するため |
| D3 | 検出の 2 s の間 desktop が 1 回止まる（worker 1 本）を Beta 2 で受け入れるか。無くすなら p005 a | **受け入れる**（p005 a は後で） | 2 s は 1 回だけで、その後は動き続ける。p005 a は WS241 の lock の改善と同じ所に手が入るので、WS241 の次の round と合わせて判断する方がよい |
| D4 | `reset`（GSP の再起動）を作るか（p005 b） | **作らない** | 4.6。WS037 の判断 14・17 と同じ。段 2 以降は reboot |
| D5 | RC の event（GSP の firmware からの channel error）でも session を喪失にするか | **する** | MMU fault・GR exception は期限を待つ理由が無い。exceptType 79 は device の fault |
| D6 | compositor の channel の優先（timeslice） | **しない**（Future Work） | TSG を自分で作る道が未検証 |
| D7 | p002 の順を WS241 の次の round の merge の後にするか | **後にする** | 4.13。同じ file（work.c・vk-node.c）の衝突を避ける |

## 9. 危険と未確認（敵対的に見た点）

1. **STOP_CHANNEL（bImmediate）が GSP-RM 570.144 で user の channel に効くか、無限 loop の warp を本当に止めるか**: source は「転送する」までしか言わない。効かなければ DISABLE_CHANNELS、それも効かなければ段 2（device の fault）で今と同じ。p003 の S1・S2 が決める。
2. **RESET_CHANNEL の後に同じ runlist の他の channel（compositor）が壊れないか**: RM の engine の reset は runlist 全体の context を切り替え直す可能性がある。S2 の「vkquad ok」と PNG で見る。壊れるなら RESET を省いて STOP+FREE だけにし、再び S2。
3. **RM の channel の数・USERD の page の割り当て**: chid 16〜47 を `USERD_INDEX_PAGE_FIXED` で使う（chid/8 の page、chid%8 の index）。RM が「page fixed」で許す範囲は未確認。open の status で分かる（S3 で chid の再利用も見る）。
4. **own の context buffer の VA**: 事実: `nvrtx_gr_promote`（`gr.c` 545〜600 行）は promote の entry に `gr->virtual_address[index]`（golden context の VA）と呼び手の物理 address を書く。work channel は own の main・patch をその同じ VA に map している（`work_tables`）。session ごとの channel で vaspace を共有すると、**同じ VA に channel ごとの別の物理を map することはできない**。p002 は (i) session の channel の own buffer を channel ごとの別の VA（例 `NVRTX_WORK_VA_USER` の下に channel ごとの 2 MiB の区画）に map し、`nvrtx_gr_promote` に VA の配列も渡してその VA を promote に書く。RM が「golden context と違う VA の main・patch」を通すかは **未確認**（nouveau の `r535_gr_promote_ctx` は vmm ごとに context buffer を map し VA を渡すので、通る見込み）。通らなければ (ii) session ごとに vaspace を分ける（page table 2 MiB＋run の map を session ごとに行う: 費用が増え、import の VA が session ごとに変わる）。p002 の最初の実機の確認（`channel 16: session channel on runlist` と first fence）で分かる。
5. **drain の turn を 50 ms にする副作用**: `nvrtx_rpc_wait` の loop の step は 10 µs なので CPU の費用は変わらない。turn の切れ目で `nvrtx_present_refresh_sample` と perf の行の処理が 20 倍走るが軽い（refresh の読みは register 1 つ）。
6. **worker の中で RM を待つ時間**: kill が長いと他 session の frame がその分遅れる（4.3）。call の timeout を 1 s にして最悪 6 s。実機で測る。長ければ kill を別の thread に。
7. **close の途中の kill（S5）**: `node_close` は `node->lock` を取ってから executor を閉じる。kill は rpc_lock も要る。lock の順（4.10）を守れば deadlock は無い。
8. **isolate の後の leak**: STUCK の session の VRAM（blob と run）は reboot まで戻らない。繰り返せば VRAM が尽きる。段 2 は device の fault（全員 EIO）なので、繰り返しは起きない（新しい open は fault の後に失敗）。
9. **gpu.c の close の待ち**: `gpu_monitor_close` は `stop_state` が FINISHED/QUARANTINED になるまで待つ。`stop_poll` が LOST/STOPPING で EAGAIN を返すので、kill が終わらない限り close が返らない。kill は worker が行い、worker が frame の run で 2 s 待っている最中なら最大 2 s＋kill。`CONFIG_GPU_JOB_STOP_MS` 10 s の中に収まる見込み（S5 で測る）。
10. **compute を通す将来**: dispatch が executor に入った時、1 dispatch の長さは app 次第。期限の config と、長い仕事の submission を分けて待つ executor の作りは別の WS。

## 10. license と転記の扱い

- 参照した source とその SPDX: Linux v6.19 nouveau `nvkm/subdev/gsp/rm/r535/fifo.c`・`nvkm/engine/fifo/chan.c`・`nouveau_chan.c`・`nouveau_exec.c`・`nouveau_sched.c`・`nouveau_fence.c`・`nvkm/engine/gr/gf100.c`（MIT の表示）、open-gpu-kernel-modules 570.144 `src/common/sdk/nvidia/inc/ctrl/ctrla06f/ctrla06fgpfifo.h`・`ctrl/ctrl906f.h`・`ctrl/ctrl2080/ctrl2080fifo.h`・`ctrl/ctrl2080/ctrl2080gr.h`・`ctrl/ctrla06c.h`・`nverror.h`・`alloc/alloc_channel.h`・`src/nvidia/generated/g_rpc-structures.h`・`g_kernel_channel_nvoc.h`・`g_kernel_channel_group_api_nvoc.h`・`src/nvidia/src/kernel/gpu/fifo/kernel_channel.c`・`kernel_fifo_ctrl.c`・`gpu/rc/kernel_rc.c`・`kernel_rc_callback.c`・`kernel_rc_watchdog.c`・`kernel_rc_watchdog_callback.c`・`gpu/gr/kernel_graphics.c`（MIT、`COPYING`）、Mesa 25.3.6 `src/nouveau/vulkan/nvk_queue.c`・`nvkmd/nouveau/nvkmd_nouveau_ctx.c`（MIT）。
- 取った物は値（control の番号、param の欄と大きさ、RC の struct の欄、Xid の番号）と手順（STOP → RESET → FREE の順、nouveau の channel の消す順、RC の event の扱い）だけ。code・comment・構造・symbol の名前は写さない。実装では block ごとに「Values referenced: / Procedure referenced:」（複数行の comment、file の上部には書かない）。
- `plan/ws250/temp/ogkm-src/`（GitHub から取った kernel の source）は commit しない（`temp` は git に入れない規則）。この文書は GPL の code を含まない（nouveau は MIT の file だけを参照、Linux の GPL の file は読んでいない）。
- zedBSD の code は Zlib。

## 11. 参照

- [ws.md](ws.md)、[phase001](phase001/phase.md)
- [WS037 nvrtx の設計](../ws037/nvrtx-design.md) 8.3（回復）、[license の監査](../ws037/nvrtx-license-audit.md)
- [WS241](../ws241/ws.md)、[ws241-p001](../ws241/phase001/phase.md)
- `include/drivers/gpu/gpu.h`、`src/drivers/gpu/gpu.c`、`src/drivers/gpu/i915/reset.c`、`src/drivers/gpu/nvrtx/{work.c,runtime.c,rpc.c,channel.c,node.c,vk-node.c,vk-queue.c,vk-execute.c,objects.c}`
- design-reviewer の結果: [design-review.md](design-review.md)（p001 の中で受ける）
