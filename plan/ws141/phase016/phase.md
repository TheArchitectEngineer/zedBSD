<!-- awesome-plan project=zedbsd record=ws141-p016 -->

# ws141-p016: GPU の実行中に ioctl と cursor を止めない（worker の mutex、cursor の plane の GPU 無しの更新）

Parent: [WS141](../ws.md)
Status: planning（設計の第 4 版。[review-1](review-1.md)〜[review-3](review-3.md) と 2026-10-12 のユーザーの判断（列を app ごとに）を反映、review-4 の前）。(B) は第 6 版（[review-B5](review-B5.md) を反映、B-u を P2 が実装中）
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
- **B: cursor が動く**: cursor の plane の copy と位置の present が、他の session の pass の後ろに並ばない（(B) の GPU 無しの位置の present と、compositor の session の優先、下）。測り方と閾値は「(B) 第 5 版」の 8 で置き換えた（閾値 16.7ms、場面は位置だけ・像の変化・animated、p016 は呼び出し→ioctl、端から端は WS213。第 4 版までの「100ms を超えて空かない」はやめた）。

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

## A2 の進捗（2026-10-12 P3、Q1 の「A2 の実装に入ってよい」）

- **3 区分**: `bcm2711_vulkan_native_pass_run` を `pass_start`（検証・CPU の clear、mutex あり）、`pass_launch`（TFU の変換と CL の job、窓の中・mutex なし、pass と結果だけを書く）、`pass_finish`（snapshot の filled・`memory_written`・synced・layout、mutex あり）に分けた。`pass_run` は 3 つを順に呼ぶ（mutex を外さない、host 試験の直接の呼び出し用）。job の step は start → `bcm2711_render_window_open`（mutex を外す）→ launch → `bcm2711_render_window_close`（mutex を取り直し、遅延解放）→ finish。TFU の snapshot の filled は変換の直後でなく finish でまとめて行う（変換の途中の失敗では filled にしない）。使われなくなった `bcm2711_vulkan_native_job_execute` は削った（host 試験は同じ形の helper を持つ）。
- **窓の判定**: `hardware.window`（guard の下）。`memory_ready` は `!job_busy && !window`。窓を開くのは mutex を持つ executor だけ（L-c）。
- **遅延解放**: 窓の間に参照が 0 になった view は unmap せず `delayed` にする（quarantine にしない、PTE は変えない）。窓を閉じた executor が `bcm2711_v3d_memory_release_delayed` で 1 回の batch（unmap → 1 回の flush → 解放）にする。fault の後は `memory_ready` が偽なので何もせず、`bcm2711_v3d_memory_recover` が delayed を quarantined と同じに扱って回収する（N5、fault の経路で list を触らない形にした）。
- **map の待ち**: map する ioctl（`allocate_resource`・`render_import`）は `bcm2711_render_map_lock`/`unlock` で mutex を取る。待ち手として数えられ（`pte_waiters`）、窓が閉じるのを待ち、mutex を取った後に窓が開いていれば外して待ち直す。解放の側（vkFreeMemory・destroy・close）は待たない（N6）。
- **pte_waiters の世代**: `window_open` は待ち手がいれば、その時の待ち手の数だけ完了する（`pte_done` が目標に達する）まで mutex を外して待ち、その後は新しい待ち手がいても窓を開く（1 回だけ譲る、N7）。fault で待ちは終わる。`worker_fault` は窓の待ち手も起こす。
- **mutex の順番**: mutex は handoff しないので、executor は歩の間に `sched_yield` し、窓の間（GPU の実行の間）は mutex を持たない。decoder・ioctl はその間に進む。
- **display の generation の guard（M4）**: `bcm2711_v3d_memory_written`・`memory_changes` は run の guard の下で generation と ring を読み書きし、display の `resource_write` の generation の加算も同じ guard の下にした。
- host 試験: (a)(l)(p) vulkan-device: executor が QueueSubmit の pass を実行する間に、模した decoder が mutex を取れる（窓の間）。その中の解放は delayed、map は EIO（ioctl は map_lock で待つ）、窓を閉じた後に view は消える。窓の前の待ち手 1 つは先に通り、後から来た待ち手は窓を止めない。(b) TFU も窓の中（CL と同じく窓と mutex なしを確かめる、fixture の assert）。(a)(b)(l)(m) v3d-hardware: 窓の間の解放で PTE が変わらず flush も無い、map と batch の開始は EIO、閉じた後に 1 回で解放、fault の後は残り reset と recover で回収。display の host 試験は PASS。

## 計測と並行の直し（2026-10-12 P3、A2 の後）

- 計測の準備: Pi の image a4b833e（A1・A2 入り）。kernel.log の perf（操作中の窓）: submission あたり xfer（CPU の buffer copy）10〜22ms が最大、prep 3〜7ms、exec 4〜15ms、max_us は毎窓 220〜360ms、submit ioctl の mutex 待ち submit_lock_us 平均 1〜3ms・最大 120〜216ms、markers_max 4〜8。compositor の `--log-frames` は session の書き換えが要り、権限の判定で止まった（2026-10-12 にユーザーが Pi への書き込みを許可、再開後に行う）。/tmp/wayland（main 349f5650d、cksum 3455874609/968248）と /tmp/session.new は Pi の /tmp に置いてある。
- Vulkan の予算（Music が他の app と同時に起動しない件、Q1 の指示）: b9d011eef（main 20c7ef161）。予算と heap の大きさを 1 つの定数 `BCM2711_VULKAN_MEMORY_BUDGET`（512MiB）に、計上を run（bcm2711_buffer）ごとに 1 回（import の二重計上をやめる）、perf に `v3d: memory kib= max_kib= budget_kib= refused= last_kib=`。実機は未確認。
- TFU の有効化（Q1 了承の形）: boot の V9 の比較を 5 つ（96x40 pitch 512、33x9 pitch 132、1x1、200x150 pitch 1024 で入力を 64 byte ずらす、256x256 pitch 1024）にし、見える texel の byte（全部 1 の raster の CPU 変換で padding を除く）の一致と、出力の後ろ 64 byte を書かないことを確かめ、全部通れば `tfu_uif_ready = true`。perf に `v3d: tfu jobs= avg_us= max_us=`。
- **TFU を止める方法**: boot の行（Pi の FAT の cmdline.txt）に `v3d.tfu=0` を足す（build し直し不要。V9 は走り「v3d.tfu=0 keeps conversion on the CPU」と出る）。`v3d.stop=V9` は使わない（V9 以降を止め、render device が登録されず GPU が無くなる）。
- 次: snapshot_fill の部分変換（write ring の box、UIF block 単位）→ (B) の設計（review へ）→ HAL arm64 の allocator（呼び出し元の監査が先）。
- **snapshot の部分変換**（2026-10-12 P3 第 2 世代）: 有効な snapshot の source の generation が進み、write ring（`bcm2711_v3d_memory_changes`）が snapshot の generation 以降の全部の write の box を名指せる時は、CPU がその box だけを utile（4×4、64 byte＝cache line 1 本）単位に広げて書き直す（`bcm2711_native_texture_copy_box`）。utile 単位なので CPU が書く cache line は全部 CPU の内容で、TFU が書いた後の古い line を書き戻さない。clean は触れた 32 pixel の column ごとの block 行の範囲だけ（`bcm2711_native_storage_clean_range`）。TFU が使える時は広げた box が 256KiB 以下の時だけ CPU、それより大きいか ring が名指せない（unknown・別の offset・8 個より前・display の記録の無い書き込み）時は従来どおり全体（TFU または CPU）。box が image の外だけなら generation だけ進めて reuse。generation は ring より先に読む（後の write は box を広げるだけ）。失敗は snapshot を invalid にして次に全体を変換。`snapshot_get` は raster を常に受け、`unit` で全体の変換を TFU に任せるかを選ぶ。perf の行に `upd=`（部分変換の数）。GPU の L2T は render の前に invalidate 済み（`bcm2711_v3d_hardware_invalidate`）。host 試験: native-state（4 つの box、column の境・右と下の padding・整列・全体を全 padded word で旧/新と照合、拒否 5 種）、vulkan-device（TFU ready で 1 pixel の write は utile だけ CPU、unknown は TFU、image の外は reuse）、v3d-hardware PASS。kernel の build は warning 0。実機は未確認（client が render area を絞るかに依る）。

## (B) 第 3 版の設計（2026-10-12 P3 第 2 世代、review の前、実装しない）

**目的**: 位置だけの cursor の present（compositor の `plane_present` が copy を記録しない回、`copy=0`）が GPU の列（executor の順番と WSI の worker の fence の待ち）を通らず、呼び出しの thread で `CURSOR_SET`（kernel は写さない）＋`MOVE` になること。像が変わった回（`copy=1`）は今どおり GPU を通る（Q1: 像が変わった時だけ kernel が 16KB を写すのは可）。

**第 2 版の F8（swapchain の image を display に import）をやめる**: import の代わりに、libvulkan が「cursor の swapchain の image の中身が、前に native の image へ写した時から変わったか」を自分で知る（下の 1）。swapchain の image の作り方・export・placement・oldSwapchain の共有は変えない。

1. **libvulkan: swapchain の image への書き込みの記録**（cursor の chain のためだけに使うが、記録は全部の command buffer で行う）
   - 生成の command（`commands-generated.inc`、generator の表 `api-commands.tsv`）のうち `dstImage` を取るもの（CopyImage・BlitImage・ClearColorImage・ClearDepthStencilImage・CopyBufferToImage・ResolveImage と各 `2` 版）で、`dstImage` が `swapchain_owned` の image なら command buffer の小さな表（8 個、重複なし）に記す。表が溢れたら「どれか分からない」の印。`vkCmdExecuteCommands` は secondary の表を primary に足す（溢れは印）。`vkBeginCommandBuffer`・reset で表を空に。
   - `vkQueueSubmit`・`vkQueueSubmit2` の受理の時に、記された image の `content_serial` を 1 進める。「分からない」の印があれば、その device の全部の swapchain の image を進める。
   - **対象の限定**: fast path を使う chain は usage が TRANSFER_DST・TRANSFER_SRC だけのもの（compositor の cursor の chain は TRANSFER_DST だけ、`cursor-plane.c:84`）。COLOR_ATTACHMENT・STORAGE・INPUT_ATTACHMENT を持つ chain は render pass や descriptor で書かれ得るので fast path を使わない（記録の漏れで古い像が残る危険を、使い道の限定で消す）。
2. **libvulkan: present の fast path**（`wsi-swapchain.c`、cursor の plane の chain だけ）
   - 条件（全部）: (a) chain が cursor の plane で上の usage、(b) image の `content_serial` が、その image の native の image へ最後に写した時の値（`copied_serial`）と同じ、(c) wait の semaphore が全部、software の payload で既に signal 済みの binary semaphore（acquire のもの）、(d) その chain（同じ lease）の present の job が worker に 1 つも無い（F6: 先の present を追い越さない）、(e) present の fence・present id など GPU の完了を要る拡張の struct が無い。
   - 動作: device の mutex の下で wait の semaphore の payload を消費し（`queue.c` の QueueSubmit の消費と同じ所）、`platform->position`（dstRect）と native の present（`display_present_cursor`: SET＋MOVE）を呼び、image を acquire に返し、sequence・frame を進める。worker も GPU の job も使わない。
   - 条件を満たさない present は今の経路（GPU の copy → worker が fence を待つ → SET＋MOVE）。その copy の submit の時に `content_serial` を控え、worker の present の後に `copied_serial` にする。
