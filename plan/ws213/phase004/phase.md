<!-- awesome-plan project=zedbsd record=ws213-p004 -->

# ws213-p004: S2: 描画 thread・cursor の thread・入力の thread（2 段の pull）

Parent: [WS213](../ws.md)
Status: in-progress（2026-10-12 Q1「p004 に進む」。段 I・C（main に merge 済み）・R（描画の thread）を実装、T1・Pi の確かめ待ち）

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

### 段 R: 描画の thread（2 段の pull、2026-10-12 P4）

- **新しい file**: `userland/desktop/wayland/render-thread.{c,h}`（R の pull の手順だけ。Vulkan の仕事は compose.c が渡す 4 つの op（出力の acquire（50ms ずつ）、head の acquire（待たない）、一覧の記録・submit・present、fence の待ち（100ms ずつ））で行い、host 試験は fake の op で同じ code を動かす）。
  - want（番号・種類（コマ / layer の絵）・要る heads）は M が `kwl_render_set_want` で置く。番号は種類が変わった時（と下ろした後に立て直した時）だけ進み、heads の list の置き換えでは進まない（4.1）。
  - R の 1 歩（`kwl_render_step`）: IDLE で want を見て、コマなら持っていない出力の画像と持っていない heads を acquire し、持っている全部を付けて ready。ANSWER では M の答えを待つ間 Vulkan を呼ばない（3.2 の不変条件）。取り消しなら画像を持ったまま IDLE へ。一覧（job）なら want を compare-and-clear で下ろし、job の画像を帳簿から落とし、記録・submit・present、FLIGHT で fence を待って完了を知らせる。出力の acquire の失敗は want を compare-and-clear で下ろして知らせる。完了は必ず次の ready より前（R→M の 1 本の FIFO、4.5）。完了の message は job を取る前に作る（失われない）。
  - park（`kwl_render_park`、M は 2 秒まで待つ）: R は飛んでいるコマ（と、一覧を渡されてまだ取っていない job）を終えてから止まる。止まっている間に M は `kwl_render_forget` で作り直す swapchain の画像を帳簿から落とせる。stop も同じ（飛んでいるコマの後）。
