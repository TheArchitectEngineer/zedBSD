<!-- awesome-plan project=zedbsd record=ws291-p010a -->

# ws291-p010a nvrtx の executor に mview が要る物を足す（depth・index の draw・instance・mip・uniform buffer・cull）

- 親: [WS291](../ws.md)。
- 状態: planned（2026-10-29 07:30 P2 新設、q1670 の中で Q1 の判断）。
- 出典: Q1 の判断（2026-10-29、q1670）「(2) nvrtx の executor に depth・index・mip（mview が動くまで）→ 新しい p010a、その後 p011（scene-5000 の 50 fps）」
  「RDNA の p002a と同じ「機能を足してから比較」の形」。RDNA の手本は [p002a](../phase002a/phase.md)（2026-10-26 ユーザー「機能を足してから比較（推し）」）。
- 依存: p010 の BAR1 の window の直し（06cdbf381）と work channel の VA の拡張（01478d325）。後続: p011。

## 今の executor（main 42da8d889 の src/drivers/gpu/nvrtx/vk、読んだ事実）

- 記録できる command（vk-record.c）: BIND_PIPELINE・SET_VIEWPORT・SET_SCISSOR・SET_LINE_WIDTH・BIND_DESCRIPTOR_SETS・BIND_VERTEX_BUFFERS・DRAW・
  COPY_IMAGE・BLIT_IMAGE・COPY_BUFFER_TO_IMAGE・COPY_IMAGE_TO_BUFFER・CLEAR_COLOR_IMAGE・PIPELINE_BARRIER・PUSH_CONSTANTS・BEGIN/END_RENDER_PASS。
  無い: BIND_INDEX_BUFFER・DRAW_INDEXED・COPY_BUFFER。
- depth: どの format にも DEPTH_STENCIL_ATTACHMENT の feature が無い（vk-device.c 600）、render pass は depth を断る（vk-target.c 161）、push は
  depth 無しの固定（exec-push.c 343・368）。mview は起動すると `api=depth format result=-11` で止まる（p010 の実機）。
- image は mipLevels 1 だけ（vk-resource.c 245）。descriptor は COMBINED_IMAGE_SAMPLER だけ（vk-layout.c 729）。
- cullMode は pipeline に読む（vk-pipeline.c 325）、instance の入力の rate は扱いがある（vk-pipeline.c 488、push で効くかは未確かめ）。

## mview が使う物（userland/tests/mview の grep）

- D32_SFLOAT（または D24_UNORM_S8_UINT・D32_SFLOAT_S8_UINT のどれか）、DrawIndexed（UINT32）、vertex の rate VERTEX と INSTANCE（copy_offset）、
  descriptor の UNIFORM_BUFFER と COMBINED_IMAGE_SAMPLER、cull の BACK・NONE・FRONT_AND_BACK、mipLevels > 1 の texture と level の blit、
  CopyBuffer・CopyBufferToImage、push constants。

## 範囲

1. depth: D32_SFLOAT の optimal の DEPTH_STENCIL_ATTACHMENT（1 level、sampled は不可でよい）。zeta の surface（Turing の 3D class の SET_ZETA_*・ZETA_ENABLE・
   ZETA_SIZE、block linear の layout）、render pass の depth の attachment（clear・load・store）、pipeline の depth の test・write・compare、depth の clear。
2. index の draw: vkCmdBindIndexBuffer（UINT16・UINT32）と vkCmdDrawIndexed（vertexOffset・firstIndex・instanceCount・firstInstance）。
3. instance: instanceCount > 1 と rate INSTANCE の vertex の流れ（divisor）。
4. uniform buffer の descriptor（constant buffer の bind、SM75 の compiler の cbuf の読みが要るなら compiler の対応も）。
5. mip: mipLevels > 1 の image（level ごとの offset、copy・blit の level、view の level の範囲、sampler の LOD、TIC の level）。
6. vkCmdCopyBuffer（copy engine か 3D の copy）。cull の BACK・FRONT_AND_BACK。
- 参照（MIT、値と手順だけ、code は写さない）: Mesa 25.3.6 src/nouveau/vulkan（nvk_cmd_draw.c、nvk_image.c、nvk_cmd_copy.c）、open-gpu-doc
  classes/3d/clc597.h（TURING_A の method）。block ごとに「Value(s) referenced:」「Procedure referenced:」。

## 受け入れ

- amd64 の vmunix が warning 0。plan/tools/nvrtx の host 試験（nvrtx-vk-host-test.sh・nvrtx-sm75-host-test.sh）が通る（追従は可）。
- 実機（RTX 2070）で mview の既定の scene（`--content=game --present=mailbox`、copies 1 と 5000）が動き、keiland-shot の PNG で Linux（NVK）と同じ見た目
  （深度の前後、texture）。scene-5000 の fps を p011 の起点として記録。

## 記録

- 2026-10-29 07:30 P2: 新設（未着手）。
