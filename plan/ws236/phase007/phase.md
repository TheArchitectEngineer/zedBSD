<!-- awesome-plan project=zedbsd record=ws236-p007 -->

# ws236-p007: 規約の全文の確認

Parent: [WS236](../ws.md)
Status: planned
Phase disposition: normal

## 範囲

WS236 が足した・変えた C（`src/drivers/platform/rpi4/rpi4-{vchiq,vchiq-wire,mmal,mmal-wire,mmal-probe,h264}.c/.h`、`src/drivers/gpu/bcm2711/vulkan-video.c/.h` と `#if CONFIG_DRIVER_BCM2711_VIDEO` の部分、`rpi4-firmware.c` の追加、`userland/tests/vkvideo-probe` の変更、`plan/ws236/tests/*.c`）を `plan/coding-style.md` の全文と照らす。

## 見つけてある点（2026-10-14、P3 の自己点検）

- 条件の中の関数の呼び出し: `rpi4-vchiq.c`（`sched_ticks()`）、`rpi4-vchiq-wire.c`（`drv_rpi4_vchiq_slot_bytes()`・`drv_rpi4_vchiq_stride()`）、`rpi4-mmal.c`（`drv_rpi4_mmal_get32()` の多数）、`rpi4-h264.c`（`high_profile()`・`is_slice()`・`read_bits()`）、`rpi4-mmal-probe.c`（`sched_ticks()`・`kern_clock_gettime()`）、`vulkan-video.c`（`bcm2711_vulkan_video_available()`・`bcm2711_vulkan_object_find()`・`find_pps()`）。
- 式で作る Boolean: `vulkan-video.c` の `append_set`（`same = … && kern_memcmp(…) == 0`）、`rpi4-vchiq.c` の `send_bulk`（`open = …`・`full = …`）、`rpi4-mmal.c`（`busy = …`・`outstanding = …`）。
- 試験の C（`plan/ws236/tests/*.c`）は詰めて書いてある（宣言の後の空行、各段の注釈、関数の注釈）。
