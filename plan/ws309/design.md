<!-- awesome-plan project=zedbsd record=ws309-design -->

# WS309 設計: Keiland の compositor の thread の構成（2026-10-29 P1、q1662）

- 親: [ws.md](ws.md)。ユーザーの決定の原文は ws.md の 1〜8 と pool の N の回答（「CPU 数で固定 (推し)」）。この文書はその解釈と実装の設計。
- 調べた tree: main 61630bb0b（worktree `p1-ws309`）。前身の点検: P2 の keiland-blocking.md（`agent/p2-ws291i`、main に未 merge。表の番号 #1〜#20 はそれを指す）。
- 範囲: compositor（`userland/desktop/wayland/`）と、その thread 化に要る libkeiland-backend（共通と zedBSD）、game mode の (V2) に要る libvulkan の WSI・display の部分（J1 の決定）。kernel は範囲の外。Linux/FreeBSD の seat の入力は後の Phase（J4）。

## 1. 原則

1. **main は待たない。** frame の間に main が呼んでよいのは、待たない system call（非 blocking の fd、kernel の写しを返すだけの ioctl）、lock の短い区間（相手も短い区間しか持たない lock）、Vulkan の object の作成・記録（queue を使わない物）だけ。
   次は main で呼ばない: file の read・write・fsync・rename、`connect`・blocking の socket、device の open・close、`fork`、`pthread_join`（終わった事を見た後の join は除く）、
   期限の無い・長い `pthread_cond_wait`、GPU の待ち（`vkQueueWaitIdle`・`vkDeviceWaitIdle`・`vkWaitForFences`）、`vkQueueSubmit`・`vkQueuePresentKHR`。
   例外は起動（READY の前）と終了（event loop の後）だけ。
2. **待つ物は名前のある thread が持つ。** thread の種類は 3 つ: 常駐（process と同じ寿命）、pool（本数は起動の時に決めて固定）、その都度（1 回の仕事で終わる）。本数は §2 の表のとおりで、増減しない。
3. **thread の間は queue と共通の wake pipe だけ。** 各 thread は自分の受け口（lock で守った FIFO と cond）で main の依頼を受け、結果を自分の出口（lock で守った FIFO）に積み、
   出口が空から非空になった時だけ main の wake pipe（`server->wake`、今の render・input・glyph・cursor と同じ）に 1 byte 書く（`main_woken` で重複を落とす。今の glyph worker の形）。
   main は poll の後に wake pipe を空にし、全ての出口を順に取る（§3.3）。fd の event を kernel に足さない（決定 4・5）。GPU の完了は VkFence を render thread が待つ、present は thread が同期で待つ。
4. **持ち主は 1 つ。** 状態は 1 つの thread だけが書く。他の thread は依頼と結果の写しを渡すだけ。compositor の状態（client・surface・window・protocol）は main だけが持つ。
5. **結果は main が当てる。** backend の callback（`kl_backend_host`）を呼ぶのは main だけ（今の契約: `kl_backend_poll_done` と `kl_backend_tick` から）を保つ。
6. **signal**: main 以外の thread は全ての signal を block して作る（今の audio の pump・P2 の worker と同じ）。SIGINT・SIGTERM・SIGCHLD は main が受ける。
7. **終了**: event loop を出た後、main は依頼を止め、各 thread に stop を頼んで join する（ここだけは待ってよい）。順は §8。

## 2. thread の一覧（決定 4〜8 の構成）

