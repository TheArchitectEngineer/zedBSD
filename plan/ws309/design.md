<!-- awesome-plan project=zedbsd record=ws309-design -->

# WS309 設計: Keiland の compositor の thread の構成（2026-10-29 P1、q1662）

- 親: [ws.md](ws.md)。ユーザーの決定の原文は ws.md の 1〜8 と pool の N の回答（「CPU 数で固定 (推し)」）。この文書はその解釈と実装の設計。
- 調べた tree: main 61630bb0b（worktree `p1-ws309`）。前身の点検: P2 の keiland-blocking.md（`agent/p2-ws291i`、main に未 merge。表の番号 #1〜#20 はそれを指す）。
- レビュー: design-reviewer の敵対的レビュー（2026-10-29、16 件: blocker 1・major 11・minor 4）を §4.1・§4.3・§4.4・§4.6・§4.8・§4.10・§6・§7・§10・§11 に反映した（各所に「レビュー n」）。
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
6. **signal**: main 以外の thread は全ての signal を block して作る（今の audio の pump・P2 の worker と同じ）。SIGINT・SIGTERM・SIGHUP は main が受ける（子は今どおり `waitpid(WNOHANG)` で回収、main.c:198-210）。
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
| T6 | 常駐 | io-device | 1 | I/O-1: 入力の device の検出（readdir・open・capability・close、backend が自分で開く時だけ）、電源の読み取り（power の変化の時。起動（READY の前）と sleep の答え（lid をその場で規則に渡す）は main の同期のまま、p004 の Q1 の判断）、network の詳細の読み取り、sysmon の標本（machine の読みは A2） | device の open（PS/2 の aux で 約 80 ms）、ioctl | 開いた device の一覧、電源の状態、各 job の答え |
| T7 | 常駐 | io-file | 1 | I/O-2: 設定の file の書き込みと fsync（displays.conf・desktop.conf・printers.conf）、file の読み込み（apps.conf・言語の catalog・printers.conf の読み直し）、遅い処理（account の helper、network の鍵の保存、process の起動 §4.10） | write・fsync（UFS で秒）・rename、read、helper の終わり、posix_spawn | 書いた・読んだ結果（`saved` の答えはここから） |
| T8 | 常駐 | font | 1 | 予備の font・emoji の font の file の先読み、字形の rasterize、color emoji の decode と縮小（§4.6、metrics は main が bytes の上の自分の face で引く） | file の read、rasterize | font の bytes、字形の coverage、color の画素 |
| T9 | 常駐 | blur | 1 | 壁紙のすりガラスの blur（CPU、決定 6）と、出力の大きさへの縮小 | 計算だけ | blur した画素 |
| T10〜T14 | 常駐 | audiod・networkd・bluetoothd・phone・volumed | 5 | daemon の接続ごとに 1 本、blocking の I/O（connect・send・timed read） | connect、recv、2 s の timed read | 状態の写し、答え、event |
| P1 | pool | hand-N | N（§5） | 手書きの照合 | 計算だけ（最初に template の読み込み） | 候補 |
| A1 | その都度 | decode | 0〜1 | 壁紙・画像の file の read と decode（1 回ごと） | read、decode | decode した画素（blur へ渡す） |
| A2 | その都度 | machine-read | 0〜1 | machine の読み（名前・file system の大きさ（statvfs）・account・mount）。detached、同時に 1 本、10 s で諦めて query を failed で答え、止まった読みが残る間の query は即 failed、終了でも待たない（p004 の Q1 の判断: T6 に載せると止まった mount の後ろで入力の検出と終了の join が止まる） | statvfs、account の読み | 読んだ部分（main が tick で見る） |
| S | （J5） | — | 0 | process の起動: T7 からの `posix_spawn`（vfork）、spawner の process は作らない（§4.10、J5 の Q1 の決定） | — | pid |

- 常駐の thread の数: main を除いて 14（T1〜T14、T2 は output の間）＋ pool の N。今ある thread で無くなる物: glass の loader・prefetch（A1 と T9 へ）、heads の writer・settings の writer・print の writer（T7 へ）、
  hand の loader（P1 へ）、sysmon・system.c の job（T6 へ）、account の job（T7 へ）、machine-shell は A2（名前を付けて台帳へ）、scanout-zedbsd の present thread（T4 へ）、P2 の input の worker（T6 へ、merge しない）。
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