- **compose.c**: `kwl_compose_draw` は ready（今の want の番号のコマ）が来ていれば walk して一覧を R に渡す（`in_flight` は渡してから完了を当てるまで、`server->frame++` は渡した時）、来ていなければ want だけ置いて 0 を返す。want は `kwl_schedule` が dirty・damaged の時に pacing の待ちの前に置く（R が待ちの間に acquire を済ませる）。layer の静かな絵（`kwl_layers_tick`）は `kwl_compose_want_layer` が ready を返した時に walk して `kwl_compose_submit_quiet` で渡す。R の message は `kwl_compose_poll`（毎 pass、wake pipe を空にした後）で順に当てる: ready は保存（want が違えば即取り消し）、acquire の失敗は出力の消失か失敗、完了は今の `kwl_compose_complete` の後ろ半分（layer の完了、scene、保持した buffer の解放、frame callback、shot、pacing）と present の結果（`switch_proving`・`output_lost`・`kwl_displays_move_settled`）と heads の present の結果と `KWL LAT recorded/queued/submit`・`KWL DRAWS` の行。fence の fd の export と M の fence の poll はやめた（`kwl_compose_waiting` は常に 0）。`kwl_compose_quiesce`・`kwl_compose_complete` は R の完了を待って当てる。
- **heads.c**: `kwl_heads_acquire`・`kwl_heads_waits`・`kwl_heads_signals`・`kwl_heads_present` を、M の `kwl_heads_wanted`（want の heads）・`kwl_heads_take`（walk で R の持つ head を全部描く、持っていない要る head は dirty・missed、失敗は lost）・`kwl_heads_presented`（完了で dirty・lost を当てる）と、R の op（compose.c、job の heads で wait・signal・present）に分けた。R は head の状態を書かない（4.1）。
- **park と swapchain**: `kwl_compose_park`・`kwl_compose_unpark`（入れ子）・`kwl_compose_forget`（帳簿から落とし、出す前の ready を取り消し、出力の画像を持っていたら acquire の semaphore を作り直す、review-3 M-5）。出力の open・close、`kwl_heads_sync`、`heads_close` を park の中で行う。出力の close では want も下ろす。
- **M の queue・pool の使用**: walk の外の `import_layout`（import の溢れ）と shm の `image_layout` は `kwl_render_wait_idle`（R が job を持っていない）の後に、glass の `vkDeviceWaitIdle` 4 か所は `kwl_compose_quiesce` の後に。R の Vulkan は job の外では acquire（swapchain・semaphore）だけなので、job の無い間の M の作成・破棄・submit とは重ならない（Vulkan の外部同期: command pool・queue・descriptor pool は job の間 R だけ）。
- **設計からの違い（記録）**:
  1. object の破棄を R の step 1 にしていない（4.2・M-C）。M の破棄は今どおり飛んでいるコマが無い時（quiesce・retire の in_flight の確かめ）だけで、その時 R が呼ぶのは acquire だけなので重ならない。
  2. client の破棄の延ばし（4.9）はしていない。`objects.c:554` の quiesce は R の完了を待つ（今と同じく最大 1 コマ M が止まる）。
  3. walk の外の host image の staging の書き込み（IME の tick の glyph など、5 節 H-b）は今のまま（今も GPU の copy と重なり得る種類で、1 コマの乱れ）。p005 で W と合わせて扱う。
  4. Mesa の build の `queue_lock`（3.3）は足していない。Linux・FreeBSD では plane が開かず C は queue を使わず、M は job の間 queue を使わない（上）ので、queue を同時に使う thread が無い。
  5. 一覧の buffer は 1 つ（M は飛んでいるコマがある間 walk しないので足りる）。
- **計数**: `KWL PERF pass` に `walk_max_us=`（窓の walk の最長）と `vk_outside_max_us=`（M の walk の外の Vulkan: GPU buffer の import・解放、shm の写し、飛んでいるコマの待ちの最長）。`KWL PERF compose` の `draw_ms` は M の walk と渡しまで、`acquire` と `submit+present` は R の時間。`KWL LAT acquired` は R の acquire の時刻（ready を当てた時に出す）、`recorded`・`queued`・`submit` は R の時刻（完了を当てた時に出す）、`shown` は M が完了を当てた時（11 節 4）。

確かめ（host、2026-10-12）:
- `sh plan/ws213/tests/render-thread-host-test.sh` → PASS（145 checks、plain と ThreadSanitizer）。1 歩ずつ（9 (b)(c)(e)(h)）: want が無いと acquire しない、want で出力と要る head を acquire して ready、heads の list だけでは番号が進まない、種類が変われば番号が進み取り消しで画像を持ち続け次の ready で acquire し直さない、head を二度 acquire しない、一覧で持つ head も全部 present、完了が次の ready より前、古い番号の一覧は新しい want を下ろさない（compare-and-clear）、acquire の失敗で want が下りて知らせ立て直すまで acquire しない、失敗の間に M が立て直した want は残る、park は飛んでいるコマの後・ready の答え待ちでも止まり何も acquire しない、forget で落とした画像は acquire し直す、渡された一覧は park より先に走る、submit できなかった job はすぐ完了。thread: 本物の R と、want・walk・答え・取り消し・park・forget を乱数で 10 万歩の M、op が walk の間と lock を持ったまま呼ばれないこと、acquire していない画像を present しないこと（初めの版で 1 件の取りこぼしを見つけて直した: park が「渡されたがまだ取っていない一覧」を残して止まり、forget の後にその一覧を走らせていた → 一覧が渡されている間は park・stop を待たせる）、TSan の報告 0。
- 他の host 試験 PASS（input-thread・cursor-due・list-replay）。
- build（warning 0）: rpi4・amd64・keiland-linux。ThreadSanitizer 付きの Linux の build も通る: `make -j16 KEILAND_ASK=n KEILAND_LINUX_BUILD=build/keiland-linux-tsan CC="gcc -fsanitize=thread -Wno-tsan" KEILAND_LINUX_OPT="-O1 -g" keiland-linux`（`-Wno-tsan` は既存の `libkeiland-backend/audio/pump.c:815` の `atomic_thread_fence` の警告のため。その pump の報告は既存の thread どうしの物として扱う）。
- 試験の道具: `plan/ws213/tests/uinput-drive.c`（guest の uinput の mouse と keyboard、`all` で円 60 秒・super 10 回・drag 30 秒）、`plan/ws213/tests/tsan.supp`（Mesa の driver と LLVM の中だけ）。
- 未実施: T1（i915・Venus・Linux の TSan の guest）と Pi。

