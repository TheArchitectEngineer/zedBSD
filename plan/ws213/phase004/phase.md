<!-- awesome-plan project=zedbsd record=ws213-p004 -->

# ws213-p004: S2: 描画 thread・cursor の thread・入力の thread（2 段の pull）

Parent: [WS213](../ws.md)
Status: in-progress（2026-10-12 Q1「p004 に進む」。段 I（入力の thread、main に merge 済み）・段 C（cursor の thread）を実装、R は次）

## 依存と再開の条件（review-4 L-f）

- p003 の cleared と、p003 の 5 の閾値（walk の p90・最長）を満たすこと。満たさなければ WS211 の描画の削減（P3）を先にし、その結果で p003 の 5 を測り直して満たした後に着手する。
- 受け入れ 5 の box_lag の最長を 1 core で満たせない原因が kernel の中の長い transaction（p001 の 11 節 9、`KWL CURSOR_LAG` と kernel の `hold_us` の時刻の対応で確かめる）なら、uncleared にして WS208（SMP）・kernel への依存として Q1 に返す（Q1 の判断）。p99 は必達のまま。
- C と R が同じ VkQueue を同時に使うことの libvulkan の安全は WS141 p016 B-u が明記する前提（p001 6.3、review-B4 M-d）。
- p004 の始めに確かめる事（p001 11 節 3・9・12）: `event_us` と `kwl_microseconds` の時計が同じ基準か（違えば box_lag の代わりに超過分で判定）、zedBSD の evdev の fd を別の thread から `poll` してよいか、kernel の preempt と tick の周期・decode が preempt を止めるか、I の pthread の優先度を上げる手段、`seat-freebsd.c` の fd を閉じる順。
Phase disposition: normal

## 範囲

- p001 の 2〜6 節: want（番号は種類の変化だけ、heads の slot と世代）と ready、取り消し、R の持つ画像（出力の k と slot ごとの head、取り消しと park を越えて使い回す）、compare-and-clear、R は ready の後 Vulkan を呼ばない不変条件、破棄は R の step 1、R と C の両方を止める park と失敗の `_exit`、R→M の 1 本の FIFO、lock の規則（4.6）、host image の書き込みは walk と park の中だけ、未着の glyph の list、cursor の 2 枚（中身の大きさ、全体の写し、pin、plane の元の制限）、cursor の thread C（C0 を C へ、専用の command pool、M と C の持ち物の分け方、失敗の経路、計数）、入力の thread I（evdev・touchpad・加速・位置を受け箱へ、M へ順序付きの FIFO、warp の番号）、client の破棄の延ばし（4.9）、Mesa の build の queue_lock（新しく足す）。
- review-5 と WS141 review-B4 で足した物（p001 第 6 版）: device の付け外しを I への依頼に（付ける、同期の外すと上限付きの ack、backend の 3 つの callback と session の pause、logind の resume の close の順の入れ替え）、絶対の pointer を I が受け箱に（warp の番号を進めない）、I の位置の計算と M の warp を `cursor_lock` の 1 区間で（`band_replay` は一時の書き換え）、I と M の持ち物の表、`set_cursor` の写し元、C の copied の gate を acquire した画像の fence だけに（pin は飛んでいる copy の数）、import の layout の溢れと shm の画像の layout を一覧の barrier にして M の `vkQueueWaitIdle` を外す、`KWL CURSOR_LAG` と box_lag の計数。
- `kwl_compose_quiesce` と `->in_flight` の全部の割り当て、`KWL LAT` の定義し直し、`KWL PERF main` の `pass_max_us`・`walk_max_us`・`vk_outside_max_us` と `KWL PERF cursor`（p001 6.4）。

設計は [p001](../phase001/phase.md)。

## 受け入れの条件