### 3.4 pool: 同じ仕事の N 本（lane の N 本、p002 で実装）

- lane を N 本の thread で始めた物（`kwl_lane_start(lane, "hand", N, wake)`、名前は hand-1〜hand-N）。N 本が 1 つの受け口を分け合う。key つきの submit は、同じ key の未着手の job を取り消す（「同じ文字は最新の 1 件だけ」、決定 6 の解釈）。
- 実行中に同じ key の新しい job が来た時は止めない（照合は数〜数十 ms）。結果に job の世代を付け、main は最新の世代でない結果を捨てる。

### 3.5 その都度の thread（`adhoc.c` の小さな helper）

- `kwl_adhoc_start(adhoc, name, job)`: 1 本作って job を走らせ、終わりに出口へ積み wake。main は take で出口を見た後に join（終わった thread の join は待たない）。同時に 1 つ（A1 は壁紙の decode 1 つ）。
  走っている間に次の依頼が来たら、依頼を 1 つだけ覚え（最新で上書き）、終わった後に始める。

## 4. 各 thread の設計

### 4.1 render（T1）: GPU の queue の唯一の持ち主

今 main が queue を使う所（GPU の点検の報告、file:line は main 61630bb0b）と直し方:

| 今 | 直し |
| --- | --- |
| `image_layout`（shm.c:941、submit ＋ `vkQueueWaitIdle`）: shm の初回・size の変化、cursor の形（cursor.c:759）、cursor plane の copy（cursor-plane.c:644）、glass の画像 | **fence 付きの task**（レビュー 1）: main が自分の command pool で one-time の command buffer に遷移を記録し、host の書き込み（矢印の画素 shm.c:268-280、cursor plane の memset cursor-plane.c:648 など）を済ませてから render に渡す。render は受けた task を、後に受けた frame・layer の job より先に、fence を付けて submit する。main は TASK_DONE を受けるまでその image を draw-list と cursor box に出さない（image の状態 `pending` → `ready`）。「次の frame の command buffer の先頭」に積む案は取らない（copy_command が command より先に走り遷移の前に copy する、cursor thread は frame の無い時も staging を GENERAL で読む） |
| `kwl_host_image_batch_end`（shm.c:465、submit ＋ wait）: 起動と回転 | 起動（render thread の前）は今のまま（原則 1 の例外）。回転は T3 の手順の中（§4.3）で task |
| `import_layout`（import.c:293、KWL_LAYOUTS_MAX を超えた時） | 上限を超えた分を task にする（同じ `pending` の扱い） |
| `kwl_compose_quiesce` ＋ `vkDeviceWaitIdle`（glass.c:567・610・850・2956）、`kwl_compose_quiesce`（shm.c:198、objects.c:561） | **遅らせた破棄**（レビュー 3）: 今ある retire の list（objects.c:682-765、`in_flight` を見る）を広げ、Vulkan の資源（image・memory・buffer・view・descriptor set の予備への返し `kwl_compose_set_put` import.c:408-414）を「submit の serial n まで使われ得る」として積み、render の完了（frame・layer・task の全ての submit に増える serial）が n に届いた時に main が破棄・返却する。別の `kwl_gpu_retire` は作らない。**その場の書き込み**（今の wallpaper の image への `loader_copy` glass.c:850-860、大きな数字の描き直し glass.c:2953-2956）は、常に新しい image を作り古い物を retire する形にする（二重の image） |
| park ＋ `vkDeviceWaitIdle`（compose.c:661・1061、output-rotate.c:722、heads.c:1080、cursor-plane.c:217） | T3 の手順（§4.3）の中の drain（レビュー 2）: 全ての submit（task を含む）が fence を持つので、drain は render が自分の fence を全て待つ事（park の時点で `flight_count==0` なので frame の分は済み、残りは task）。cursor の submit は cursor thread の stop と join で覆う。起動の後は `vkQueueWaitIdle`・`vkDeviceWaitIdle` を使わない（終了の時は残す）。T3 の手順は render の保持の image の forget（`kwl_compose_forget` compose.c:662）を含む |
| `kwl_render_wait_idle`（render-thread.c:467） | 上の 3 つで使う所が無くなるので消す |