### T1 と Pi で見る事（段 I・C・R をまとめて）

- T1（i915 の 5330・Venus の QEMU、30 分まで）: shot.c の画が前と同じ、`KWL VULKAN_ERROR`・`KWL FAILED` なし、App Home の開閉・窓の drag・終了（終了で `KWL FAILED site=park` が出ないこと）、`KWL PERF pass`・`KWL PERF cursor` の行が出ること。FreeBSD の build（`plan/tools/keiland-freebsd/backend-test.sh`）。
- T1（WS131 の Debian の QEMU+KVM guest、受け入れ 3）: 上の TSan の build を guest に入れ、`TSAN_OPTIONS="suppressions=plan/ws213/tests/tsan.supp"` で compositor を 10 分。guest の中で `gcc -O1 plan/ws213/tests/uinput-drive.c -lm -o uinput-drive && sudo ./uinput-drive all`、加えて app の起動を 1 回。判定は WS213 の thread（input・render、Linux では cursor の thread は plane が無く動かない）と lock に触れる報告 0。既存の thread どうしの報告は `tsan.supp` に理由付きで足し Q1 に ticket を頼む。
- Pi（1 boot、ユーザーか Q1 の手で）: 場面 (1) pointer だけ 10 秒、(2) pointer を動かしながら App Home を 5 回開閉、(3) 窓の drag 10 秒、(4) Music・Settings の起動 1 回ずつ。各場面の `KWL PERF cursor` の `box_lag_p99_us` ≤ 16700（必達）と `box_lag_max_us`（超えたら `KWL CURSOR_LAG` の行を kernel の `hold_us` の行と並べる）、`KWL PERF pass` の `pass_max_us` ≤ 33300（(1)〜(3)）と `pass_p99_us`（(4)）、`vk_outside_max_us` ≤ 16700、`KWL PERF cursor` の `presents`・`present_max_us`、`KWL LAT` の入力→walk（`draw`）が p003 より短いか。画面の cursor の 60fps は WS141 p016 (B) の後。

### place20 の停止（2026-10-12、段 R の直し）

