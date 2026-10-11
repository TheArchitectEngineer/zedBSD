<!-- awesome-plan project=zedbsd record=ws141-p016 -->

# ws141-p016: GPU の実行中に ioctl と cursor を止めない（worker の mutex、cursor の plane の GPU 無しの更新）

Parent: [WS141](../ws.md)
Status: planning（設計の第 1 版、design-reviewer の前）
Phase disposition: normal
Queue: P3（Q1 が割り当て、2026-10-12）

## 由来

- 2026-10-12 ユーザー（Q1 経由、kernel 237d26834 の実機）:「App Homeへの遷移がまだ数秒かかっています。ウィンドウドラッグは5fpsくらい。…アプリアイコンのポイントは1秒くらいマウス移動が2fpsに重くなります」。
- 2026-10-12 Q1:「(A) を先に: 並行の設計（mutex を外す区間、hold で守る object、job の queue の順序と fence の通知、reset・recovery・suspend・close との競り、mutex の下に残す物）を phase に書いて design-reviewer に掛け…その後に実装。(B) は cursor の像が変わった時だけ kernel が 16KB を写す形で、Guardrail の『内容が変わった時の一度だけの用意』に入るので可（位置だけの present は MOVE だけ）。i915・Venus は変わらないことを確かめて記録。」

## 問題（source で確かめた事実）

1. render の worker は 1 つの request の実行の全体（stream の decode、pass の準備＝CPU 変換・storage の確保・MMU の PTE、GPU の起動と完了待ち、解放）の間、controller の mutex を持つ（`render-worker.c` `bcm2711_render_worker_step`: `mutex_lock(&controller->mutex)` → `request->execute` → `mutex_unlock`）。
2. 投入の ioctl（`render-runtime.c` の command の submit、`runtime_ready` と `bcm2711_render_worker_submit`）も同じ mutex を取る。worker の queue は 8 個まで入る（`BCM2711_RENDER_RESERVATIONS`）のに、ioctl が mutex で待つので、vkQueueSubmit は実行中の job の終わりまで戻らない（p013 の compositor の log の queue-submit 中央値 約 200ms）。resource の作成・破棄・map など他の render の ioctl も、compositor・client を問わず待たされる。
3. GPU の実行中（`hardware.job_busy`）は `bcm2711_v3d_memory_map`・`bcm2711_v3d_memory_release` が `memory_ready` で EIO を返す（PTE の編集と MMU の flush を job の実行と並べない作り）。今は mutex で直列なので起きないが、mutex を外すとこれが表に出る。
4. cursor（p014）: compositor は位置だけの更新でも cursor の swapchain の画像を acquire して vkQueuePresentKHR する。libvulkan の WSI は present ごとにその画像（OPTIMAL）を native の cursor の画像（LINEAR）へ GPU の copy の job で写し、完了を待ってから `GPU_DISPLAY_CURSOR_SET`・`MOVE` を呼ぶ。この copy の job は frame の job と同じ queue に並ぶので、重い frame の間は cursor が動かない。

## (A) worker が GPU の完了を待つ間 mutex を外す

### 区間

worker の 1 つの request の中で、native の pass ごとに次の 3 つに分ける。

| 区間 | mutex | 内容 |
| --- | --- | --- |
| 準備 | 持つ | stream の decode と dispatch、pass の作成（`bcm2711_vulkan_native_pass_create` など）、CPU の変換、storage・view の確保、`bcm2711_v3d_memory_begin/end` の PTE の公開、**実行の窓の開始の印**（下） |
| 実行 | **外す** | `bcm2711_vulkan_native_pass_run`（job の admission、MMIO の起動、IRQ による完了待ち、cache の clean、性能 counter）。触るのは pass と job が所有する物と hardware の register・`hardware.perf`・`hardware.events` だけ |
| 解放 | 持つ | 実行の窓の終わりの確認、`bcm2711_vulkan_native_pass_release`、view の release、sync・fence の状態の更新、次の pass の準備 |

- 実装: `bcm2711_vulkan_native_job_execute` の pass の loop の `bcm2711_vulkan_native_pass_run` の前後で controller の mutex を外して取り直す。controller の pointer は既に引数にある。stream の decode の他の所（buffer の CPU の copy の `bcm2711_vulkan_buffer_copy_run`、barrier、sync の操作）は mutex の下のまま。
- worker の外側（`bcm2711_render_worker_step`）の mutex の取り方は変えない（request の最初と最後）。外すのは pass の実行の間だけ。

