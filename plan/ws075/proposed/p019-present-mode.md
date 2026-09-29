# ws075-p019 の提案: display の present mode（MAILBOX・IMMEDIATE）を UAPI で運ぶ

状態: 提案（未適用、2026-09-29）。WS075 の規則（UAPI の変更は事前に提示する）による。承認の前は適用しない。

## 今

- `include/uapi/gpu-display.h` の present の flag は `GPU_DISPLAY_PRESENT_FIFO`（1）と `GPU_DISPLAY_PRESENT_BLOB`（2）だけ。GPU core（`src/drivers/gpu/gpu.c`）は
  FIFO の立っていない present を EINVAL で断る。libvulkan の display WSI（`wsi-display.c`）は FIFO だけを報告する。compositor（`userland/desktop/wayland/display.c`）は
  ioctl を直接 FIFO で呼ぶ。
- ws075-p019 で FIFO は「flip を arm して返り、次の present（と display の wait）が latch を待つ」形になった（UAPI を変えない。tear なし、1 frame の先行）。

## 提案する差分

1. `include/uapi/gpu-display.h`:
   ```c
   #define GPU_DISPLAY_PRESENT_FIFO	1U
   #define GPU_DISPLAY_PRESENT_BLOB	2U
   +#define GPU_DISPLAY_PRESENT_MAILBOX	4U	/* 新しい frame が armed の flip を置き換える。vblank で latch、tear なし */
   +#define GPU_DISPLAY_PRESENT_IMMEDIATE	8U	/* 待たない。実装は MAILBOX と同じでよい（tear は許されるが起こさない） */
   +#define GPU_DISPLAY_MAILBOX		64U	/* gpu_display_info.flags: MAILBOX を受ける display */
   ```
   FIFO・MAILBOX・IMMEDIATE はちょうど 1 つを立てる。
2. `src/drivers/gpu/gpu.c` の `GPU_DISPLAY_PRESENT` の検査: 上の 3 つのうち 1 つと、任意の `BLOB`。MAILBOX・IMMEDIATE は、backend が info で `GPU_DISPLAY_MAILBOX` を
   報告した display だけ。
3. i915（`display/present.c`・`scanout.c`・`modeset.c`）: resident の buffer を 3 つに（front・armed・free）。MAILBOX の present は latch を待たず、armed の flip が
   latch していなければ free の buffer に描いて flip を置き換える（PLANE_SURF の書き直し。置き換える前に latch したかを PLANE_SURFLIVE で確かめ、front・armed・free
   を付け直す）。display の wait は newest の latch を待つ。
4. libvulkan `wsi-display.c`: info の `GPU_DISPLAY_MAILBOX` があれば MAILBOX と IMMEDIATE も報告し、swapchain の mode を present の flag に写す。
5. compositor は変えない（FIFO のまま）。MAILBOX を使うのは display WSI の app（vkdemo 等）。

## 要る判断

- 上の UAPI の追加（2 つの present の flag と 1 つの info の flag）の承認。承認されたら p019 の続き（または新しい Phase）で 3 と 4 を入れる。
- 承認されない場合: display の present mode は FIFO だけ（今の libvulkan の報告のまま）で、p019 の MAILBOX・IMMEDIATE は範囲の外に移す。
