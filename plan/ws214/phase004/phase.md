<!-- awesome-plan project=zedbsd record=ws214-p004 -->

# ws214-p004: libvulkan の overlay の plane

Parent: [WS214](../ws.md)
Status: in-progress（2026-10-12 P2。実装と host 試験・build 済み。clear は WS141 p016 B-u の Pi の受け入れの後、Q1 の判定）
Phase disposition: normal
Queue: P2（2026-10-12 Q1 が着手を許した）

## 依存

- p003b の kernel（COMMIT・OVERLAY_INFO・眠る WAIT、main に merge 済み）。
- WS141 p016 B-u（cursor の fast path・ticket・lane）。code は main にある。**p004 の clear は B-u の Pi の受け入れの後**（Q1、2026-10-12）。実装は先に始めてよい。
- P3 が `wsi-swapchain.c`・`wsi-display.c` を触る（present の copy の command buffer の使い回しは main にある）。着手の前と各 commit の前に main を merge する。

## 範囲（設計 p001 の 4.1・4.4、そのとおり）

1. 列挙と能力: 91b119330 の暫定の 7 行（HAS_OVERLAYS の出力は primary だけ）を外し、plane を primary 0、overlay 1〜K、cursor（最後）に並べる。`vkGetDisplayPlaneCapabilitiesKHR`（overlay）は supportedAlpha PER_PIXEL_PREMULTIPLIED、src・dst の範囲は `GPU_DISPLAY_OVERLAY_INFO`（拡大縮小の flag で min/max を決める）、minDstPosition (0,0)、maxDstPosition は出力の大きさ − 1。`vkCreateDisplayPlaneSurfaceKHR` は overlay の imageExtent を maxSrc の中で受ける。
2. surface と claim: overlay の surface は cursor と同じく primary の claim を借りる（`display_claim_native` の検索の条件、`wsi_display_lease` に overlay の印と plane の番号）。overlay の swapchain は 2 枚（FIFO）、minImageCount 1。画像は native の共有の画像（linear、連続、uncached）。
3. present の道（4.1 規則、4.4 (3)）:
   - **保留**: 見える・隠れるの切り替え（kernel に記録済みの状態に対して）と、保留がある plane への全部の present は、display ごとの保留の表（hold の lock）に置く。`min_primary`＝保留した時に発行済みの primary の ticket＋1。置き換えた保留の画像はすぐ空ける。
   - **primary の ticket**: primary の present は `vkQueuePresentKHR` の呼び出しの順の ticket を swapchain_mutex の下で振る（cursor の `cursor_ticket` と同じ形）。ordinary の lane は ticket の順に kernel へ出る（今の FIFO の worker）。
   - **primary の commit**: ticket ≥ `min_primary` の primary の job は、kernel に出す時に保留を全部取り出し、primary と一緒に 1 回の `GPU_DISPLAY_COMMIT`（COMMIT_WAIT）にする。保留が無い時と overlay の無い出力は今どおり PRESENT（i915・Venus は変わらない）。
   - **fast path**: 保留の無い plane の、位置・src の変更だけで中身が変わっていない present（`transfer_only` と `content_serial`、cursor の `present_route` と同じ判定）は、呼んだ thread で COMMIT（wait なし）。
   - **overlay の lane**: 新しい中身は、cursor の lane と同じく ordinary の後ろに並ばない worker（`WSI_LANE_OVERLAY`）が GPU の copy の完了を待って COMMIT。
   - **隠す**: `srcRect`・`dstRect` がともに (0,0) の 1×1 なら SHOWN を落とした状態（画像を名指さない）。
