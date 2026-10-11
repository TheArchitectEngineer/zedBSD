<!-- awesome-plan project=zedbsd record=ws141-p016 -->

# ws141-p016: GPU の実行中に ioctl と cursor を止めない（worker の mutex、cursor の plane の GPU 無しの更新）

Parent: [WS141](../ws.md)
Status: planning（設計の第 2 版。第 1 版は review-1 で作り直し、[review-1](review-1.md)）
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

## (A) 第 2 版: QueueSubmit の実行を decoder の FIFO から切り離す

### 全体

- worker（今の FIFO の thread、以下 decoder）と別に、controller に 1 つの **executor** の thread を置く。executor は「準備済みの submission」（以下 execution）の queue を順に実行する。
- decoder の QueueSubmit: 今どおり decode・`check_admission`・`prepare_submission`（検証、command buffer・sync・fence の retain、native の job の作成、全部か無し）を mutex の下で行い、成功したら submission を **arena から heap に移して** execution として executor の queue の末尾に入れ、**reply に VK_SUCCESS を書いて request を終える**（vkQueueSubmit はここで戻る）。prepare の失敗は今どおり同期の error。
- executor: execution ごとに mutex を取り、今の `run_submission` の流れ（native の job の実行、sync・fence の publish、`release_submission`）を行う。GPU の実行の間だけ mutex を外す（下の「executor の窓」）。
- DEVICE_LOST: execution の失敗は今の `publish_failure`（sync・fence の status）と `bcm2711_render_worker_fault`（controller の uncertain）で後の command と fence の待ちに伝わる。vkQueueSubmit の戻り値では伝えない（Vulkan の非同期の意味どおり）。

### 順序

- execution は全 session 共通の 1 本の queue に、decoder が入れた順（＝各 session の vkQueueSubmit の順）で並ぶ。各 execution に単調増加の番号 `sequence` を付ける。
- **fence の marker**（`job_commit` の queue_marker）: decoder が marker に達した時、その時点の最後の execution の番号 `required` を記録し、`executed < required` なら marker を **保留の list**（controller の、hardware.guard の下）に移して decoder は次へ進む。executor は execution を終えるたびに `executed` を進め、`required ≤ executed` の保留の marker を順に `finish_request` する。fence の完了の順序は今と同じ（同じ timeline の marker は decoder の順＝required の順）。
- **idle の問い合わせ**（vkQueueWaitIdle・vkDeviceWaitIdle、`vulkan-device.c:470-510`）: 同じく `required` を記録し、未完了なら request ごと保留し、executor が `finish_request` する。reply（と trailer）は保留を解く時に executor が書く。1 つの session の decoder の transaction は context の mutex で 1 つずつなので、保留の間その session の decoder は次を出さず、context の timeline の順は崩れない。他の session の decoder は進む。
- execution の実行の順は submission の順なので、image の layout の検査と公開（execution の時だけ、`vulkan-native-image.c`・`vulkan-barrier.c`）は今と同じ順で成り立つ（prepare は layout を見ない）。

### 保留中の submission をまたぐ状態（今は 1 つしか無い前提の所）

- **binary semaphore**（`prepare_sync`）: 今は `sync->pending` と `reserved != completed` を EINVAL にしている。複数の execution が保留中の時、「N が signal して N+1 が wait」（acquire→render→present の普通の鎖）を受けるため、予約（`signals_reserved`・`waits_reserved`）を保留中の submission をまたいで数える形にし、`pending` は「保留中の操作の数」にする。検査は予約の数どうしの関係（wait は既に予約された signal を 1 つ消費、signal は前の signal が wait で消費済み）だけで行う。
- **fence**: 保留中の fence を別の submission に渡すのは Vulkan の誤り。今どおり EINVAL（`pending` の fence を断る）。
- **command buffer**: `command->pending` の間の reset・begin・free は既に断る（`vulkan-command-buffer.c:69,114,185,243`、pool は `vulkan-command-pool.c:210`）。保留が長くなるだけで同じ。ONE_TIME_SUBMIT の INVALID は execution の時に設定（今どおり）。
- **image の layout・host の map**: execution の時だけ変わる。CPU が保留中の image の memory を map して書くのは Vulkan の誤り（kernel は hold で安全を保つ）。