1. host: `plan/ws213/tests/` に p001 の 9 節 (b)〜(i)・(k)（(k1)〜(k4) を含む）・(l) の決定的な試験（M・R・C・I の手順を 1 歩ずつ）と (m) の本物の thread と stub の Vulkan の TSan の試験、全部 PASS（TSan の報告 0、外部同期の object の重なり 0）。
2. build warning 0（下のコマンド）。
3. Linux の build を ThreadSanitizer 付きで、WS131 の Debian の QEMU+KVM guest で compositor を 10 分動かし（guest の uinput を SSH で: pointer の円 60 秒、App Home の開閉 10 回、title の drag 30 秒、app の起動）、WS213 の thread と lock に触れる報告 0（T1）。Mesa の中は `called_from_lib:` で抑え、既存の thread どうしの報告は `plan/ws213/tests/tsan.supp` に理由付きで置き Q1 に ticket を頼む（p001 9 節）。C はこの guest では通らない（plane が開かない、p001 3.3）。
4. T1（i915・Venus）: shot の画が前と同じ、`KWL VULKAN_ERROR`・`KWL FAILED` なし。T1 の 1 回は 30 分まで。
5. Pi（1 boot、ユーザーか Q1 の手で、p001 6.4）: 場面 (1) pointer だけ 10 秒、(2) pointer を動かしながら App Home を 5 回開閉、(3) 窓の drag 10 秒、(4) Music・Settings の起動 1 回ずつ。各場面の `KWL PERF cursor` の `box_lag_p99_us` ≤ 16700（**必達**）と `box_lag_max_us` ≤ 16700（超えた原因が kernel の長い transaction なら上の依存で uncleared、`KWL CURSOR_LAG` と `hold_us` の時刻の対応を結果に記録。時計が確かめられなければ `box_excess_p99_us`・`box_excess_max_us` ≤ 16700 を代わりに）、`KWL PERF main` の `pass_max_us` ≤ 33300（場面 (1)〜(3)。(4) の app の起動は `pass_p99_us` を記録）と `vk_outside_max_us` ≤ 16700、`box_gap` は記録だけ、`KWL LAT` の入力→walk の中央値が p003 より改善、C の present の全段の最長（`present_max_us` と内訳）を記録（画面の cursor の 60fps の判定は WS141 p016 (B) の後）。

## コマンドと上限

- build（warning 0、出力は自分の `build/ws213-p004/` に、review-4 L-b）: `make -j16 ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/ws213-p004/arm64 build/ws213-p004/arm64/bin/wayland`、`make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws213-p004/amd64 build/ws213-p004/amd64/bin/wayland`、`make -j16 KEILAND_ASK=n KEILAND_LINUX_BUILD=build/ws213-p004/linux keiland-linux`（`KEILAND_ASK=n` で package の導入と install を聞かない、`Makefile:476-482`）（各 900 秒まで）。FreeBSD は T1 の guest（`plan/tools/keiland-freebsd/backend-test.sh`）。
- build の注記（p001 review-5 の答え (4)、Q1）: `BUILD=` の override は動き（`Makefile:79`）、libc.so なども自分の dir に作るので最初の build は長い（`vmunix.mk:411`）。LLVM と sysroot は共有のまま使う（wayland は `$(DYNAMIC_SYSROOT)/usr/lib/crt1.o` に依存、`userland-dynamic.mk:239`）。**make が共有の sysroot を作り直そうとしたら（sysroot の規則が動き出したら）止めて main に報告する**。toolchain と共有の `build/` は変えない。
- host 試験は各 120 秒まで。同じ条件で変更なしの再試行は 3 回まで。QEMU・実機は T1 と Q1 経由（担当は QEMU を起動しない）。

## 実行（2026-10-12 P4、agent/p4）

段に分ける: I（入力の thread と受け箱）→ C（cursor の thread）→ R（描画の thread、2 段の pull・park）。各段で host 試験と WIP commit、区切りで Q1 に merge を頼む。

### p003 の small の 1 回の超過（17.29ms）の場面（Q1 の依頼、Pi place17 の session.log）