- task の message: `kwl_render_task(render, task)`（command buffer・fence・印）、render は submit し、fence を待つか次の poll で見て、終わったら TASK_DONE を出口へ（main は command buffer と fence を解放し、image を `ready` に）。
- 実装（p008）での変え方: draw-list は `pending` を見ず submission order に頼る（render は LIST より先に受けた task を先に submit）。task が飛んでいる間は host の staging の書き込み（描画・shm の upload・layer の絵）をしない。
  `pending` を見るのは cursor plane（box）だけ。task の完了の前の image の解放は p009 まで main が task の完了を待つ（[phase008](phase008/phase.md)）。
- **cursor thread と queue**（J2 の決定）: cursor thread も同じ queue に submit・present している（cursor-thread.c:505・555）。device の queue は 1 本（compose.c:1472 の queueCount）。Vulkan の外部の同期の規則を満たすため、
  render と cursor（と §6 の V2 の present）が queue を使う区間を `kwl_queue_lock`（mutex、submit・present の区間だけ、µs）で守る。lock を持って待つ事はしない（fence の待ちは lock の外）。main は queue を使わない。

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
- **T3 が触れてよい物と main の物の分け方**（レビュー 8）: 遷移ごとに「T3 の部分」（swapchain・surface・cursor plane の swapchain の作成と破棄、render の park・drain・forget の依頼、cursor thread の stop と join、
  scanout の ioctl、backlight の ioctl、列挙）と「main の `done` の部分」（`server->cursor_box` の field、cursor plane の copy の host image（compose->layouts）の作成・解放、targets・scene・layers・glass、
  `server->*` の大きさ、window と pointer、protocol の event）に分ける。T3 は main の struct の field を書かず、完了の message に新しい object の handle を入れて返す。
  今の `kwl_cursor_plane_close/open`（cursor-plane.c:192-240）は 2 つに割る（thread と swapchain は T3、box と host image は main）。
- **失敗**: T3 の手順の途中の失敗（`vkCreateSwapchainKHR` の失敗など）は、その時点までに作った物を T3 が壊し、失敗の message（どこで・errno）を返す。main は今の失敗の扱い（前の display に戻す
  `output_fail_back`、game mode の refuse）を done で行う。戻しの open も T3 への新しい依頼。
- **同期の完了を要る callback**（レビュー 4）: seat の pause（backend-host.c:36-52、Linux の logind）と handoff（handoff.c:146）は callback の中で output の close の完了を要する。
  これらは「BUSY の間は frame を出さない」状態にした上で T3 に close を依頼し、seat への答え（pause の完了）を T3 の完了の done で返す（logind は答えを待てる）。handoff は終了の流れの中なので待ってよい（原則 1 の例外）。
- game mode の可否の query（`kl_backend_scanout_possible`、open と query の ioctl）も T3 の依頼にする（答えが来た pass で main が入る判断をする）。

### 4.4 present（T4）: game mode の present

- 今の backend の present thread（scanout-zedbsd.c:538、scanout ごとに作る、自分の pipe）を compositor の常駐の thread にする。backend は `kl_backend_scanout_present(scanout, buffer)`（blocking、thread から呼ぶ）を出し、thread と pipe を持たない。
- T4 は受け口の buffer を 1 つ取り、present を同期で待ち（決定 6 「同期で待っていいです」）、結果を出口へ、共通の wake pipe に 1 byte。main は take で前の front を release（今の `kwl_scanout_ended` の中身）。
- claim・release は T3 の SCANOUT_OPEN・CLOSE（release の前に T4 が in flight の present を終えるのを T3 が待つ）。buffer の import（`GPU_RESOURCE_IMPORT`、scanout-zedbsd.c:261-266、`record->handle` と `record->claim` を書く）は main に残す（短い ioctl）。T4 に渡すのは import の済んだ handle の写しで、main は present の結果が返るまで buffer と record を保つ（client が消えても record は retire まで残る、レビュー 14）。
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
  - **network の詳細**（links・DNS・保存した network の読み取り）、**machine の読み取り**（machine-shell.c、statvfs を含む）、**sysmon の標本**（周期の job、key つきで最新だけ）。
  - 遅い job は T6 に置かない（レビュー 10、差した keyboard を待たせない）: account の helper（`passwd -s` の hash は秒になり得る）と network の鍵の保存は T7（file の lane）へ。
