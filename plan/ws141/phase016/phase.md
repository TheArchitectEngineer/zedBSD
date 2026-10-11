<!-- awesome-plan project=zedbsd record=ws141-p016 -->

# ws141-p016: GPU の実行中に ioctl と cursor を止めない（worker の mutex、cursor の plane の GPU 無しの更新）

Parent: [WS141](../ws.md)
Status: planning（設計の第 4 版。[review-1](review-1.md)〜[review-3](review-3.md) と 2026-10-12 のユーザーの判断（列を app ごとに）を反映、review-4 の前）
Phase disposition: normal
Queue: P3（Q1 が割り当て、2026-10-12）

## 由来

- 2026-10-12 ユーザー（review-3 の N4 への判断、Q1 経由）:「列を app ごとに」→ executor の実行の列を session ごとに分け、pass の単位で round-robin するところまで p016 に含める。受け入れの条件を「A: main loop と入力が止まらない」「B: cursor が動く」に分ける。

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

## 第 4 版の差分（review-3 とユーザーの判断）

第 3 版の (A) の本文は、この節と食い違う所をこの節で置き換える。第 3 版で解決とされた所（C1 の窓の判定と遅延解放、H1 の idle の保留の廃止、H5 の公開の順序、PARKED の状態の表の骨格、3 区分の窓）はそのまま。

### 受け入れの条件（N4、ユーザーの判断）

- **A: main loop と入力が止まらない**: compositor の main loop が他の session の GPU の仕事で塞がらない。compositor の vkQueueSubmit・vkQueueWaitIdle（import・shm の時、`import.c:352`・`shm.c:391,881`）・WSI の present は、compositor 自身の session の仕事の完了だけを待ち、他の session の pass の後ろに並ばない。測り方: compositor の log の queue-submit・input→draw が、重い client（Settings の描き直し、App Home を開いた直後）の間も数十 ms 以下。
- **B: cursor が動く**: cursor の plane の copy と位置の present が、他の session の pass の後ろに並ばない（(B) の GPU 無しの位置の present と、compositor の session の優先、下）。測り方: `KWL CURSOR_PLANE present` の間隔が入力の間隔に従い、App Home・drag の間も 100ms を超えて空かない。

### session ごとの列と pass の単位の round-robin

- executor は session ごとの execution の列を持つ。各 execution は今の native job の列（pass と CPU の段）で、executor は **1 歩＝1 つの pass（または CPU だけの段 1 つ）** を実行する step 関数で進める（今の `bcm2711_vulkan_native_job_execute` の loop を、event の位置を execution に保存して 1 歩ずつ進める形に分ける）。
- 選び方: 準備のできた session（列の先頭の execution が wait の依存を満たしている）を round-robin で回し、1 歩ずつ実行する。**表示の session の優先**: display の import を持つ buffer（scanout の native image、cursor の image）に書く execution を出した session を「表示の session」として印を付け（kernel が share の相手が display の buffer への書き込みで判定、compositor を名指さない）、他の session の 1 歩の後に毎回、表示の session に準備のできた歩があれば先にそれを実行する。これで compositor の frame・present の copy・cursor の copy は、他の session の pass 1 つ分より長く待たない。
- 同じ session の中の順序は今と同じ（列の順、pass の順）。layout の検査と公開は execution の歩の時だけ（session の中で順序どおり）。

### session をまたぐ順序（cross-session）

- **session をまたぐ順序は marker の完了（C-1）だけに依る**（review-4 の H-1）: Wayland の WSI は early commit（acquire の fence 付き、`wsi-wayland.c:711-721,826-827`）で、compositor は client の acquire の fence を CPU で poll（`wayland/display.c:49-97`）してから buffer を使う。fence は client の session の marker で、client の execution の完了まで完了しない。semaphore・fence は session の中だけで、session をまたぐ GPU の依存の機構は作らない。
- **buffer の暗黙の依存**: Vulkan の外（同じ memory を 2 つの session が GPU で書いて読む、明示の sync 無し）は順序を保証しない（Vulkan の規則どおり）。kernel の安全（memory・PTE）は hold で守る。