3. **kernel: CURSOR_SET の写しの省略**（`display-device.c` `output_cursor_set`、F7）
   - 4 枚の cursor の image（`cursors[]`）ごとに、写した元の鍵を持つ: `buffer->identity`（run ごとに一意）・offset・幅・高さ・stride・format・`generation`（run の guard の下で読む）。
   - SET の元が host_mapped でなく、鍵が一致する image があれば、写さずにその image を見せる（list が名指していても、中身は変えないので可）。無ければ今どおり名指されていない image に写して鍵を記す。`cursor_reset`（lease の解放・新しい lease・close）で鍵を全部消す。display の CPU の書き込み（`resource_write`）は generation を進めるので写し直しになる。
   - （第 5 版で訂正、review-B3 M3・review-B4 軽微）kernel の cursor の image は 4 枚で、鍵が一致すれば写さないが、鍵は 4 枚の取り合い（oldSwapchain の旧と新の image など）と `cursor_reset` で消えるので、「各 image は最初の 1 回だけ写す」は保証されない。写しの省略は最適化で、写し直しても見える結果は同じ。
4. **他の platform**: cursor の plane は `GPU_DISPLAY_HAS_CURSOR` の bcm2711 だけ。i915・Venus・Linux・FreeBSD では fast path の条件 (a) が成り立たず、変わるのは command buffer の小さな表と submit の時の serial の加算だけ（費用は command あたり数命令）。UAPI は変えない。HAL は触れない。
5. **確かめ**: host: libvulkan の WSI の fixture（cursor の chain で位置だけの present が QueueSubmit も worker も通らず SET・MOVE の ioctl だけ、copy の後の present は worker、未完了の present がある間は fast path を使わない、`dstImage` の記録・ExecuteCommands・溢れ）、display の host 試験（同じ鍵の SET は写さない、generation・host_mapped・cursor_reset で写す）。実機: App Home・drag・Music の起動の間の `KWL CURSOR_PLANE present` の input→at の差（今は通常数 ms、最大 195ms）。
6. **未決**: (i) generator の変更（`dstImage` の hook）の形（表の列か、生成の規則か）、(ii) ExecuteCommands の secondary の表を primary に足す時の溢れの扱い、(iii) present の fence（VK_EXT_swapchain_maintenance1）を compositor は使っていないが、来たら通常の経路へ。

## (B) 第 4 版（2026-10-12 P3、[review-B3](review-B3.md) の C2・H1・H2・M1〜M5 を反映、再 review の前、libvulkan は実装しない）

第 3 版の 1〜5 は、この節と食い違う所をこの節で置き換える。kernel の (3)（CURSOR_SET の鍵）は M3 を入れて ef5e651bc で実装済み（段 B-k）。

**段の分け（M5）**: B-k（kernel の鍵、済み）、B-u（libvulkan の fast path、この節）。受け入れ B は B-u に加えて (b)（decoder の request が executor の準備の mutex を待たない）、A3（lazy map）、compositor の直し（10ae7a26e: cursor の copy の fence の問い合わせを SUCCESS の後にやめる、import・shm の vkQueueWaitIdle の扱いは (b) か A3）を前提にする（C1）。

1. **追い越しは ticket で（C2）**: 第 3 版の条件 (d)（chain の present の job が worker に無い）をやめる。cursor の lease は `next_ticket` と `applied_ticket` を持つ。cursor の chain の present は、受け付け（`vkQueuePresentKHR` の中、fast path でも通常の経路でも）の時に lease の `next_ticket` から番号を取る。display の層の 1 つの op `present_cursor_at(lease, native_image, x, y, ticket)` は display_mutex の 1 区間で、`ticket <= applied_ticket` なら何もしない（古い present を捨てる）、そうでなければ `CURSOR_SET`＋`CURSOR_MOVE` を出して `applied_ticket = ticket`。worker の通常の経路も同じ op を使う。これで重い primary の job の後ろに cursor の job が残っていても、新しい present は fast path で即時に出て、後から来た古い job の SET・MOVE は捨てられる。
   - **追い越しが安全な理由（不変条件）**: (i) native の image（chain の `presentation[]`）に書くのは WSI の copy の job だけで、その job の image は present の終わり（`present_finish`）まで acquire に戻らない。だから fast path が使う image i と、未完了の job の image j は違い、fast path は j の native の image を読まない。(ii) Vulkan の present は後の present が前の present を置き換える。古い job の SET を捨てても、より新しい present の像が出ている。(iii) 古い job の copy は捨てずに実行し、完了後にその image の `copied_serial` を更新する（次の fast path に使える）。
2. **判断の位置と lock（H1）**: 判断は `present_prepare` の検証（image が ACQUIRED、cursor の rect、`results[0] == VK_SUCCESS`）の後、`present_record`（vkResetFences・fence の query・Begin/End を含む）の前。条件: swapchainCount == 1、cursor の lease の chain で usage が TRANSFER_DST/SRC だけ、wait の semaphore に重複が無く全部 software の payload で signal 済み、image の `content_serial` と `epoch`（下の 4）が `copied_serial`・`copied_epoch` と同じ、present fence など GPU の完了を要る拡張が無い。device->mutex の下で行うのは状態の確認・payload の消費・serial の比較・ticket の取得だけ。mutex を外してから `present_cursor_at`（ioctl）。後始末は通常の job と同じ `present_finish`（submitted の扱いで image を acquire に返し、sequence・frame を進める）。SET・MOVE の失敗は chain の error に latch（worker の失敗と同じ所）。
   - 条件のうち (b)(c)（中身が未変更・wait は software）が成り立たない時は通常の経路。(b) だけが成り立たない（像が変わった）時は GPU の copy を記録する。(b) が成り立ち (c) が成り立たない時（GPU の semaphore を待つ）は、通常の経路でも GPU の copy を記録しない（wait だけの空の submit、または fence の待ちだけ）。
3. **lease ごと（H2）**: 位置と像を 1 つの op で 1 回の display_mutex の区間に（第 3 版の `display_position` と `display_present_cursor` の別々の区間をやめる）。ticket・`applied_ticket` は lease に（lease は oldSwapchain の旧と新の chain で共有）。
   - L1（計測）: worker が primary の `PRESENT_SYNC` の間 display_mutex を持ち、kernel の `output_present` は adoption の待ちの間 controller の mutex を持つので、cursor の op は 1〜2 refresh 待ち得る。B-u の後の実機で present の input→ioctl の差を測り、1 refresh を超えるなら cursor の op を display_mutex から分ける（lease の cursor 専用の lock、kernel の CURSOR_SET・MOVE を output_present の adoption の待ちから外す）設計を次の版で。
4. **serial の置き場・初期値・更新（M1・M2）**:
   - `content_serial` は group の image（oldSwapchain で共有される VkImage）に、`copied_serial`・`copied_epoch` は chain の `presentation[]`（native の image は chain ごと）に。初期値は「未コピー」（`copied_valid = false`）。新しい chain は全部未コピー。
   - 加算は `queue_attempt` の受理（`queue.c` の device->mutex の下、vkQueueSubmit・vkQueueSubmit2（KHR は変換）・WSI の内部の submit・WaitIdle の空の submit が全部通る 1 か所）で、受理した command buffer の表の image の `content_serial` を 1 進める。表が溢れた（ExecuteCommands を含む）command buffer の受理は device の `epoch` を 1 進める。fast path は serial と epoch の両方を比べる。
   - 控え: copy の job の受理（同じ device->mutex の区間）で、その image の `content_serial` と device の `epoch` を job に控え、worker が fence の待ちに成功した後に `copied_serial`・`copied_epoch`・`copied_valid` を更新する。
   - hook: `api-commands.tsv` に「dstImage を書く」列を足し、generator が該当の command（CopyImage・BlitImage・ClearColorImage・ClearDepthStencilImage・CopyBufferToImage・ResolveImage。表に `2` 版は無い）の記録の時に `dstImage` が swapchain_owned なら表に記す。他の書き込みの経路（COLOR_ATTACHMENT の render pass、STORAGE は不可、BindImageMemorySwapchain・export は無い）は usage の限定で塞がる。
5. **受け入れ B の測り方（C1）**: compositor の `input_us`（入力を処理した時刻）では main loop の停止が見えない。evdev の event の timestamp（kernel の入力の時刻）と `present_cursor_at` の ioctl の時刻の差、および cursor の present の間隔（入力が続く間）で測る。compositor の log に evdev の timestamp を出す（`--log-frames` の `KWL CURSOR_PLANE present` に `event_us=`）。
6. **試験（M4）**: libvulkan の WSI の host fixture（偽の render・display の kernel）を作る。場面: 負荷の下（primary の job が fence で待つ間）で像の変化の後も fast path に戻り、古い job の SET が捨てられる（C2）、oldSwapchain で lease を共有する 2 つの chain の ticket（H2）、fast path が ioctl の間 device->mutex を持たない（H1）、`copied_valid` の初期値と新しい chain（M1）、表の溢れと epoch（M2）。kernel の鍵（M3）は display の host 試験で済み。見積もり: fixture が最大（偽の kernel の transport）。
7. **i915・Venus（L3）**: 変わるのは command buffer の表と受理の時の加算だけ（HAS_CURSOR は bcm2711 だけ）。

## (B) 第 5 版（2026-10-12 P2、[review-B4](review-B4.md) と Q1 の判断を反映、再 review の前、libvulkan は実装しない）

第 4 版の 1〜7 は、この節と食い違う所をこの節で置き換える。段 B-k（kernel の鍵）は ef5e651bc で済み。この版は段 B-u（libvulkan）の設計。行番号は main 1d9c9749e。

### review-B4 への対応

| 指摘 | 対応 | 節 |
| --- | --- | --- |
| F1 致命的（像の変化の後に cursor が止まる） | Q1 の判断で (a): cursor の lease の通常経路の job を、他の chain を待たない専用の worker（cursor の lane）に移す。ticket と併用。(b) thread C の copied の gate は WS213 の側（Q1 が P4 に伝える）。(c) 受け入れ B に像の変化と animated の場面と上限を入れ、「位置だけ」には狭めない（ユーザーの目標は「すべて 60fps」） | 1、8 |
| H-a 受け入れ B が測れない | libvulkan が cursor の present ごとに 1 行の log（環境変数で有効、経路・ticket・CLOCK_MONOTONIC の時刻）。閾値は 16.7ms に揃え、百分位と場面を書く。evdev の時刻の clock は source で確かめた | 7、8 |
| H-b device->mutex の待ちに上限が無い | 測定の項目に入れる（log の `mutex_ns`）。device->mutex を外す案（payload 専用の小さな lock、または atomic CAS）を書く。採るかは計測の後 | 4 |
| M-a ticket の lock と失敗 | 取る所は 1 か所（`present_reserve` の swapchain_mutex の区間、PRESENTING にした直後）、lease の atomic な fetch-add。SET の失敗・SET の成功の後の MOVE の失敗・EBUSY の時の `applied_ticket` を決めた | 2、3 |
| M-b `chain->frame`・`sequence` の競合 | cursor の chain では、両方とも op の中で display_mutex の下でだけ書く | 3 |
| M-c hook | `content_serial` は `struct vulkan_image` に置く（back pointer は要らない）。ExecuteCommands は secondary の表を足し、溢れは epoch。生成は `tools/maintain-commands.noct`（Vulkan の header から生成する。`api-commands.tsv` ではない）の固定の表。控えの時点の議論 | 5 |
| M-d WS213 との契約 | fast path は queue->mutex を取らない。C の fast path・R の submit・worker が同時でも安全な理由を書き、fixture の場面に入れる | 9、10 |
| M-e import・shm の vkQueueWaitIdle | WSI の drain の問題で、受け入れ B の前提の外（WS213 p004、または main loop の WaitIdle の扱い）。p016 の受け入れ B は「main loop が止まらない時」 | 8 |
| M-f acquire の display_mutex・kernel の controller の mutex・L1 の位置付け | acquire は cursor の chain で `image_available`（display_mutex）を呼ばない（B-u に入れる）。primary の PRESENT_SYNC の display_mutex と kernel の adoption の待ちは log で測る。L1 は前提ではなく計測の後の次の版（段 B-l）。WS213 の側は Q1 が揃える | 3、11 |
| M-g 試験 | 場面を足した | 10 |
| 軽微: 第 3 版の「各 image は最初の 1 回だけ写す」 | 第 3 版の 3 の文を直した | 第 3 版 |
| 軽微: 段の表の B | B-k・B-u・B-l に分けた | 段の表 |
| 軽微: lock の順 | fast path は device・swapchain・display の mutex を入れ子にしない。worker の `copied_*` の更新は display_mutex の外 | 9 |
| 軽微: FIFO の present mode で古い present を捨てる | cursor の plane だけの例外と明記 | 2 |
| 軽微: has_display の無い present の位置 | op は「位置あり／なし」を受け、なしなら lease の前の位置 | 3 |
| 軽微: ioctl の直前の error の見直し | fast path は op の直前に chain と device の error を atomic で読む | 3 |
| 軽微: UAPI の注記に EBUSY | B-u で `include/uapi/gpu-display.h` の CURSOR_SET の注記に「全部の cursor の image が flip の list に名指されている間は EBUSY（一時的）」を足す。libvulkan は一時的な失敗として扱う | 3 |
| 軽微: `vulkan-buffer-copy.c:289-290` | source で確かめた: backing が解決できない時は copy を失敗にする（generation を進めずに中身だけ変わることは無い）。推測ではなく解決 | — |
| 未確認: evdev の時刻の clock | source で確かめた: evdev の時刻（`src/drivers/generic/input.c` の `report_timestamp`）は `clock_milliseconds`、CLOCK_MONOTONIC は `clock_ticks`（`src/kern/clock.c`）で、同じ tick の数。evdev は ms に切り捨て（比べる時の誤差は 1ms 未満） | 7 |

