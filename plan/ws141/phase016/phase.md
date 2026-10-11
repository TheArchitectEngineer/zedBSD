<!-- awesome-plan project=zedbsd record=ws141-p016 -->

# ws141-p016: GPU の実行中に ioctl と cursor を止めない（worker の mutex、cursor の plane の GPU 無しの更新）

Parent: [WS141](../ws.md)
Status: planning（設計の第 3 版。[review-1](review-1.md)・[review-2](review-2.md) を反映、review-3 の前）
Phase disposition: normal
Queue: P3（Q1 が割り当て、2026-10-12）

## 由来

- 2026-10-12 ユーザー（Q1 経由、kernel 237d26834 の実機）:「App Homeへの遷移がまだ数秒かかっています。ウィンドウドラッグは5fpsくらい。…アプリアイコンのポイントは1秒くらいマウス移動が2fpsに重くなります」。
- 2026-10-12 Q1:「(A) を先に: 並行の設計（mutex を外す区間、hold で守る object、job の queue の順序と fence の通知、reset・recovery・suspend・close との競り、mutex の下に残す物）を phase に書いて design-reviewer に掛け…その後に実装。(B) は cursor の像が変わった時だけ kernel が 16KB を写す形で、Guardrail の『内容が変わった時の一度だけの用意』に入るので可（位置だけの present は MOVE だけ）。i915・Venus は変わらないことを確かめて記録。」

## 問題（source で確かめた事実、第 2 版で review-1 の F1 を入れて直した）

1. **vkQueueSubmit は wire の意味として同期**（review-1 F1）: libvulkan の `queue_attempt` は device の mutex の下で `vulkan_command_execute(..., VK_TRUE)` の reply を待つ（`queue.c:422,460`、`context.c` の `vulkan_context_transaction`: `GPU_COMMAND_SUBMIT`＋context の timeline の通知＋reply の trailer）。kernel の QueueSubmit（`vulkan-queue.c` `bcm2711_vulkan_queue_dispatch`）は decode・prepare の後に `run_submission`（全 job を GPU で実行して retire）を行ってから reply を書く。だから vkQueueSubmit は自分の GPU の実行と、worker の FIFO の前の request（全 session 共通の 1 本）の実行を待つ。fence は FIFO の後ろの marker（`job_commit`→`queue_marker`）で、worker が marker に達した時に完了する。
2. worker は 1 つの request の間 controller の mutex を持つ（`render-worker.c` `bcm2711_render_worker_step`）。FIFO を通らない ioctl（resource の作成・map・破棄など）もその間待つ。
3. GPU の実行中は `memory_map`・`memory_release` が `memory_ready`（`job_busy`）で EIO（今は mutex で直列なので起きない）。
4. cursor の plane の present（p014）は compositor の main loop の中（`cursor-plane.c:392-478`）で、frame の vkQueueSubmit が戻らない間は動かない。libvulkan の WSI は present ごとに GPU の copy の job を同じ FIFO に入れる。
5. 実機（2026-10-12、kernel 9d89e5c27）: compositor の 1 frame 170〜200ms のうち submit＋present 約 140ms。App Home を開いた最初の frame の vkQueueSubmit は 10 秒近く戻らず、その間の kernel の submission は解放（view・storage）が平均 149ms・最大 4.5s（HAL の `hal_pmem_unmap_uncached` の page ごとの TLB の全体の flush、Q1 が b17a753d4 で直した）。

## 第 1 版（mutex を外すだけ）を捨てた理由

review-1: F1（submit は mutex でなく wire の同期で待つので目的を達しない）、F2（旧来の同期の GPU_COMMAND の経路で mutex が外れると session の arena が巻き戻され memory が壊れる）、F3（pass_run は共有の物に触る）、F4〜F9。第 2 版は「QueueSubmit を準備の後に返し、実行は別の executor」に組み直し、mutex を外す区間は executor の中だけに限る。

## (A) 第 3 版: QueueSubmit の実行を decoder の FIFO から切り離す

第 2 版の方向（QueueSubmit を準備の後に返し、実行は別の executor）は review-2 で正しいとされた。第 3 版は review-2 の C1・H1〜H5・M1〜M5・L1〜L5 を入れて書き直した。

### 全体