### 上限と背圧（N1・N2）

- **背圧は EAGAIN**: session ごとの execution の上限を `job_reserve`・`job_capacity` の判定に入れ、上限なら RESERVE が EAGAIN（libvulkan は `vulkan_sync_capacity_wait` で待って再試行、`queue.c:345-366`、`sync.c:636-645`）。libvulkan は 1 transaction に 1 QueueSubmit と 1 marker なので、marker の session の上限 ≤ execution の session の上限なら QueueSubmit が上限で OOM になることは無い。kernel の OUT_OF_HOST_MEMORY は細工の stream（1 request に多数の QueueSubmit）への防御だけに残す。
- 値: session ごとの execution 12・bytes 48MiB、全体の execution 64・bytes 192MiB（compositor の同時の execution＝frame 1・WSI の present 1・cursor の copy と present 最大 8 を下回らない。M の `markers_max` の実測で決め直す）。上限は execution の完了で `drv_gpu_capacity_changed` と retired の wake で解く（H5 の (3) の後に）。
- **slot**（N2）: PARKED の marker を worker の slot から外し、execution の heap の entry に持たせる（marker の completion と session の pending の数を移す）。slot は decoder の command（FIFO）だけに使い、16 のまま足りる。`reservation_capacity` は session を受け取り、session ごとの上限で判定する。

### execution の状態と取り消し（N8）

| 状態 | 意味 | stop（session） | fault（全体） | close | recovery |
| --- | --- | --- | --- | --- | --- |
| QUEUED | 列にある、未実行 | 取り消し: 実行せず dispose、その execution の sync・fence を DEVICE_LOST、`release_submission`。`bcm2711_render_worker_fault` は呼ばない（他の session に影響しない） | 取り消し（fault の印、dispose は executor） | drain が待つ（stop の取り消しで終わる） | 列が空であること |
| RUNNING | 歩の途中（窓の外か中） | 今の歩を終えてから取り消し（窓の中は待つ） | 今どおり fault の流れ | 同上 | — |
| CANCELED・DONE | 完了 | — | — | — | — |

- 取り消された execution も `executed` を進める。その session の PARKED の marker は required ≤ executed まで待ち、ECANCELED で完了する（「marker の完了 ⇒ 前の仕事は退役済み」を保つ。外に出した fence の fd を見る他の process が書き途中の buffer を読まない）。
- fault の時は executor の眠り（pte_waiters の waitq）と窓の待ち手の両方を起こす。

### 遅延解放と fault（N5）・close と destroy（N6）

- 窓の間に参照 0 になった view は遅延解放の list へ（第 3 版のとおり）。fault の経路（`bcm2711_render_worker_fault` と同じ guard の下）で list の view を quarantined=true に移し、`render_reset` の hardware の reset の後の `bcm2711_v3d_memory_recover` で回収する。
- 解放の側（vkFreeMemory → `render_destroy`、compositor の import の解放、close）は窓を待たずに list へ入れる（第 3 版の「close・destroy は窓の終わりを待つ」は誤りで削る）。窓を待つのは新しい PTE を作る map の ioctl（blob の作成・import の `memory_map`）だけ。import の map が compositor の main thread を窓の長さ止める点は、GPU の VA の map を executor の「窓の前」まで遅らせる lazy map で避けられる見込み（V3D の MMU が無効の PTE を TLB に持つかを確かめてから、A2 の後の段で検討）。

### 公平と飢え（N7）

- kernel の mutex は handoff しない（`src/kern/lock.c`）。executor は 1 歩ごとに mutex を外し、decoder と ioctl の待ち手（mutex の待ちの数）がいれば一度眠って順番を渡す（待ち手が mutex を取った sequence を見て起きる）。
- pte_waiters は「executor が待ち始める前から居た待ち手」だけを優先する世代番号を持ち、1 回の窓の開始を最大 1 回だけ譲る（map の ioctl が続いても executor が窓を開けない状態にしない）。

### 窓を開く者（L-c）

- 不変条件: 窓を開くのは controller の mutex を持つ executor だけ（C1 の遅延解放の安全はこれに依る）。