- job の遅れ: PS/2 の aux の open（80 ms）の後ろで電源の読み取りが待つ事はある（main は待たない）。
- **close と fd の番号の再利用**（レビュー 4）: seat が手放す device の close を T6 に出すのは、input thread の detach の ack（今は `kwl_input_thread_detach_wait`、input.c:642-656・1743-1748、最大 100 ms の同期）の後だけ。
  ack の前に閉じると番号の再利用で input thread が別の file を読む。p003 の close は ack の後の経路（detached の event の処理、input.c:1539）だけを T6 にした。ack の同期の待ち自体（通常 µs、input thread が止まった時だけ 100 ms）を wake で受ける形に変えるのは p004 の後半で。

### 4.6 font（T8）

- 決定 4 の原文は「フォントの先読み、字形のラスタライズは…単体の常駐スレッド」。face を T8 だけが持つ形は取らない（レビュー 5）: text の幅（`glass_text_width` glass.c:2640-2662 → `glass_glyph_of` → `glass_cache_glyph`）は
  同じ pass の中の配置・省略記号・hit-test に同期で要り、17 file・56 か所が使う。metrics を非同期にすると text の位置が後から動く。
- 形: 今の glyph worker（glyph-worker.h、「fonts of its own opened over the same bytes」）を広げ、T8 が
  - 予備の font（DroidSansFallbackFull、4 MB）と emoji の font の **file を読み**（先読み、READY の後の最初の依頼）、bytes を main に渡す（main は bytes の上に自分の face を開き metrics を引く。I/O は無い）、
  - 字形の **rasterize**（今のまま、T8 の face）、
  - **color emoji** の PNG の decode と縮小（今は main、glass.c:4181-4196 `kl_color_glyph`）
  を持つ。予備の font が読めていない間の予備の文字は空けて、読めた pass で 1 度だけ layout を直す（dirty）。
- 起動の atlas（`atlas_fill`）は READY の前なので main のまま（原則 1 の例外）。大きな時計の数字（`large_fill`）は T8 の rasterize と §4.1 の新しい image と retire にする（glass.c:2955 の quiesce と `vkDeviceWaitIdle` を消す）。

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
- 意味の変化（p006、Q1 OK 2026-10-29）: 送れなかった依頼の errno（connect・send の失敗、phone の queue の満杯など thread の側で分かる物）は、同期の戻り値でなく結果（network の DONE と get_request、volumes・bluetooth・phone の result、audio は REACHABLE の変化）で返る。引数の誤りと最後の写しで分かる ENOTCONN・EBUSY は今どおり同期。
- Linux・FreeBSD の backend（wpa・ALSA・OSS・BlueZ の D-Bus）は今のまま（待つ所は job の thread か非 blocking）。§9。
- **5 本に絞った基準**（レビュー 9）: main で blocking の I/O（blocking の `connect`、blocking の send、timed read）をする daemon の接続を 1 本ずつ thread にする。今それに当たるのが
  audiod・networkd・bluetoothd・phone（bluetoothd）・volumed の 5 つ。非 blocking で待たない接続（audiod の stream ごとの接続 audio-stream-zedbsd.c:200、sessiond session-zedbsd.c:1089、
  printd print.c:1407 の MSG_DONTWAIT、notify の bus、IME の Wayland の接続）は main のまま。基準を J6 で Q1 に確かめる。
- API の lock: getter（`kl_backend_*_get_state` など、const の pointer を返す物）は、update の時に thread の写しを main の側の写しへ写し、getter は main の写しを返す（thread と main は同じ memory を読まない）。
- 起動の順: backend が wake の fd を要るので、wake pipe の作成（今 `kwl_threads_prepare` の中、main.c で `kl_backend_open` と glass の prefetch の後）を `kl_backend_open` の前へ移す。

### 4.9 手書きの照合（P1）

- N = min(max(CPU の数 − 1, 1), 4)。起動の時に `kwl_cpu_count()` で決め、`KWL THREADS` に出す（決定 6 と pool の N の回答）。thread は手書きの face を初めて出した時（今の `kwl_hand_preload`）に N 本作り、終了まで残す。
- template の読み込み: pool の最初の job の前に 1 本が読む（他は cond で待つ）。main は待たない。
- 依頼: ink の写し（`struct kwl_hand_ink`、約 131 KB）、area の top と height、key（手書きの面で今書いている文字＝ 1 つの key）、世代。照合の途中の static の配列（xs・ys・starts）は thread ごとの物にする。
- 結果: `kwl_hand_result`。main の take で最新の世代だけを `keyboard.result` に入れ、`server->dirty`、今の log の行（`KWL OSK hand recognize ...`）を出す。
- 「N 文字」の解釈: 1 つの文字の照合は 1 本で、N 本で同時に N 文字まで（文字を続けて書くと、前の文字の照合を待たずに次の文字を始める）。同じ文字の依頼は最新の 1 件（§3.4）。

