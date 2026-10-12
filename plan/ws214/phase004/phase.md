<!-- awesome-plan project=zedbsd record=ws214-p004 -->

# ws214-p004: libvulkan の overlay の plane

Parent: [WS214](../ws.md)
Status: planned（2026-10-12 P2 が実装の手順を書いた。実装は未着手）
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

## 結果

（未着手）