### wait_idle（L-a）

- `GPU_OP_QUEUE_WAIT_IDLE`・`DEVICE_WAIT_IDLE` は EOPNOTSUPP（libvulkan は使わない、host 試験を直す）。

### A0（userland の点検、N3・N4・N9）

- cursor-plane.c（N3）: 最後に submit した copy の fence を **acquire の前に** `vkGetFenceStatus` で見て、NOT_READY なら acquire しない（acquire 済みの image を present せずに残さない）。queue は session の中で 1 本の順なので signaled なら前の copy も完了。再試行は main loop の poll の上限 10ms（`main.c:856`）。`kwl_cursor_plane_close` の `vkDeviceWaitIdle`（`cursor-plane.c:117`）は close の時だけで残す。
- compositor の main thread の `vkQueueWaitIdle`（import・shm）: session ごとの列の後は compositor 自身の列だけを待つので A の妨げにならない。残す（点検の記録）。
- EBUSY の候補（N9）: `compose.c:1958-1969` の spare_sets を `kwl_compose_set_get` の後に `vkUpdateDescriptorSets`、`backdrop.c:174,456` が frame の in_flight の間に起きないか。非同期の後は EBUSY→DEVICE_LOST になり得るので source で確かめ、起きるなら直す。

## review-4 への加筆（2026-10-12、A1 の前、Q1 が差分を確かめる）

- **C-1 executed・required は session ごと**: 全体の 1 本の番号（第 3 版の「順序と marker」・第 4 版の表の「executed」）を、session ごとの `submitted`（その session の列に入れた execution の数）と `executed`（その session で完了した数）に置き換える。marker は decoder が受けた時に `required = session->submitted` を記録し、`session->executed < required` なら PARKED、executor はその session の execution を終えるたびに `session->executed++` の後に、その session の PARKED だけを集めて完了させる。session に未完了の execution が無ければ marker は decoder がその場で完了させる。drain・stop_poll・recovered も session の数で判定する。他の session が先に進んでも、自分の marker は自分の execution の完了まで完了しない（試験 (q) に足す）。
- **H-1 session をまたぐ wait の機構は作らない**: semaphore・fence は session の中だけ（`vulkan-sync.c:131`、import は ENOTSUP `:139-141`）、libvulkan が出し入れするのは OPAQUE_FD の fence だけで CPU で待たれる（compositor の poll `wayland/display.c:49-97`、`GPU_DISPLAY_PRESENT_SYNC`）。Wayland の WSI は factory の revision 2 以上で early commit（acquire の fence 付き、`wsi-wayland.c:711-721,826-827`）。よって **session をまたぐ順序は marker の完了（C-1）だけに依る** を不変条件とし、第 4 版の「session をまたぐ wait の semaphore・外に出した fence の fd」の段落・依存の記録・列の飛ばし・循環の期限・A1 の「session をまたぐ wait」は削る。試験 (r) は「early commit の client と compositor の poll で、compositor は client の marker の完了の前に buffer を使わない」に差し替える。
- **H-2 背圧の判定**: RESERVE（`job_reserve`・`job_capacity`、session を受け取る）で (a) 件数と bytes の両方（保留中の bytes＋8MiB ≤ session の上限）、(b) 全体の件数と bytes、ただし session ごとに最低の枠（2 件・16MiB）は全体の上限に関わらず保証、(c) session の数に上限（render の open の数、超えたら open を EBUSY）、(d) 件数・bytes の減算は H5 の (3)（guard の下、`finish_request` より前）で行い、marker の完了の直後の RESERVE と decoder の受け入れが食い違わないようにする。全体の bytes の上限は連続の物理 page（`hal_pmem_alloc`）の取り合いを避けて M の実測でもっと小さくする（今の候補 64MiB）。
- **Q1 の判断（H-3）**: lazy map（GPU の VA の map を executor の「窓の前」まで遅らせ、compositor の import・blob の作成の main thread が窓を待たない）を **段 A3** として表に入れ、受け入れ A の前提にする（受け入れ A は狭めない）。A3 の最初に V3D の MMU が無効な PTE を TLB に持つかを確かめる。
- **Q1 の判断（M-2）**: 表示の優先の印は execution ごと（sticky にしない）。display に import された buffer への書き込みがある execution だけに付ける。buffer に display の import の数（buffer の guard の下）を持ち、decode の時に書き込み先（attachment・copy/blit/clear の先）を列挙して判定する。game mode の client が scanout 中に優先を得るのは許す（その時はそれが画面）。