- 症状（ユーザー「place20でApp Homeが固まっています」、Pi、Q1 が写した session.log）: frame 124（`KWL COMPOSE frame=124 at_ms=526736`、3308 行）の後、`KWL LAT acquired` が二度と出ず（最後は 3304 行の frame=124）、`walk_max_us=0` が続いた。cursor の plane と M の pass は回り続けた。
- 原因: want の取り消し（compare-and-clear）を R が一覧を**取った時**に行っていた（旧 `render_answer` の `if (render->want.number == job.number) render->want.kind = KWL_RENDER_NONE;`）。M は一覧を渡すと自分の `want_kind` を NONE にするので、R が一覧を取る前に次の pass の `kwl_schedule`（`display.c:222`、dirty の時の `kwl_compose_want_frame`）が同じ種類（FRAME）の want を置くと、`kwl_render_set_want` は R の want がまだ FRAME なので番号を進めず、渡した job と同じ番号を返す。その後 R が一覧を取った時に番号が一致して、置き直した want を下ろす。M は「want FRAME n を置いた」と思って同じ want を二度と伝えず（`compose_want` の same）、R は want が無いので待つ: 両方が相手を待つ。frame 124 は一覧を渡してから R の recording の終わりまで 15 ms（`recorded at_us=526751000`、3332 行。frame 122・123 は 4〜8 ms）で、R の起床が遅れた間に App Home の開く動き（3311 行 `HOME opened at_ms=526760`）の dirty が want を置き直した。layer の静かな絵（番号を進める）は dirty が残るので走らず、回復しなかった。
- 直し（render-thread.c）: 一覧の答え（`kwl_render_answer` の LIST）が、M の lock の中で want を下ろす（番号が同じ時だけ）。M の `want_kind = NONE` と同じ時点になるので、その後に置く want は必ず新しい番号になる。R は一覧を取る時に want に触れない。
- 試験: `render-thread-host-test.c` に、一覧を渡した直後・R が取る前に同じ種類の want を置く場面（新しい番号・取った後も want が残る・完了の後にその ready）を足した（直す前は 4 件 FAIL を確かめた）。thread の部の M を compose.c と同じく「種類が変わった時だけ want を伝え、一覧を渡したら下ろす」にし、M の want が R の want と違う瞬間（want の消失）を毎歩数える（直す前の render-thread.c では FAIL）。
- 診断の行: `KWL RENDER stalled waited_ms=… want=… number=… ready=… ready_number=… in_flight=… park_depth=… | thread want=… number=… answer=… answer_number=… park=… parked=… busy=… held_output=… messages=… last_ready=… last_completed=…`。M が R を 1 秒待った時（飛んでいるコマの完了、または want に ready が来ない）に、初めは 1 秒ごと 5 行、その後は 1 分ごと（`compose_stalled`、`kwl_compose_poll` の毎 pass、R の側は新しい `kwl_render_status`）。
- 確かめ（host）: render-thread（151 checks）・input-thread・cursor-due・list-replay の各 host 試験 PASS（plain と ThreadSanitizer）。build warning 0: rpi4（`build/arm64/bin/wayland`）・amd64・keiland-linux。Pi は未実施。

### Pi の USB タッチモニターの hotplug の後の exit 1（2026-10-12、P5）