| # | 種類 | 名前（log の名） | 本数 | 持つ仕事 | 待ちの点（その thread の中） | main への結果 |
| --- | --- | --- | --- | --- | --- | --- |
| T0 | 常駐 | main | 1 | Wayland の protocol と状態、scheduler、draw-list の作成 | poll（10 ms・2 ms・0） | — |
| T1 | 常駐 | render | 1 | GPU の queue の唯一の持ち主（cursor は §4.2）。frame・layer の job、取り込みの layout の遷移、upload、GPU の資源の片付けの時期 | acquire（50 ms）、fence（`vkWaitForFences`）、`vkQueueWaitIdle`（drain の命令） | 今の message queue（READY・COMPLETED）＋ 新しい TASK_DONE・DRAINED |
| T2 | 常駐 | cursor | 1（output が開いている間） | 今のまま（cursor plane の present） | 今のまま | 今のまま（失敗だけ wake） |
| T3 | 常駐 | display | 1 | display の状態の変更の直列化: swapchain・surface の作り直し、modeset、回転、hotplug の列挙、direct scanout の claim・release、backlight の ioctl | `vkCreateSwapchainKHR`（claim）、`vkDestroySwapchainKHR`（release）、`GPU_DISPLAY_CLAIM/RELEASE`、cursor thread の join、render の drain の待ち | 変更の完了（新しい swapchain の写し・失敗） |
| T4 | 常駐 | present | 1 | game mode の present（今は backend の `GPU_DISPLAY_PRESENT`、目標は §6） | present の ioctl（1 refresh） | 前の buffer の release（共通の wake pipe） |
| T5 | 常駐 | input | 1 | evdev の read だけ（今のまま、device の検出を入れない） | 今のまま | 今のまま |
| T6 | 常駐 | io-device | 1 | I/O-1: 入力の device の検出（readdir・open・capability・close）、電源の読み取り、network の詳細・鍵の保存、account の処理、machine の読み取り（statvfs）、sysmon の標本 | device の open（PS/2 の aux で 約 80 ms）、ioctl、helper の終わり | 開いた device の一覧、電源の状態、各 job の答え |
| T7 | 常駐 | io-file | 1 | I/O-2: 設定の file の書き込みと fsync（displays.conf・desktop.conf・printers.conf）、file の読み込み（apps.conf・言語の catalog・printers.conf の読み直し） | write・fsync（UFS で秒）・rename、read | 書いた・読んだ結果（`saved` の答えはここから） |
| T8 | 常駐 | font | 1 | 予備の font・emoji の font の先読み、字形の rasterize、color emoji の decode と縮小。face はこの thread だけが持つ | file の read、rasterize | 字形の coverage、color の画素、face の metrics の写し |
| T9 | 常駐 | blur | 1 | 壁紙のすりガラスの blur（CPU、決定 6）と、出力の大きさへの縮小 | 計算だけ | blur した画素 |
| T10〜T14 | 常駐 | audiod・networkd・bluetoothd・phone・volumed | 5 | daemon の接続ごとに 1 本、blocking の I/O（connect・send・timed read） | connect、recv、2 s の timed read | 状態の写し、答え、event |
| P1 | pool | hand-N | N（§5） | 手書きの照合 | 計算だけ（最初に template の読み込み） | 候補 |
| A1 | その都度 | decode | 0〜1 | 壁紙・画像の file の read と decode（1 回ごと） | read、decode | decode した画素（blur へ渡す） |
| S | process | spawner | 1 | Vulkan を開く前に fork した process。app・IME・printd・mediastorage・account の helper の起動を頼まれる | fork・exec・waitpid | pid、終わり |

- 常駐の thread の数: main を除いて 14（T1〜T14、T2 は output の間）＋ pool の N。今ある thread で無くなる物: glass の loader・prefetch（A1 と T9 へ）、heads の writer・settings の writer・print の writer（T7 へ）、
  hand の loader（P1 へ）、machine-shell・sysmon・system.c の job・account の job（T6 へ）、scanout-zedbsd の present thread（T4 へ）、P2 の input の worker（T6 へ、merge しない）。
- Linux・FreeBSD の audio の pump（stream ごとの real-time の thread）は compositor の zedBSD の構成の外で、今のまま残す（§9）。
- 名前: libc に `pthread_setname_np` が無い（`include/libc/pthread.h`）。名前は compositor の thread の台帳（§3.1）が持ち、起動の時に `KWL THREADS` の 1 行で名前と本数を出す。

## 3. 共通の部品（wayland/ の新しい file）

### 3.1 thread の台帳と作成（`threads.c`・`threads.h`）

- `kwl_thread_start(struct kwl_thread *thread, const char *name, void *(*run)(void *), void *argument)`: 全 signal を `pthread_sigmask` で block してから `pthread_create`、元の mask に戻す。台帳（固定の配列、main だけが書く）に名前を載せる。
- `kwl_thread_join(struct kwl_thread *thread)`: 終了の時だけ。台帳から外す。
- `kwl_threads_report(void)`: `KWL THREADS count=N names=render,cursor,...` を 1 行。
- `kwl_cpu_count(void)`: `sysconf(_SC_NPROCESSORS_ONLN)`、失敗・0 以下は 1。

### 3.2 lane（`lane.c`・`lane.h`）: 常駐の 1 本の thread と FIFO の job