## A0 の結果（2026-10-12 P3）

- cursor-plane.c: 最後に submit した copy の fence を acquire の前に `vkGetFenceStatus` で見て、VK_NOT_READY なら今回は acquire しない（acquire 済みの image を present せずに残さない）。再試行: `kwl_schedule`（`display.c`）は main loop の毎回（poll の上限 10ms）に `kwl_cursor_plane_update` を呼び、それは「見せたい像と位置」と「present 済みの像と位置」を比べて違えば `plane_present` を呼ぶ。早く return した時は present 済みの状態を変えないので、pointer が止まっていても次の pass で再試行され、最後の形・位置が出ないまま残らない。copy は 1 本の queue で順に完了するので、後の image ごとの待ちは塞がらない。plane の struct は open・close で 0 に戻る（`last_copy`・`copied` も）。arm64 の compositor の build は warning 0。
- EBUSY の候補: `kwl_compose_set_put` の呼び出し元（import の解放 `import.c:394,398`、shm の `image_release` `shm.c:940,942`、backdrop の破棄 `backdrop.c:174`）は「frame が完了まで buffer を持つ」（`import.c:107` の注記）・「device が idle」（`backdrop.c:152`）の前提で、in_flight の frame が使う set を書き直さない。非同期の後も成り立つ（A1 の試験で確かめる）。
- libvulkan の WSI: early commit（review-4 の確認）。compositor は client の acquire の fence を poll する。session をまたぐ順序は marker の完了で保たれる。
- M-5 の libglesv2・libegl・libmedia の点検: review-4 も具体の誤りは未発見。A1 の後の T1 の回帰で見る。

## A1 の進捗（2026-10-12 P3）