- 症状: Pi 4（place23、段 I・C・R 入り）に 10 インチの USB タッチモニター（hub 1a40:0801 の下の 056a:5131、`/dev/input/event4`＝touch、`event5`＝pen tablet、report 40 bytes、device-mode=2）を起動の後に挿した。sessiond.log の 1767227455 に SEAT が event4・5 を渡し、24 秒後の 1767227479 に session（`exec /bin/wayland`）が `status=256`（exit 1、signal なら status は signal の番号）。ユーザー「いま落ちました」。compositor の stdout（session.log）は次の login で切り詰められて残っていない。kernel の log は hotplug の後の v3d の窓（57 秒、compositor の終わりまで）が `subs=2 draws=0`・cursor の `sets=2 copies=1`: 触れて cursor が隠れた（touch.c の `hide_cursor`、空の像の copy）直後、コマを 1 枚も描かずに終わった見込み。
- source から追った経路: scan（`input_changed` の後 100ms ごと 2 秒）→ `kwl_input_probe` → `attach_touch`・`attach_tablet`（`kwl_touch_add`・`kwl_tablet_add` の後に live・`input_attach`）→ I の `input_commands`（poll(-1) で待つ I は wake の pipe で起きる）→ `input_consume` が SYN_REPORT まで集め `input_report` で M へ → M の `apply_report` → `kwl_touch_frame`・`kwl_tablet_frame` → `place_pointer` は `kwl_pointer_warp_to`（受け箱の warp）。
- exit 1 の経路の全部と確度（確定できず）:
  1. `site=input-detach` の `_exit(1)`（input.c）: zedBSD では**届かない**。`input_detach` を呼ぶ `kwl_input_forget`・`pause`・`resume`・`detach_all` は backend-host.c の seat の callback からだけで、zedBSD の backend（libkeiland-backend-zedbsd）はそれらを呼ばない（FreeBSD・Linux の seat だけ）。I が自分で外す（EOF・read の誤り）経路は ack を待たず M が閉じる。
  2. I の poll の失敗（`site=input-thread`、`server->failed` → EIO → exit 1）: kernel の poll は fd ごとの失敗を POLLNVAL・POLLERR にし、失敗を返すのは EINVAL（数が上限超え）・EINTR（I は繰り返す）だけ。見込み低い。
  3. M の他の `server->failed`（`compose_draw` の record・R の acquire・submit・present の失敗、`frame_done`、`layers_tick`、`buffer_put`、window mode）: 触れた後のコマ（cursor が隠れた damage、client の再描画）で起きうる。kernel の窓に draws=0 なのと合う。中程度。
  4. park の時間切れの `_exit(1)`（R の park 2 秒・C の park/stop）: 出力の開閉・game mode・sleep の時だけ。touch では通らない。低い。
  5. event_loop の失敗の return（accept の失敗＝fd の枯渇、poll の失敗、calloc の失敗、listener の POLLERR）: 何も印字せず `KWL EXIT error=N` だけ。touch の後の app の起動の accept で fd が尽きていれば合う（上限 1024）。低い〜中。
  - I・M の入力の経路は host 試験で再現しなかった（下）。
- 直し（原因が分かる log）: 新しい `kwl_fatal()`（main.c、kwl.h）が KWL の行を stdout に書いて flush し、**syslog にも送る**（`/var/log/messages` に `wayland[pid]: …`、次の login で消えない）。exit 1 に至る全部の所をこれに: `KWL EXIT …`（失敗の時だけ syslog）、`site=accept`・`accept-client`・`accept-display`・`clock`・`poll-set`・`poll`（count と errno）・`listener`（revents）、`site=input-thread`、`site=input-detach`／`input-detach-all`（device の path・kind・slot・devices・sequence・`waited_ms`・errno）、`site=park`（`thread=render depth=`・`thread=cursor step=stop|hold`）、`site=compose_draw`（`operation=acquire|submit|present result=`）、`compose_poll`・`acquire-semaphore`・`frame_done`・`layers_tick`・`window_mode`・`buffer_put`・`seat-lost`。`KWL INPUT_CLOSED` に `kind=`・`reason=ended|revoked`。
- 試験: `input-thread-host-test.c` に第 3 部 `hotplug()`: mouse だけで poll(-1) で待つ I に touch（slot 4）と pen tablet（slot 5）を付け、すぐ読まれる、5000 報告ずつ（2 本の指・MSC_TIMESTAMP・BTN_TOUCH・ABS_X/Y、97 番ごとに 64 events を超える報告、1 度の SYN_DROPPED）が全部 1 回ずつ順に・欠けずに届く（超えた報告と切れた報告だけ落ちる）、抜いた（EOF）両方が ENDED の最後の event で終わりその後に何も来ない、同じ slot に挿し直すと読まれる。PASS（131 checks、plain と ThreadSanitizer、TSan の報告 0）。
- 次に起きた時: Pi で `grep 'wayland\[' /var/log/messages | tail` を見る（session.log が消えても残る）。

## WS214 からの伝達（2026-10-12 Q1）