### arena と旧来の経路（review-1 F2）

- submission は今 session の arena に置かれ、stream の次の command で `arena.used = 0`（`vulkan-stream.c:97`）で巻き戻される。execution に移す時に必要な物（queue・fence・device の object、actions、syncs、jobs）を heap に写し、arena を指さない。
- 旧来の同期の `GPU_COMMAND`（`runtime_command`、`render-runtime.c:117-178`、呼び出しの thread で stream を実行）では QueueSubmit を EOPNOTSUPP で断る（bcm2711 は `GPU_CAP_NOTIFICATION` を公開し、libvulkan は `GPU_COMMAND_SUBMIT` を使うので、通常の利用は無い）。旧来の経路は executor と mutex の窓に関わらない。

### executor の窓（review-1 F3・F4・F5）

- executor は mutex を持って execution の pass を準備し、`hardware.window = true`（guard の下、既に立っていたら trap）にしてから mutex を外し、**GPU の実行だけ**（admission・起動・IRQ の完了待ち・cache の clean・性能 counter の読み取り）を行う。終わったら guard の下で `window = false`・`waitq_wake_all(&hardware.idle)`、それから mutex を取り直す。
- `pass_run` の中の共有の物への書き込み（`memory_written`、`output->synced_*`、`snapshot_filled`、`target->layout`、`space->deferred`・`view->references` の読み）は「出力の公開」として **mutex を取り直した後** に移す（review-1 F3 の一覧、`vulkan-native-execute.c:109-156,371`）。性能の perf の加算（`vulkan-native-job.c:266-284`）も後へ。
- 窓の間に PTE を編集したい ioctl（`memory_map`・`memory_release`・`memory_begin`）: **mutex を取る前に** mutex なしで窓の終わりを待ち（`hardware.idle`、guard の下で条件を確かめる）、mutex を取った後に再確認し、立っていたら mutex を外して待ち直す（mutex を持ったまま眠らない、F4）。executor は PTE の待ち手（`hardware.pte_waiters`）がいれば次の窓を開く前に mutex を一度譲る。`memory_release` の参照の減算と quarantined は今どおり ready の判定より前（待つのは unmap・flush の前）。待った後に faulted なら今と同じ EIO と quarantine。
- 待ちの上限は実値: bcm2711 の job の監視は `JOB_PROGRESS_WINDOWS`(4)×0.5s を job の段（TFU・bin・render）ごとに重ねた値（`v3d-job.c:20-21`）。fault の全ての経路（IRQ の fault、`record_failure`、`bcm2711_render_worker_fault`、`render_fault`）で `hardware.idle` を wake_all する。待ち手は faulted・!ready を先に見る（EIO が優先）。
- `render_write`（`render-device.c:432`）など窓を待たずに buffer の generation・write log を変える ioctl は、generation と ring の更新を buffer の guard の下に移す（executor の出力の公開と並べるため）。

### reset・recovery・close

- close: common の close は drain（`bcm2711_render_worker_drain`）で session の request を全部待つ。第 2 版では drain は「その session の保留中の execution と保留の marker・idle」も待つ（session の pending の数に execution を含める）。
- fault: executor の execution の失敗は今の fault の流れ。decoder の queue の残りは今どおり cancel、executor の queue の残りも cancel（job を実行せずに dispose、fence・sync は DEVICE_LOST）。
- reset（`render_reset`）: sessions が 0 で、executor の queue が空で、窓が無い時だけ（EBUSY）。
- suspend の op は無い。

### libvulkan の側（private wire の意味の変更）