- `KWL PERF walk` の small の max 17.29ms（log の 4259 行の窓）は、App Home から Files を起動した窓（`KWL HOME launch name=Files`、Files の窓の最初の map、titlebar の control・menu の 42 回の commit、`KWL GLASS launch`、frame 190〜195）。middle の max 17.39ms（6119 行の窓）も client 9 の窓の最初の map（`KWL MAP client=9`、`SHM_COPY`）の窓。どちらも場面 (4)（app の起動）で、walk の中の初出の仕事（新しい窓の shm の画像の作成と写し・import の descriptor set・titlebar の字）と見られる。場面 (1)〜(3) の窓の small・large の max は ≤ 2.67ms。p004 の受け入れでも (4) は `pass_p99_us` の記録だけ（6.4）なので、閾値の判断は変わらない。

### 始めに確かめた事（p001 11 節 3・9・12、source を読んだ。Pi での実測ではない）

1. **pthread の優先度**: I の優先度を上げる手段は無い。libc に `pthread_setschedparam`・`sched_setscheduler` は無く、`pthread_attr_setschedparam` は 0 以外を `ENOTSUP`（`userland/base/libc/pthread.c:1014-1029`）。`setpriority`/`nice` は `process->nice_value` を書くだけで sched は読まない（`src/kern/syscall.c:8656-8729`）。user の thread は全部 `SCHED_PRIORITY_DEFAULT`（`src/kern/thread.c:101,175,395`）。→ I は M・R・C と同じ優先度。下の 2 のとおり同じ優先度の起床で横取りされるので、p004 は kernel の変更なしで進める（優先度の API が要ると分かったら Q1 に理由を送る）。
2. **sched の横取りと tick**: Pi の tick は 1ms（`HAL_TIMER_FREQUENCY`=1000、`src/hal/arm64/bsp-rpi4/clock.c:29-41`）、quantum 10ms。同じか高い優先度の thread が起きると `cpu->preempt` が立ち（`src/kern/sched.c:1812-1835`）、user への戻り（763-774）か tick（957-1004、`preempt_count==0` の時、kernel の中の実行も）で切り替わる。同じ CPU の起床は割り込みを送らないので、遅れは最大 1 tick（1ms）。
3. **decode が preempt を止めるか**: 止めない。`hold_us decode` の decode は kernel thread `worker_main`（`render-worker.c:121`、既定の優先度）が sleep できる `controller->mutex`（`render-device.h:75`、`render-worker.c:249-317`）の下で行い、`preempt_count` も割り込みも触らない（`spin_lock_irqsave` は decode の前後の短い区間だけ）。tick で decode の途中でも I に切り替わる。I は GPU の mutex を取らないので decode を待たない。→ p001 11 節 9 の「decode の間 I が待つ」見込みは外れ、box_lag の最長を決めるのは decode ではなく 1ms の tick と他の `spin_lock_irqsave` の区間の見込み（Pi で `KWL CURSOR_LAG` を見る）。
4. **evdev の fd を別の thread から poll**: 可。fd は process の表（`process->fd`）で全 thread が共有、`input_poll`（`src/drivers/generic/input.c:2041-2077`）は待ち手の制限が無く、poll の待ちは全体の waitq（`src/kern/poll.c:31-127`）。別の thread の close と並んでも poll は object の参照を持ち、閉じた fd は `POLLNVAL`（`poll.c:319-337`）。
5. **時計**: 同じ基準。evdev の時刻は `clock_milliseconds`（`kernel_ticks`、`src/drivers/generic/input.c:1725-1731`）、`kwl_microseconds` の `CLOCK_MONOTONIC` も `clock_ticks()`（`src/kern/clock.c:507-548`）。どちらも boot からで分解能は 1ms。→ box_lag（`event_us` → 書き込み）で判定できる（代替の超過分は使わない）。値は 1ms 刻み。
6. **fd を閉じる順**: FreeBSD の `seat_disable`（`seat-freebsd.c:496-535`）は `session_paused` → 各 lease の `input_gone` → `lease_close` の順で、callback が close の前（設計どおり）。Linux logind の gone（`seat-logind-linux.c:676-686`）も callback が先。logind の `seat_resume` は古い fd を close してから `input_resumed`（`seat-logind-linux.c:730-741`）なので、設計の保険（callback の後に close）を p004 で入れる。