### 1. cursor の lane（F1 (a)）

- `struct wsi_queue_worker` に lane（ORDINARY・CURSOR）を足し、`present_worker_get(queue, lane, &worker)` は (queue, lane) ごとに 1 本の worker を返す。job の lane は、`job->count == 1` で、その chain が cursor の surface（`surface->cursor`）かつ `gpu_present` の時に CURSOR、他は ORDINARY（cursor と primary を 1 回の present に混ぜた物は ORDINARY で、fast path も使わない）。
- cursor の lane の worker は今の worker と同じ loop（FIFO、job の fence を待ち、`present_native`、`present_finish`）で、ORDINARY の job を待たない。だから cursor の通常経路の present が待つのは、自分の GPU の copy の fence と display の op だけになる。
- 自分の copy の fence が完了するまでの時間（G）は、kernel の executor の順番で決まる: copy の execution は compositor の session の列で、先に入った compositor の execution（frame）の後ろ。表示の session の優先（A1d の `select_session`）は display の歩と他の session の歩を交互にするので、先の frame が k 歩（pass）なら他の session の歩も最大 k 歩挟まる。よって G は「compositor の session の GPU 1 回分（k 歩）＋他の session の最大 k 歩＋copy の 1 歩」で、primary の present の adoption（1〜2 refresh）を含まない。G は log の `fence_ns − publish_ns` で測る（7）。
- `present_drain`（vkQueueWaitIdle・vkDeviceWaitIdle）と `present_workers_stop` は今どおり worker の list を全部見るので、cursor の lane も含む。fence の slot の cache（`worker->fences`）は lane の worker ごと。
- 順序: 同じ swapchain の present の順は cursor の lane の中の FIFO と ticket（2）で保つ。cursor の plane と primary の plane の present の間の順は、Vulkan で見える約束が無い（別の swapchain）。lane を分けても、primary の job の fence を待たないだけで、cursor の copy の GPU の順は変わらない。

### 2. ticket（M-a、第 4 版の 1 を置き換え）

- lease（`struct wsi_display_lease`、oldSwapchain の旧と新の chain で共有）は `next_ticket`（atomic）と `applied_ticket`（display_mutex の下）を持つ。
- **取る所は 1 か所**: `present_reserve` の swapchain_mutex の区間で、cursor の lane の job の image を PRESENTING にした直後に `__atomic_add_fetch(&lease->next_ticket, 1)`（platform の新しい op `cursor_ticket(lease)`）。fast path も通常経路もこの値を使う。予約の順＝ticket の順になる。
- 予約の後に失敗した present（record・submit の失敗）は ticket を使わずに捨てる。番号に穴が空くだけで、比べるのは大小だけなので害は無い。
- **FIFO の例外**: cursor の chain の present mode は FIFO だが、cursor の plane だけは「新しい ticket が出た後の古い present は見せずに捨てる」（mailbox に近い）。cursor の plane は位置と像の最新だけに意味があり、古い present を見せないことは利用者に見えない。primary の plane は今どおり全部の present を順に出す。

### 3. display の op（H2・M-b・軽微、第 4 版の 3 を置き換え）

- platform の新しい op `present_cursor_at(lease, image, has_position, x, y, ticket, &frame, &sequence)`。display_mutex の 1 区間で次を行う。fast path と cursor の lane の worker は同じ op を呼ぶ。
  1. `ticket <= applied_ticket` なら何もせず `DROPPED`（成功）を返す。frame・sequence も変えない。
  2. `*frame == UINT64_MAX` なら device lost（今の `present_native` の確かめを移す）。
  3. 位置: `has_position` なら (x, y)、なしなら lease の前の位置（`has_display` の無い present）。
  4. `CURSOR_SET`、成功なら `CURSOR_MOVE`。
  5. 結果: SET の失敗は `applied_ticket`・lease の位置を変えない（kernel は前の像と位置を見せたまま）。SET の成功の後の MOVE の失敗は `applied_ticket = ticket`（新しい像が出ているので、古い job の SET は捨てる）、位置は変えない。両方の成功で `applied_ticket = ticket`、lease の位置を更新。
  6. 成功（DROPPED を除く）の時だけ `(*frame)++` と `*sequence = plane->frame`。cursor の chain の `frame`・`sequence` を書くのはこの op だけで、display_mutex の下（M-b）。WSI の `present_native` は cursor の chain では `present_cursor_at` だけを呼び（今の `platform->position` と `chain->frame++`、`wsi-swapchain.c:2322-2329` は通らない）、frame・sequence を触らない。
- **EBUSY**（kernel の 4 枚の cursor の image が全部 flip の list に名指され、鍵も一致しない時、一時的）: op は `BUSY` を返し、error にしない。fast path は同じ job（予約と wait の消費は済み、GPU の仕事は無い）を cursor の lane に「native だけの job」（fence 無し）として渡す（lane の worker が作れなければ error を latch）。worker はこの job では fence を待たず、1 refresh 待って（`display_wait_native` で plane の sequence の次、無ければ 4ms の眠り）同じ ticket で op をやり直す。新しい ticket が先に出れば DROPPED で終わる。3 回続けて BUSY なら `VK_ERROR_NATIVE_WINDOW_IN_USE_KHR` を chain の error に latch（今と同じ扱い）。worker の通常経路の BUSY も同じ。fast path で BUSY になるのは、鍵が 4 枚の取り合い（oldSwapchain の旧と新の 6 枚など）で消えた時だけの見込み。
- fast path は op を呼ぶ直前に `chain->error`・`device->error`・`context->error` を atomic で読み、どれかが立っていれば op を呼ばずにその error で終える（worker の loop の先頭と同じ扱い）。
- op の失敗（DROPPED・BUSY 以外）は、今の worker と同じく chain の error に latch する（`wsi-swapchain.c:2785-2795` の形）。

### 4. fast path の判断・順序・lock（H1・H-b、第 4 版の 2 を置き換え）

- 位置: `vkQueuePresentKHR` の `present_prepare`（検証、ACQUIRED、cursor の rect、`present_reserve`）の後、`present_worker_get`・`present_record` の前。
- 条件（全部）:
  1. job が cursor の lane（1）で `results[0] == VK_SUCCESS`。
  2. chain の usage が TRANSFER_DST・TRANSFER_SRC だけ（swapchain の作成の時に chain に印を持つ）。
  3. pNext に `VkDisplayPresentInfoKHR`・`VkPresentRegionsKHR` 以外の struct が無い（present fence・present id など GPU の完了を要る物は通常経路）。
  4. 中身が未変更: swapchain_mutex の下で `presentation[i].copied_valid` かつ `copied_serial == __atomic_load(vulkan_image(group の image i)->content_serial)` かつ `copied_epoch == __atomic_load(device->content_epoch)`。
  5. wait の semaphore が重複なしで、全部 software の payload で signal 済み（acquire のもの）。確かめと消費は 1 つの区間で行い、全部が満たす時だけ全部を消費する。1 つでも満たさなければ何も消費せずに通常経路へ。
- 順序: (4) の確かめ（swapchain_mutex、短い）→ (5) の確かめと消費（device->mutex、短い）→ error の見直し（3）→ op（mutex を何も持たない状態で呼ぶ、op の中で display_mutex だけ）→ `job->submitted = VK_TRUE` にして `present_finish`（image を AVAILABLE に、pending_jobs を戻す）→ log（7）。
- 4 と 5 の間で中身が変わらない理由: image i は PRESENTING で、application はそれに書けない。Vulkan では present の wait の semaphore を signal する submit は present より前に submit されている（binary semaphore の wait は signal の submit の後）ので、image i への合法な書き込みは全部 present の呼び出しの前に受理されている。epoch の加算が 4 の後に起きても、それは image i への合法な書き込みではない。
- 条件のうち 1〜4 が成り立ち 5 だけが成り立たない（GPU の semaphore を待つ）時は、通常経路で copy を記録しない（wait と fence だけの空の command buffer、第 4 版の 2 のまま、log の path=empty）。この job は控えを持たず、`copied_*` を変えない。1〜4 のどれかが成り立たない時は今どおり GPU の copy を記録する。
- **H-b（device->mutex の待ち）**: B-u の最初の実装では、(5) は device->mutex の下で行う（今の `queue_attempt` の消費 `queue.c:465-472` と同じ lock）。device->mutex は `queue_attempt` が kernel との round trip の間（`queue.c:422-479`）、`vkGetFenceStatus`（`sync.c:187-224`）、worker の `vulkan_fences_wait`、acquire の signal（`sync.c:429`）が持ち、WS213 の後は R の submit とも取り合う。待ちの上限は無いので、log の `mutex_ns`（lock の待ちの時間）で測る。
  - 外す案（採るかは計測の後。目安は場面 S1 で `mutex_ns` の p99 が 2ms を超えるか、呼び出し→ioctl の閾値を外す原因になった時）: `vulkan_sync` の `software_signaled` を device->mutex から外し、payload 専用の小さな lock（device ごとの `payload_mutex`、持つ間に他の lock も round trip も取らない）で守る。`queue_attempt`・acquire の signal・fence の問い合わせ・external fence の読み書きは device->mutex の中で `payload_mutex` を取る（順は device->mutex → payload_mutex の一方向）。fast path は `payload_mutex` だけを取る。device の error は atomic で読む（今も atomic の所がある）。
  - 代案の atomic CAS（`software_signaled` を atomic にし、消費は 1→0 の CAS）は、複数の wait の「全部か無しか」の消費で戻しが要り、application の誤用（同じ semaphore を 2 か所で wait）で戻しが競る。小さな lock の方が単純なので第 1 案にする。
- **lock の順（軽微）**: fast path は device->mutex・swapchain_mutex・display_mutex のどれも入れ子にしない（1 つずつ取って外す）。queue->mutex は取らない（9）。今ある順（queue->mutex → device->mutex、queue->mutex → swapchain_mutex、swapchain_mutex → display_mutex（acquire の `image_available`、primary の chain だけに残る、11））に新しい辺を足さない。worker の `copied_*` の更新は `present_finish` の swapchain_mutex の区間で、display_mutex の外。

### 5. 中身の記録（M1・M2・M-c、第 4 版の 4 を置き換え）