### 4.10 process の起動（S、J5 で確かめる）

- 初版は「Vulkan の前に fork した spawner の process」（ws.md の表、Q1 の解釈。ユーザーの原文には spawner の語は無い）。レビュー 7 の問題: pid を使う所が多い（`kill(ime->pid)` input-method.c:554・1248、
  `waitpid` desktop.c:905・input-method.c:281、`waitpid(-1)` と media の振り分け home.c:1460-1467、media・printd・account の同期の waitpid）、pid が後から来る間の kill、spawner の回収と kill の競合
  （再利用された pid を殺す）、spawner が control-fd・auth-fd を継いで sessiond が EOF を見られない、落ちた後に app を起動できない。
- 代案（推し）: libc の `posix_spawn` は vfork を使う（userland/base/libc/posix.c:199 `__vfork_spawn`、address space を写さない）。起動は lane（T7）の thread で `posix_spawn`（環境の配列と fd の動作を main が作って渡す）、
  pid は job の done で main に戻り、`waitpid(WNOHANG)`・`kill` は今のまま main に残す。pid が戻る前の kill の依頼は覚えておき done で送る。main は fork しない（P2 #6 の待ちは無くなる）。
  fork の後の `setenv`（今の kwl_spawn、async-signal-safe でない）も無くなる。
- J5（Q1 の決定）: posix_spawn の lane（T7）。spawner の process は作らない。

### 4.11 main に残る小さな直し（thread でない）

- clipboard の書き込みの spin（clipboard.c:314-342、100 ms の busy loop）: 残りを持って fd を poll の集合に入れ、POLLOUT で pass ごとに書く。
- `kl_backend_print_update` の毎 tick の `stat(printers.conf)`（print.c:399）と読み直し: T7 の周期の job（1 s、key つき）に。
- 言語の catalog の読み直し（settings.c:634、`ui.language` の変更）と apps.conf の読み込み（home.c:1719）、`home_prepare` の app ごとの `access()`（home.c:1841）: T7 の read の job、結果で main が差し替え。
- `getpwuid`（settings.c:456、/etc/passwd の読み）: 起動の時に 1 回写す（READY の前）。
- client の GPU buffer の import の ioctl（gpu-buffer-zedbsd.c:514-582、0.1〜2 ms、P2 #15）: commit の処理に同期で要るので main に残す（kernel は device を待たない写しの登録）。p017 で pass の時間を測って確かめる。

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
    （release の event）→ client の commit（描画の完了の fence）→ T4 が present（`vkQueuePresentKHR`、queue は §4.1 の lock の下）→ 前の image が画面から外れた事は、その image を再び acquire できる事で知る（T4 の acquire が同期で待つ）。
    `VK_EXT_swapchain_maintenance1` の present fence は present の資源（wait semaphore など）を再び使ってよい事を示すだけで、画面から外れた事は示さない（レビュー 6。spec の本文との照合は V2 の設計の Phase で）。
  - 暫定: (V2) が入るまでは今の backend の ioctl を T4 から呼ぶ（§4.4）。
- §9 の判断の点 J1（決まった）。

## 7. 待ちの点の行き先（P2 の表 #1〜#20 との対応）

| P2 # | 行き先 |
| --- | --- |
| 1 input の scan・close | T6（§4.5） |
| 2 direct scanout の出入り | T3（可否の query・claim・release・swapchain）＋ T4 |
| 3 quiesce・`image_layout` | T1（§4.1、frame の先頭の barrier・task・遅らせた破棄） |
| 4 displays.conf の fsync | T7（`saved` は書き終わりの結果で答える。protocol の意味は今と同じで、答えが後になるだけ） |
| 5 予備の font・emoji | T8 |
| 6 fork・posix_spawn | T7 の posix_spawn（J5） |
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

- 起動: wake pipe（最初、今の `kwl_threads_prepare` から分ける）→ backend の open（daemon の thread は最初の update で作る）→ T6・T7（lane）→ A1（壁紙の prefetch）→ Vulkan（compose）→ T1・T2 → T8（font）→ T9 → T5（input）→ T4 → T3 →
  READY → `KWL THREADS`。P1 は手書きの face を初めて出した時。