4. serial: plane ごとの present の番号（surface ごとに持ち、chain を作り直しても増え続ける）を `gpu_display_plane_state.serial` に。DROPPED は画像をすぐ空ける。
5. 借りと acquire（4.4 (5)）: 画像の holds は「commit が名指した数」。置き換えた commit の番号を画像に記録し、COMMIT が返す `adopted` 以下ならその場で holds を減らす。acquire で空きが無ければ、その番号で `GPU_DISPLAY_WAIT`（acquire の timeout）を **display_mutex を持たずに**呼ぶ。primary の COMMIT_WAIT の ioctl の間も display_mutex を持たない（holds の更新の前後だけ）。
6. 単独の flush と期限（4.1 規則 6）: 2 refresh の間に `min_primary` 以上の primary が present されなければ単独で commit（`why=expired`、overlay の lane の worker が `pthread_cond_timedwait`）。`vkQueueWaitIdle`・`vkDeviceWaitIdle`（`present_drain`）、lease の release、surface の破棄は単独の flush。`vkDestroySwapchainKHR` は chain の保留を捨て、出ている plane を隠す commit を単独で出して採用を待つ。退役した chain の保留は捨てる。
7. 失敗（4.4 (6)）: 保留を含む commit の ENOSPC・EINVAL は保留を全部捨て、primary も出さない。overlay の chain には次の present・acquire で `VK_ERROR_OUT_OF_DATE_KHR`。primary の present の結果は成功のまま。ESTALE・ENXIO・EBUSY は primary と取り込んだ chain の両方に今の失敗と同じ扱い。保留の無い道の ENOSPC はその chain だけ OUT_OF_DATE。
8. log: `WSI OVERLAY present plane= serial= ticket= min_primary= path=held|replaced|fast|lane|commit|flush why= result= sequence= adopted= call_ns=`（環境変数 `VULKAN_ZEDBSD_OVERLAY_LOG=1`、cursor の log と同じ作り）。
9. README（`userland/desktop/libvulkan/README.md`）に 4.1 の規則 1〜6 と隠す約束。

## 手を入れる所（2026-10-12 P2 の読み）

- `wsi-display.c`: `vulkan_wsi_display_query`（暫定の 7 行を外し、`VULKAN_WSI_OUTPUT_OVERLAYS` と overlay の数を `vulkan_wsi_output` に）、`display_claim_native`（overlay の surface も primary の plane を借りる）、新しい platform の op `present_overlay`（COMMIT、DROPPED、ENOSPC）と `commit_primary`（保留と primary の 1 commit、COMMIT_WAIT、display_mutex の外）、`display_image_available` と `wait`（番号で holds を減らす）。
- `wsi.c`: `wsi_cursor_plane` の隣に `wsi_overlay_plane`、`vkGetDisplayPlaneCapabilitiesKHR`、`vkCreateDisplayPlaneSurfaceKHR`。
- `wsi-internal.h`: `vulkan_surface` に overlay の印・`overlay_ticket`（plane の serial）、platform の op。
- `wsi-swapchain.c`: `present_prepare`（overlay の display の矩形の検査: 隠す 1×1 と src・dst の範囲）、`present_reserve`（ticket、primary の ticket）、`present_lane`（`WSI_LANE_OVERLAY`）、`present_route`（cursor と同じ判定を overlay に）、`present_native`（overlay の show、primary の commit）、`present_drain`・`vkDestroySwapchainKHR`（単独の flush）、保留の表と期限の worker。

## 試験（設計 8）

- host: `plan/ws141/tests/vulkan-cursor-host.c` と同じ形の fixture（偽の display の transport、**compositor と同じ usage（TRANSFER_DST だけ）の chain**）を `plan/ws214/tests/` に: (i) 前のコマの primary が lane に残っている時に切り替えを保留し、その primary は取り込まず次の primary が取り込む、(ii) 保留の後の同じ plane の present は保留を置き換え kernel に直に出ない、置き換えた画像はすぐ acquire できる、(iii) primary が来ない時 2 refresh で単独の commit（`why=expired`）、(iv) WaitIdle・破棄・release の単独の flush、(v) 退役した chain の保留は commit に入らない、(vi) 保留を含む commit の ENOSPC で primary も出ない、(vii) fast path（why=none）、(viii) DROPPED、(ix) acquire の WAIT の間 display_mutex を持たない。ordinary と ASan/UBSan。既存の `vulkan-cursor-host.sh` と `vulkan-device-host-test.sh` の回帰。
- build: rpi4 の libvulkan.so と compositor、amd64、keiland-linux（libvulkan を使う物）が warning 0。
- T1: i915・Venus の回帰（overlay の無い出力は今どおり）。Pi: p005 の compositor と一緒に（p004 単独では compositor が overlay を使わない）。