- **置き場**: `content_serial` は `struct vulkan_image`（`internal.h:205`）に置く（atomic、64 bit）。group の image は application が見る VkImage そのもので、oldSwapchain で共有される。`swapchain_owned` が真なのは group の image だけ（`wsi-swapchain.c:1248`。WSI の native の `shared_image` には付かない）なので、group の image への back pointer は要らない。`copied_serial`・`copied_epoch`・`copied_valid` は chain の `presentation[]`（native の image は chain ごと）に置き、初期値は `copied_valid = false`。新しい chain は全部未コピー。`content_epoch` は device に置く（atomic）。
- **command buffer の表**: `VkCommandBuffer_T` に、書き込み先の `struct vulkan_image *` の小さな表（8 個、重複なし）と「分からない」の印。`vkBeginCommandBuffer`・reset（command buffer と pool）で空にする。記すのは `swapchain_owned` の image だけ。
- **hook の生成**: `commands-generated.inc` は `tools/maintain-commands.noct` が Vulkan の header（`*PFN_vkCmd`）から作る（`api-commands.tsv` は dispatch の表で、この生成には使わない）。generator に固定の表 {vkCmdCopyImage: dstImage, vkCmdBlitImage: dstImage, vkCmdCopyBufferToImage: dstImage, vkCmdResolveImage: dstImage, vkCmdClearColorImage: image, vkCmdClearDepthStencilImage: image} を持たせ、該当の command の記録の終わりに `command_note_image_write(command, <引数>)` を出す。vkCmdExecuteCommands には `command_note_secondaries(command, count, buffers)` を出し、secondary の表を primary に足す（溢れ、または secondary が「分からない」なら primary も「分からない」）。生成物は作り直して commit する（NoctLang は共有の toolchain を使うだけで変えない）。header の 44 の command の数の確かめ（generator の終わり）に、固定の表の 6 つが全部見つかったことの確かめを足す。2 版（vkCmdCopyImage2 など）は今の libvulkan に無い。足す時は同じ表に入れる。
- **他の書き込みの経路**: render pass（COLOR_ATTACHMENT・vkCmdClearAttachments）と descriptor（STORAGE は不可）は usage の限定（4 の 2）で fast path の外。layout の遷移（oldLayout UNDEFINED の barrier）は中身を未定義にするだけで、古い像を見せても Vulkan の規則に反しない。
- **加算**: `queue_attempt` の受理（`queue.c:461-473`、device->mutex の下）の 1 か所で、受理した command buffer の表の image の `content_serial` を 1 進め、「分からない」があれば `content_epoch` を 1 進める。vkQueueSubmit・vkQueueSubmit2（KHR は変換）・WSI の内部の submit・WaitIdle の空の submit は全部ここを通る（review-B4 で確認済み）。
- **控えの時点**（M-c）: WSI の copy の job の控え（`content_serial` と `content_epoch`）は、`present_reserve` の swapchain_mutex の区間（PRESENTING にした直後）に atomic で読む。4 の理由（image i への合法な書き込みは全部 present の前に受理済み、予約の後は書けない）で、`queue_enqueue` の再試行（`queue.c:345-366`）の受理された回に読むのと同じ値になる。予約の後に epoch が進んでも、控えが古い側になって fast path が 1 回余計に copy するだけ（安全な側）。worker は fence の待ちに成功した後、`present_finish` の swapchain_mutex の区間で `copied_serial`・`copied_epoch`・`copied_valid` を更新する（DROPPED の job も copy は実行したので更新する）。fence の待ちの失敗（device lost）では更新しない。

### 6. 追い越しが安全な理由（不変条件、第 4 版の 1 の (i)〜(iii) に足す）

- (iv) cursor の lane と fast path が同時に動いても、native の image j を書くのは image j の copy の job だけで、その job が終わる（`present_finish`）まで image j は PRESENTING。fast path が使う image i は ACQUIRED から予約した物なので j ではない。kernel は `CURSOR_SET` の中で行を写す（cursor の present は holds を取らない）ので、op が返った後に native の image を書き換えてよい。
- (v) `applied_ticket` と lease の位置・chain の frame・sequence は display_mutex の下だけで変わるので、fast path と worker の op が同時でも、見える結果は ticket の大きい方。

### 7. log（H-a）

- 環境変数 `VULKAN_ZEDBSD_CURSOR_LOG=1`（device の作成の時に 1 回読む）の時だけ、cursor の lane の present ごとに 1 行を stderr に出す。行は全部の lock を外した後に 1 回の `write(2)` で出す（fast path は op の後、worker は job の終わり）。
- 書式: `VK CURSOR ticket=<n> path=fast|worker|empty|drop|retry result=<VkResult> image=<i> x=<x> y=<y> depth=<lane の待ちの数> call_ns= mutex_ns= queue_ns= publish_ns= fence_ns= display_ns= set_ns= done_ns=`。時刻は全部 CLOCK_MONOTONIC の ns の絶対値、`mutex_ns`・`queue_ns` は lock の待ちの長さ。
  - `call_ns`: vkQueuePresentKHR に入った時刻。`mutex_ns`: (5) の device->mutex の待ち（H-b）。`queue_ns`: 通常経路の queue->mutex の待ち（R の submit の round trip との取り合い）。`publish_ns`: lane に積んだ時刻。`fence_ns`: worker の fence の待ちが終わった時刻（G の測り）。`display_ns`: op が display_mutex を取れた時刻（primary の PRESENT_SYNC との取り合い、M-f）。`set_ns`・`done_ns`: SET の直前と MOVE の後（差が kernel の中の待ち、controller の mutex と adoption、M-f）。
- **clock**: compositor の `KWL CURSOR_PLANE present` の `event_us`（evdev の時刻）は CLOCK_MONOTONIC と同じ tick の数で ms に切り捨て、`at_us`・`input_us` は compositor の `kwl_microseconds`（CLOCK_MONOTONIC、`wire.c:57`）。同じ時間軸で比べられる（evdev との差は 1ms 未満の誤差）。
- **対応づけ**: compositor の 1 回の vkQueuePresentKHR は libvulkan の 1 行と compositor の 1 行（`--log-frames`）になり、同じ thread の順で並ぶ。x・y と順で対にする。集計の script（対にして、場面ごとに百分位を出す）は B-u で `plan/ws141/tests/` に置き、使い終われば Q1 に削除を頼む（回帰に残すかは B の判定の後に決める）。
- 費用: 有効でない時は、present ごとに `clock_gettime` を 0 回（時刻は log が有効な時だけ取る）。

### 8. 受け入れ B（第 4 版の 5 と「受け入れの条件」の B を置き換え）

- **分担**（Q1）: p016 の B は「main loop が止まらない時の入力→ioctl」を受け持つ。これは compositor が present を呼んでから kernel の ioctl が終わるまで（**呼び出し→ioctl** ＝ `call_ns` → `done_ns`）で、libvulkan と kernel の中だけで決まる。入力→呼び出し（main loop・C0 の間隔・thread C・import・shm の vkQueueWaitIdle、M-e）と端から端の 60fps は WS213。
- **閾値は 16.7ms**（1 refresh、WS213 と同じ。第 4 版までの 100ms はやめる）。
- **場面**（Pi 4、1 回の boot。WS213 の 6.4 と同じ場面で、同じ boot の log で両方を判定できる。Pi には入力の自動化が無いので、ユーザーか Q1 の手で）: 各場面で pointer を動かし続けながら、App Home を 5 回開閉、窓を 10 秒 drag、Music・Settings の起動を 1 回ずつ。
  - **S1 位置だけ**（像が変わらない間）: (1) path が fast の割合: 像が変わらない present のうち、像の変化の後の各 image の最初の 1 回と epoch の変化の後を除いて全部 fast。(2) 呼び出し→ioctl の p99 と最長 ≤ 16.7ms。(3) 入力が続く間の ioctl の間隔（続く 2 つの `done_ns` の差、compositor の呼び出しの間隔が 16.7ms 以下だった組だけ）の p99 ≤ 16.7ms。
  - **S2 像の変化**（hover で形が変わる、client の set_cursor）: (1) 形が変わった present の呼び出し→ioctl ≤ 16.7ms＋G（G は同じ present の `fence_ns − publish_ns`、compositor の session の GPU 1 回分、1）。G の p99 も記録する。(2) 形が変わった後、像が安定すれば image の数（3）以内の通常経路の present の後に fast に戻る。(3) その間の位置の present も (1) の上限を超えない。
  - **S3 animated**（client の cursor の surface が毎コマ変わる）: 全部の present が通常経路。各 present の呼び出し→ioctl ≤ 16.7ms＋G、lane の待ちの数（`depth`）は image の数を超えない（積み上がらない）。実機に animated の cursor を出す app が無ければ、host の fixture（10 の u3）と試験の client で確かめ、実機は未実施と書く。
- **前提**: (b)（(b) 第 2 版の計測で decoder の待ちは数 ms に下がった）と A3（lazy map）は、呼び出し→ioctl には通常経路の G を通してだけ効く。fast path（S1）は A3 を待たずに判定できる。S2・S3 の G は A3 の前後で比べて記録する。
- 判定は B-u の実装と T1 の回帰（i915・Venus で変化が無い）の後、Pi の log で Q1 が行う。満たさない時は log の内訳（`mutex_ns`・`queue_ns`・`display_ns`・`set_ns→done_ns`・G）で原因を分け、H-b（4）または B-l（11）へ進む。

### 9. WS213 との契約（M-d）

- zedBSD の libvulkan には compositor の queue_lock が無く、WS213 の後は thread C（cursor）と thread R（描画）が同じ VkQueue を同時に使う。B-u は次を保つ。
  - fast path は queue->mutex を取らない（queue の状態に触れない。submit も lane への公開もしない）。R の vkQueueSubmit が queue->mutex を持って kernel と round trip している間も、C の fast path は進む。
  - 通常経路の present は今どおり queue->mutex の下で submit と lane への公開をする（R の submit と取り合う。log の `queue_ns`）。
  - fast path が触る共有の状態は、chain の状態（swapchain_mutex）、sync の payload（device->mutex、H-b の後は payload_mutex）、lease の ticket（atomic）と display の状態（display_mutex）だけで、どれも入れ子にしない。R の submit（queue->mutex → device->mutex）、worker（swapchain_mutex、fence の待ち、display_mutex）との間に新しい lock の循環は無い。
- Mesa の build（Linux・FreeBSD）は cursor の plane を開かない（WS213 3.3）ので、この契約は zedBSD の libvulkan だけのもの。
- thread C の copied の gate（compositor が acquire の前に前の copy の fence を見る、F1 (b)）は WS213 の側の変更で、B-u はそれに依らない。

### 10. 試験（M4・M-g、第 4 版の 6 を置き換え）

- libvulkan の WSI の host fixture（偽の render と display の kernel の transport、ioctl ごとに止められる）を作る。見積もり: この段で一番大きい（transport の偽物と、WSI・queue・sync の host build）。場面:
  - (u1) 位置だけの present が QueueSubmit も worker も通らず、SET・MOVE の ioctl だけ（path=fast）。
  - (u2) F1: primary の job が fence で止まっている間に、cursor の通常経路の job が cursor の lane で先に完了する。
  - (u3) 全部の image が PRESENTING の時の像の変化: 3 枚の copy が順に完了し、acquire が空いた順に進み、像が安定した後は image の数以内に fast に戻る。毎回像が変わる（animated）時は depth が image の数を超えない。
  - (u4) fast path と worker の同時: 古い ticket の job が後から op に来て DROPPED、frame と sequence は単調で DROPPED では進まない。
  - (u5) SET の失敗（`applied_ticket` と位置は変わらず、chain の error）、SET の成功の後の MOVE の失敗（`applied_ticket` は進む、error）、EBUSY（native だけの job で retry、新しい ticket が来れば DROPPED、3 回で error）。
  - (u6) oldSwapchain で lease を共有する 2 つの chain の ticket の順。
  - (u7) `copied_valid` の初期値、新しい chain は未コピー、ExecuteCommands で secondary の表の合算、溢れで epoch が進み fast path が 1 回 copy に戻る、Begin・reset で表が空。
  - (u8) fast path が op（偽の ioctl）の間、device->mutex・swapchain_mutex・queue->mutex を持たない（偽の ioctl の中から 3 つを try-lock して取れる）。
  - (u9) M-d: C の fast path・R の vkQueueSubmit（偽の kernel が round trip の中で止まる）・worker が同時に動き、fast path は R を待たずに終わる。
  - (u10) wait が GPU の semaphore の時（中身は未変更）は copy を記録しない空の submit。
  - (u11) cursor の chain の acquire は `image_available` を呼ばない（display_mutex を偽の primary の PRESENT_SYNC が持っていても acquire が進む）。
  - (u12) log: 環境変数で cursor の present 1 回に 1 行、時刻が単調、無効の時は出ない。
  - (u13) has_display の無い present は前の位置で SET・MOVE。
  - (u14) generator: 6 つの command と ExecuteCommands の hook が生成物にあり、header の 44 の command の数の確かめが通る。