- job: `struct kwl_lane_job { next; run(job); done(server, job); drop(job); }`。`run` は lane の thread で、`done` は main で呼ぶ。job の memory は main が確保し、`done` か `drop` の後に main が解放する。
- `kwl_lane_start(lane, name, wake_fd)`、`kwl_lane_submit(lane, job)`（lock して末尾へ、cond を signal、数 µs）、`kwl_lane_take(lane, server)`（main: 出口の job を全て取り `done` を呼ぶ）、
  `kwl_lane_stop(lane)`（終了: 未着手の job は `drop`、実行中の 1 つは終わりを待つ）。
- 置き換え（最新だけ）: `kwl_lane_submit_keyed(lane, job, key)` は、同じ key の未着手の job を出口へ「取り消し」として移し（main が `drop`）、新しい job を同じ位置に置く。
  displays.conf の連続の保存・sysmon の標本など、最新の 1 件だけに意味がある job に使う。
- 順: lane の中は FIFO。同じ file への書き込みの順は submit の順のまま（T7）。
- T6・T7・T9 はこの lane。T8（font）は今の glyph worker を広げた専用の thread（§4.6、face の持ち主のため）。

### 3.3 main の wake の扱い（main.c）

- 今の `wake_drain` の後に、出口を持つ全ての部品の take を 1 か所で呼ぶ（`kwl_threads_take(server)`: T6・T7・T9 の lane、T4、P1、A1、T8、daemon の backend）。
  順は「device・電源 → file → font → 壁紙 → 手書き → present → daemon」。take は出口の lock を取って list を外し、lock の外で `done` を呼ぶ（lock の中で main の状態に触れない）。
- poll の timeout は今のまま（10 ms）。wake で起きれば即座に取る。daemon の backend は今の tick の `*_update()` で写しを取る（§4.8）。
- 取り忘れの防ぎ: 出口に積んだ thread は、`main_woken` が 0 の時だけ 1 byte 書いて 1 にする。main は take で出口を空にする前に `main_woken` を 0 にする（lock の中）。
  wake pipe が満ちて write が EAGAIN でも、`main_woken` は 1 のままで、main は次の poll の 10 ms 以内に必ず take を回すので取り落とさない。

### 3.4 pool（`pool.c`・`pool.h`）: 同じ仕事の N 本

- lane と同じ job の形で、N 本の thread が 1 つの受け口を分け合う。key つきの submit は、同じ key の未着手の job を取り消す（「同じ文字は最新の 1 件だけ」、決定 6 の解釈）。
- 実行中に同じ key の新しい job が来た時は止めない（照合は数〜数十 ms）。結果に job の世代を付け、main は最新の世代でない結果を捨てる。

### 3.5 その都度の thread（`adhoc.c` の小さな helper）

- `kwl_adhoc_start(adhoc, name, job)`: 1 本作って job を走らせ、終わりに出口へ積み wake。main は take で出口を見た後に join（終わった thread の join は待たない）。同時に 1 つ（A1 は壁紙の decode 1 つ）。
  走っている間に次の依頼が来たら、依頼を 1 つだけ覚え（最新で上書き）、終わった後に始める。

## 4. 各 thread の設計

### 4.1 render（T1）: GPU の queue の唯一の持ち主

今 main が queue を使う所（GPU の点検の報告、file:line は main 61630bb0b）と直し方:

| 今 | 直し |
| --- | --- |
| `image_layout`（shm.c:941、submit ＋ `vkQueueWaitIdle`）: shm の初回・size の変化、cursor の形、cursor plane の copy、glass の画像 | 遷移の barrier を **次の frame の command buffer の先頭**に積む（今の `kwl_import_layouts_record` の一般化、`kwl_layouts_record`）。frame の前にその画像を使う所は無い（使うのは frame の中だけ）。frame が無い時は render に「task」（main が自分の command pool で記録した one-time の command buffer）を渡す。render は task を受けた順に submit する（queue の submission の順で後の frame の前に終わる）。main は待たない |
| `kwl_host_image_batch_end`（shm.c:465、submit ＋ wait）: 起動と回転 | 起動（render thread の前）は今のまま（原則 1 の例外）。回転は T3 の手順の中（§4.3）で task |
| `import_layout`（import.c:293、KWL_LAYOUTS_MAX を超えた時） | 上限を超えた分を task にする |
| `kwl_compose_quiesce` ＋ `vkDeviceWaitIdle`（glass.c:567・610・850・2956、shm.c:198、objects.c:561） | **遅らせた破棄**: 捨てる Vulkan の資源（image・memory・buffer・view）を「frame の番号 n まで使われ得る」として retire の list に積み、render の COMPLETED が n に届いた時に main が破棄する（`kwl_gpu_retire`）。frame が in flight でなければ即座に破棄 |
| park ＋ `vkDeviceWaitIdle`（compose.c:661・1061、output-rotate.c:722、heads.c:1080、cursor-plane.c:217） | T3 の手順（§4.3）の中の「drain」: render が in flight の fence を全て待ち、`vkQueueWaitIdle` を自分の thread で呼んで DRAINED を返す。T3 は DRAINED を待ってから swapchain を壊す（T3 は待ってよい） |
| `kwl_render_wait_idle`（render-thread.c:467） | 上の 3 つで使う所が無くなるので消す |