## 受け入れ

上の host の (i)〜(ix) が PASS、build が warning 0、T1 の回帰が変わらない、WS141 p016 B-u の Pi の受け入れが済んでいる（Q1 が判定）。

## コマンドと上限

- host: `timeout 600 sh plan/ws214/tests/vulkan-overlay-host.sh <out>`（ordinary と ASan/UBSan、log off/on）、回帰 `timeout 300 sh plan/ws141/tests/vulkan-cursor-host.sh <out>`。
- build: `timeout 3000 make -j16 -o <worktree>/build/arm64/sysroot/.zedbsd-sysroot-complete ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/p2-ws214-rpi4 "ZEDBSD_USER_PROGRAMS=wayland libvulkan" build/p2-ws214-rpi4/bin/wayland build/p2-ws214-rpi4/dynamic/libvulkan.so`、amd64 は `ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/p2-ws214-amd64`。QEMU は起動しない（T1）。

## 結果（2026-10-12 P2、段 2）

commit: d1e626a52（この phase.md）、d6973d075（段 1: 列挙・能力・overlay の surface、前の P2）、e7e634cca（段 2: claim・保留・primary の commit・fast/lane・watcher・flush・reclaim）、6bb24987c（host 試験）、046a376af（README・書き方）。

### 実装

- `wsi-internal.h`: platform の op を 9 個足した（`primary_ticket`・`present_primary`・`primary_skip`・`overlay_decide`・`overlay_commit`・`overlay_ready`・`overlay_status`・`overlay_forget`・`reclaim`）。overlay の present の要求 `struct vulkan_wsi_overlay_present` と結果 `enum vulkan_wsi_overlay_outcome`、`vulkan_wsi_display_overlay_flush`。wayland の platform と ws141 の cursor の fixture の初期化子に NULL を足した（`-Wextra`）。
- `wsi-display.c`:
  - claim: overlay の surface は cursor と同じく primary の claim を借りる。1 つの overlay の plane は 1 つの surface（2 つ目は `VK_ERROR_NATIVE_WINDOW_IN_USE_KHR`）。claim は overlay の数、mode の generation、1 refresh の長さを持つ。overlay の surface の minImageCount は 1。copy の経路は無い。
  - 状態は `display_overlay_mutex`（display_mutex の後に取る leaf の lock）の下: plane ごとの保留（`wsi_display_hold`、`min_primary`・期限・ready・owner）、front の画像、退役中の画像（置き換えた記録の番号）、serial、`recorded_serial`、`shown`（保留の外で決めた present を全部記録した後の見え方）。overlay の画像の holds もこの lock。
  - decide（`vkQueuePresentKHR` の呼び出しの順）: serial を振り、保留がある plane は置き換え（`min_primary`・期限は最初の物、置き換えた画像はすぐ空く）、見える・隠れるの切り替えは新しい保留（`min_primary`＝それまでに振った primary の ticket＋1、期限＝2 refresh）、他は単独の commit へ。隠す（src・dst ともに (0,0) の 1×1）は画像を名指さない。
  - 単独の commit（fast は呼んだ thread、lane は worker）: overlay_mutex の下で decide の順に記録（後の serial が記録済みなら ioctl せずに捨てる）、APPLIED で front を替え前の画像は退役（`adopted` ≥ 番号で解く）、DROPPED は画像をすぐ空ける、EBUSY は BUSY（worker が 1 refresh ごとに 1 秒まで再試行）、ENOSPC・EINVAL はその chain だけ OUT_OF_DATE。display_mutex は取らない。
  - primary（`present_primary`）: ticket が `min_primary` 以上の保留があれば、保留の ready（copy の完了、1 秒まで）を待ち、primary（serial＝ticket、wait の fence 付き）と保留を 1 回の COMMIT にし、`GPU_DISPLAY_WAIT` で採用を待つ（同期の selection と同じ約束）。無ければ今の PRESENT_SYNC/PRESENT（overlay の無い出力・i915・Venus は ticket 0 で今どおり）。ENOSPC・EINVAL は保留を全部捨てて OUT_OF_DATE を owner の chain に残し、primary も出さず present は成功（N10）。他の失敗は primary と保留の chain の両方に。
  - `primary_skip`: primary が present_primary に届かない時（copy の経路は単独の flush、失敗は保留を捨てて OUT_OF_DATE）。
  - watcher（claim ごとの thread、最初の保留で起こし、最後の release で止めて join）: 期限を過ぎ、その後に primary が呼ばれていない ready な保留を単独で commit（`why=expired`）。
  - `vulkan_wsi_display_overlay_flush`（`present_drain` の最後、`vkQueueWaitIdle`・`vkDeviceWaitIdle`）: ready な保留を単独で commit して採用を待つ（`why=flush`）。
  - 画像の破棄（`vkDestroySwapchainKHR`）: その画像の保留を捨て、出ていれば隠す commit を単独で出し、退役中なら採用を 1 秒まで待つ（`why=destroy`）。overlay の lease の release は保留を捨て plane を空ける。最後の release は watcher を止め、kernel の release の後に全部の借りを解く。
  - reclaim: acquire で空きが無い時、退役中の一番古い番号で `GPU_DISPLAY_WAIT` を lock 無しで呼ぶ。
  - log `WSI OVERLAY present plane= serial= ticket= min_primary= path=held|replaced|fast|lane|commit|flush why= result= sequence= adopted= call_ns=`（`VULKAN_ZEDBSD_OVERLAY_LOG=1`）。