- kernel の鍵（M3）は display の host 試験で済み（B-k）。UAPI の注記の EBUSY は文書の変更だけ。
- 実機は 8 の場面（Pi）、i915・Venus の回帰は T1（fast path は bcm2711 だけで、他は表と加算と cursor の lane の分けだけが変わる）。

### 11. 段 B-l（L1、計測の後の次の版。前提ではない）

- 8 の判定で、`display_ns − call_ns`（display_mutex の待ち）か `done_ns − set_ns`（kernel の controller の mutex と adoption の待ち）が原因で S1 の閾値を外した時に、次の版で設計する: lease の cursor 専用の lock で cursor の op を display_mutex から分ける、kernel の CURSOR_SET・MOVE を `output_present` の settle・adoption の待ち（`display-device.c:861-930`）の controller の mutex から外す。
- B-u に入れる分（M-f の一部、安くて明らか）: acquire（`wsi-swapchain.c:3060-3061`）は cursor の chain で `image_available` を呼ばない。cursor の present は holds を取らない（`display_present_cursor`）ので、cursor の native の image は常に空いている。これで cursor の acquire は swapchain_mutex を持ったまま display_mutex を待たない。
- WS213 の ws.md・p001 の 6.3 の「L1 を前提」は Q1 が「計測の後の次の版」に揃える。

### 12. i915・Venus・Linux・FreeBSD（L3）

- 変わるのは command buffer の表と受理の時の加算、worker の lane の分け（cursor の surface だけが CURSOR の lane）。cursor の plane は `GPU_DISPLAY_HAS_CURSOR` の bcm2711 だけなので、fast path・ticket・op は他の platform では動かない。HAL は触れない。UAPI は注記だけ。

### 13. 未決（Q1 に）

1. S2・S3 の上限を「16.7ms＋G」とする件（G を compositor の session の GPU 1 回分として許す）。G 自体の上限を受け入れに置くか（App Home のコマの exec は 4〜15ms の実測）。
2. H-b の採否の目安（S1 の `mutex_ns` の p99 が 2ms 超）と、B-l の起動の条件（8 の内訳で S1 を外した時）でよいか。
3. animated の cursor を出す app が実機にあるか（無ければ S3 は host と試験の client だけ）。
4. 未確認のまま: kernel が submit の外で group の image に書く経路（無いと見ているが source で網羅していない）、decoder が session ごとか全体か（G に効く）、hover・drag で client が cursor を変えるか（S2 の起き方）。

## (B) 第 6 版（2026-10-12 P2、[review-B5](review-B5.md) と Q1 の判断を反映。B-u を実装する版）

第 5 版の 1〜13 は、この節と食い違う所をこの節で置き換える。行番号は main 255efde24。

### review-B5 への対応

| 指摘 | 対応 | 節 |
| --- | --- | --- |
| 重大 1 (a) S1 は display_mutex の待ちで閾値を外す | Q1 の判断で B-u に入れる: `present_cursor_at` は lease 専用の cursor の lock（`lease->cursor_mutex`）だけを取り、display_mutex を取らない。`applied_ticket`・x・y と chain の frame・sequence はこの lock の下、`plane->frame` は atomic（primary の present は display_mutex の下で atomic に書く） | 3 |
| 重大 1 (b) kernel の controller->mutex | 段 **B-k2**（kernel、「おそらく必要」）として設計だけ置く。今回は実装しない（kernel は P3 の範囲と重なるので Q1 が後で割り当てる） | 11 |
| 重大 2 S2 は止まりを検出できない、S3 の depth は常に満たす | S2 は「形が変わった present の `call_ns` から、次に位置を反映した ioctl の `done_ns` まで」と「入力が続く間の `done_ns` の間隔」、S3 は ioctl の間隔と DROPPED の数。WS213 の copied の gate の変更の前後どちらで測ったかを書く。上限は Q1 の判断で直接（G の p99 ≤ 16.7ms、S2・S3 の呼び出し→ioctl の p99 ≤ 33.4ms） | 8 |
| 中 3 EBUSY の再試行が動かない | `display_wait_native` を使わない。待ちは refresh_millihz からの nanosleep、回数で latch しない。新しい ticket が出る（`surface->cursor_ticket` が自分より大きい）か上限の 1 秒で DROPPED（error にしない）。fast path から渡す job は「native だけ」の印で、worker は `vulkan_fences_wait` を飛ばす。fast path の判断は `job->info`・indices の写し（`wsi-swapchain.c:534-545`）の後 | 3、4 |
| 中 4 H-b の payload_mutex 案の穴 | 文だけ直す: 分類の時に「消費予約」の印を付け、受理で確定、拒否で戻す。B-u には入れない | 4 |
| 中 5 時刻の分解能 1ms | 長さは counter（arm64 は `cntvct_el0`÷`cntfrq_el0`、EL0VCTEN は `locore.S:66-69` で有効。他の arch は CLOCK_MONOTONIC）。CLOCK_MONOTONIC は compositor の log と対にする絶対時刻 `call_ns` だけ | 7 |
| 軽微 (u1) 「SET・MOVE の ioctl だけ」は不成立 | fast path でも `present_prepare`→`swapchain_current`→`display_capabilities`→`vulkan_wsi_display_refresh` の `GPU_DISPLAY_QUERY` が 2 回以上ある（kernel の `output_query` は mutex を取らない、`wsi.c` の `wsi_mutex` は短い）。fixture の期待値は「QueueSubmit も worker も通らない、SET・MOVE が 1 回ずつ」に直す | 9、10 |
| 軽微 §9 と §3 の EBUSY の受け渡しの矛盾 | EBUSY の job の lane への公開は swapchain_mutex だけで、queue->mutex は要らない（GPU の仕事が無く、queue の submit との順序が無い。drain は worker の list で見える） | 9 |
| 軽微 command buffer の表が `struct vulkan_image *` を直に持つ | valid usage に頼る: 記録した image が破棄された command buffer は invalid で、submit は VUID-vkQueueSubmit-pCommandBuffers-00070 違反。handle も pointer の bit（`objects.c:345`）なので handle で持っても同じ。表は Begin・reset で空にし、受理の時にだけ辿る | 5 |
| 軽微 条件 5 に binary semaphore の temporary import | zedBSD の libvulkan に semaphore の import は無い（`vkImportSemaphoreFdKHR` 無し、external は fence だけ）。判定は `queue_write_waits` と同じ規則（`software_signaled`、同じ semaphore の 2 回目は native）で、重複は fast path にしない | 4 |

### 1. cursor の lane（第 5 版の 1 のまま）

- 第 5 版の 1 のとおり。lane の判定は `job->count == 1`、`surface->cursor`、`gpu_present`。

### 2. ticket（第 5 版の 2 を置き換え）

- 番号の出所は **surface**（`struct vulkan_surface` の `cursor_ticket`、atomic）。surface は oldSwapchain の旧と新の chain で共有され、lease と 1 対 1（`surface->platform_private`）なので、第 5 版の lease の `next_ticket` と同じ順になる。WSI が platform を呼ばずに「新しい ticket が出たか」を読めるので、EBUSY の再試行の打ち切り（3）に使える。
- 取る所は 1 か所: `present_reserve` の swapchain_mutex の区間、cursor の lane の job を PRESENTING にした直後。`applied_ticket` は lease（cursor_mutex の下）。
- 予約の後の失敗で番号に穴が空くのは害が無い（大小だけを比べる）。FIFO の例外（cursor の plane だけ古い present を捨てる）は第 5 版の 2 のまま。

### 3. display の op（第 5 版の 3 を置き換え）

- platform の op `present_cursor(lease, image, has_position, x, y, ticket, &frame, &sequence, &outcome, times)`（第 5 版の `present_cursor_at`）。旧来の `position` の op は無くす（cursor の chain だけが使っていた）。
- lock: **lease の `cursor_mutex`** の 1 区間（display_mutex は取らない）。区間の中で読む物: `lease->identifier`・`generation`・`plane->connection->fd`・`image->imported`（どれも lease と image が生きている間は変わらない。lease は chain の `pending_jobs` が、image は chain が持つ）、`plane->frame`（atomic の load）。書く物: `applied_ticket`、lease の x・y、chain の frame・sequence（cursor の chain ではこの op だけが書く）。
  1. `ticket <= applied_ticket` → `DROPPED`、何も書かない。
  2. `*frame == UINT64_MAX` → device lost。
  3. 位置: `has_position` なら (x, y)、無ければ lease の前の位置。
  4. `CURSOR_SET`、成功なら `CURSOR_MOVE`。
  5. SET が EBUSY → `BUSY`（error にしない、何も書かない）。SET の他の失敗 → error、何も書かない。SET の成功の後の MOVE の失敗 → `applied_ticket = ticket`、位置は変えず error。両方の成功 → `applied_ticket = ticket`、位置を更新、`(*frame)++`、`*sequence = plane->frame`、`SHOWN`。
- `times` が NULL でなければ（log が有効）、cursor_mutex を取れた時・SET の直前・MOVE の後の counter を書く。
- primary の present（`display_present_image_sync`）は `plane->frame` を display_mutex の下で `__atomic_store_n` で書く（読みは今どおり display_mutex の下）。`display_release_native` の `CURSOR_HIDE` は lease の参照が 0 の時だけで、その時に op を呼ぶ chain は無い。
- **EBUSY の再試行**（中 3）: op を呼ぶ関数（WSI の `present_cursor_show`）は、worker の中でだけ再試行する。BUSY なら refresh_millihz（surface の mode、0 なら 60Hz）の 1 周期を `nanosleep` で待ち、同じ ticket で op をやり直す。打ち切り: `surface->cursor_ticket > ticket`（新しい present が出た、それが見せる）か、最初の BUSY から 1 秒 → DROPPED（成功、error を latch しない）。kernel の `cursor_owner` の EBUSY（lease がこの open の物でない、永続）も同じく 1 秒で DROPPED になるが、その時は primary の present も失敗して chain が閉じる。
- fast path で BUSY なら、同じ job（予約と wait の消費は済み、GPU の仕事は無い）に `native_only` の印を付けて cursor の lane に公開する（swapchain_mutex の下だけ、9）。worker は `native_only` の job では `present_early`・`vulkan_fences_wait` を飛ばし、上の再試行で op を出す。lane の worker が作れなければ chain の error に latch する（OOM は device lost）。
- fast path は op を呼ぶ直前に `chain->error`・`device->error`・`context->error` を atomic で読み直す（第 5 版のまま）。op の失敗は chain の error に latch し、その present の結果としても返す（同期なので）。OOM は device lost。

### 4. fast path（第 5 版の 4 を置き換え）

- 位置: `present_prepare` と indices の写しの後、`present_worker_get` の前。
- 条件（全部）: (1) cursor の lane で `results[0] == VK_SUCCESS`、platform に `present_cursor` がある (2) application の usage（`imageUsage`）が TRANSFER_DST・TRANSFER_SRC だけ（作成の時に chain に印） (3) pNext が `VkDisplayPresentInfoKHR`・`VkPresentRegionsKHR` だけ (4) swapchain_mutex の下で `presentation[i].copied_valid`、`copied_serial == content_serial`、`copied_epoch == content_epoch` (5) device->mutex の下で wait の semaphore が重複なしで全部 `software_signaled`、device の status も成功 → 全部を消費（`software_signaled = VK_FALSE`）。1 つでも満たさなければ何も消費しない。
- (1)〜(4) が成り立ち (5) が成り立たない → 通常経路で copy を記録しない（空の command buffer、path=empty）。(1)〜(3) のどれかか (4) が成り立たない → 通常経路で copy（path=worker）。
- 成り立てば: error の見直し → op（何の mutex も持たない）→ `job->submitted = VK_TRUE` で `present_finish` → log。BUSY は 3 のとおり lane へ。
- 4 と 5 の間で中身が変わらない理由と lock の順は第 5 版の 4 のまま。
- **H-b**（文だけ、B-u には入れない、中 4）: payload 専用の lock にする時は、`queue_attempt` の分類（`queue_write_waits`）で payload に「消費予約」の印を付け（他の分類はそれを software と見ない）、受理で `software_signaled = VK_FALSE` と印の解除、拒否・再試行で印だけ戻す。分類から消費までの round trip の間 device->mutex を持たなくても、同じ payload を 2 つの submit（または submit と fast path）が消費しない。