### 段 I: 入力の thread と cursor の受け箱（2026-10-12 P4）

- **新しい file**: `userland/desktop/wayland/cursor-box.{c,h}`（受け箱: 位置・出力・`event_us`・書いた時刻・warp の番号・書いた回数・出力の矩形の写し、`cursor_lock`＝box の mutex と C を起こす condvar。I の相対の動き `kwl_cursor_box_move`、絶対の pointer `kwl_cursor_box_place`（warp の番号を進めない、N-2）、M の warp `kwl_cursor_box_warp`（受け箱の位置から純粋な関数で計算し、出力の矩形の写しと同じ区間で書いて warp を進める、N-3）、矩形だけの更新、読み）、`input-thread.{c,h}`（I: device の表（I だけが触る）、M→I の命令の列（付ける・外す、`cursor_lock` の下）と設定の世代、I→M の順序付きの FIFO（`input_lock` の下の list＋M の wake pipe）、外しの ack（FIFO の device の最後の event と同じ区間で `acked_sequence`、condvar、M は 100ms まで待つ）。I は Vulkan・printf・backend の seat・server に触れない。evdev の読み取りと device の間の時刻の順（BUG-142）、SYN_DROPPED、加速、touchpad の層と tap の tick（touchpad がある間 poll は 10ms まで）、touchpad の速さの端数、絶対の pointer は I。device の EOF・失敗は I が自分で外して ENDED、ENODEV は REVOKED を最後の event に）。
- **M の側**（input.c）: `kwl_input_drain`（FIFO を順に当てる: mouse・keyboard の frame、touchpad の action、tablet・touch の report（M の record の `frame` に写して tablet.c・touch.c）、DETACHED）。位置は event の motion を M の見る位置に当て、warp の番号が今の物でなければ使わない（ボタン・key は配る）。`KWL POINTER output=` の log と dirty、`pointer_unmoved`、`pointer_event_us`、`input_ms`・`input_events` は M。device の record は M の物だけ（I の持ち物の `accel`・`pad`・`abs_x/y`・`discarding` を record から外し、I に渡す `reading` を足した）、`attached`・`closing`（最後の event まで record と path を残す）。`kwl_input_forget`・`kwl_input_pause`・`kwl_input_resume`・`kwl_input_detach_all`（session の pause）・`kwl_input_attach_all`（session の resume、seat が fd を残した device）。外しが上限を超えたら `KWL FAILED site=input-detach` で `_exit`。`kwl_input_sync`（毎 pass、poll の前に設定と出力の矩形）、`kwl_pointer_warp`・`kwl_pointer_warp_to`。
- **warp**: `heads_evacuate`・`heads_windows_follow`（heads.c）、出力の大きさの clamp（output-switch.c）、pen tablet・touch screen の位置（tablet.c・touch.c）を受け箱の warp に。`kwl_pointer_relative` は無くした。`band_replay`・keyboard.c の一時の書き換えは M の見る位置だけ（変えていない）。
- **main loop**: evdev の fd を poll しない。代わりに threads の wake pipe（`server->wake`）を poll し、空にしてから FIFO を取る。`kwl_input_tick` は I へ。終了は `kwl_input_threads_stop`（join）の後に `kwl_input_cleanup`。
- **backend**: `backend-host.c` の pause・resume・gone・session の callback を上の関数に。`seat-logind-linux.c` の `seat_resume` は古い fd を callback の後に close（6.1 N-1 の保険）。
- **計数**: `KWL PERF pass passes= pass_p99_us= pass_max_us=`（M の 1 pass の work）、`KWL PERF cursor box_writes= dropped= box_lag_p99_us= box_lag_max_us= box_lag_over= box_excess_p99_us= box_excess_max_us= box_gap_p99_us= box_gap_max_us=`（M が当てた motion ごと）、`KWL CURSOR_LAG event_us= write_us= lag_us=`（16.7ms 超え、5 秒の窓に 20 行まで）。C の present の計数は段 C で `KWL PERF cursor` に足す。`walk_max_us`・`vk_outside_max_us` は段 R。
- 段 I の時点で cursor の plane の present は今どおり M（`server->pointer_x`）。受け箱を読む C は段 C。