### 実行の窓（execution window）の印

- 新しい真偽 `hardware.window`（`hardware.guard` の spin lock の下で読み書き）。worker は mutex を持ったまま `window = true` にしてから mutex を外し、`pass_run` が戻った後、mutex を取り直す**前に** `window = false` にして待ち手を起こす（`waitq_wake_all(&hardware.idle)`）。
- 理由: `job_busy` は `pass_run` の中の admission で立つので、mutex を外してから admission までの間に ioctl が mutex を取って PTE の編集と MMU の flush を始めると、GPU の起動と重なる。窓の印を mutex の下で先に立てれば、ioctl は必ず窓を見る。
- 窓を下ろすのに mutex は要らない（spin lock だけ）。だから mutex を持って窓の終わりを待つ ioctl と、mutex を取り直したい worker は deadlock しない（worker は窓を下ろしてから mutex を待つ）。

### ioctl の側: PTE の編集は窓の終わりを待つ

- `memory_ready` を「窓または job_busy なら待つ、faulted・not ready なら EIO」に分ける。`bcm2711_v3d_memory_map` と `bcm2711_v3d_memory_release`（ioctl から呼ばれる時）と `bcm2711_v3d_memory_begin` は、窓または busy の間 `hardware.idle` の waitq で眠り（`hardware.guard` の下で条件を確かめる、起床は worker の窓の終わりと `finish_job`・fault）、起きたら条件を確かめ直す。mutex は持ったまま眠る（他の ioctl も待つが、worker の窓の終わりには mutex は要らない）。
- 待ちの上限: 窓は job の実行の期限（`CONFIG_GPU_JOB_EXECUTION_MS`）で必ず終わる（期限切れは fault にして窓を下ろす）。fault なら待ち手は EIO を返す（今と同じ結果）。
- PTE の編集を GPU の実行と並べる案（V3D の MMU の cache の flush を実行中に行う）は採らない（実行中の flush の安全を確かめる資料が無い）。

### hold で守る物と、mutex の下に残す物

- 実行の区間で worker が使う物は全部、準備の区間で hold 済み: pass・draw・job の root（job が所有）、出力と入力の view（`bcm2711_v3d_memory_retain`、snapshot の users）、prepared primary と command buffer と sync の payload（queue の参照、`vulkan-command-buffer.c:111`・`vulkan-sync.c:192`）。並行の ioctl が namespace から object を消しても、storage と payload は hold が外れる解放の区間まで残る。
- mutex の下に残す物: Vulkan の namespace（object の作成・破棄・参照）、`bcm2711_v3d_space` の VA の予約と view の list、native storage の cache と snapshot の cache、render と display の share の参照、session の状態。実行の区間ではこれらに触らない（`pass_run` の中で触らないことを実装で確かめ、host 試験で mutex を外した区間の中の呼び出しを検査する）。
- `hardware.perf`・`hardware.events`・`hardware.window`・`job_busy` は `hardware.guard` か worker だけが書く（今と同じ）。

### queue の順序と fence

- request は worker の FIFO の順に 1 つずつ実行する（変えない）。worker は 1 つなので、mutex を外している間に別の request が始まることは無い。
- 投入の ioctl は窓の間も mutex を取れるので、queue に入れてすぐ戻る（容量 8、満ちたら今と同じ EAGAIN→libvulkan の capacity の待ち）。fence・semaphore の完了の通知は今と同じく request の終わり（`finish_request`、mutex の外）。vkQueueSubmit は Vulkan の意味どおり非同期になり、待ちは vkWaitForFences・semaphore・present だけになる。
- idle の問い合わせ（`vulkan-device.c:500` の `job_busy` の検査）は worker の request として順に処理されるので、前の request の後に評価される（今と同じ）。

### reset・recovery・suspend・close との競り