### 5. 中身の記録（第 5 版の 5 に足す）

- 置き場は新しい file `content.c`（command buffer の表への記録・secondary の合算・空にする・受理の時の加算）。generator（`tools/maintain-commands.noct`）は固定の表の 6 command の記録の終わりに `vulkan_content_note_image(command, <dstImage か image>)`、`vkCmdExecuteCommands` に `vulkan_content_note_secondaries(command, commandBufferCount, pCommandBuffers)` を出し、6 つが全部見つかったことを確かめる（見つからなければ生成を止める）。
- 表は Begin と `command_buffer_initial`（reset・pool の reset）で空。記すのは `swapchain_owned` の image だけ。表の pointer は受理の時にだけ辿る（valid usage、上の表）。
- 加算は `queue_attempt` の受理の分岐（device->mutex の下）で、sparse でない時に submit の全 command buffer の表を辿る。

### 6. 追い越しが安全な理由（第 5 版の 6 に足す）

- (vi) cursor_mutex は lease ごとで、display_mutex と入れ子にしない（op は display_mutex を取らず、display_mutex を持つ所は cursor_mutex を取らない）。新しい lock の辺は無い。

### 7. log（第 5 版の 7 を置き換え）

- 環境変数 `VULKAN_ZEDBSD_CURSOR_LOG=1` を cursor の swapchain の作成の時に 1 回読み、chain に印を持つ（device ではなく chain。device.c を変えない）。
- 書式: `VK CURSOR ticket=<n> path=fast|worker|empty|busy result=<VkResult> outcome=shown|dropped|busy image=<i> x=<x> y=<y> depth=<lane の待ちの数> call_ns=<CLOCK_MONOTONIC の絶対> mutex=<ns> queue=<ns> publish=<ns> fence=<ns> lock=<ns> set=<ns> done=<ns> retries=<n>`。`call_ns` の後は全部 counter の長さで、`mutex`・`queue` は lock の待ちの長さ、`publish`〜`done` は呼び出しからの経過（無い所は 0）。`lock` は cursor_mutex を取れた時（第 5 版の `display_ns`）。
- 時刻は log が有効な時だけ取る（無効なら counter も clock も読まない）。

### 8. 受け入れ B（第 5 版の 8 を置き換え）

- 分担・場面（App Home 5 回、drag 10 秒、Music・Settings の起動）は第 5 版のまま。閾値は 16.7ms（1 refresh）。
- **S1 位置だけ**: (1) path=fast の割合（像の変化の後の各 image の最初と epoch の変化の後を除いて全部） (2) 呼び出し→ioctl（`done`）の p99 と最長 ≤ 16.7ms (3) 入力が続く間の `done` の間隔（compositor の呼び出しの間隔が 16.7ms 以下の組）の p99 ≤ 16.7ms。
- **S2 像の変化**（形が交互に変わる）: (1) 形が変わった present の `call_ns` から、次に位置を反映した ioctl（どの path でも、DROPPED でない行）の `done` まで（compositor が copy の fence の NOT_READY で present の前に戻る止まりもここに入る）の p99 ≤ 33.4ms (2) 入力が続く間の `done` の間隔の p99 ≤ 33.4ms (3) G（path=worker の `fence − publish`）の p99 ≤ 16.7ms (4) 像が安定すれば image の数以内の通常経路の後に fast に戻る。
- **S3 animated**（毎コマ形が変わる）: (1) 続く 2 つの SHOWN の ioctl の `done` の間隔の p99 ≤ 33.4ms (2) DROPPED の数（全 present に対する割合）を記録 (3) G の p99 ≤ 16.7ms。
- 実機の S2・S3 は試験の client（`plan/ws141/tests/` の cursor の client: 毎コマ cursor の buffer を commit し、形を交互に変える）で起こす。どの判定も、WS213 の thread C の copied の gate（F1 (b)）の変更の前か後かを書く。
- 判定は Pi の log で Q1 が行う。外した時は内訳（`mutex`・`queue`・`lock`・`set→done`・G）で分け、`done − set` の p99 > 4ms なら B-k2、`mutex` の p99 > 2ms なら H-b。

### 9. WS213 との契約（第 5 版の 9 に足す）

- fast path は queue->mutex を取らない。EBUSY の lane への公開も swapchain_mutex だけ（queue->mutex は要らない、3）。
- fast path は `GPU_DISPLAY_QUERY` を出す（u1 の訂正）が、kernel の `output_query` は mutex を取らない。
- R の vkQueueSubmit は round trip の間 device->mutex を持つ（`queue.c` の `queue_attempt`）ので、fast path の (5) はその間待つ（H-b、`mutex` で測る）。fixture の u9 は「R が queue->mutex を持って止まっている間に fast path が終わる」を確かめ、device->mutex の待ちは H-b の計測に回す。

### 10. 試験（第 5 版の 10 の訂正）

- (u1) 位置だけの present は QueueSubmit も worker も通らず、SET・MOVE が 1 回ずつ（display の query は数えない）。
- (u5) EBUSY: worker で refresh の 1 周期ごとに再試行、新しい ticket が出たら DROPPED、上限で DROPPED（error にならない）。fast path の BUSY は native だけの job で lane へ、fence を待たない。
- (u8) op の間、device->mutex・swapchain_mutex・queue->mutex・display_mutex を持たない（偽の ioctl・偽の op の中から try-lock）。
- (u15) primary の PRESENT_SYNC が display_mutex を持っている間も cursor の op が進む（重大 1 (a)）。
- fixture は 2 つ: WSI（`wsi-swapchain.c` を取り込み、core の関数と platform を偽物にする）と display の op（`wsi-display.c` を取り込み、`ioctl` を `--wrap` で偽物にする）。中身の記録は `content.c` の単体。generator（u14）は作り直して生成物と比べる。

### 11. 段 B-k2（kernel、おそらく必要。今回は実装しない）

- 目的: kernel の `CURSOR_SET`・`MOVE` が、primary の `output_present` の settle・adoption の待ち（`display-flip.c:322-360`、`display-device.c:862-940`）の間 controller->mutex を待たない。
- 案: cursor の状態（`controller->cursor`、`cursors[]`、`keys[]`）を controller->mutex から cursor 専用の lock に移す。flip の state への公開（`bcm2711_display_flip_cursor`）は既に flip の spinlock の guard なので、mutex の外で呼べる。`cursor_owner` の判定（`controller->owner`・`session->lease`）は claim・release と競るので、lease の世代を cursor の lock の下に写すか、owner の参照を cursor の lock でも読めるようにする。`cursor_reset`（claim・release）は両方の lock を controller->mutex → cursor の lock の順で取る。
- 起動の条件: 受け入れ B の S1 で `done − set` の p99 > 4ms（ほぼ確実に起きる見込み、review-B5 重大 1）。担当と時期は Q1 が決める。

### 12. 未決（Q1 に）

- 上限の 1 秒（EBUSY の再試行の打ち切り）でよいか。
- 未確認のまま: kernel が submit の外で group の image に書く経路、decoder が session ごとか全体か（G に効く）、hover・drag で client が cursor を変えるか、実機の時間。

### 13. B-u の実装（2026-10-12 P2、agent/p2 3ed1225c1・651dcadc9 ほか）

- **libvulkan**: `content.c`（新規。command buffer の表・secondary の合算・空にする・受理の時の加算）、`internal.h`（`vulkan_image.content_serial`、`VkDevice_T.content_epoch`、`VkCommandBuffer_T` の表）、`commands.c`（Begin と `command_buffer_initial` で表を空に）、`queue.c`（`queue_attempt` の受理の分岐で `vulkan_content_commit`、sparse は除く）、`tools/maintain-commands.noct` と作り直した `commands-generated.inc`（6 command と ExecuteCommands の 7 つの hook、数の確かめ）、`wsi-internal.h`（`present_cursor` の op・outcome・times・`surface->cursor_ticket`、旧 `position` の op を削除）、`wsi-display.c`（op `display_present_cursor`、`plane->frame` の atomic の store）、`wsi-swapchain.c`（lane・route・fast path・EBUSY の再試行・`copied_*`・acquire の `image_available` の省略・log）。`include/uapi/gpu-display.h` は CURSOR_SET の注記に一時的な EBUSY を足しただけ（ABI は変えない）。HAL・kernel・toolchain は変えていない。
- **設計からの違い**: (1) cursor の lock は lease ごとでなく file に 1 つの `display_cursor_mutex`（全部の cursor の lease で共有、display_mutex と入れ子にしない）。Pi の出力は 1 つで、cursor の op は短いので、lease ごとの mutex の初期化・破棄の経路を増やさずに済ませた。出力が増えて cursor の op が互いに待つと分かれば lease ごとに分ける。(2) log の環境変数は cursor の swapchain の作成の時に読み、file の印（一度立てたら戻らない）にした（7 のとおり device.c は変えない）。(3) 位置の無い present の log の x・y は 0 と出る（op は前の位置を使う）。
- **確かめ（host）**: `sh plan/ws141/tests/vulkan-cursor-host.sh`（wsi-swapchain.c・wsi-display.c・content.c を取り込み、core の関数・platform・display の ioctl を偽物にした fixture。ordinary と ASan/UBSan、log の off（0 行）と on（21 行、書式の確かめ））PASS: u1（位置だけは submit・copy・fence の待ち無しで呼び出しの thread で SET）、u2（primary の fence が止まっている間に cursor の copy が出る）、u3・u4（形の変化の copy の fence を止めた間に次の image が fast で出て、後から来た古い ticket は DROPPED、その image も次からは copy しない）、u5（BUSY は lane で refresh ごとに再試行し fence を待たない、新しい ticket が出たら DROPPED、上限 1 秒で DROPPED で error にしない、display の op の EBUSY・SET の失敗・MOVE の失敗）、u6（oldSwapchain の新しい chain も surface の ticket を続ける）、u7（表）、u8（op の間 swapchain・device・queue の mutex、display の op の ioctl の間 display_mutex が空いている）、u9（queue->mutex を他の thread が持っていても fast path が終わる）、u10（GPU の semaphore を待つ未変更の image は copy 無しの submit）、u11（cursor の chain の acquire は `image_available` を呼ばない）、u12（log）、u13（位置の無い present は前の位置）、u15（display_mutex を他が持っていても cursor の op が進む）。u14: `sh plan/ws141/tests/vulkan-cursor-generator.sh`（generator で作り直して commit 済みの生成物と一致、hook 7 つ）PASS。
- **S2・S3 の試験の client**（Q1 の判断 (a)）: `userland/tests/wlshm` に `--cursor-every=N`（`--cursor` と一緒に。pointer が窓の中にある間、窓の N コマごとに塗りの四角と枠だけの四角を交互に cursor の surface に commit。1 で毎コマ＝S3、30 で 0.5 秒ごと＝S2）。既定の動作は変えない。例: `wlshm --cursor=ff00ff00 --cursor-every=1 --frames=3600 --hold`。
- **build**: `make -j16 -o build/arm64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/p2-rpi4 "ZEDBSD_USER_PROGRAMS=wlshm libvulkan" build/p2-rpi4/bin/wlshm build/p2-rpi4/dynamic/libvulkan.so` と amd64（`config/ci/config-amd64.mk`、`BUILD=build/p2-amd64`）が warning 0。
- **未実施**: Pi の実機（受け入れ B の S1〜S3、log は compositor に `VULKAN_ZEDBSD_CURSOR_LOG=1`）、T1 の i915・Venus の回帰（cursor の plane は無いので、変わるのは表と加算と lane の分けだけ）。wlshm の `--cursor-every` は build だけ（実行は未実施）。

