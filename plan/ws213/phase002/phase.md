<!-- awesome-plan project=zedbsd record=ws213-p002 -->

# ws213-p002: main loop の計測（M0）と cursor の plane の present の間引き（C0）

Parent: [WS213](../ws.md)
Status: in-progress（実装は main 41bd7a5a4 まで、review-2 の Low（touchpad の event_us）は agent/p4。Pi と T1 の確認待ち）
Phase disposition: normal
Queue: P4（2026-10-12 Q1「review を待たずに M0・C0 を実装してよい」）

## 目的と受け入れ

- M0: main loop の区間ごとの時間と、cursor の plane の present の内訳（fence・acquire・copy・present と最長）、入力の kernel の時刻（`event_us`）を log に出す。動作は変えない。
- C0: cursor の plane の位置だけの present を 1 refresh に 1 回（最新の位置）にする。最初の動きと像の変化はすぐ出す。
- 受け入れ: host 試験 PASS、build warning 0、Pi で `KWL PERF main` の行と `cursor presents=` の減り（約 60/s）、`KWL CURSOR_PLANE present` の間隔（約 16.7ms）を確かめる。T1（i915・Venus）は plane が無いので M0 の行が出て `KWL FAILED` が無いこと。設計は [p001](../phase001/phase.md)。

## 実装（2026-10-12 P4、Q1 の「review を待たずに M0・C0 を実装してよい」、main 33cdc3042）

- **M0**（計測だけ、動作は変えない）: `struct kwl_perf`（kwl.h）に pass の区間の cycle を足し、`main.c` の event loop と `display.c` の `kwl_schedule` で測る。5 秒ごとの `KWL PERF` に 1 行 `KWL PERF main ticks= input= clients= frame_done= schedule= flush= other= | schedule ticks= adopt= shm= cursor= layers= draw= | cursor presents= held=` を足した（値は窓の中の ms の合計。ticks は poll の前の tick で work の外、other は work のうち区間に入らない分（descriptor の作成・accept・OS・clipboard ほか）、held は refresh を待った pass の数）。
- **C0**（cursor-plane.c）: 同じ像（source と version が同じ）の位置だけの present は、前の present から 1 refresh（`server->refresh`、無ければ 60Hz）の間は出さずに `deferred` にし、refresh の終わりに最新の位置を出す。最初の動き（静かな refresh の後）と像の変化はすぐ出す（plane と frame の cursor が 1 refresh 食い違わないように）。`kwl_cursor_plane_due_ms` が event loop の poll の timeout を次の present の時刻まで縮める（最小 1ms、さらに 1 refresh 過ぎても誰も出さなければ -1 に戻して 1ms ごとに起きない。pause・sleep・game mode で scheduler が plane まで来ない時のため）。`plane_present` の早い return（copy の fence が未完了・空き画像が無い）は今どおり次の pass で再試行（`deferred` は立てない）。WS141 p016 (B) の B-u（libvulkan の速い経路）とは独立で、B-u が入れば 1 回の present がさらに軽くなる。
- 変えた file: `userland/desktop/wayland/kwl.h`・`compose.h`・`cursor-plane.c`・`display.c`・`main.c`。試験: `plan/ws213/tests/cursor-due-host-test.c`・`.sh`（新規）。
- 確かめ:
  - host: `sh plan/ws213/tests/cursor-due-host-test.sh` → `cursor-due-host-test: PASS`（偽の Vulkan と時計で、最初の動きはすぐ、同じ refresh の中の 2 回は保留して due が 12・7ms、refresh の終わりに最新の位置、止まった pointer は何もしない、静かな後の動きはすぐ、像の変化は保留しない、誰も出さない保留は 1 refresh 後に -1、refresh 0 は 60Hz、perf の数）。
  - build: rpi4（`make -j16 ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/arm64/bin/wayland`）rc 0・warning 0、amd64（`config/ci/config-amd64.mk`、`build/amd64/bin/wayland`）rc 0・warning 0、`make keiland-linux` rc 0・warning 0。`git diff --check` 空。
  - 注: 自分の worktree の `build/arm64/sysroot` は、この make が初めて作った（共有の build・toolchain の tree には触れていない）。最初の make は -j の順で sysroot より先に libc の math を compile して失敗し、2 回目で通った。
- 未実施: Pi・T1。Pi で見ること: `KWL PERF main` の区間（pointer だけ・App Home の開閉・drag・app の起動）、`cursor presents=` が 60/s 程度に減ること、`KWL CURSOR_PLANE present` の間隔（約 16.7ms）と入力→present、work% の減り。T1: i915・Venus は plane が無いので C0 は動かない（M0 の行が出て、`KWL FAILED` が無いことだけ）。

### review-1 の後の追加（2026-10-12 P4、agent/p4）

- **M0 の追加**（C1・H3）: `plane_present` の内訳を `struct kwl_perf` に（copy の fence の問い合わせ・acquire・copy の記録と submit・`vkQueuePresentKHR` の呼び出しの中の時間と、その最長）。`KWL PERF main` の行の末に `fence= acquire= copy= present= present_max=`（ms、早い return の pass は数えない）。`present` は libvulkan の `queue->mutex`・`device->mutex`・`context->mutex` の待ちを含む。`KWL CURSOR_PLANE present` の行に `event_us=`（そのマウスの report の kernel の時刻、`input.c` の `device->frame_time_us` を `server->pointer_event_us` に）と `present_cycles=` を足した。表示の側の時刻（kernel の CURSOR_SET・MOVE）は compositor からは取れないので WS141 に頼む（11 節）。
- **C0 の Low の確かめ**: (1) plane と composite の切り替え（`kwl_cursor_plane_update` の `shown` の変化）は間引きの前にあり、切り替わる時は像（`source`）が変わる（NULL ⇔ cursor の像）ので間引かれない。(2) 最初の動きは即時、周期の間の動きは保留して周期の終わりに最新の位置（host 試験の (1)・(4)・(6)）。直す所は無い。
- 確かめ: host `sh plan/ws213/tests/cursor-due-host-test.sh` PASS（`kwl_cycles` の stub を足した）。build: rpi4・amd64 の `bin/wayland` rc 0・warning 0、`make keiland-linux` rc 0・warning 0、`git diff --check` 空。

### review-2 の Low（2026-10-12 P4）

- touchpad の report でも `server->pointer_event_us` を入れる（`input.c` の `apply_touchpad`、review-2 Low: 今までは mouse の相対の動きと絶対の report だけだった）。
