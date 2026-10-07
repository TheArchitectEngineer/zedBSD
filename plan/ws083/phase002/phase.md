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
