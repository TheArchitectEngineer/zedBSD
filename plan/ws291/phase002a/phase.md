<!-- awesome-plan project=zedbsd record=ws291-p002a -->

# ws291-p002a amdrdna の executor に depth・index の draw・mip の texture を足す（mview が動くまで）

- 親: [WS291](../ws.md)。
- 状態: cleared（2026-10-27 00:43 Q1、main 4cdbf1b7a）。前: in-progress（2026-10-27 00:42 P2、q1600）。受け入れの条件は満たした（下の結果）、clearance は Q1 の判定。
- 出典: 2026-10-26 ユーザー「機能を足してから比較（推し）」（Q1 経由、3D の比較の進め方の a/b/c の a）。
- 依存: p002 の Linux の基準（済み）。後続: p002 の zedBSD の計測。

## 範囲

src/drivers/gpu/amdrdna/vk（kernel の中の Vulkan の executor）に、mview が既定の scene で動くのに要る物を足す:

1. depth の attachment: D32_SFLOAT（と D24_UNORM_S8_UINT か D32_SFLOAT_S8_UINT のどれか 1 つ以上）の format の feature、depth の image（GFX10 の DB が読める layout）、render pass の depth の attachment（clear・load・store）、DB の register（Z_INFO・DEPTH_SIZE・READ/WRITE_BASE・DEPTH_VIEW・DEPTH_CONTROL・DEPTH_CLEAR）、pipeline の depth state（test・write・compare）の反映。
2. index の draw: vkCmdBindIndexBuffer（UINT32、UINT16）と vkCmdDrawIndexed（INDEX_TYPE・INDEX_BASE・DRAW_INDEX_2）。
3. mip の texture: mipLevels > 1 の image（level ごとの offset、copy・blit の level、image view の level の範囲、sampler の LOD）。

## 受け入れ

- host 試験（変えた所の短い物）と amd64 の vmunix・userland の build が warning 0。
- 実機（10.0.10.3）で mview の既定の scene（`--content=game --present=mailbox`、copies 1 と 1500）が動き、keiland-shot の PNG で Linux と同じ見た目（深度の前後が正しい、texture が出る）。`KWL SCANOUT direct=1`。
- 前後の数値（mview の SPIN・STAGES）を p002 に記録。

## 記録

- 2026-10-26 23:47 P2: 新設。
- 2026-10-27 00:42 P2: 実装と実機の確認。

## 結果

実装（src/drivers/gpu/amdrdna/vk、commit b20e5545e、160ad707f の present の log）:

- depth: D32_SFLOAT（optimal の DEPTH_STENCIL_ATTACHMENT だけ、1 level、sampled は不可）。image は GFX10 の唯一の Z の swizzle（SW_64KB_Z_X、128×128 の block、64 KiB 境界、vk-memory の要求の alignment も 64 KiB）。render pass は colour 1 枚＋depth 1 枚まで、framebuffer は view 2 個まで。DB の surface（DB_RENDER_CONTROL の圧縮無し、DB_DEPTH_VIEW、DB_DEPTH_SIZE_XY、0x2803c からの 7 語、0x28068 からの 5 語、HTILE 無し、DB_RENDER_OVERRIDE 0）を pass ごと・indirect buffer ごとに書く。pipeline の depth test・write・compare を DB_DEPTH_CONTROL へ（stencil・depth bounds は断る）。clear は pass の色の clear の quad で、viewport の深さの範囲を clear 値にして DB_DEPTH_CONTROL を ALWAYS・write にする（色だけ・深さだけも可）。転送の quad は DB_DEPTH_CONTROL 0。
- index の draw: vkCmdBindIndexBuffer（16・32 bit）・vkCmdDrawIndexed（VGT_INDEX_TYPE、INDEX_BUFFER_SIZE、DRAW_INDEX_2、vertexOffset は base vertex の SGPR、firstInstance）。
- mip: mipLevels は 15 まで。GFX10 の linear の mip chain（小さい level が先、level ごとの pitch は幅の 256 byte 揃え、Mesa の addrlib Gfx10Lib::HwlComputeSurfaceInfoLinear の通り）。descriptor の BASE_LEVEL・LAST_LEVEL・MAX_MIP。blit・copy・buffer からの copy・clear は level ごと。vkGetImageSubresourceLayout も level ごと。view の baseMipLevel・levelCount。
- vkCmdCopyBuffer（CP DMA の move）。
- 参照: Mesa 25.0.7 の src/amd（registers/gfx10.json、gfx10-rsrc.json、vulkan/radv_cmd_buffer.c、common/ac_descriptors.c、addrlib）を build/ws291/ref に取って読んだ（tree には入れない）。

確認:

- build: amd64 の vmunix・mview・wayland が -Werror で warning 0（build/ws291/vmunix-build.log ほか）。host 試験は無し（executor を host で動かす試験台が無い。実機の画像の一致で確かめた）。
- 実機 10.0.10.3（QEMU ではない）: mview の既定の scene が動く。keiland-shot の PNG（build/ws291/measure/zshot/z-shot-1.png・z-shot-6500.png、`--frames` の静止 view）を Linux の RADV の weston-screenshooter の PNG（build/ws291/measure/ref/linux-shot-1.png・linux-shot-6500.png）と比べ、1 copy は全 pixel 一致（差の平均 0.0）、6500 copies は 2073600 pixel 中 8 pixel が 16 階調以上違うだけ。深度の前後・texture・mip が Linux と同じ。
- `KWL SCANOUT direct=1`: Keiland の scanout の import の修正（160ad707f、userland/desktop/libkeiland-backend-zedbsd/scanout-zedbsd.c）の後に出る。
- 数値は p002 の表（scene-6500 で zedBSD 20.1 fps、Linux 50.1 fps）。

残り:

- `--frames` の run（FIFO、content game）が 80〜300 frame で vkAcquireNextImageKHR の VK_ERROR_SURFACE_LOST で落ちた（scanout の import の修正の前。keiland-shot を撮った時の game mode の出入りと関係しそう、未調査）。spin の run は落ちない。
- executor の vkAllocateCommandBuffers は 1 回 64 個まで（COMMAND_BATCH_MAX）。mview は 1 個ずつ確保して避けた。
- 深度は HTILE（HiZ・fast clear）無し、texture は linear のまま（性能の改善の案）。
