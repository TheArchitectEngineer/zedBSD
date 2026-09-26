<!-- awesome-plan project=zedbsd record=ws069p011 -->

# ws069-p011: zdesktop-x11server の窓を Vulkan で表示

Phase ID: `ws069-p011`
Parent: [WS069](../ws.md)
Status: in-progress（q491-i01）
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
