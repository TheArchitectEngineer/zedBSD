<!-- awesome-plan project=zedbsd record=ws083-p002 -->

# ws083-p002: libvulkan の Vulkan Video の骨組み

Status: in-progress（q833、P1）
Disposition: normal
Parent: [WS083](../ws.md)

## 範囲（Q1 の ACK 2026-10-07、ユーザーの判断 H1・H2・H3・HD1・HD6 の後）

1. `include/uapi/gpu-op.h` に承認済みの `proposed/gpu-op-video.diff` を当てる（H1）。
2. `include/libc/vulkan/vulkan_video.h`（`tools/maintain-video.noct` で 1.4.309 の header から選ぶ）と `vk_video/` の 3 header、API-PROVENANCE・LICENSE-API（H3）。
3. libvulkan: capset の native の語（HD1）、拡張の列挙と照合、queue family の濾しと video の properties、external-properties の pNext と format の feature、`video.c`、`sync2.c`（H2）、codec・dispatch の生成し直し、README の非適合（HD6）。
4. host の試験 `plan/ws083/tests/host-libvulkan-video.c`、build warning 0、export の数。

kernel（boot.c）と i915 は触らない（p003b）。toolchain は触らない。

## 記録

- 2026-10-07 夕 段 1: `gpu-op.h` に diff を当てた（`git apply` そのまま、版 2、`0x10000`〜`0x1000d`）。`make BUILD=build/p1-k … vmunix dynamic/libvulkan.so` 成功 warning 0。
- 2026-10-07 夕 段 2: `include/libc/vulkan/vulkan_video.h`（`userland/desktop/libvulkan/tools/maintain-video.noct` で 1.4.309 の header から: 4 拡張の block、19 command、Vulkan 1.3 の 8 構造体・6 typedef・146 の stage/access の定数）、`vk_video/` の 3 header（package から無変更で複写、SHA は API-PROVENANCE）、`vulkan.h` が `vulkan_video.h` を include、API-PROVENANCE.md の節。確認: host の gcc（`-std=c99 -pedantic -Werror`）と target の clang で `<vulkan/vulkan.h>` の video・sync2・StdVideo の型と関数の宣言を使う 1 file が通る。`make … vmunix dynamic/libvulkan.so bin/wayland bin/vkdemo dynamic/libGLESv2.so` 成功 warning 0。

## 再開の情報（段 3 から）

段 3（libvulkan の C、design.md §3〜§5・§4.2・§4.3）の順:
1. `context.c`: capset の native の語（`bytes == 168 || bytes == 176` で vendor 部、176 かつ byte 168 が `0x5a4e4154` の時 byte 172 の bit 0 → `context->video_h264`）。
2. `internal.h`: object kind 2 つ、拡張の bit 4 つ（256・512・1024・2048）、physical に family の codec ops。
3. `instance.c`・`device.c`: `video_h264` の時だけ 4 拡張を列挙・照合（sync2 は `vkCreateDevice` の時に properties2 を検べる）、`physical_load_queues` の後に D3 の濾しと `GPU_OP_GET_PHYSICAL_DEVICE_QUEUE_FAMILY_VIDEO_PROPERTIES`。
4. `external-properties.c`: queue family properties2 の pNext、features2 の sync2、image format properties2 の profile list、format の feature（D23）。
5. `video.c`（13 entry point、§3.3〜§3.6、record は §4.2）、`sync2.c`（6、§3.7）。
6. `tools/maintain-codec.noct`・`maintain-dispatch.noct` で codec と dispatch を生成し直す（173 → 192）、`Makefile`・export の数の検査（所在は U13）。
7. README の非適合 N1〜N4。
8. host の試験 `plan/ws083/tests/host-libvulkan-video.c`（transport の stub）。