確かめ（host、2026-10-12）:
- `sh plan/ws213/tests/input-thread-host-test.sh` → PASS（120 checks、plain と ThreadSanitizer）。1 歩ずつ: motion が M の取る前に受け箱に入る、motion→ボタン→motion の順、warp の後の相対の動きは warp の位置から・warp の前の motion は古い番号、設定と出力の矩形は次の report から、絶対の pointer は anchor に写し warp を進めない、外しの ack は I の pass の後で device の最後の event の後（ack の前の待ちは ETIMEDOUT）、外した番号を別の pipe に使い回しても読まない、EOF で ENDED、touchpad の tap の click が I から、押したままのボタンの release が DETACHED の前、touch screen の report の写し。thread: 本物の I と書き手の thread、3 つの device の 20000 report が全部 1 回ずつ順に届く、M の warp・設定・出力の変更と 40 回の外し・付け直し（外した番号を毒の pipe にして読まれないこと）、TSan の報告 0。
- `cursor-due-host-test.sh` PASS（main の 2ef0688a8 で cursor-plane.c が `vkdemo_display_create_swapchain_exact` に変わったので stub の名前を直した）、`list-replay-host-test.sh` PASS。
- build（warning 0）: rpi4・amd64（自分の worktree の `build/arm64`・`build/amd64`、`-o .../sysroot/.zedbsd-sysroot-complete`）、`make keiland-linux`。`style-check.py` で新しい file と触った input.c・main.c の指摘 0（試験の .c は p006 で）。
- 未実施: Pi・T1。段 I だけでも Pi で `KWL PERF cursor` の box_lag を測れる（C が無いので画面の cursor は M の present のまま）。

### 段 C: cursor の thread（2026-10-12 P4）

