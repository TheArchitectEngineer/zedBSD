<!-- awesome-plan project=zedbsd record=ws291-p010a -->

# ws291-p010a nvrtx の executor に mview が要る物を足す（depth・index の draw・instance・mip・uniform buffer・cull）

- 親: [WS291](../ws.md)。
- 状態: in-progress（2026-10-29 07:30 P2 新設、q1670 の中で Q1 の判断。08:20 から q1670-i02 で P2 が実装）。
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

## q1670-i02（P2、worktree p2-ws291n、2026-10-29）

### 実装の最初の段（aaf52d03b、基点 main 7b297b46a）

- depth: D32_SFLOAT は optimal で DEPTH_STENCIL_ATTACHMENT だけ（sampled・copy は不可、1 level）。image は block linear の 1 GOB の block（行は 64 byte の倍数、
  高さは 8 行の倍数、memory の requirement と bind の境界は 512 byte）。render pass は colour 1 つと depth 1 つまで（framebuffer は 2 つの view）。
  colour target の push に ZT（SET_ZT_A〜ARRAY_PITCH・ZT_SELECT 1・ZT_SIZE（幅は行の pixel）・ZT_LAYER 0・Z_COMPRESSION off・ZT_SPARSE off）。
  load の CLEAR は SET_Z_CLEAR_VALUE と CLEAR_SURFACE の Z だけ。draw ごとに SET_DEPTH_TEST・WRITE・FUNC（pipeline の depthTest・Write・CompareOp、
  stencil・depth bounds は ENOTSUP）。PTE の kind は今の work channel の map のまま（pitch の kind）。GPU だけが読み書きするので layout は揃うはずだが未確かめ。
- DrawIndexed: BIND_INDEX_BUFFER（UINT16・UINT32）、SET_INDEX_BUFFER_A・SIZE_A・E、BEGIN（instance iterate）・SET_INDEX_BUFFER_F（first）・
  DRAW_INDEX_BUFFER（count）・END、vertexOffset は GLOBAL_BASE_VERTEX_INDEX と VERTEX_ID_BASE、primitive restart off。
- cull・front face: draw ごとに OGL_SET_CULL・CULL_FACE・FRONT_FACE（Vulkan の CCW は 0x901）。
- UNIFORM_BUFFER: vkUpdateDescriptorSets の buffer の descriptor を slot に保持（buffer・offset・range）。compiler の uniform の constant buffer 1+k を
  draw ごとに group 0（vertex）・4（fragment）へ SET_CONSTANT_BUFFER_SELECTOR と BIND_GROUP_CONSTANT_BUFFER（256 byte の境界、16 byte の単位、64 KiB まで）。
- mip: colour image は level の列（各 level は pitch linear、256 byte の境界）。CopyBufferToImage・CopyImageToBuffer（CPU の経路も）・CopyImage・
  BlitImage の各 region の mipLevel、ClearColorImage は全 level、view の baseMipLevel。sampling は pitch の TIC が 1 level なので view の先頭の level だけ
  （遠い copy の縮小の見た目は NVK と違う見込み。block linear の texture にすれば mip の sampling ができる、未着手）。
- CopyBuffer: CPU（BAR1 の window）で、前の work の後に（mview の staging からの一度の upload）。
- 参照は Mesa 25.0.7（tree の外の build にあった物。25.3.6 は取れなかった: archive.mesa3d.org の名前が引けない）の clc597.h・cl9097.h・nvk_cmd_draw.c・
  nvk_cmd_clear.c・nvk_shader.h。値と手順だけ。
- 確かめ（host）: `make ZEDBSD_CONFIG=plan/ws291/tests/config.mk BUILD=build/ws291/k build/ws291/k/vmunix` warning 0（cksum は build/ws291/k/vmunix）、
  plan/tools/nvrtx/nvrtx-vk-host-test.sh PASS 60 checks（D32_SFLOAT の feature の check を新しい形に追従）、nvrtx-sm75-host-test.sh 287 checks 0 failures。
- 未実施: 実機、新しい push の関数の host の check、instance の rate の実機の確かめ、arm64・riscv64 の build。