- **A1a**（46ed1959e）: `bcm2711_vulkan_native_job_step`（1 歩＝CPU の段から次の pass まで、job に cursor・region・remaining・tick の和を保存）。`job_execute` は step の loop。
- **A1b**: executor の thread（`executor_main`、controller に 1 つ、最初の open で decoder の前に起動）、session ごとの execution の列（`execution_head/tail`、`submitted`・`executed`・`execution_bytes`）、ready の session の round-robin（1 歩ごとに列の後ろへ）、`bcm2711_render_execution_submit`・`bcm2711_render_executor_step`。QueueSubmit は decode・prepare の後に heap の execution（actions・syncs を写す）を作り、予約を公開して列に入れ、VK_SUCCESS を返す。execution の step は wait・signal を順に進め command の job を 1 歩、end は成功なら completed・signaled を公開、失敗・取り消しなら DEVICE_LOST（その execution の分だけ）、`release_owner` の後に free。step の失敗と release の失敗は executor が `worker_fault`（mutex の下）と `render_fail`（mutex の外）にする。stop（ECANCELED）・fault（EIO）の取り消しは実行せずに end し、stop は global fault にしない。semaphore・fence の `pending` は数、`prepare_sync` は予約の数どうしで判定（pending が 0 の時だけ completed・signaled との一致も）。旧来の GPU_COMMAND の QueueSubmit は EOPNOTSUPP（`bcm2711_vulkan_session.synchronous`）、QUEUE_WAIT_IDLE・DEVICE_WAIT_IDLE は EOPNOTSUPP。executor は 1 歩ごとに `sched_yield`（mutex は handoff しない）。
- **設計からの差（A1b）**: PARKED は domain（timeline≠0）を持つ request の完了全部に広げた（job の marker と、選ばれた context fence の command）。decoder が実行と dispose を終えた後、session に未完了の execution があれば PARKED にして自分の結果を残し、executor が `required ≤ executed` で `stop・fault・自分の結果` を付けて完了させる。timeline 0 の command（libvulkan の decoder の通知）は待たない。**PARKED はまだ worker の slot に居る**（slot から外して execution の entry に持たせるのは A1c の背圧と一緒に行う）。
- host 試験（vulkan-device）: 既存の場面を executor の後で確かめる形に（`execute()` は executor を空になるまで回す、`execute_queued()` は回さない、drain の fixture は decoder が空なら executor を 1 歩）。足した場面: (j) 予約だけの signal を後の submission が wait する鎖と、予約の signal が残る間の 2 度目の signal の拒否、(k) 旧来の経路の EOPNOTSUPP、PARKED の marker が executor の後に完了、(h)・(s) の一部（stop で列の execution が取り消され fence は DEVICE_LOST、PARKED は ECANCELED、global fault にならない）、uncertain の job は accept の後に fault。v3d-hardware の host 試験に stub（`sched_yield`、以前からの `bcm2711_buffer_release_stats` の欠け）を足した。
- **A1c**: producer の marker は decoder の command の slot（16）と別の pool（`markers`、96）に置く（PARKED の marker も slot を占めない。設計の「execution の entry に持たせる」の代わりに固定の pool で、確保の失敗が無い）。RESERVE・CAPACITY は `marker_capacity`: 最低の枠（2 件、未完了の execution 2 件未満、bytes＋8MiB ≤ 16MiB）は全体の上限に関わらず通す。それを超える分は session の上限（marker・未完了の execution 12、bytes＋8MiB ≤ 48MiB）、全体の上限（execution 64、bytes 192MiB）、他の open の session の最低の枠の残り（`2×sessions − covered`）を空けた残りの範囲で通す。満たさなければ EAGAIN（libvulkan は capacity の待ちで再試行）。session の数は 32 まで（`render_open` は 33 個目を EBUSY、pool 96 ≥ 2×32）。数（`worker.sessions`・`covered`・`executions`・`execution_bytes`、session の `markers`）は guard の下で更新し、execution の数の減算は PARKED の完了の前（H-2 (d)）。marker の印の無い QueueSubmit は session の未完了 24 件か bytes 96MiB で VK_ERROR_OUT_OF_HOST_MEMORY（細工の stream への防御だけ）。close の後に `drv_gpu_capacity_changed`（mutex の外）。timeline の再利用の検査は slot と pool の両方（`bcm2711_render_worker_timeline_busy`）。
- host 試験: (c) 未完了の execution に従って capacity が 12 から減り 0 で RESERVE が EAGAIN、executor の後に戻る。(i) marker 無しの QueueSubmit の 25 件目が OOM。(d) 32 個の session が順に取れるだけ取っても、どの session も最低 2 個を得て pool の 96 個がちょうど埋まる、33 個目の open は EBUSY（v3d-hardware の host 試験）。session の上限の 12 個で EAGAIN、command の slot は使わない。
- **A1d**: run（`bcm2711_buffer`）に display の resource の数 `displayed`（buffer の guard の下）を持つ。display の resource が run を持つ 3 か所（作成・blob・import）で増やし、`resource_destroy` で減らす。QueueSubmit の execution は、その command の prepared の event と pass の record が名指す object（image・buffer・image view・framebuffer）の run に `displayed` が 1 以上のものがあれば印 `display` を持つ（M-2 の「書き込み先」の代わりに、record の object を読み書きの区別なしに見る。client が display の run を読むのは scanout の時だけで、その時の優先は M-2 で許す範囲）。executor の選び方（`select_session`）: 前の歩が display でなければ、最初の display の execution を持つ session を選ぶ。前の歩が display なら、display でない最初の session（無ければ先頭）を選ぶ。これで display は他の 1 歩より長く待たず、他の session も飢えない。歩の後は選んだ session を列のどこからでも外して、残りがあれば後ろへ付ける。
- host 試験: (q) 3 つの session（普通 2、display 1）の 10 歩で、session の中の順序、display が先、display の待ちは他の 1 歩まで。(g) 失敗の歩の後に global fault と `render_fail`、PARKED の marker は EIO。display の host 試験（4 つ）は PASS。
- A1 の範囲はここまで。A2 の窓（(a)(b)(l)(m)(p)）と A3 の lazy map は後。(e) は A0 の compositor、(r) は実機。

