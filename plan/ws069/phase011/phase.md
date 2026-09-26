<!-- awesome-plan project=zedbsd record=ws069p011 -->

# ws069-p011: zdesktop-x11server の窓を Vulkan で表示

Phase ID: `ws069-p011`
Parent: [WS069](../ws.md)
Status: cleared（q491-i01、2026-09-27）
Phase disposition: normal
Queue: q491-i01
承認: 2026-09-27 ユーザー「zdesktop-x11serverは、標準のWaylandとVulkanを使いつつ、libzdesktopを活用してください。」「続けてください。」
設計: [design.md](../design.md) §0（窓の表示は Vulkan の `VK_KHR_wayland_surface` の swapchain、top-level ごと）

## 方式

- `userland/base/zdesktop-x11server/vulkan.c`: 接続ごとに instance・device（最初の窓の surface に present できる queue family）、
  窓ごとに `VkSurfaceKHR`（その窓の `wl_surface`）・swapchain（B8G8R8A8 か R8G8B8A8 の UNORM、FIFO、3 枚）・host-visible の staging buffer・
  command buffer・fence・semaphore。
- present: 前の copy の fence と `vkAcquireNextImageKHR` を待ち時間 0 で見て、塞がっていれば後の pass（server を止めない。wl_shm の
  「両 buffer が compositor の手にある」と同じ扱い）。窓の画素を staging に写し（alpha は 0xff、R8G8B8A8 なら byte を入れ替え）、
  image へ `vkCmdCopyBufferToImage`、present。大きさの変化・out of date で swapchain を作り直す。
- Vulkan が使えない（device が無い等）ときは wl_shm の道のまま。
- GLX の画像を GPU のまま渡す段（libzdesktop の GPU buffer）はこの Phase に入れない。

## 受け入れ

1. build warning 0、新しい・変えた C は style-check の指摘 0。
2. Venus: x11-p003（zterm）・x11-p004（glxtest）・x11-p005（zgears、frame 300、回る）・zdesktop-p070 が PASS し、x11server の log で窓が
   Vulkan で出ていることが分かる。
3. i915 実機の `zdesktop-x11` の run で 6 検査 PASS（BUG-056 の run は数えない）。boot test。

## 結果（2026-09-27、q491-i01）

- `userland/base/zdesktop-x11server/vulkan.c`（新規）: 接続ごとの instance・device（最初の窓の surface に present できる graphics の
  queue family）、窓ごとの `VkSurfaceKHR`・swapchain・host-visible で coherent な staging buffer・command buffer・fence・semaphore。
  present は fence と acquire を待ち時間 0 で見て、塞がっていれば 1（後の pass で再試行、wl_shm の「両 buffer が desktop の手に
  ある」と同じ扱い）。画素を staging へ（alpha 0xff、R8G8B8A8 なら byte を入れ替え）、UNDEFINED→TRANSFER_DST→PRESENT_SRC の barrier と
  `vkCmdCopyBufferToImage`、acquire の semaphore を待って submit、image ごとの semaphore で present。out of date・suboptimal で作り直す。
- 設計からの変更: present mode は **MAILBOX があれば MAILBOX**（libvulkan の Wayland WSI の FIFO は前の frame の callback を最長 10 秒
  待ち、server の全 client を止めるため）。swapchain の大きさが X の窓と違う（surface が clamp した）ときは使わず wl_shm に戻す。
- `wayland.c`: 窓を開くとき Vulkan を試し、だめなら（または `X11SERVER_SHM=1` なら）wl_shm。present・resize で Vulkan が失敗したら
  その窓と以後の窓は wl_shm（stderr に `X11SERVER PRESENT wl_shm (...)`）。Vulkan が使えたら `X11SERVER VULKAN device ready`。
- **見つけた不具合と修正**: `x11_wayland_dispatch` が blocking の `wl_display_dispatch` を使っていた。libvulkan の present の worker は
  同じ接続を自分の event queue のために読むので、descriptor を readable にした event を worker が先に取ると、server の本体が
  desktop の次の event まで止まった（Venus で zgears の swap が約 11 秒止まり、lldb の backtrace で `wl_display_dispatch_queue` の
  ppoll を確認）。`wl_display_prepare_read`・`read_events`（非 blocking の read）・`dispatch_pending` に直した。
- build: `platform/amd64/vmunix.mk` の link に libvulkan と `DYNAMIC_VULKAN_CHECK`、package の依存に base/libvulkan。

## 検証

- build（`plan/ws035/tests/build-zdesktop-image.sh`）warning 0。style-check: zdesktop-x11server の全 file で指摘 0。
- Venus（QEMU）: x11-p003（zterm、`build/ws069-p011-x3/`）・x11-p004（glxtest、docked の大きさの変化を含む、`build/ws069-p011-x4/`）・
  x11-p005（zgears 300 frame、DONE、回る、`build/ws069-p011-x5/`）・zdesktop-p070（`build/ws069-p011-p070/`）PASS。画面を目で確かめた。
  x11server の log に `X11SERVER VULKAN device ready family=0`（fallback の行なし）。
- 速さ（Venus、同じ guest で A/B、host は他の guest も動く負荷）: zgears 200 frame で **Vulkan 2.4〜2.6 fps、`X11SERVER_SHM=1` 7.3〜7.8 fps**。
  lldb の標本では server は `vkBeginCommandBuffer`・`vkEndCommandBuffer`・WSI の present の内部の `vulkan_decoder_wait`（Venus の命令ごとの
  同期の往復）で待つ。Venus の命令の往復の費用で、F-021 に追記。既定は Vulkan のまま（ユーザーの方針「標準の Wayland と Vulkan」）。
- i915 実機（capture、`zdesktop-x11`）: run1（`build/ws069-p011-hw1/`）で 6 検査 PASS（Gears が回り、X terminal、仮想デスクトップ 2 と戻り）。
  run2 は BUG-056（`ZWL EXIT ... cleanup_failed=1`、zdesktop が App Home の後に終わる）で数えない。実機の log は読めなかった
  （ufs-cat が疎な file に未対応）ので、実機で Vulkan の道だったことの log の確認と fps は**未実施**。実機の LCD の目視は未実施。
- boot test PASS（`build/ws069-p011-boot/login.png`）。