- `wsi-swapchain.c`: `WSI_LANE_OVERLAY`、overlay の矩形の検査（src は画像の中、dst は出力の中、persistent は不可）、primary の ticket を予約の時に swapchain_mutex の下で、route は cursor と同じ判定（隠す present は待ちだけ）、overlay の画像は丸ごと copy（合成しない）、submit の後（fast は present_fast の中）で decide、worker は fence の後に保留の ready を知らせ、present_native で単独の commit・primary の commit、primary に届かなかった ticket は `primary_skip`、退役・破棄の chain は `overlay_forget`、`swapchain_current` は落とされた保留の chain に OUT_OF_DATE、acquire は reclaim、`present_drain` は flush。
- README に overlay の節（列挙、規則 1〜6、隠す約束、失敗、log）。

### 設計（p001 4.4）からの違い

- primary と保留の commit は COMMIT_WAIT を使わず、COMMIT（記録だけ）の後に `GPU_DISPLAY_WAIT` で採用を待つ。理由: overlay の commit を decide の順に kernel に記録するため overlay_mutex を記録の間持つので、採用の待ち（1 refresh）の間 C の fast path を止めないため。signal の fence（completion_fd）は使わない（libvulkan の中で誰も待っていない）。
- overlay の plane の state に wait の fence を付けない。lane の worker と primary の job は CPU で GPU の完了（copy の fence）を見てから commit し、保留は ready（copy の完了）を待ってから commit に入る。job の fence の fd は slot が再利用するので、保留に持たせると寿命が合わないため。
- primary の present は保留が無い時は今の PRESENT_SYNC のまま（display_mutex を採用まで持つ今の形）。acquire の WAIT を display_mutex の外で行うのは overlay の chain（overlay の画像の holds は overlay_mutex）。
- fast path と lane は 1 つの platform の op（`overlay_commit`）で、ioctl の前に `overlay_mutex` を取る（decide の順の記録のため）。C の fast path が待つのは他の overlay の記録（µs〜clock を上げる時の ms）だけで、primary の採用は待たない。