- task の message: `kwl_render_task(render, task)`（command buffer と完了の印）、render は submit して fence を持ち、終わったら TASK_DONE を出口へ（main は command buffer を解放）。
- **cursor thread と queue**: cursor thread も同じ queue に submit・present している（cursor-thread.c:505・555）。queue は 1 つ（compose.c:1472）。Vulkan の外部の同期の規則を満たすため、
  render と cursor が queue を使う区間を `kwl_queue_lock`（mutex、submit・present の区間だけ、µs）で守る。main は queue を使わない（原則 1）。cursor を render 経由にすると cursor の遅れが
  render の acquire（50 ms まで）に縛られるので取らない（ws.md の「cursor は今のまま」）。queue の持ち主の意味は「main と他の thread は queue を使わず、使うのは render と cursor の 2 つで lock の下」。

### 4.2 cursor（T2）

今のまま。上の queue の lock だけを足す。

### 4.3 display（T3）: display の状態の変更の直列化

- 依頼（main → T3）: `OUTPUT_CLOSE`（窓の mode を閉じる）、`OUTPUT_OPEN(display, mode, rotation)`、`ROTATE`、`HEADS_SYNC(config)`、`SCANOUT_OPEN(size)`・`SCANOUT_CLOSE`、`ENUMERATE`（hotplug の時の列挙）、`BACKLIGHT_SET/GET`。1 つずつ順に。
- T3 がする事（待ってよい物）: render の park と drain の依頼と DRAINED の待ち、cursor thread の停止と join、`vkDestroySwapchainKHR`・`vkDestroySurfaceKHR`（libvulkan の中で RELEASE）、
  `vkCreateDisplayPlaneSurfaceKHR`・`vkCreateSwapchainKHR`（claim・modeset）、`vkGetSwapchainImagesKHR`、display の列挙（`vkGetPhysicalDeviceDisplayPropertiesKHR` ほか）、
  scanout の backend の `kl_backend_scanout_possible/open/close`（ioctl）、backlight の ioctl。
- main に残る物: 変更の決定（scanout-rules、output-switch の判断、回転の選択）、targets・scene・layers・glass の作り直し（swapchain の image の写しを受けて main が作る。Vulkan の object の作成で queue を使わない）、
  `server->width/height`・window の配置・`wl_output` の event。
- 状態の機械（main）: `display_state = IDLE | BUSY(request)`。BUSY の間、scheduler は output の frame を出さない（今の park の間と同じ。画面は前の画像のまま）。client の要求・input・protocol は止めない。
  BUSY の間に来た次の変更は 1 つだけ覚え（最新で上書き）、完了の後に出す。
- swapchain の object の持ち主: T3 が作り、完了の message で main（と render）に渡す。壊す時は main が T3 に返す。T3 が触る間、render は park している（外部の同期）。
- hotplug の 250 ms の tick（output-switch.c:77）は main に残す（`vkGetFenceStatus` は待たない）。列挙は T3。

### 4.4 present（T4）: game mode の present

- 今の backend の present thread（scanout-zedbsd.c:538、scanout ごとに作る、自分の pipe）を compositor の常駐の thread にする。backend は `kl_backend_scanout_present(scanout, buffer)`（blocking、thread から呼ぶ）を出し、thread と pipe を持たない。
- T4 は受け口の buffer を 1 つ取り、present を同期で待ち（決定 6 「同期で待っていいです」）、結果を出口へ、共通の wake pipe に 1 byte。main は take で前の front を release（今の `kwl_scanout_ended` の中身）。
- claim・release・import は T3 の SCANOUT_OPEN・CLOSE（release の前に T4 が in flight の present を終えるのを T3 が待つ）。
- 目標の Vulkan の形は §6。

