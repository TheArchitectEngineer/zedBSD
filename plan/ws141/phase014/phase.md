<!-- awesome-plan project=zedbsd record=ws141-p014 -->

# ws141-p014: HVS の cursor plane（合成しない cursor）

Parent: [WS141](../ws.md)
Status: in-progress（2026-10-12 ユーザー承認、実装中）
Phase disposition: normal
Queue: q936 の続き（P3、Q1 が割り当て）

## 由来・承認

- 2026-10-11 ユーザー（Q1 経由）:「displayなのにplaneのx,yですか…cursorと明示されたplaneならいいと思います。」→ present に x,y を足さず、cursor 専用の UAPI と、libvulkan が VK_KHR_display の plane の 1 つを cursor の plane として見せる形。
- 2026-10-11 ユーザー（Q1 経由）:「その案でOKです。」→ この Phase で UAPI の差分の設計を書き、ユーザーに見せてから実装する。i915 の cursor plane は別の WS（Future Work）。
- 2026-10-12 ユーザー（Q1 経由）:「VK_KHR_display＋cursor 専用の UAPIでOKです。承認します。」（vendor の Vulkan 拡張は作らない）→ libvulkan は cursor の plane を標準の VK_KHR_display の plane（能力: 最大 64x64・per-pixel premultiplied・一番上の stack）として列挙し、その swapchain の present と VkDisplayPresentInfoKHR の dstRect を `GPU_DISPLAY_CURSOR_SET`（画像が変わった時）・`MOVE`・`HIDE` に変える。compositor は標準の VK_KHR_display・VK_KHR_display_swapchain だけを使い、能力で cursor の plane を選ぶ（無ければ合成）。hotspot は compositor が dstRect に含めるので、UAPI には hotspot を置かない。

## 目的

cursor の表示を描画（Vulkan の合成）から切り離し、pointer が動くたびに HVS の cursor の plane の位置だけを次の vblank で変える。p013 の測定では cursor だけの frame でも draw→shown 26ms（vkQueueSubmit 7、present 10、fence 3 など）で、表示は 2 vblank に 1 回（約 30fps）。cursor の plane なら位置の更新は数十 µs の register の書き込みで、毎 vblank（60fps）に追従し、scene の再合成の重さ（0.4 秒など）とも無関係になる。

## UAPI（`include/uapi/gpu-display.h`、追加だけ、既存の struct は変えない）

```c
/* The output has a cursor plane: GPU_DISPLAY_CURSOR_SET/MOVE/HIDE work on its lease. */
#define GPU_DISPLAY_HAS_CURSOR		(1U << n)	/* gpu_display_info.flags の空いた bit */
/* The largest cursor image a driver must accept (both sides). */
#define GPU_DISPLAY_CURSOR_MAX		64U

/*
 * Sets the cursor image of the display this open leases, copied at once from
 * session storage: width and height 1..GPU_DISPLAY_CURSOR_MAX, stride >= width*4,
 * format GPU_PIXEL_BGRA8888 or GPU_PIXEL_RGBA8888 with premultiplied alpha.
 * The storage is not borrowed after the call.  The cursor stays hidden until
 * the first GPU_DISPLAY_CURSOR_MOVE.
 */
struct gpu_display_cursor_image {
	uint32_t version;
	uint32_t size;
	uint64_t lease;
	uint64_t handle;
	uint64_t offset;
	uint32_t width;
	uint32_t height;
	uint32_t stride;
	uint32_t format;
	uint32_t reserved0;	/* zero */
	uint64_t generation;
	uint64_t reserved;	/* zero */
};

/*
 * Shows the cursor image with its top-left corner at (x, y) in output pixels
 * (the caller subtracts its hotspot); parts outside the output are clipped.  The change takes effect at the next refresh
 * boundary and the call does not wait for it; a later move before that
 * boundary replaces it.
 */
struct gpu_display_cursor_move {
	uint32_t version;
	uint32_t size;
	uint64_t lease;
	int32_t x;
	int32_t y;
	uint64_t generation;
	uint64_t reserved;	/* zero */
};

/* Hides the cursor at the next refresh boundary; its image is kept. */
struct gpu_display_cursor_hide {
	uint32_t version;
	uint32_t size;
	uint64_t lease;
	uint64_t generation;
	uint64_t reserved;	/* zero */
};

#define GPU_DISPLAY_CURSOR_SET		_IOW('G', 41, struct gpu_display_cursor_image)
#define GPU_DISPLAY_CURSOR_MOVE		_IOW('G', 42, struct gpu_display_cursor_move)
#define GPU_DISPLAY_CURSOR_HIDE		_IOW('G', 43, struct gpu_display_cursor_hide)
```