### 実機（10.0.10.3、移設の後）

- 08:16 lock（P2）。rtx-va-check.sh の deploy で Debian から BootNext 0004 → 300 s で zedBSD が network に出ない（2 回、08:17 と 08:35）。
  1 回目の後の 08:24 の SwitchBot の cycle はプラグが未接続で届いていなかった（ユーザー）。Debian（08:34）: enx6c1ff71a08b6（RTL8156 0bda:8156）10.0.10.3、
  RTX は vfio-pci、BootOrder 0002,0000。zedBSD が起動しないのか link が出ないのかは未切り分け（画面が要る、Q1 に返した）。VA の拡張（01478d325）の実機は未実施。

### 実装の 2 段目（a21aaa3f9）・host の確かめ（032fb7cfe）

- mip の sampling: 2 level 以上の colour image は tiled（各 level が 1 GOB の block の block linear、前の level の直後、境界 512 byte）。
  texture header は block linear（version 3、2D、MAX_MIP_LEVEL・RES_VIEW_MIN/MAX_MIP_LEVEL、address は 512 byte 単位）、sampler は
  mipmapMode（POINT・LINEAR）と minLod・maxLod の clamp（4.8 の固定小数点）。colour target も block linear にできる（幅は行の pixel、
  MEMORY 0、ARRAY_PITCH は level の大きさ）。quad の source の tiled の level は level 0 だけの面として読む（画素の座標のため）。
  CPU の copy の経路は tiled の image に GOB の中の 16 byte の並び（Turing の 2D の GOB）で 1 画素ずつ。
  1 level の image（swapchain・desktop の物）は前と同じ pitch linear。
- host: nvrtx-vk-host-test.sh PASS 86 checks（push の check を足した: depth target・indexed draw・index buffer・cull・depth test・
  depth の clear・uniform buffer・block linear の colour target・tiled の header）。mview の 6 つの shader は SM75 の compiler で全部
  compile でき IR と一致（nvrtx-sm75-desktop-test の build で `--sm 75 userland/tests/mview/shaders/*.spv`、6 compiled 0 refused）。
  rpi4（arm64）・riscv64-virt の vmunix も rc 0・warning 0（nvrtx は amd64 だけ、libvulkan は触っていない）。
- 実機の script: plan/ws291/tests/rtx-mview-check.sh（f7ff592c9、copies 1・5000、shading=pixel、shot 1・5000、kernel の vk の行）。

### 実機の「zedBSD が出ない」の原因（08:52）

- 新しい worktree の plan/tmp/guest/id_ed25519 が mode 0664 で、ssh が鍵を拒み、measure.sh の uname の確かめが失敗していた
  （「zedbsd did not come up」）。chmod 600 で直った（git は mode を追わない）。08:51 に zedBSD は 10.0.30.5 にいた（DP の cable で起動）。

### 再開点（2026-10-29 08:57 P2）

- 実機: 08:56 に返した（Debian、RTX は vfio-pci、NVMe の zedBSD の kernel は vmunix-va1 = 01478d325 の build）。VA の拡張は実機で OK（[p010](../phase010/phase.md) の最後の節）。
  Q1 の指示: p010a の実機の確かめは P1（WS235）の枠の後の枠で。
- 次の枠: `sh plan/ws291/tests/rtx-mview-check.sh build/ws291/rtx/m1 build/ws291/vmunix-p010a1`（dadd28a8d の build、cksum 2422632154。
  新しい worktree なら `make ZEDBSD_CONFIG=plan/ws291/tests/config.mk BUILD=build/ws291/k build/ws291/k/vmunix` で作り直し、plan/tmp/guest/id_ed25519 は chmod 600）。
  見る物: mview の MVIEW SPIN（copies 1・5000、shading=pixel）、shot-1・shot-5000 の PNG（深度の前後と texture）、kernel の `nvrtx: vk` の行と MMU の fault。
  block linear（depth・tiled の texture）の PTE の kind は pitch のままなので、壊れた画や fault が出れば kind を見る。