### 4.5 input（T5）と io-device（T6）

- T5 は今のまま（evdev の read、attach・detach の命令）。
- T6 の job:
  - **device の検出**（決定 1・3）: main は 2 s（device の event の後は 100 ms）ごとに「scan」job を出す（前の scan が終わっていなければ出さない）。job は main が作った「読んでいる node の集合」の写しを持つ。
    T6 は backend の新しい `kl_backend_input_find(backend, known, found, capacity)`（readdir・open・capability の ioctl、callback を呼ばない、thread から呼んでよい）で開け、結果（fd・path・caps）を返す。
    main の `done` で今の `input_found` の分類（`kwl_input_probe`）を呼び、keep しない fd と重複（2 度目の open）は「close」job で T6 に閉じさせる（close は driver の close の callback を呼ぶので待ち得る）。
    keep した device の後の close（`kl_backend_input_close`）も T6。開けなかった node の記憶（b574fbf6a、P2 の判断）は T6 の中に置き、system の INPUT の event で忘れる。
    Linux・FreeBSD は seat（logind の D-Bus）経由の open で、seat の状態は main の物なので今の同期の scan のまま（`kl_backend_input_find` は ENOTSUP を返し、main は今の `kl_backend_input_scan` を呼ぶ）。§9 に残す。
  - **電源**: `kwl_power_read`（起動・power の event・sleep の後）を job に。今の system.c の power の job の thread を置き換える。
  - **network の詳細・鍵の保存**、**account**（`kl_backend_account_*`、helper の fork は S へ、§4.10）、**machine の読み取り**（machine-shell.c、statvfs を含む）、**sysmon の標本**（周期の job、key つきで最新だけ）。
- job の遅れ: PS/2 の aux の open（80 ms）の後ろで電源の読み取りが待つ事はある（main は待たない）。

### 4.6 font（T8）

- 今の glyph worker（glyph-worker.c）を広げる。T8 が持つ: 全ての face（今 main が持つ `glass->faces[]` を含む）、予備の font（DroidSansFallbackFull）・emoji の font の読み込み、字形の rasterize、color emoji の decode と縮小。
- main が字形に要るのは cell の大きさと advance などの metrics。main は「字形の依頼」（文字・font・大きさ）を出し、結果（metrics と coverage か color の画素）で atlas に書く。結果が来るまでの frame は今の worker の未着の字形と同じ扱い（空けて次の frame）。
- 予備の font・emoji の font は起動の後に T8 が先読みする（main の READY の後の最初の依頼）。読む前に予備の文字が来たら、T8 が読み込みの後に rasterize（main は待たない）。
- 起動の atlas（`atlas_fill`）と大きな時計の数字（`large_fill`、glass.c:2955 の quiesce と `vkDeviceWaitIdle` を含む）: 起動の atlas は READY の前なので main のまま（原則 1 の例外）。大きな数字は T8 の依頼と §4.1 の遅らせた破棄にする。
- face を 1 つの thread だけが持つので、main の `glass_cache_glyph` の glyph index・metrics の解決も依頼にする（metrics の表の写しを T8 が main に渡す: font と大きさごとの ascent・descent と、既に解決した字形の metrics の cache）。

### 4.7 blur（T9）と decode（A1）

- 壁紙の変更・出力の大きさの変化・回転で要る絵: A1（その都度）が file を読み decode、T9（常駐）が出力の大きさへ縮小し blur（今の `wallpaper_render` の CPU の経路、決定 6）。
- 結果が来たら main が host の image に写し（memcpy、今の `loader_copy`）、古い image は §4.1 の遅らせた破棄。glass.c の `kwl_compose_quiesce` ＋ `vkDeviceWaitIdle`（567・610・850）は無くなる。
- 結果が来るまでは古い壁紙のまま描く（大きさが変わった時は古い絵を引き伸ばすか単色。今は画面が止まる）。
- 起動の prefetch（READY の前、main.c:255）は A1 の最初の 1 回にする（Vulkan の作成と並ぶのは今と同じ）。
- 画像の処理は GPU で（2026-10-12 ユーザー）との関係: 壁紙の blur は「内容が変わった時の一度だけの用意」で CPU が許される範囲。毎 frame の glass の blur は今の GPU のまま。GPU の blur との比較は Future Work（ws.md）。