- エラー: ENOTSUP（driver に cursor plane が無い、`GPU_DISPLAY_HAS_CURSOR` が無い）、EBUSY（この open の lease ではない）、ESTALE（古い generation）、EINVAL（大きさ・stride・format・reserved）、ENXIO（切断）。lease の release と device の fault では cursor を隠す。
- cursor は primary の lease（plane 0 の claim）に付く。別の claim は要らない（cursor だけを他の open が持つ形は作らない）。

## kernel の共通部（`include/drivers/gpu/gpu-display.h`・`src/drivers/gpu/gpu.c`）

- `struct drv_gpu_display_ops` に任意の 3 つ `cursor_set`・`cursor_move`・`cursor_hide` を足す（NULL なら core が ENOTSUP）。core は既存の display の ioctl と同じ検査（version・size・書き込みの open・reserved）と copy をし、driver へ渡す。HAL・HAL の API は変えない。
- i915・Venus などの既存の driver は NULL のままで、動きは変わらない。

## bcm2711 の driver（`display-device.c`・`display-flip.c`）

- 起動時に cursor 用の連続の buffer（64x64x4 を 2 枚、1 GiB より下）を確保。`cursor_set` は session の storage から画素を写し（premultiplied、BGRA/RGBA を HVS の順に）、cache を clean。
- HVS の display list に 3 枚目の plane（cursor、premultiplied alpha の合成、64x64 の unscaled）を足す。今の合成の list（console＋frame、17 word）を cursor 付きの list（約 26 word）にする。位置が画面の外にかかる時は、source の開始位置と幅・高さを詰めて clip する。
- `cursor_move` は新しい位置を記録し、使っていない list の slot に cursor の位置だけを変えた list を書いて「次の list」に載せ、採用（vblank）は待たずに戻る。flip（frame の present）が進行中なら位置だけを記録し、その flip の list に入れる。どちらも flip.guard の下で直列にし、採用前の list を上書きしない（2 slot を交互、採用の IRQ で解放）。
- `info.flags` に `GPU_DISPLAY_HAS_CURSOR`、`plane_count` を 2 にする。

## libvulkan（`userland/desktop/libvulkan/wsi*.c`）

- `vkGetPhysicalDeviceDisplayPlanePropertiesKHR`: cursor のある出力は plane を 2 つ（0 = primary、1 = cursor）。
- `vkGetDisplayPlaneCapabilitiesKHR`（plane 1）: supportedAlpha = PER_PIXEL_PREMULTIPLIED、min/max SrcExtent・DstExtent = 1x1〜64x64、minDstPosition = (-63, -63)、maxDstPosition = (幅-1, 高さ-1)。
- plane 1 の display surface（`VkDisplaySurfaceCreateInfoKHR.planeIndex = 1`、imageExtent ≤ 64x64）の swapchain は scanout の native image を作らない。present では、wait semaphore と GPU の完了の後に、表示する image の storage を `GPU_DISPLAY_CURSOR_SET` に渡し（16KB の copy）、`VkDisplayPresentInfoKHR.dstRect.offset`（compositor が hotspot を引いた左上の位置）を `GPU_DISPLAY_CURSOR_MOVE` に渡す（dstRect の extent は image の大きさと同じであること、違えば VK_ERROR_INITIALIZATION_FAILED）。同じ image を続けて present する時は SET を省く（image ごとに最後に送った present の世代を記録）。
- primary の plane の lease を持つ同じ connection の中だけで使える（cursor の plane は primary の lease に付く）。

## compositor（`userland/desktop/wayland/`）

- 出力が cursor の plane を持つ（plane 1 の capabilities が上のとおり）時だけ、64x64 の cursor の swapchain を作る。cursor の形（compositor の矢印・resize の形・client の cursor の面で 64x64 以下の物）が変わった時だけ、その image に描き（host image からの copy）、present する。pointer の移動は dstRect だけを変えた present（GPU の仕事は無い）。
- この時、scene の frame は cursor を描かず、pointer の移動は scene に damage を出さない（hover の記録による判定は残す）。
- cursor の plane が無い出力（i915・Venus・QEMU など）、client の cursor が 64x64 より大きい時、drag の icon などは、今までどおり合成で描く（fallback）。

## 検証

- host: UAPI の struct の大きさ・ioctl の番号の重複無し、core の ENOTSUP・検査、bcm2711 の list の符号化（cursor 付き・clip・位置だけの更新・flip との直列）、libvulkan の plane の列挙と capabilities（fixture）。
- 実機（Pi 4）: cursor が毎 vblank に追従するか（compositor の log と、kernel の cursor_move の数・採用の数の 2 秒の集計）、ユーザーの目視。QEMU（Venus・llvmpipe）は cursor の plane が無いので従来の合成のまま変わらないこと（T1）。

## 範囲外

- i915 の cursor plane（hardware にはある）: 別の WS（Future Work に記録、Q1）。
- cursor の拡大・回転、複数の出力（heads）への cursor（最初は primary の出力だけ）。