- fault（IRQ・期限切れ）: `pass_run` の中で今と同じく `faulted` を立てる。worker は窓を下ろしてから mutex を取り直し、今と同じ fault の処理（`bcm2711_render_worker_fault`、quarantine）を mutex の下で行う。窓を待つ ioctl は起きて faulted を見て EIO。
- recovery（`render_reset`）: `sessions == 0` と mutex の下で行い、`bcm2711_v3d_hardware_reset` は `job_busy` で EBUSY を返す。窓の印も同じく EBUSY の条件に足す。session が 0 なら worker の request も無い（close は drain の後）ので、普通は窓は無い。
- close: common の close は `runtime_drain`（`bcm2711_render_worker_drain`）で session の request が全部終わるのを待ってから namespace を消す。drain は spin lock と waitq だけで mutex を取らないので、窓の間も待てる。close の他の mutex の区間は drain の後。
- stop（`bcm2711_render_worker_stop`）: spin lock だけ。実行中の request は最後まで実行し、queue の残りを cancel（今と同じ）。
- suspend: bcm2711 の render に suspend・resume の op は無い（`render-device.c`・`attach.c` に無いことを確かめた）。
- display device: 別の mutex。render の share の参照の増減は render の mutex の下（今と同じ）。

### 他の platform

- 変えるのは bcm2711 の kernel の driver（`render-worker.c`・`vulkan-native-job.c`・`v3d-memory.c`・`v3d-hardware.[ch]`・`v3d-job.c`）だけ。i915・Venus・llvmpipe の driver と libvulkan・compositor は変わらない（libvulkan は既に fence・semaphore で完了を待つ作りで、submit が早く戻るだけ）。

### 確かめ

- host: vulkan-device の試験に、実行の区間の間に別の thread から resource の作成・破棄・投入をする場面（pthread で worker を模す、`pass_run` を止める fixture）を足し、(1) 投入がすぐ戻る、(2) map・release は窓の終わりを待って成功する、(3) 窓の間に PTE が変わらない、(4) fault で待ち手が EIO、(5) drain が窓の間も待てる、を確かめる。
- 実機: compositor の log の queue-submit が GPU の時間から切り離される（数 ms）、Settings などの client の ioctl の待ちが減る、ユーザーの体感（drag・App Home）。

## (B) cursor の plane の位置だけの更新は GPU を使わない

- libvulkan（`wsi-swapchain.c`・`wsi-display.c`、cursor の plane の swapchain だけ）: cursor の swapchain の画像を LINEAR で作り（実装の選択で app には見えない、usage は今と同じく transfer dst・color attachment）、display に直接 import する（今の native の cursor の画像への GPU の copy をやめる）。
- present: wait の semaphore が全部 software で完了済み（acquire の semaphore だけ）なら、queue を通さずに呼び出しの thread で `CURSOR_SET`（その画像）＋`MOVE` を呼ぶ。GPU の job が signal する semaphore を待つ present（compositor が像を写した時）は、今の WSI の worker が GPU の完了を待ってから同じ ioctl を呼ぶ。
- kernel（`display-device.c` の cursor_set）: SET の元（storage の handle・offset と、その buffer の書き込みの世代 `generation`）を 4 つの cursor の slot ごとに記録し、同じ元の slot があれば写さずにその slot を使う（位置だけの present は 3 枚の swapchain の画像を回るが、どの画像も一度写せば以後は写さない）。違う元なら今どおり 16KB を写す（像が変わった時だけ、Guardrail の「内容が変わった時の一度だけの用意」）。
- UAPI は変えない（`GPU_DISPLAY_CURSOR_SET` は今も storage の handle を取る。LINEAR の swapchain の画像の import は今の display の import の経路）。
- 他の platform: cursor の plane は `GPU_DISPLAY_HAS_CURSOR` の出力（bcm2711）だけで、i915・Venus・Linux・FreeBSD の backend には無いので変わらない。

### 確かめ

- host: libvulkan の WSI の fixture で、位置だけの present が queue を通らず SET・MOVE の ioctl だけになること、GPU の semaphore を待つ present は worker を通ること。display の host 試験で、同じ元の SET が写しを省き、世代が変われば写すこと。
- 実機: app の icon の hover・drag の間の cursor の present の間隔（`KWL CURSOR_PLANE present` の log）が frame の重さに依らず入力の間隔に従う。

## 段

| 段 | 内容 | 確かめ |
| --- | --- | --- |
| A | worker の実行の窓と ioctl の待ち | host（上）、arm64 warning 0、実機 |
| B | cursor の LINEAR の swapchain・GPU 無しの present・kernel の SET の省略 | host（上）、実機 |

## 未解決

- 準備の区間（CPU の変換が 1 submission あたり最大 20ms）も mutex の下に残る。p015（UIF）と F の案で変換が減れば短くなる。準備自体を mutex の外に出す案は、namespace の参照の hold を広げる必要があり、この Phase の範囲外。