### 4.8 daemon（T10〜T14）

- zedBSD の backend の 5 つ（audio-zedbsd.c・network-zedbsd.c・bluetooth-zedbsd.c・phone-zedbsd.c・volume-zedbsd.c）に、それぞれ 1 本の thread（backend の中、§3.1 の台帳に載せるため作成は compositor が渡す
  `kl_backend_thread_start` の hook を使う。hook が無い試験では pthread をそのまま）。
- thread の loop: 接続（blocking の `connect`）、送る（blocking の send）、受ける（blocking。依頼の pipe と socket を poll して、依頼が来たら送る）、timer（再接続・定期の読み直し）は poll の timeout。
  networkd の frame の timed read（2 s）も thread の中。
- main の API（`kl_backend_<daemon>_update/get_*/request/take_*`）の形は変えない: update は lock の下で写しを取るだけ、request は依頼を積んで依頼の pipe に 1 byte。
  thread は状態が変わった時に共通の wake pipe に 1 byte（backend は `kl_backend_open` の時に host から wake の fd を受ける）。
- phone の `send_mms` の `pread`（MIME の file の読み込み）も phone の thread へ。
- Linux・FreeBSD の backend（wpa・ALSA・OSS・BlueZ の D-Bus）は今のまま（待つ所は job の thread か非 blocking）。§9。

### 4.9 手書きの照合（P1）

- N = min(max(CPU の数 − 1, 1), 4)。起動の時に `kwl_cpu_count()` で決め、`KWL THREADS` に出す（決定 6 と pool の N の回答）。thread は手書きの face を初めて出した時（今の `kwl_hand_preload`）に N 本作り、終了まで残す。
- template の読み込み: pool の最初の job の前に 1 本が読む（他は cond で待つ）。main は待たない。
- 依頼: ink の写し（`struct kwl_hand_ink`、約 131 KB）、area の top と height、key（手書きの面で今書いている文字＝ 1 つの key）、世代。照合の途中の static の配列（xs・ys・starts）は thread ごとの物にする。
- 結果: `kwl_hand_result`。main の take で最新の世代だけを `keyboard.result` に入れ、`server->dirty`、今の log の行（`KWL OSK hand recognize ...`）を出す。
- 「N 文字」の解釈: 1 つの文字の照合は 1 本で、N 本で同時に N 文字まで（文字を続けて書くと、前の文字の照合を待たずに次の文字を始める）。同じ文字の依頼は最新の 1 件（§3.4）。

### 4.10 spawner（S）

- main.c の最初（`kl_backend_open` と Vulkan の前）に `fork` で作る小さな process。compositor とは `socketpair(AF_UNIX, SOCK_SEQPACKET)` でつなぐ（fd は SCM_RIGHTS で渡す）。
- 依頼: 起動の種類（`/bin/sh -c` の command、IME、printd、mediastorage、account の helper）、環境変数の差分、渡す fd（IME の socket、printd の socket、pipe）、依頼の番号。
  spawner は `fork`（小さな process なので速い）・`setsid`・fd の整理・`exec`。答え: 依頼の番号と pid か errno。子の終わり（`waitpid`）も spawner が受けて「終わった pid と status」を送る。
- compositor の側: 送受は main の非 blocking の socket（poll の集合に 1 つ）。`kwl_spawn` は依頼の番号を返し、pid は答えで後から入る（今 pid を使う所: desktop.c・input-method.c・home.c の waitpid、media の子）。
  compositor の `waitpid(-1)`（home.c:1460）は spawner の「終わった」の知らせに置き換える。
- spawner が落ちたら: compositor は作り直さない（Vulkan の後の fork になる）。起動の依頼は失敗（errno）を返し log に出す。§9 の判断の点。
- fork の後の `setenv`（今の kwl_spawn は多 thread の process で fork の後に setenv、async-signal-safe でない）も spawner では単一 thread の process なので問題でなくなる。

### 4.11 main に残る小さな直し（thread でない）

- clipboard の書き込みの spin（clipboard.c:314-342、100 ms の busy loop）: 残りを持って fd を poll の集合に入れ、POLLOUT で pass ごとに書く。
- `kl_backend_print_update` の毎 tick の `stat(printers.conf)`（print.c:399）と読み直し: T7 の周期の job（1 s、key つき）に。
- 言語の catalog の読み直し（settings.c:634、`ui.language` の変更）と apps.conf の読み込み（home.c:1719）: T7 の read の job、結果で main が差し替え。