- decoder（今の FIFO の worker）とは別に controller に 1 つの executor の thread。decoder の QueueSubmit: decode・`check_admission`・`prepare_submission`（全部か無し）を mutex の下で行い、成功したら **予約の公開**（下の「公開の順序」(1)）をして、submission を heap の execution に移して executor の queue に入れ、VK_SUCCESS を reply する。
- executor: execution を順に実行する（「区間」）。DEVICE_LOST は今の `publish_failure`（この execution の分だけ）と fault の流れで fence・後の command に伝える。
- 上限（H2）: execution は session ごとに 4 個・全体で 16 個、heap の bytes は全体で 32 MiB（submission の CPU snapshot は最大 8 MiB）。超えたら decoder の QueueSubmit は reply に `VK_ERROR_OUT_OF_HOST_MEMORY`（libvulkan は既にこの値を返せる、vkQueueSubmit の Vulkan の意味どおり）。decoder は眠らない。上限の値は M の計測（1 frame の submission の数）で決め直す。

### 順序と marker

- execution は全体の 1 本の queue に decoder が入れた順で並び、番号 `sequence` を付ける。executor は `executed` を進める。
- fence の marker（`job_commit`→`queue_marker`）: decoder が marker に達した時に `required = 最後に入れた execution の番号` を記録し、`executed < required` なら slot を新しい状態 **PARKED** にして保留の list（`hardware.guard` の下）へ移し、decoder は次へ進む。executor は (3) で `executed` を進めた後に `required ≤ executed` の PARKED を集め、(4) で guard と mutex の外で `finish_request` する（L4）。
- marker の枠（H3）: 今の 8 個（`BCM2711_RENDER_RESERVATIONS`）を全体で 16 に増やし、加えて session ごとに 2 個を予約から外さない最低の枠とする（compositor が他の session の marker で詰まらない）。M の計測（`markers_max`）で、1 frame の間に保留になる marker の数を確かめて値を決める。受け入れの条件: 「marker の枠が他の session で埋まっても compositor の vkQueueSubmit が戻る」。
- **idle の保留は作らない**（H1）: libvulkan は `GPU_OP_QUEUE_WAIT_IDLE`・`DEVICE_WAIT_IDLE` を送らず、vkQueueWaitIdle・vkDeviceWaitIdle は空の submit＋private fence＋marker の待ち（libvulkan `queue.c:203-236`）。kernel の wait_idle（`vulkan-device.c:468-509`）は、executor の導入の後は「`executed` が最後の番号に達し窓が無ければ VK_SUCCESS、でなければ待たずに VK_NOT_READY」の即時の判定に変え、`job_busy` を DEVICE_LOST とみなす判定を外す。wire の上では bcm2711 の libvulkan は使わない opcode と記録する。「request ごと保留」の機構は作らない。
- layout の検査と公開は execution の時だけ（`vulkan-barrier.c:174`、`vulkan-native-execute.c:130`）で順は今と同じ。

### 公開の順序（H5）

1. **enqueue の時（decoder、mutex の下）**: 今 `run_submission` の始めにしている予約の公開（`signals_reserved`・`waits_reserved`・`sync->pending`、fence の pending、`vulkan-queue.c:526-537`）を、heap に移す前に行う。semaphore の `pending` は「保留中の操作の数」にし、`prepare_sync` の検査は予約の数どうしの関係で行う（N の signal を N+1 が wait できる）。
2. **executor の execution の終わり（mutex の下）**: completed・signaled の公開（`vulkan-queue.c:559-581`）と `release_submission`（`prepared_release` で `command->pending--`・`set->pending--`、`vulkan-prepared.c:94-108`）を終える。失敗は `publish_failure` を「この execution の分の pending を減らす」形に（`vulkan-queue.c:640-653`）。
3. **guard の下**: `executed++`、`required ≤ executed` の PARKED の marker を list から外して手元に集める。
4. **guard と mutex の外**: 集めた marker に `finish_request`（`drv_gpu_complete` の再入を避ける）。失敗は mutex の外で `bcm2711_render_fail`。
- これで marker の完了（vkWaitForFences が戻る）の時には fence・command buffer・descriptor set の pending が下りており、直後の vkResetFences・vkResetCommandBuffer・vkUpdateDescriptorSets・再 submit が EBUSY にならない（試験に入れる）。

### PARKED の状態機械（M1・M3）