### 確かめ（host・build。QEMU・実機ではない）

- `timeout 600 sh plan/ws214/tests/vulkan-overlay-host.sh build/p2-ws214/overlay-host` PASS（3 回続けて、ordinary と ASan/UBSan、log off で 0 行、on で 29 行・形の外 0、全部の path と why=expired・flush・destroy・nospace）。本物の display の platform と偽の kernel（plane ごとの serial・DROPPED・記録の番号・adopted・WAIT）、overlay の chain は TRANSFER_DST だけ:
  - (i) N1: 前のコマの primary が lane で fence を待つ間に window と bar を出す → その primary は PRESENT（取り込まない）、次の primary が primary＋2 枚の 1 commit（primary の serial は ticket 3）。
  - (vii) 新しい中身は lane の単独の commit、変わらない画像は呼んだ thread で submit 無しに commit、overlay の commit は display_mutex を持たない。
  - (ii) 隠す present は保留、後の present は置き換え（kernel に出ない）、置き換えた画像はすぐ acquire できる、次の primary が最後の物を出す。
  - (iii) primary が来ない保留は 2 refresh で単独の commit（why=expired）。
  - (iv) `vulkan_wsi_queue_idle` が保留を単独で commit してから返る。
  - (v) oldSwapchain で退役した chain の保留は捨て、次の primary は PRESENT。退役した chain の破棄は出ている plane を隠す。
  - (vi) 保留を含む commit の ENOSPC: kernel は何も変わらず primary も出ない、primary の chain は続き、overlay の chain は acquire で OUT_OF_DATE。
  - (viii) DROPPED の画像はすぐ holds 0。
  - (ix) 採用が遅れると退役中の画像は空かず、acquire は `GPU_DISPLAY_WAIT` を display_mutex も overlay_mutex も持たずに呼んで空きを得る。
  - fast path の ENOSPC はその chain の present が OUT_OF_DATE。
  - 最後に overlay の chain の破棄で plane が隠れ、primary の破棄で claim の release が 1 回。
- 回帰: `timeout 300 sh plan/ws141/tests/vulkan-cursor-host.sh build/p2-ws214/cursor-host` PASS（ordinary・sanitize、log off/on）。
- build: rpi4 の `build/p2-ws214-rpi4/dynamic/libvulkan.so` と `bin/wayland`、amd64 の `build/p2-ws214-amd64/dynamic/libvulkan.so` と `bin/wayland` が exit 0・warning 0。keiland-linux の build は行っていない（libvulkan はその build の対象でない）。

### 未実施・残件

- Pi の実機: p004 だけでは compositor が overlay を使わない（p005）。Pi で見る点は、今の desktop が今どおり動くこと（overlay の plane の列挙で compositor の plane の選びが変わらない: cursor は maxSrcExtent 32〜256 と負の minDstPosition で選ぶので overlay（minDst (0,0)、maxSrc は出力の大きさ）は選ばれない、primary は plane 0）と、`hvs-load` の後の p005 での overlay。Pi は root の UFS が書けない（BUG-294）ので焼き直しの後。
- T1: i915・Venus の回帰（ticket は overlay の無い出力で 0 なので present の道は今どおり）。
- 出力が 2 つの時、compositor の cursor の plane の探し（`cursor-plane.c` の properties[8]）は 1 つの出力が 4 plane（primary・overlay 2・cursor）なので 2 出力でちょうど 8。3 出力目の cursor は探せない（p005 で compositor 側に記録）。
- 保留の期限 2 refresh が R の 1 core の preempt に対して十分かは p005 の Pi の log（`why=expired` が出ないこと）で見る。
- WS141 p016 B-u の Pi の受け入れ（clear の条件、Q1 が判定）。