## 5. 本数のまとめ

| 機械の CPU | 常駐（T1〜T14） | pool N | その都度 | 合計（main を除く、最大） |
| --- | --- | --- | --- | --- |
| 1 | 14 | 1 | 1 | 16 |
| 2 | 14 | 1 | 1 | 16 |
| 4（Pi 4・VF2） | 14 | 3 | 1 | 18 |
| 8 以上 | 14 | 4 | 1 | 19 |

## 6. ゲームモード（決定 6〜8）

- 今: game mode は backend の ioctl（`GPU_DISPLAY_CLAIM/PRESENT/RELEASE`、Guardrail の例外）で client の buffer をそのまま flip。決定 8 で当面は許される。
- 目標（決定 8 「Vulkan display の仕様内で実装したい」）: libvulkan の `VK_KHR_display` の swapchain（今の窓の mode と同じ display の plane の surface）で present し、buffer が画面から外れた事を
  Vulkan の機能（`VK_EXT_swapchain_maintenance1` の present fence か `VK_KHR_present_wait`）で知る。調べた結果: libvulkan は `VK_KHR_display`・`VK_KHR_display_swapchain`・`VK_EXT_display_control` を出すが、
  `VK_EXT_swapchain_maintenance1`・present fence・present_wait は無い（libvulkan の grep）。
- 仕様の中で client の buffer を「そのまま」flip する標準の口は Vulkan に無い（swapchain の image は WSI が作る）。仕様の中の形は 2 つ:
  - (V1) **copy**: T4 が display の swapchain の image を acquire し、client の buffer（import した VkImage）から `vkCmdCopyImage`（同じ形式・大きさ）で写して present。client の buffer は copy の fence で
    release（画面から外れるのを待たない分、client は早く次を描ける）。present の完了は present fence で T4 が待つ。1 frame に 1 回の全画面の GPU の copy（1080p で約 8 MB）。
  - (V2) **swapchain の image を client に貸す**: compositor の swapchain の image を client の buffer として渡す。VK_KHR_swapchain に image の export は無く、仕様の外（独自の拡張）になる。
- **決定（J1、2026-10-29 ユーザーの回答（クリック、Q1 経由）「V2 swapchain 共有 (推し)」）**: (V2) を取る。Q1 の伝えた解釈（原文は上の 1 行）:
  compositor が display（`VK_KHR_display`）の swapchain の image を external memory で export して client に渡し、client が直接描く（compositor が acquire → client に渡す →
  client の描画の完了 → compositor が present、copy なし）。決定 7 (b) の acquire・release の形に合わせる。libvulkan の変更（export の口・display の swapchain の external memory）は
  WS309 の範囲に含める（libvulkan の WSI・display の部分を P1 の所有に Q1 が足す。P2 の amdrdna の経路と重なる所は先に Q1 に知らせる）。
- (V2) の形（設計は後の Phase で詳しく。ここは骨組み）:
  - libvulkan: display の swapchain の作成に、image を外へ出せる印（`VkExternalMemoryImageCreateInfo` を `VkSwapchainCreateInfoKHR` の pNext に繋ぐ形を libvulkan が受ける）と、
    swapchain の image の memory の export（`vkGetSwapchainImagesKHR` の image に bind された memory を `vkGetMemoryFdKHR` で出す口）を足す。標準の型を使い、独自の型を足さない方向で設計する
    （標準に無い組み合わせの受け付けは libvulkan の実装の判断として記録）。present の完了（画面から外れた）は present fence（`VK_EXT_swapchain_maintenance1`）を足して知る。
  - compositor: game mode の client に swapchain の image を buffer として渡す（Wayland の buffer の object、client は import して描く）。T4（present）が acquire（同期で待つ）→ 空いた image を client に知らせ
    （release の event）→ client の commit（描画の完了の fence）→ T4 が present（`vkQueuePresentKHR`、queue は §4.1 の lock の下）→ present fence で前の image が画面から外れた事を知り、空きとして client へ。
  - 暫定: (V2) が入るまでは今の backend の ioctl を T4 から呼ぶ（§4.4）。
- §9 の判断の点 J1（決まった）。

## 7. 待ちの点の行き先（P2 の表 #1〜#20 との対応）