- 終了: event loop を出た後、client を閉じ、T3（display を閉じる依頼を完了まで）→ T4 → T5 → P1 → T9・A1 → T8 → T1・T2（今の compose の close）→ T7（書き込みを全て終え、fsync の後で止める。
  Log Out の設定の保存は今の `kwl_settings_store_finish` の位置で T7 に積んで待つ）→ T6 → daemon（backend の close）。

## 9. 判断の点（Q1・ユーザーへ）と制限

| ID | 点 | 推し |
| --- | --- | --- |
| J1 | game mode の Vulkan の形（§6） | **決定** 2026-10-29 ユーザー「V2 swapchain 共有 (推し)」: display の swapchain の image を export して client が直接描く。libvulkan の WSI・display は WS309 の範囲（P1）。暫定は T4 が今の ioctl |
| J2 | cursor thread も queue に submit する（§4.1） | **決定**（2026-10-29 Q1: 推しで OK、技術の判断）: main は使わず、render と cursor（と T4 の V2 の present）が lock の下で使う |
| J3 | spawner が落ちた後（§4.10） | **決定**（2026-10-29 Q1: 推しで OK）: 作り直さず、起動の依頼を errno で失敗にする |
| J4 | Linux・FreeBSD の seat 経由の入力の open（logind の D-Bus） | **決定**（2026-10-29 Q1・ユーザー）: 当面は同期のまま残し、WS309 の後の Phase に置く（別の WS にしない） |

| J5 | process の起動（§4.10） | **Q1 の決定**（2026-10-29、ユーザーの原文ではない。spawner の process は Q1 の解釈だったので取り下げ、ユーザーには Q1 が伝える）: io-file（T7）の thread から `posix_spawn`、pid・kill・`waitpid(WNOHANG)` は main |
| J6 | daemon の thread の基準（§4.8） | **Q1 の決定**（2026-10-29、ユーザーの原文ではない）: main で blocking の I/O をする接続だけ（audiod・networkd・bluetoothd・phone・volumed の 5 本） |

- 範囲の外・制限: Linux・FreeBSD の backend の daemon・audio の pump は今のまま。stdout の log の write（#14）は未確認のまま残す。shm の行の copy（#20）は残す。

## 10. 試験

- host（ASan+UBSan と TSan）: lane・pool・adhoc・台帳（submit・take・置き換え・stop・wake の取り落とし無し）、T6 の input の find（P2 の host 試験の --wrap の偽の /dev/input を作り直す）、
  T7 の書き込みの順と fsync、P1 の世代、process の起動（J5 の形で: pid の戻りと kill の保留）。
- 遅さの注入（レビュー 11）: host 試験で --wrap により fsync 2 s・device の open 80 ms・accept しない daemon・止まった present を作り、main の側の呼び出し（submit・take・tick）が 5 ms 未満で返る事を測る。
  thread の検査: host 試験の build だけで `KWL_THREAD_CHECKS` を定義し、`vkQueueSubmit`・present・fsync・open・fork の wrapper で「main の thread でない」事を assert する（製品の build には入れない、coding-style §12 の試験の切り替えではなく host 試験の link の差し替え）。
  game mode の中で後から作る cursor の形、frame が in flight の間に client が消える（ASan）。
- QEMU（Venus、p1 の名前・port を分ける）: 起動の確認（boot-test.sh）、`KWL THREADS` の行、WS099 の基準の C1・C2・C9、GPU の境界の試験（forge・fence）、`KWL PERF pass` の pass_max_us。
- 実機（RDNA・VF2）は Q1 の割当で（未定）: game mode の出入りの SLOW_PASS、壁紙の変更・回転の間の frame。

## 11. Phase の順と依存（レビュー 12）

- GPU の順は固定: (1) fence 付きの task と queue の lock → (2) 遅らせた破棄（serial、descriptor set、二重の image）→ (3) quiesce と main の `vkDeviceWaitIdle` を消す → (4) T3。
- process の起動（J5）が入るまで `waitpid(-1)` は消さない。
- Linux・FreeBSD の backend の stub（`drm/display-drm.c` の scanout、evdev の find）は、それぞれの API を変える Phase で一緒に直す。
- 結果の順と相手の生存（レビュー 13）: lane をまたぐと結果の順は保たれない（T7 の saved と T3 の完了）。client に答える done（heads.c:786 の `saved` の答えなど）は、client と object の世代を確かめてから答える。
- Phase の表は [ws.md](ws.md)。