- WS214（窓を HVS の plane に載せる）p001 第 3 版の 5.8 で、この WS の不変条件「C は自分専用の swapchain だけを使う」（p001 3.2）を変える: drag の間、窓の plane の swapchain の持ち主を R と C の間で entered・took・given の message で移す（C だけを止めて ack、park は使わない）。WS214 p005 の実装の時に WS213 の 3.2 の文と試験を合わせる。文案は [plan/ws214/phase001/phase.md](../../ws214/phase001/phase.md) の 5.8。

### place23 の box_lag と pass_max の切り分け（2026-10-12 P4、Pi の log を読んだ。Pi での新しい実測ではない）

読んだ物: Q1 が写した place23 の 2 回（`p23b`: 1024×600、`res480`: 解像度を変えた回）の session.log・kernel.log・kwl.log。

**数字**（5 秒の窓、pointer が動いた窓だけ）:

| 回 | 窓 | box_lag_p99_us（ms） | box_lag_max（ms） | p99 ≤ 16.7 | max ≤ 16.7 | pass_max（ms、起動の窓を除く） | vk_outside_max（ms） |
| --- | --- | --- | --- | --- | --- | --- | --- |
| p23b | 8 | 21, 11, 16, 19, 20, 17, 30, 21 | 36, 11, 18, 27, 31, 18, 30, 26 | 2/8 | 1/8 | 21.6, 18.2, 20.9, 33.7, 36.5, 36.2, 36.1, 39.3 | 0.1〜14.6 |
| res480 | 13 | 1, 13, 21, 9, 23, 17, 11, 10, 9, 13, 16, 9, 15 | 2, 18, 25, 9, 50, 23, 13, 16, 16, 20, 17, 10, 15 | 10/13 | 7/13 | 12.3〜83.6（70.0・51.5・83.6 は zterm と client 5 の起動の窓） | 0.1〜69.7 |

- `KWL CURSOR_LAG`（16.7ms 超え）は 38 行（p23b 23、res480 15）、17〜50ms。**同じ `write_us` に 2〜3 個の報告がまとまる**（例 p23b: event 66605・66613・66620 が全部 write 66641）。I がその間 1 度も受け箱を書かず、戻った時にまとめて処理した。
- **その間も kernel の HID の worker（kernel thread）は動いていた**: 報告の時刻（`usb-hid.c` の完了の時刻）は 7〜8ms おきに並び、HID の worker は報告ごとに URB を出し直す（`usb-hid.c:1200`）ので、worker は各報告の後に走っている。CPU も割り込みも止まっていない。
- 同じ時に C も止まっている（p23b: present 66609 の次が 66655、kwl.log の `VK CURSOR ticket=305` は set の後 done まで 34ms）。compositor の user の thread（I と C）がそろって進まず、kernel thread は進む。
- R と M の印との重なり（38 行のうち）: R の長い `vkQueueSubmit`（recorded→queued 26〜90ms）の間 4、長い `vkQueuePresentKHR`（queued→submit 29〜51ms）の間 4、直前 40 行に新しい client の buffer の `VULKAN_IMPORT` 9。残りの約 23 は M・R の印が無く、コマの shown（client への frame callback、client の描画）の直後。kernel の v3d の 2 秒の窓（時刻が無いので位置は ±2 秒）の `hold_us decode` は lag のある窓で 24〜48ms、無い窓でも 19〜62ms で、対応ははっきりしない。

**kernel の source から（読んだ。測ってはいない）**:

1. **1 core の同じ優先度の順番**（`src/kern/sched.c`）: user の thread も v3d の worker・executor も HID の worker も `SCHED_PRIORITY_DEFAULT`、quantum 10 tick。起きた thread は `woken` の列（`run` より先）に入り `cpu->preempt` を立て、次の tick で列の頭が走る。ただし (a) `sched_yield`・`sched_preempt` は切り替えるたびに `cpu->preempt = 0` にする（`sched.c:656` など）ので、`woken` に残った thread は走っている thread が眠るか quantum（最大 10ms）を使い切るまで待つ。(b) 起床で横取りされた thread は `run` の頭に戻り、`woken` が空になるまで走らない。`poll_notify`（`src/kern/poll.c`）は system 全体の 1 本の待ち行列を全部起こす（入力・pipe・socket の変化ごとに全部の `poll` が起きる）ので、`woken` は絶えず埋まる。CPU を使い続ける起きた thread（M の pass 14〜21ms、R の記録、v3d の worker の decode 20〜60ms、client の描画: kwl.log の `ZSETTINGS SLOW-FRAME draw=94〜219`）がそれぞれ最大 1 quantum ずつ I の前に走り得る。
2. **user の写しの global な lock**: copyin・copyout（I の `poll` の pollfd、`read` の event）は毎回 `vm_metadata_enter()`（system 全体の 1 つの mutex）と process の `vm->lock` を取る（`vmspace_check`、`src/kern/vmspace.c:3021-3041`）。fault・mmap・munmap・pin・fork・exec もこの mutex を取る（`vmspace.c` の 49 か所）。`mutex_unlock` は待ち手を 1 つ起こすだけで持ち主を渡さない（`src/kern/lock.c:361-394`）ので、mutex を繰り返し取る thread（新しい 2MB の buffer に描く client の fault、import の pin）がいると、起きた I が取り損ね続け得る。HID の worker は user の memory を写さないので影響を受けない（観察と合う）。

**結論**:

- box_lag の 17〜50ms は **kernel の側**（1 core の同じ優先度の thread の順番と、user の写しの global な lock のどちらか、または両方）。I は自分の thread で M を待たない（受け箱の lock の区間は数 µs）ので **M の止まりではない**。app の起動の窓に限らず場面 (2)(3) にも出るので **app の起動だけでもない**。4 つの分類では「1 core の preempt の遅れ」と「kernel の長い transaction（lock）」で、今の log ではこの 2 つを分けられない。
- p99 ≤ 16.7ms（必達）は p23b で 2/8、res480 で 10/13 の窓。最長 ≤ 16.7ms は 1/8 と 7/13。**今の kernel のまま userland で直せる所は見つからなかった**（優先度を使わずに I の待ちを短くする手は、下の K-1 のような kernel の順番の変更か、I の読みを global な poll から外す設計の変更（device ごとの blocking read の thread、外しの ack の設計を変える）で、どちらも p001 の設計の外）。
- **WS211 p010 の object の hash の効果の見込み**: v3d の worker の decode の CPU 時間（`hold_us decode`）が減るので、上の 1 の「CPU を使い続ける起きた thread」の 1 つが短くなる。原因が 1 なら最長の一部（decode と重なった 8 行程度）が縮む見込み。2 が原因の分と、client の描画・M の pass が前に並ぶ分には効かない。
- **`pass_max_us=401327`**（p23b）と `436631`（res480）は**どちらも最初の窓の 1 回で、起動の出力の open**（`HANDOFF go=1 at_ms=17159` から最初のコマの walk `at_us=17469000` まで: `output-pipelines` 74ms、`output-swapchain` 14ms、`output-targets` 17ms、`MODE window switch_ms=221`）。場面 (1)〜(4) の外。場面の窓の pass_max は p23b で 18〜39ms（33.3ms 超えが 4 窓）、res480 で 12〜84ms（app の起動の窓の 51・70・84ms は新しい client の buffer の import の `vk_outside` 69〜70ms と重なる）。**pass の時間は壁の時計**（`kwl_cycles` の差）なので、M が 1 core で他の thread に横取りされた時間も入る。
- 直した物（userland、診断だけ。動作は変えない）:
  1. `KWL SLOW_PASS us= at_ms= input_us= clients_us= frame_done_us= schedule_us= flush_us= other_us= | adopt_us= shm_us= cursor_us= layers_us= draw_us= | walk_us= vk_outside_us= frame=`: M の 1 pass が 33.3ms 以上の時に部分ごとの時間（窓に 10 行まで、main.c の `perf_pass_end`）。次の Pi で場面の pass_max の原因が読める（部分の和が pass より短ければ、残りは M が横取りされた時間）。
  2. `KWL CURSOR_LAG` に I の時刻 `poll_entered_us= poll_returned_us= read_us=`（I の pass の poll の始め・戻り、報告を読んだ read の戻り）。`poll_returned − event` が大きければ I の起きる遅れ（上の 1、または poll の中の copyin・copyout の lock）、`read − poll_returned` が大きければ read の中（copyout の lock か横取り）、`write − read` が大きければ受け箱の lock（userland）、`poll_entered > event` なら I は前の pass の途中だった。