| P2 # | 行き先 |
| --- | --- |
| 1 input の scan・close | T6（§4.5） |
| 2 direct scanout の出入り | T3（claim・release・swapchain）＋ T4 |
| 3 quiesce・`image_layout` | T1（§4.1、frame の先頭の barrier・task・遅らせた破棄） |
| 4 displays.conf の fsync | T7（`saved` は書き終わりの結果で答える。protocol の意味は今と同じで、答えが後になるだけ） |
| 5 予備の font・emoji | T8 |
| 6 fork・posix_spawn | S |
| 7 壁紙の差し替え | A1 ＋ T9 ＋ 遅らせた破棄 |
| 8 出力の size の変化・回転・modeset | T3 ＋ A1・T9 |
| 9 clipboard の spin | main の poll（§4.11） |
| 10 手書き | P1 |
| 11・12 daemon の connect・timed read | T10〜T14 |
| 13 printers.conf の stat | T7 の周期の job |
| 14 stdout の write | 残す（session の log の file への行 buffer の write。§9 の未確認） |
| 15 他の GPU の同期 | T1（遅らせた破棄・task） |
| 16 cursor plane の hold（100 ms まで） | T3 の手順の中（game mode の出入り） |
| 17 電源 | T6 |
| 18 backlight | T3 |
| 19 小さな file の read | T7（言語・apps.conf）、phone の MMS は T13 |
| 20 shm の行の copy | main に残す（client の memory の copy、待ちでなく CPU の仕事。画像の処理は GPU で の方針の別の WS） |

- 起動と終了だけの物（READY の前・event loop の後）は残す。

## 8. 起動と終了の順

- 起動: spawner の fork（最初）→ backend の open（daemon の thread は最初の update で作る）→ T6・T7（lane）→ A1（壁紙の prefetch）→ Vulkan（compose）→ T1・T2 → T8（font）→ T9 → T5（input）→ T4 → T3 →
  READY → `KWL THREADS`。P1 は手書きの face を初めて出した時。
- 終了: event loop を出た後、client を閉じ、T3（display を閉じる依頼を完了まで）→ T4 → T5 → P1 → T9・A1 → T8 → T1・T2（今の compose の close）→ T7（書き込みを全て終え、fsync の後で止める。
  Log Out の設定の保存は今の `kwl_settings_store_finish` の位置で T7 に積んで待つ）→ T6 → daemon（backend の close）→ spawner に終わりを送る。

## 9. 判断の点（Q1・ユーザーへ）と制限

| ID | 点 | 推し |
| --- | --- | --- |
| J1 | game mode の Vulkan の形（§6） | **決定** 2026-10-29 ユーザー「V2 swapchain 共有 (推し)」: display の swapchain の image を export して client が直接描く。libvulkan の WSI・display は WS309 の範囲（P1）。暫定は T4 が今の ioctl |
| J2 | cursor thread も queue に submit する（§4.1） | **決定**（2026-10-29 Q1: 推しで OK、技術の判断）: main は使わず、render と cursor（と T4 の V2 の present）が lock の下で使う |
| J3 | spawner が落ちた後（§4.10） | **決定**（2026-10-29 Q1: 推しで OK）: 作り直さず、起動の依頼を errno で失敗にする |
| J4 | Linux・FreeBSD の seat 経由の入力の open（logind の D-Bus） | **決定**（2026-10-29 Q1・ユーザー）: 当面は同期のまま残し、WS309 の後の Phase に置く（別の WS にしない） |

- 範囲の外・制限: Linux・FreeBSD の backend の daemon・audio の pump は今のまま。stdout の log の write（#14）は未確認のまま残す。shm の行の copy（#20）は残す。

## 10. 試験

- host（ASan+UBSan と TSan）: lane・pool・adhoc・台帳（submit・take・置き換え・stop・wake の取り落とし無し）、T6 の input の find（P2 の host 試験の --wrap の偽の /dev/input を作り直す）、
  T7 の書き込みの順と fsync、P1 の世代、spawner（socketpair で依頼と答え、fd の受け渡し、子の終わり）。
- QEMU（Venus、p1 の名前・port を分ける）: 起動の確認（boot-test.sh）、`KWL THREADS` の行、WS099 の基準の C1・C2・C9、GPU の境界の試験（forge・fence）、`KWL PERF pass` の pass_max_us。
- 実機（RDNA・VF2）は Q1 の割当で（未定）: game mode の出入りの SLOW_PASS、壁紙の変更・回転の間の frame。