## (b) decoder の request が executor の準備を待つ件（2026-10-12 P3、計測の後に設計）

- 観察（1803317345 の kernel、App Home 中）: compositor の frame の record（vkBegin/EndCommandBuffer の同期の request）が 85〜149ms、`submit_lock_us` の最大 113〜168ms。controller の mutex は decoder（request ごと）と executor（1 歩の窓の外: pass の準備・finish・解放）が持つ。
- 計測を足した（a80ce65fa）: `hold_us decode=<最大>/op<opcode> step=<executor の 1 歩の窓の外の最大>`。次の配置の実機の数で、長く持つのが decoder の特定の op（resource の作成・map の memset など）か executor の準備（CPU の変換・upload）かを分け、そこを mutex の外に出す設計を書く（候補: 準備の draw ごとの区切りで待ち手に譲る、session の object の状態を session ごとの lock に分ける、重い CPU 作業（memset・変換）を mutex の外の所有物に対して行う）。

## (b) 第 2 版の設計（2026-10-12 P3、v3d.perf=1 の計測の後、review の前）

### 計測（place9・10 の boot、v3d.perf=1、log は scratchpad/p3g3/b10）

- perf の報告を mutex の外に出し（ffacaf9b2）、rpi4 の UART の console を止めた（51776cf11）後の数:
  - step の hold の最大は 1〜6ms（前は 48〜103ms）。
  - decode の hold の最大は 1〜19ms。
  - submit_lock の最大は 0〜9ms（前は 60〜90ms）。
- **(b) の元の症状（decoder の request が executor の準備を 60〜90ms 待つ）は、ほとんどが perf の報告そのもの（mutex の下で UART に約 1000 字を polling で出していた）だった。**
- 残るのは次の 2 つ。
  1. decode の 1 request が最大 11〜19ms、mutex を持つ。どの command かは分かっていない。perf の op は、request の最後の command（op137 = EnumerateInstanceVersion、stream の終わりの印）を出していたため。
  2. alloc の費用は、ほぼ全部が 0 埋め。allocator（pmem_us）は全部 0 だった。使い回す storage の view を毎回 0 で埋め直していて、2 秒の窓で reused 135〜735 個、23〜134MB、zero_us 12〜67ms だった。これは executor の step（mutex の下）で走る。
- compositor だけの小さな damage のコマ（窓の右上のボタンの hover など）は、submit→shown の中央値 14ms、入力→shown の中央値 35ms で、前の 28〜88ms・65〜112ms から改善した。

### 計測の追加（実装済み、agent/p3）

- decode の 1 request の中の command を 1 つずつ時間を測り、request の中で一番長い command の opcode と時間を、perf の `hold_us decode=<request の最大>/op<一番長い command> cmd=<その command の時間> step=` に出す（`vulkan-stream.c` の `execute_command`、`render-runtime.c`）。wrapper の `EXECUTE_STREAMS` は中の command の合計なので、数えない。
- 次の v3d.perf=1 の boot で、11〜19ms の command を特定する。それが資源の作成（vkCreateImage・AllocateMemory の map・memset）や map の ioctl 系なら、その command だけを mutex の外に出す設計を、次の版で書く。

### 0 埋めの見直し（設計、review の後に実装）

- `bcm2711_native_storage_create` は、空いた view を使い回す時にも、view の全部を `kern_memset` で 0 にしている（`native-storage.c` の `cache_take` の後）。
- 0 埋めが要らない種類（GPU か CPU が、読まれる前に読む範囲の全部を書く物）を、呼び出し元が「埋めない」と指定できる形にする（`bcm2711_native_storage_create_unzeroed`、または引数）。

| 種類（呼び出し元） | 読まれる前に全部書かれるか | 案 |
| --- | --- | --- |
| private の texture の変換先（`vulkan-native-draw.c` の host-mapped の canvas。1180×800 で 3.7MB） | はい（TFU か CPU が全体を書く） | 埋めない |
| snapshot の pixels（`native-storage.c:443`） | はい（fill が全体を書く。部分の更新はその後） | 埋めない |
| shader の code（`vulkan-native-draw.c:638`） | はい（program の大きさだけ書いて、その範囲だけ使う） | 埋めない |
| upload の chunk（`vulkan-native-draw.c:1256`） | 切り出した範囲は使う側が全部書く。残りは読まない | 埋めない |
| overflow の reserve（`vulkan-native-pass.c:671`、pass ごとに 256KiB × 4 = 1MiB） | PTB が書いた所だけを RCL が読む | 埋めない（Mesa の v3d も、BO の cache から再利用する時に埋めない） |
| tile の pool と tile state（`vulkan-native-pass.c:640・645`） | pool は PTB が書く。tile state は初期化が要るかを正本で確かめる | pool は埋めない。state は正本で確かめるまで埋める |
| bin・render・tile の list の image（`vulkan-native-pass.c:628-636`） | encoder が 0 の image を前提にしている（`native-bin.c:82`） | 埋める（今どおり） |

- 新しく確保する時（`bcm2711_buffer_create`）は、今どおり全部を埋める。allocator の page には、別の process の中身が残り得るため。埋めないのは、同じ space の中で使い回す時だけにする。
- 安全の議論: 埋めない view の前の中身は、同じ controller の別の pass か別の session の、GPU の内部の物（texture の変換・overflow など）。表の種類は、読まれる前に全部書かれるか、書かれた所だけが読まれる。そのため、前の中身は user にも GPU の出力にも出ない。
- 見積もり: 0 埋めの 23〜134MB/2s のうち、大きいのは canvas の変換先（1 回 3.7MB）と overflow（pass ごとに 1MiB）で、この 2 つで大半が消える見込み。
- 試験:
  - host（vulkan-device の fixture）: 埋めない種類が使い回しで 0 にされないこと、埋める種類は今どおり 0 になること、新しい確保は 0 になること。
  - Pi: `v3d: alloc` の reused の zero_us、prep、Settings の hover の submit→shown。

## (b) 第 2 版への review（[review-b2](review-b2.md)）と Q1 の判断の反映（2026-10-12 P3）

- **Q1 の判断**:
  - (1) 今すぐ実装してよいのは、overflow・tile の pool・upload の chunk・shader の code。
  - (2) TFU が書く物（変換先・snapshot）は、H1 の (a)（cached view に前の使用者の session の印を付け、同じ session の時だけ埋めない）を先に入れる。安全（別の process の画素が見える経路）を最優先にする。(b)（padding の実機の確かめ）は T1 の後の候補。
  - (3) 計測は M3 のとおり。
- **実装（(1) と (3)）**:
  - `bcm2711_native_storage_create_as(space, bytes, fill, storage)` を足した（`BCM2711_NATIVE_STORAGE_ZEROED`／`_WRITTEN`。H2: 呼び出し元ごとの明示の引数）。今の `bcm2711_native_storage_create` は ZEROED。WRITTEN は、使い回しの時だけ 0 で埋めない。新しい確保は今どおり埋める。
  - WRITTEN にしたのは 4 つ。code（`bcm2711_native_program_upload`）、upload の chunk（`vulkan-native-draw.c` の pool の chunk）、tile の pool、overflow の reserve（`vulkan-native-pass.c`）。
  - ZEROED のままの物: defaults、meta と独立の draw の upload、大きな upload、変換先、snapshot、list の image、tile state。
  - M1: GPU に渡す前の全体の clean は、今どおり呼び出し元で行う。加えて、cache に戻す時（`cache_put` の受け入れの後）に view の全体を clean する。cache に dirty な line を持ち込まないため。
  - M2: 埋めない使い回しは別に数える（`bcm2711_buffer_kept`、`v3d: alloc ... kept= kib=`）。overflow を controller に 1 つの reserve として worker が持つ代案は、reserve の寿命と recovery・drain の扱いを設計してからにする（この版は pass ごとの reserve を埋めないだけ）。
  - L3: uniform が 0 個の時の 4 byte の placeholder に、0 を 1 word 書く。
  - L1 の訂正: list の image を埋める理由は「encoder が 0 の image を前提にしている」ではない。bin は encoder が全部を書き、render と tile は encoder が全体を memset する（`native-pass.c:174-184`）。埋めるのは冗長だが、この版は変えない。
  - M3: perf の `hold_us decode=<最大の request>/cmds<その command の数>/op<その中で一番長い command> command=<窓の中で一番長い 1 command の時間>/op<その opcode> step=`。preempt と IRQ で wall の時間が膨らむ外れ値に注意して読む。
- **試験**:
  - vulkan-device の fixture: draw の storage の全部を 0xA5 で埋めて cache に戻し、作り直すと、defaults は (0,0,0,1)×16 で、code 以外の storage の先頭に目印が残らず、code の使い回しが kept に数えられる。単独の storage で、WRITTEN は目印を保って kept が 1 増える、ZEROED は 0 になる、不正な fill は EINVAL になる。
  - v3d-hardware・vulkan-stream・display・stage は PASS。
  - pass の pool の chunk の経路（pool の draw）は、この fixture では通らない（独立の draw だけ）。
- **(2) の残り**: 変換先と snapshot の H1 (a)。draw は session を持たないので、印の元（job の session の番号）を draw の作成まで渡す必要がある。次の段で設計して入れる。

## (b) 第 2 版 (2): 変換先と snapshot の session の印（review-b2 H1 (a)、2026-10-12 P3）

- **印**: render の session ごとに、controller の中で再利用しない 0 以外の番号（`bcm2711_render_session.owner`、open の時に `controller->owners` を進めて付ける）。
- **作る側**: job の step は、pass・clear・image の copy を組み立てる間だけ `space->builder` にその session の番号を置き、組み立ての直後に 0 に戻す。draw は executor だけが組み立てるので、他の storage の利用者がこの値を見ることはない。この値の持ち方は暗黙の状態だが、5 つの作成の入口（pass・clear・image・meta の draw・draw）と host 試験の引数を変えずに済ませるための選択である。
- **storage**: 新しい種類 `BCM2711_NATIVE_STORAGE_OWNED` を足した。cached view を 0 で埋めずに渡すのは、view に付いた前の使用者の印が `space->builder`（0 以外）と同じ時だけで、それ以外は埋める。storage は `owner`（OWNED なら builder、他の種類は 0）と `stale`（埋めずに渡したか）を持つ。release で view の印を storage の owner にしてから cache に戻すので、ZEROED と WRITTEN の storage を戻すと印は 0 になる。大きな run は、同じ大きさの中で同じ印の物を先に取る（owner が 0 の時は今どおり最初の一致で止める）。1 page の list は検索しない。
- **変換先**（private な変換）: OWNED にした。その draw と、同じ pass の後の draw（同じ session）だけが読む。この経路を通るのは、次の場合（L5）。
  - source が host-mapped の時。
  - snapshot の上限が 0 の時。
  - snapshot の cache が使用中の物で一杯の時。
  - 他の session が使用中の snapshot に当たった時。
  - meta の draw（blit）。
- **snapshot**: pixels を OWNED で作り、stale なら `snapshot->owner` に session を残す。snapshot は buffer を鍵に session をまたいで共有されるので、owner が 0 でなく今の builder と違う時は次のように扱う。
  - 使用中なら ENOTSUP（private な変換に回す）。
  - 使っていなければ、pixels の全体を 0 にして clean し、owner を 0、valid を false にして、全体を変換し直す（TFU なら TFU、そうでなければ CPU）。
  - CPU の全体の変換は texture の bytes の全部を書く（padding も 0）が、page の残りは書かないので、印は消さない。
  - snapshot の画素は、作った session の後に、他の session の変換や部分の更新でも書かれる（L5）。そのため evict・drain で pixels を cache に戻す時は、印を 0 にしてから戻す（`snapshot_release`、review の M1）。scrub でも pixels の印を 0 にする。
  - 使用中の snapshot が他の session に当たった時の ENOTSUP（L4）が起きるのは、他の session の draw が使っている間、つまり失敗して retire していない pass が users を持ったままの時（quarantine）だけである。後の private な変換は draw の予算（`available`）を使うので、大きな canvas では ENOMEM で job が失敗し得る（安全の問題ではない）。