- **新しい file**: `userland/desktop/wayland/cursor-thread.{c,h}`（C: plane の present の状態の全部（swapchain・**専用の VkCommandPool**・command buffer・fence・semaphore・`held[]`・present の記録）、受け箱を読んで C0 の規則（最初の動きと像の変化はすぐ、同じ像の動きは refresh − 2.5ms まで待つ）で present。`kwl_cursor_thread_step` が 1 歩（受け箱の写し → 終わった copy の pin を下ろす → 判断 → present）、thread はその間に受け箱の `sequence` の変化か期限を condvar で待つ。copied の gate はやめ、待つのは acquire した swapchain の画像の前の copy の fence だけ（review-B4 F1 (b)）。失敗（acquire・copy・present）で `failed` を立てて以後 Vulkan を呼ばず、M の pipe を起こす（M の持ち物を消さない）。停止は自分の fence を 100ms ずつ待ってから終わり、終わらなければ M が `KWL FAILED site=park` で `_exit`。park（`kwl_cursor_thread_park`）は答えるまで M が 2 秒まで待つ。計数は present ごとに受け箱の `stats` に足す）。
- **受け箱**（cursor-box.h）に C の物を足した: M が書く像（cursor の形・arrow・client の cursor の 2 枚の copy のどれか）とその世代・version・hotspot・出すか、park・stop、C が書く parked・failed・2 枚の pin・計数、C が待つ変化の番号 `sequence`、C の答えの condvar。
- **cursor-plane.c は M の側に**: `struct kwl_cursor_plane`（compose.h）を M の持ち物（open・shown・extent・2 枚の copy・最後に受け箱に書いた物・parked・C への pointer）に分けた。open で swapchain（`vkdemo_display_create_swapchain_exact(..., VK_IMAGE_USAGE_TRANSFER_DST_BIT)`、main 2ef0688a8 の P2 の修正を保持）・C 専用の pool と object・2 枚の copy（plane の extent の host image、透明で初期化、`written` を立てないのでコマは写さない）を作って C を起こす。close は C を止めて（join）から壊す。`kwl_compose_output_close` は `vkDeviceWaitIdle` の前に plane を閉じる（C を先に止める）。毎 pass の `kwl_cursor_plane_update` は C の failed を見たら plane を閉じて composite、park を解き、像を決めて受け箱に書くだけ（位置は I）。client の wl_shm の cursor は 2 枚の copy へ全体を写す（外側は透明）。書くのは受け箱が今見せていなくて pin が 0 の copy だけで、両方ふさがっていれば次の pass（4.7 の 3）。C は受け箱から copy を取る時に同じ区間で pin を足し、copy を記録しなければすぐ下ろし、記録したらその fence の完了を見た時に下ろす。
- **park**: sleep の依頼の保留・game mode（scanout）・window mode に入る途中（EAGAIN）の pass で `kwl_cursor_plane_hold`（C は答えて Vulkan を呼ばない）、次の `kwl_cursor_plane_update` で解く。session の pause は output の close で C を止める。
- main loop の `kwl_cursor_plane_due_ms`（M を C0 のために起こす）は無くした（C が自分で待つ）。
- **箱と pipe の作成を前に**: `kwl_threads_prepare`（wake pipe と受け箱）を `kwl_compose_open` の前に（plane の open で C が受け箱を使うため）。I の開始で受け箱に最初の位置と出力の矩形を warp で書く。
- **計数**: `KWL PERF main` の cursor の欄は C の計数（受け箱から取る）、`KWL PERF cursor` に `presents= held= copies= present_gap_max_us= present_max_us=`（C の present の全段の最長）を足した。`KWL CURSOR_PLANE present` の行は C が出す（`input_us=` は無くした、`event_us=` は受け箱の report の時刻）。

確かめ（host、2026-10-12）:
- `sh plan/ws213/tests/cursor-due-host-test.sh` → PASS（34 checks、plain と ThreadSanitizer）。cursor-plane.c・cursor-thread.c・cursor-box.c を本物で、Vulkan と時計を fake に書き直した（9 (i) と C0 を C に移す）。1 歩ずつ: arrow を受け箱へ、最初の動きはすぐ・hotspot を引いた位置、refresh の中の動きは refresh − 2.5ms まで待ち最新の位置で present、止まった pointer は変化だけを待つ、新しい像は待たない、copy を飛ばしたまま別の swapchain の画像から present して前の copy の fence を待たない、copy の間は pin、pin の立った copy と見せている copy には M が書かない（書けなければ次の pass）、fence の完了を見て pin を下ろし M が書く、計数を受け箱から読める、present の失敗で failed が受け箱に立ち以後 Vulkan を呼ばない、lock を持ったままの Vulkan の呼び出し 0。thread: 本物の C と 1000Hz の書き手と M（像の変更・update・park）を約 2 秒、fake の GPU が copy の元が submit から fence までに書き換わらないことを見る（0 件）、park の間 present しない、停止で copy を待つ、TSan の報告 0。script は cursor-plane.c・cursor-thread.c に `compose->pool` が無いことも grep で見る。
- `input-thread-host-test.sh` PASS、`list-replay-host-test.sh` PASS（一覧の外の vkCmd* の例外を cursor-plane.c から cursor-thread.c に替えた）。
- build（warning 0）: rpi4・amd64・keiland-linux。style-check: 新しい・触った file の指摘は plane_find の元からの 3 件だけ（p006）。
- 未実施: Pi・T1。Pi では段 C で初めて画面の cursor が M と切り離される（WS141 p016 B-u と合わせて判定）。