- 確かめ（host）: 4 本の host 試験 PASS（plain と TSan）。build warning 0（rpi4・amd64・keiland-linux）。SHA は agent/p4 の 6d998f0bc（SLOW_PASS）・635f74272（I の時刻）。

**kernel の変更の設計（実装は Q1 の指示の後。kernel は P4 の範囲の外）**:

- **K-D（診断、動作は変えない）**: (1) `SCHED_WAKE_LATENCY=1` の build（今ある同じ CPU の起床の遅れの histogram、`sched.c:35-110`）。(2) 横取りされた thread が `run` に戻ってから再び走るまでの時間の最長と、その thread の pid・tid（`sched_preempt` の requeue の時刻を thread に残し、`pick_next_locked` で差を取る）。(3) `metadata_lock` の持った時間の最長と持ち主の呼び出し元（`__builtin_return_address`）、待った時間の最長。(2)(3) を v3d の perf の行と同じ 2 秒ごとに 1 行。次の Pi の 1 boot で、`KWL CURSOR_LAG` の新しい欄と合わせて 1 と 2 を分ける。
- **K-1（sched、1 が原因なら）**: (a) 切り替えの後も、走る thread の優先度以上の `woken` が残っていれば `cpu->preempt` を立てたままにする（次の tick で次の起きた thread が走る）。(b) 起きてから 1 tick を使う前に横取りされた thread は `run` の頭ではなく `woken` の頭に戻す（起床の順を保つ。1 tick 以上走った thread は今どおり `run`）。効果: 起きた thread は前に並ぶ起きた thread 1 つにつき最大 1 tick で走る。危険: 起き続ける thread どうしの順の公平さ（quantum の扱いは今のまま）。
- **K-2（lock、2 が原因なら）**: copyin・copyout の範囲の確かめを `metadata_lock` なしで（process の `vm->lock` だけ、またはその読み側で）行う。小さい一歩として `mutex_unlock` の持ち主の受け渡し（待ち手がいれば owner を渡す）。どちらも VM の不変条件の review が要る。
- 代わりの userland の設計（p001 の外、Q1 とユーザーの判断）: I の device の読みを global な poll から外す（device ごとの blocking read の thread。device の waitq（`input.c` の `waitq_wake_all(&device->waitq)`）は `poll_notify` の前に起きるので、herd より先に並ぶ）。外しの ack（blocking read を止める手段）を設計し直す必要がある。
- p004 の判定に要る Q1 の判断: 最長（と p99 の未達の窓）の原因は kernel への依存（p004 の「依存と再開の条件」の 2 項目）として uncleared にするか、K-D の 1 boot の後に決めるか。

### T1 と Pi で見る事（追加、2026-10-12 P4）

- Pi: `KWL SLOW_PASS` の行（場面ごとに、どの部分が長いか、部分の和と `us` の差）、`KWL CURSOR_LAG` の `poll_entered_us`・`poll_returned_us`・`read_us`（上の読み方）。kernel の K-D が入っていればその行と並べる。
- T1: 行が増えただけ（動作は同じ）。`KWL SLOW_PASS` が i915・Venus で出ても判定には使わない。