| 場面 | QUEUED | ACTIVE | PARKED |
| --- | --- | --- | --- |
| executor の (3)(4) | — | — | 完了（`finish_request`、ACTIVE を経ずに PARKED から完了できるように `finish_request` の trap の条件を変える） |
| fault（`bcm2711_render_worker_fault`、mutex を取らない文脈もある） | canceled を付ける（今どおり） | 今どおり | **EIO を付けて起こすだけ**。executor が mutex の下で queue の残りの execution を実行せずに dispose（fence・sync は DEVICE_LOST）し、`executed` を最後まで進め、(3)(4) で PARKED を EIO で完了 |
| stop（`bcm2711_render_worker_stop`） | canceled（今どおり） | 今どおり | その session の PARKED はそのまま（execution の完了か cancel で完了する） |
| drain | 今どおり | 今どおり | session の pending に PARKED と保留の execution を数えて待つ |
| recovered | — | — | `recovered` の条件に「executor の queue と PARKED の list が空」を足す |
| `reservation_capacity`・`claim_timeline` | — | — | PARKED も supervised の数に入れる |

- executor は各 execution の始めに faulted・stopping・uncertain を確かめ、立っていれば実行せずに cancel する（M3）。

### arena と旧来の経路

- execution に移すのは actions・syncs・queue・fence・device の object と、prepared が持つ command buffer・descriptor の clone（batches は不要、review-2 で確認）。arena を指さない。
- 旧来の同期の `GPU_COMMAND`（`runtime_command`）の QueueSubmit は EOPNOTSUPP。旧来の経路も mutex を取るので下の C1 の対象（「関わらない」は誤りだった）。
- 1 つの stream の中で、submit の後の command が submit した execution の使う resource を消すことは Vulkan の範囲の内（prepared の retain と pending で memory は安全、`vulkan-prepared.c:49-66,370-387`）。非同期で範囲が広がるが同じ仕組みで守る（L3）。

### executor の区間と窓（C1・M2・M4）

executor の pass ごとの 3 区分:

| 区分 | mutex | 内容 |
| --- | --- | --- |
| 窓の前 | 持つ | pass の作成（CPU の変換・storage の確保・PTE の公開）、`validate_pass` の検証（`space->deferred`・`view->references` の読み、`vulkan-native-execute.c:155-157,171-178,217,233-267`）、CPU の clear |
| 窓の中 | **外す** | `bcm2711_v3d_job_run` の列（TFU の変換の job と CL の job、admission・起動・IRQ の完了待ち・cache の clean・counter の読み）だけ。TFU と CL の隙間も窓の中（窓は pass の全部の job を包む） |
| 窓の後 | 持つ | 出力の公開（`memory_written`・`output->synced_*`・`target->layout`・TFU の後の `snapshot_filled`、`vulkan-native-execute.c:109-130,369-371`）、性能の加算、pass の解放 |

- **窓の判定**（C1）: `memory_ready` は `job_busy` でなく `hardware.window` を見る（窓は pass の全部の job を包む）。窓の間に mutex を持つ者（decoder の vkFreeMemory・最後の参照を落とす vkDestroy*（`vulkan-memory.c:340,354`）、旧来の経路、`render_close`・`render_destroy`、compositor の client の buffer の解放 `import.c:410`・`shm.c:948`）が参照を 0 にした view は、**unmap せず遅延解放の list** に入れる（references=0、quarantine にしない）。窓が閉じた後に mutex を持つ者（executor の「窓の後」、または decoder の次の request の始め）が一括で unmap・`pages_sync`・解放する。
- map（新しい PTE）は executor の「窓の前」と ioctl（blob の作成・import）だけ（decoder は map しない）。ioctl の map は mutex を取る前に mutex なしで窓の終わりを待ち、取った後に再確認し、立っていたら外して待ち直す。`render_close`・`render_destroy` も同じ（mutex の前に窓の終わりを待つ）。
- **窓を開く条件**（M2）: guard の下で `pte_waiters > 0` なら窓を開かず、executor は mutex を外して `pte_waiters == 0` まで眠る（waitq の sequence の型）。待ち手は map の後に減らして起こす。kernel の mutex は handoff しない（`src/kern/lock.c:311`）ので「譲る」には頼らない。
- 待ちの上限は bcm2711 の job の監視の実値（`JOB_PROGRESS_WINDOWS`×0.5s を pass の job ごと）。fault の全ての経路で `hardware.idle` を wake_all。待ち手は faulted・!ready を先に見る。
- display の `resource_write` の `generation++`（`display-device.c:580`）は controller の mutex と直列でないので、generation と write log の ring の更新と読みを buffer の guard の下に移す（M4）。