- **信頼の単位**: 印は render の fd の open ごとに付く。fork や SCM_RIGHTS で fd を共有した process は同じ session になる。これは Linux の DRM の file と同じ信頼の単位である（libvulkan は O_CLOEXEC で開く）。別の open は、同じ process の中でも別の印になる。
- **安全の試験**（vulkan-device の fixture、`WS141 session marks ... PASS`）:
  - session 7 が 0xA5 で残した page は、session 8 と builder 0 には 0 で渡る。印の無い view は session にも 0 で渡る。同じ session（9）には 0xA5 のまま渡り、kept に数えられる。
  - 同じ大きさの run が 2 つあれば、自分の印の付いた物を先に取る。ZEROED で戻すと印が消える。
  - session 9 が TFU で作る snapshot は印 9 を持ち、page の末尾に 0xA5 が残る。session 9 はそのまま使う。session 10 は、9 が使っている間は ENOTSUP。使い終わった後は全体が 0 になり、全体の変換が数えられ（fills +1）、印は 0 になる。
  - CPU の変換の後も印は残り、session 10 が使うと 0 にしてから変換し直す（先頭の画素は変換した値、page の末尾は 0）。
- **review（6bd0ddf2f）への対応**:
  - M1: 上の `snapshot_release` と scrub の修正。試験を足した。session 9 が作った snapshot を、session 10 が generation を進めた後に変換し直す。evict で cache に戻した page を session 9 の次の OWNED が取ると、0 で渡る。修正を外すとこの試験が落ちることを確かめた。
  - L1: 試験の `job_execute` が、step の後に毎回 `space.builder == 0` を確かめる（成功と失敗の両方の経路）。v3d-hardware の試験で、2 つの render session の印が 0 でなく互いに違うことを確かめる。
  - L2: 試験を足した。host-mapped の source の変換先は、同じ session（9）なら 0xA5 のまま stale で渡り、別の session（10）なら全体が 0 で渡る。WRITTEN は印 9 の view をそのまま取り（kept）、release した後は印が 0 になる。
- **確認**: kernel の rpi4 の build は warning 0。host 試験は vulkan-device・v3d-hardware・vulkan-stream・display・stage・native-state・native-pass が PASS。実機の計測（`v3d: alloc ... kept=` の変化と zero_us の減り）は未実施。
- **明記した残件**:
  - L3: H1 (b)。TFU が UIF の padding を書くか、TMU が padding を読むか（線形 filter と REPEAT・CLAMP）を実機で確かめる（T1）。読む場合は、同じ session の古い byte が端の画素に出得る。これは他の session への漏れではない。この確かめは cleared の条件ではなく、残件とする。
  - overflow の reserve を controller に 1 つ持つ案は、M2 の代案として残す。

## 計測: display の時刻の cadence（WS213 の依頼、2026-10-12 P3）

- 目的: cursor の速さを display 側の時刻で測る（WS213）。
- 記録: `bcm2711_display_flip_note(display, kind)` が、次の 3 種類の出来事の時刻を記録する。間隔は 1 window に最大 256 個を μs で持つ。flip の guard の下で記録する。
  - CURSOR_SET と CURSOR_MOVE（`display-device.c`、mutex を外した後）。
  - flip の adoption（`submit_frame` の wait_adoption の成功の後）。
- 出力: 2 秒ごとに 1 行を ring に出す。v3d.perf=1 の時だけで、boot line は最初の出来事の時に 1 回読む。
  - 書式: `bcm2711: cadence ms=<window> per_s/min/med/max_us set=a/b/c/d move=... flip=...`
  - 報告する 1 つの呼び出しが、guard の下で window を static な複写に移す（`cadence_reporting`）。整列と kern_logf は guard の外で行う。cursor の 2 つは display の mutex の外で呼ぶ。
  - 既存の `flip perf` の行は今までどおり（v3d.perf に依らない）。
- 試験: display-flip の host 試験 `cadence_report`。
  - v3d.perf が off の時は何も持たない。
  - 8 ms ごとの move と 4 回の set を 2 秒流すと、`set=2/10000/20000/30000 move=125/8000/8000/8000 flip=0/0/0/0` の 1 行になる。
  - window は空から始まり直し、間隔は window をまたいでも測る。
- 確認: display・stage・vulkan-device の host 試験が PASS、kernel の rpi4 の build は warning 0。実機は未実施。

## HAL arm64 の物理 page の allocator: 呼び出し元の監査（2026-10-12 P3、変更はしない、Q1 に相談）

- 実装（`src/hal/arm64/page.c` `alloc_ram`）: 8GiB 分の bitmap の first fit。`first_free_candidate`（そこより下は全部使用中）から上へ、**1 page ずつ** 候補を進め、候補ごとに alignment・boundary・空きの長さを測る。全部 **IRQ を止めたまま**（CPU は 1 つ）。zone・上からの配置は無い。
- 呼び出し元（arm64 で動くもの）: 1 page: `vm_private_page_alloc`（`vm.c:6374`、user の page・fault ごと）、`space.c:76`（page table）、`tmpfs-pages.c:295`。連続の大きな run: bcm2711 の `buffer.c:62`（GPU の storage・snapshot・import の run、`kern_pmem_alloc_limited`、上限は V3D の PA 35 bit＝実質なし、display の cursor 等は 0x3fffffff）、`buf.c:1745`（buffer cache の slab・line）、`disk.c:2695`（async I/O の領域、起動時）、`io.c:352`（I/O pool、起動時）、`ufs.c:3064`（journal image）、`audio.c:2048`（ring）。上限付きの DMA: `drivers/generic/dma.c:384`（xHCI は brcmstb の PCIe で 31 bit かつ inbound 3GiB→2GiB 未満、GENET・audio は 32 bit）、`rpi4-firmware.c:147`（firmware の buffer、上限付き）。
- 問題: (1) 大きな連続の要求は、使用中の page を 1 つずつ越えて走査するので、使用量に比例した時間 IRQ が止まる。perf の `alloc` は submission あたり平均 0.4〜2.2ms（実機 1803317345）で、GPU の storage の確保（`native_storage_create`→`buffer_create`）の費用がほぼこれ。(2) 1 page の確保も GPU の大きな run も DMA の上限付きの run も、全部下から first fit なので、GPU の大きな run が DMA の上限の下（2GiB 未満）を占め、user の page と混ざって断片化する。長く動かすと上限付きの確保や大きな run が ENOMEM になり得る。
- 案（どれも `hal.h` の API は変えない実装の変更、Q1 の判断待ち）: **P1** 走査を 32 page（bitmap の 1 word）単位に: 全部使用中の word は 1 回で飛ばし、空きの長さも word 単位で数える（IRQ の停止と `alloc` を 1/10 以下に）。**P2** 配置の方針: 上限の無い大きな run（例 64KiB 以上）は上から first fit（上の cursor を別に持つ）、1 page と上限付きは今どおり下から。**P3** 走査を区切って IRQ を開ける（世代の数で途中の変更を検出してやり直す）。P1 が最小で効果が明確、P2 は断片化の実測（`hal_get_memstat` の free と最大の連続の空き）の後。

## 段（第 4 版）

| 段 | 内容 | 確かめ |
| --- | --- | --- |
| M | 計測（済み・続き）: request の待ちと実行、解放の内訳、markers_max、submit の mutex の待ち | 実機の数 |
| A0 | userland: cursor-plane.c の acquire の前の fence の確認、EBUSY の候補の確認、libvulkan の WSI の commit の同期・early の確認と記録 | host build、T1 |
| A1 | executor・session ごとの列・1 歩の step・表示の session の優先（execution ごとの印）・round-robin・execution（heap）・session ごとの submitted・executed・required・背圧（RESERVE の EAGAIN、件数・bytes・全体・最低の枠・session の数）・PARKED を execution の entry に・状態と取り消し・公開の順序・旧来の拒否・wait_idle の EOPNOTSUPP。executor は歩の間 mutex を持つ（効果は A2 から） | host（決定的な fixture） |
| A2 | 窓（3 区分、window の判定、遅延解放の list と fault の quarantine、map の待ち、pte_waiters の世代、mutex の順番渡し、display の generation の guard） | host |
| A3 | lazy map（GPU の VA の map を executor の窓の前へ、import・blob の作成が窓を待たない）。最初に V3D の MMU が無効な PTE を TLB に持つかを確かめる | host、実機（受け入れ A） |
| B-k | kernel の CURSOR_SET の鍵（中身が同じなら写さない）。済み（ef5e651bc） | display の host 試験 |
| B-u | libvulkan: cursor の lane、ticket と display の op（lease の cursor_mutex、display_mutex を取らない＝旧 B-l の libvulkan の半分）、位置だけの fast path、EBUSY の再試行、中身の記録（hook・serial・epoch）、acquire の `image_available` の省略、log（「(B) 第 6 版」） | host（WSI・display の op の fixture）、T1、実機（受け入れ B） |
| B-k2 | kernel: CURSOR_SET・MOVE を `output_present` の adoption の待ちの controller->mutex から外す（「(B) 第 6 版」の 11、おそらく必要、Q1 が割り当てる） | display の host 試験、実機（受け入れ B） |

## 試験（M5）

- vulkan-device の host 試験に、executor の 1 歩（`executor_step`）と decoder の 1 歩（`worker_step`）を外から順に呼べる**決定的な fixture** を作る（本物の thread では interleave が再現しない）。場面: (a) 窓の間の decoder の vkFreeMemory・vkDestroyImage と別 session の close（遅延解放、EIO にならない）(b) TFU と CL の隙間で PTE が変わらない (c) RESERVE の EAGAIN の背圧（件数・bytes・全体・最低の枠）、OOM は細工の stream だけ (d) marker の枠が他の session で埋まっても compositor の submit が戻る (e) cursor plane の fence が NOT_READY で plane が閉じない（compositor の host 試験）(f) marker の完了の直後の vkResetFences・vkResetCommandBuffer・vkUpdateDescriptorSets が成功 (g) fault で PARKED が EIO で終わり render_fail が届く (h) checked stop と保留の execution (i) 細工した stream（1 request に多数の QueueSubmit、同じ session の複数 request）(j) N の signal を N+1 が wait する semaphore の鎖 (k) 旧来の経路の QueueSubmit が EOPNOTSUPP。
- 実機: compositor の queue-submit が GPU の時間から外れる、App Home・drag の間の cursor と入力、窓の間の解放（`views` の遅延解放の数を perf 行に）。i915・Venus は T1（A0 の userland の変更の回帰）。
- 第 4 版で足す場面（N9 ほか）: (l) 窓の間の map の ioctl（GPU_BLOB_CREATE・RESOURCE_IMPORT）が待って成功し EIO が userland に届かない (m) 窓の間の解放＋fault→reset の成功 (n) compositor が上限に当たっても RESERVE の EAGAIN で待ち、終わらず plane も閉じない (o) cursor の copy の fence が NOT_READY のまま image の数を超えて続いた後に回復 (p) map の ioctl が続いても executor が進む・executor が続いても decoder が進む (q) 2 つの session の列の round-robin と表示の session の優先 (r) early commit の client と compositor の poll で、compositor は client の marker の完了の前に buffer を使わない(s) stop の取り消しが他の session に影響しない。

## 未解決

- GPU の実行中に無関係の VA の PTE を編集して TLB を flush する安全（review-2 の未確認）: 第 3 版は窓の間に PTE を編集しないので不要。
- 準備の区間（CPU の変換、per-draw の upload）は executor の mutex の下に残る。per-draw の upload の削減は ws141-p013 で先に行う（2026-10-12 Q1）。