- 変えるのは「QueueSubmit の reply が execution の完了を意味しない」点だけで、wire の形は同じ。libvulkan は今も fence・semaphore・WSI の present の待ちを kernel の completion（poll）と marker で行っているので、コードの変更は無いか小さい。確かめる所: `queue.c` の QueueSubmit の後の software の semaphore の payload の扱い（今は reply の後に completed と見なしていないか）、WSI の present（`wsi-swapchain.c`）が submit の reply を GPU の完了と見なしていないか、`vkQueueWaitIdle`・`vkDeviceWaitIdle` の reply を待つ所。i915・Venus の kernel は reply の意味を変えないので、libvulkan の変更は bcm2711 の経路でも i915 でも正しい形（「reply は完了を意味しない」前提で書く）にする。

## (B) cursor の plane（review-1 F6・F7・F8 を反映）

- (A) の後に。compositor の main loop が submit で塞がらなくなれば、cursor の plane の present も frame の間に回る。残るのは WSI の present の copy の job が executor の queue の後ろに並ぶこと。
- fast path（position だけ、GPU を通さない）は、同じ lease の present の worker に未処理が無い時だけ（F6 の追い越しを防ぐ）。他は worker が順に処理し、software の待ちだけの present は GPU を通さず即時。
- kernel の「同じ元なら写さない」の鍵は `buffer->identity`＋offset＋幅・高さ・stride・format＋generation（atomic に読む）、`cursor_reset` で消す、host_mapped の buffer は常に写す（F7）。
- libvulkan の swapchain の image を display に import する経路（export・大きさ・placement・usage・oldSwapchain での共有、acquire への返却・fence・sequence・software の payload の消費）は (B) の設計の別の版で列挙する（F8）。p015（UIF）では cursor の image は LINEAR のまま。

## 段（第 2 版）

| 段 | 内容 | 確かめ |
| --- | --- | --- |
| M | 計測（kernel f5703c6b9＋b17a753d4: request ごとの FIFO の待ちと実行、解放の数と時間）。実機で App Home・drag・hover | 実機の数 |
| A1 | executor と execution（heap）、marker・idle の保留、旧来の経路の QueueSubmit の拒否、semaphore の予約の保留中の数え方 | host: 下の試験 |
| A2 | executor の窓（GPU の実行だけ mutex を外す、出力の公開を後へ、PTE の待ちを mutex の外で） | host |
| A3 | libvulkan の確かめと直し（reply を完了と見なす所） | host、T1（i915・Venus の回帰） |
| B | cursor の plane（別の版の設計で） | — |

## 試験（review-1 F9）

- kernel の host 試験（vulkan-device）の fixture を作り直す: 今の mutex の assert（再帰・競合を止める）と `waitq_sleep` の型を、2 つの thread（decoder と executor）を模せる形に。場面: (1) 2 つの QueueSubmit が続けて reply し、execution が順に走る、(2) 保留の marker と idle が execution の後に完了する、(3) N の signal を N+1 が wait する semaphore の鎖、(4) 窓の間の resource の作成・破棄・map（待って成功、窓の間に PTE が変わらない）、(5) fault で保留が全部 DEVICE_LOST で終わる、(6) close の drain が保留の execution を待つ、(7) 旧来の経路の QueueSubmit が EOPNOTSUPP、(8) arena の巻き戻しの後も execution が正しい。
- libvulkan: QueueSubmit の reply の後に完了を前提にしない（fixture の kernel が reply を早く返し、完了を遅らせる）。
- 実機: compositor の queue-submit が GPU の時間から外れる、cursor・入力が frame の重さに依らない、App Home・drag・hover の体感。i915・Venus は T1。

## 未解決

- executor を thread にするか、decoder の thread の中で「GPU の実行の間に decoder の request を処理する」協調にするか。thread の方が単純（この版は thread）。
- 準備の区間（CPU の変換、最大 20ms／submission）は executor の mutex の下に残る。