### reset・recovery・close・stop

- close: drain が mutex の外で先に、その session の QUEUED・ACTIVE・PARKED と保留の execution を待つ（今の close は drain が先、sessions の減算が後で成り立つ）。
- checked stop（`render_stop_poll`）: 保留の execution と PARKED があれば未完了と答える。
- reset: sessions が 0、executor の queue と PARKED が空、窓が無い時だけ。

### userland の点検（H4・L1、A1 の前）

- **libvulkan は書き換えない**（L1）: 既に「reply は完了ではない」前提（semaphore の software payload は受理で消費 `queue.c:461-473`、WSI の present の readback は fence の待ちの後 `wsi-swapchain.c:2772-2786`、WaitIdle は opcode を使わない）。確認の記録だけ残す。capset の STRICT_QUEUE（`context.c:192-195`）の意味も保てる。i915・Venus は libvulkan を変えないので影響なし。
- **compositor の cursor-plane.c**（H4）: `plane_present` は前の copy の fence を 100ms の上限で待ち、VK_SUCCESS 以外で plane を閉じる（`cursor-plane.c:428-454`）。非同期の後は重い job の後ろで VK_TIMEOUT になり得るので、待ちを `vkGetFenceStatus`（非 blocking）にし、NOT_READY なら今回は present しない（次の pass で再試行）、plane は閉じない。この直しは非同期化の前（A0）に入れる（今の同期の意味でも害が無い）。
- 他の userland の点検: bcm2711 の同期の意味でしか試されていない所（fence の短い上限の待ち、EBUSY を致命とみなす所）を grep で洗い、A0 に入れる。

### (B) cursor の plane

- (A) の後。F6・F7 の対処は第 2 版のとおり。F8（swapchain の image を display に import する詳細）は (B) の別の版の設計で、その review を通るまで (B) の実装は止める（L5）。

## 段（第 3 版）

| 段 | 内容 | 確かめ |
| --- | --- | --- |
| M | 計測: request の FIFO の待ちと実行、解放の内訳、`markers_max`、submit の mutex の待ち（kernel 07a445a75 まで） | 実機の数 |
| A0 | userland の点検と直し（cursor-plane.c の非 blocking の fence、他の短い待ち・EBUSY の扱い）、libvulkan の確認の記録 | host build、T1 |
| A1 | executor・execution（heap、上限）・PARKED・公開の順序・旧来の拒否・semaphore の予約・wait_idle の即時判定・marker の枠。executor は GPU の実行の間も mutex を持つ（効果は A2 から、L2） | host（決定的な fixture） |
| A2 | 窓（3 区分、window の判定、遅延解放の list、pte_waiters、display の generation の guard） | host |
| B | cursor の plane（別の版の設計の review の後） | — |

## 試験（M5）

- vulkan-device の host 試験に、executor の 1 歩（`executor_step`）と decoder の 1 歩（`worker_step`）を外から順に呼べる**決定的な fixture** を作る（本物の thread では interleave が再現しない）。場面: (a) 窓の間の decoder の vkFreeMemory・vkDestroyImage と別 session の close（遅延解放、EIO にならない）(b) TFU と CL の隙間で PTE が変わらない (c) execution の上限と OUT_OF_HOST_MEMORY (d) marker の枠が他の session で埋まっても compositor の submit が戻る (e) cursor plane の fence が NOT_READY で plane が閉じない（compositor の host 試験）(f) marker の完了の直後の vkResetFences・vkResetCommandBuffer・vkUpdateDescriptorSets が成功 (g) fault で PARKED が EIO で終わり render_fail が届く (h) checked stop と保留の execution (i) 細工した stream（1 request に多数の QueueSubmit、同じ session の複数 request）(j) N の signal を N+1 が wait する semaphore の鎖 (k) 旧来の経路の QueueSubmit が EOPNOTSUPP。
- 実機: compositor の queue-submit が GPU の時間から外れる、App Home・drag の間の cursor と入力、窓の間の解放（`views` の遅延解放の数を perf 行に）。i915・Venus は T1（A0 の userland の変更の回帰）。

## 未解決

- GPU の実行中に無関係の VA の PTE を編集して TLB を flush する安全（review-2 の未確認）: 第 3 版は窓の間に PTE を編集しないので不要。
- 準備の区間（CPU の変換、per-draw の upload）は executor の mutex の下に残る。per-draw の upload の削減は ws141-p013 で先に行う（2026-10-12 Q1）。