## 段（第 4 版）

| 段 | 内容 | 確かめ |
| --- | --- | --- |
| M | 計測（済み・続き）: request の待ちと実行、解放の内訳、markers_max、submit の mutex の待ち | 実機の数 |
| A0 | userland: cursor-plane.c の acquire の前の fence の確認、EBUSY の候補の確認、libvulkan の WSI の commit の同期・early の確認と記録 | host build、T1 |
| A1 | executor・session ごとの列・1 歩の step・表示の session の優先（execution ごとの印）・round-robin・execution（heap）・session ごとの submitted・executed・required・背圧（RESERVE の EAGAIN、件数・bytes・全体・最低の枠・session の数）・PARKED を execution の entry に・状態と取り消し・公開の順序・旧来の拒否・wait_idle の EOPNOTSUPP。executor は歩の間 mutex を持つ（効果は A2 から） | host（決定的な fixture） |
| A2 | 窓（3 区分、window の判定、遅延解放の list と fault の quarantine、map の待ち、pte_waiters の世代、mutex の順番渡し、display の generation の guard） | host |
| A3 | lazy map（GPU の VA の map を executor の窓の前へ、import・blob の作成が窓を待たない）。最初に V3D の MMU が無効な PTE を TLB に持つかを確かめる | host、実機（受け入れ A） |
| B | cursor の plane（別の版の設計の review の後） | 実機（受け入れ B） |

## 試験（M5）

- vulkan-device の host 試験に、executor の 1 歩（`executor_step`）と decoder の 1 歩（`worker_step`）を外から順に呼べる**決定的な fixture** を作る（本物の thread では interleave が再現しない）。場面: (a) 窓の間の decoder の vkFreeMemory・vkDestroyImage と別 session の close（遅延解放、EIO にならない）(b) TFU と CL の隙間で PTE が変わらない (c) RESERVE の EAGAIN の背圧（件数・bytes・全体・最低の枠）、OOM は細工の stream だけ (d) marker の枠が他の session で埋まっても compositor の submit が戻る (e) cursor plane の fence が NOT_READY で plane が閉じない（compositor の host 試験）(f) marker の完了の直後の vkResetFences・vkResetCommandBuffer・vkUpdateDescriptorSets が成功 (g) fault で PARKED が EIO で終わり render_fail が届く (h) checked stop と保留の execution (i) 細工した stream（1 request に多数の QueueSubmit、同じ session の複数 request）(j) N の signal を N+1 が wait する semaphore の鎖 (k) 旧来の経路の QueueSubmit が EOPNOTSUPP。
- 実機: compositor の queue-submit が GPU の時間から外れる、App Home・drag の間の cursor と入力、窓の間の解放（`views` の遅延解放の数を perf 行に）。i915・Venus は T1（A0 の userland の変更の回帰）。
- 第 4 版で足す場面（N9 ほか）: (l) 窓の間の map の ioctl（GPU_BLOB_CREATE・RESOURCE_IMPORT）が待って成功し EIO が userland に届かない (m) 窓の間の解放＋fault→reset の成功 (n) compositor が上限に当たっても RESERVE の EAGAIN で待ち、終わらず plane も閉じない (o) cursor の copy の fence が NOT_READY のまま image の数を超えて続いた後に回復 (p) map の ioctl が続いても executor が進む・executor が続いても decoder が進む (q) 2 つの session の列の round-robin と表示の session の優先 (r) early commit の client と compositor の poll で、compositor は client の marker の完了の前に buffer を使わない(s) stop の取り消しが他の session に影響しない。

## 未解決

- GPU の実行中に無関係の VA の PTE を編集して TLB を flush する安全（review-2 の未確認）: 第 3 版は窓の間に PTE を編集しないので不要。
- 準備の区間（CPU の変換、per-draw の upload）は executor の mutex の下に残る。per-draw の upload の削減は ws141-p013 で先に行う（2026-10-12 Q1）。
