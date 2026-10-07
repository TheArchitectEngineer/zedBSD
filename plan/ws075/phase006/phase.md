<!-- awesome-plan project=zedbsd record=ws075p006 -->

# ws075-p006: MRT・query・texel と storage の buffer・multisample（実行器と compiler）

Phase ID: `ws075-p006`
Parent: [WS075](../ws.md)
Status: in-progress（2026-09-28〜。増分 1〜5 済み（MRT・occlusion query・VS の storage buffer・stencil・multisample と resolve）。p005 の egltest の fbo・es3 の後退は command.c の op の list の再確保の後の書き込みと分かり直した。増分 6（feedback の drawn-from-captured の原因の bufferFeatures、dynamic の storage buffer、rasterizerDiscardEnable）は build と host の fixture まで、実機は未実施（5330 に届かない）。残りは texel buffer・sampler2DMS と増分 6 の実機）
Phase disposition: normal
承認: 2026-09-27 ユーザー「…i915の高度化に進んでください。」、WS075 の計画（main の登録）。p005 の後（依存 p005 cleared）。

## 範囲

egltest の OpenGL ES 3 の場面 targets・blits・queries・feedback を実機の i915 で通す。そのために要る:

1. MRT（ws031-p031）: 複数の colour attachment への描画（compiler の location 0 以外の出力、render target の binding table と
   blend state の entry、clear）。
2. depth と stencil の形式: DEPTH_COMPONENT24（`X8_D24_UNORM_PACK32`）、DEPTH24_STENCIL8（`D24_UNORM_S8_UINT`）と stencil の試験。
3. occlusion query（sync の module）と fence sync。
4. texel buffer（buffer view、samplerBuffer の texelFetch・textureSize）と storage buffer（transform feedback の VS の store）。
5. multisample の image と resolve（sampler2DMS の texelFetch、blit の resolve）。

範囲外: geometry shader・gl_Layer・layered の描画（p007）、GLX の glxtest（p007 以降で capture の場面を足す）。

## 手順

最初に 4 場面を今の実行器で実機に走らせ、log の CHECK と executor の拒否（`i915: vk: ... refused`・`XXX unimplemented`）から
不足を並べ、場面ごとに直す。11 場面は 1 回の capture に収まらないので、4 場面は別の runner
`plan/ws031/tests/zdesktop/run-egltest6.sh`（`KEILAND_APP=egltest6`、`vkloop-hw.sh` に足した）で走らせる。

基準の run（`build/ws075-p006/base-egltest6`）の不足: 4 つの colour attachment の render pass の拒否（MRT）、opcode 47
（vkCreateQueryPool）の未実装、vertex shader の SPIR-V の BufferBlock の拒否（transform feedback の capture の storage buffer）、
D24S8・D32S8（stencil）、multisample。failures: targets 16・blits 10・queries 18（glerror 0x505）・feedback 9。

## 設計

### 増分 1: MRT（2026-09-28）

- compiler（`compile.c`）: fragment の colour の location n（0〜3）を r(124 − 4n)..r(127 − 4n) に置き、書く location ごとに
  render-target write を昇順に（最後だけ Last Render Target Select と EOT）。binding table の entry は `I915_SHADER_RT_BTI(n)`
  （0、または 16 + n: texture の 1〜16 の後）、render target index は extended descriptor の bit 12〜14（Mesa の Gen11+ の
  `lower_fb_write_logical_send()`）。何も書かない shader は location 0 に 0 を書く。Mesa の brw_disasm が受ける。
- 実行器: render pass は colour attachment 4 つまで（`I915_GFX_MAX_ATTACHMENTS` 8、`i915_gfx_pass.color_attachments[]`、
  slot 0 は従来の `color_attachment`、`drv_i915_gfx_pass_color()`）。draw は slot ごとに render target の surface（view の無い slot は
  null surface、`i915_state_null_surface_write()`）、BLEND_STATE の entry を slot ごとに（書き込み mask は attachment ごと、
  blend の式は attachment 0 の、integer の target と blendEnable の無い attachment は blend しない）。colour の無い pass
  （depth だけ）は depth の view の大きさで描く（`drv_i915_gfx_draw_extent_view()`）。vkCmdClearAttachments は colour index の
  attachment を clear する。

### 増分 2: occlusion query（2026-09-28）

- `fence.c`（sync の module）: vkCreateQueryPool・vkDestroyQueryPool・vkGetQueryPoolResults（opcode 47〜49）。pool は GPU の object
  （query q の開始・終了の depth count が 16q・16q + 8、availability が 16 count + 8q）。vkCmdBeginQuery・vkCmdEndQuery は PIPE_CONTROL
  の post-sync の PS depth count（depth stall、anv の emit_ps_depth_count()）、終了の後に availability 1、vkCmdResetQueryPool は
  availability 0（64 query ずつの操作）。結果は submit が終わってから CPU で読む（clflush の後）。occlusion の pool だけ。
- 実行器の object の作成を公開（`drv_i915_gfx_object_create()`・`_destroy()`、draw.c）。host の fixture の stub を足した。

### 増分 3: vertex shader の storage buffer（transform feedback）（2026-09-28）

- compiler: BufferBlock の struct の Uniform の変数（と StorageBuffer の class）を storage buffer（`PTR_SSBO`、uniform の種類
  `I915_IR_UNIFORM_STORAGE`）、OpTypeRuntimeArray、NonWritable・NonReadable は無視。access chain の動的な index は byte offset の
  IR の整数に。load・store は `I915_IR_LOAD_STORAGE`・`I915_IR_STORE_STORAGE`（store は block の predicate を持つ）。
- codegen: A64 の untyped surface read・write（1 channel、SIMD8、stateless 253、SFID 12、Mesa の
  `brw_dp_a64_untyped_surface_rw_desc()`）。buffer の GPU address は push data の block（`i915_shader_block.address`）で渡り、
  channel ごとの address は low + offset と carry（CMP の −1 を ADD で引く）を payload の 2 register に interleave。predicate の
  ある store は f0.0 で mask。Mesa の brw_disasm が受ける。
- 実行器: storage buffer の descriptor を bind（`descriptor.c`）、push data に address を書く（`state.c`）。draw ごとの cache の
  invalidate に VF cache を足した（shader や copy の書いた vertex buffer）。`vertexPipelineStoresAndAtomics` を報告（libGLESv2 は
  これで GL_VERSION を 3.0 にする。atomic は未実装）。

### 増分 4: stencil（2026-09-28、62a10ad0 を再適用して直した）

- 62a10ad0（D32_SFLOAT_S8_UINT・S8_UINT、Y tile の separate stencil plane、3DSTATE_STENCIL_BUFFER、3DSTATE_WM_DEPTH_STENCIL の
  stencil、vkCmdSetStencil*、pass の stencilLoadOp と ClearAttachments の aspect、stencil の clear は Y tile の R8_UNORM に value/255）
  を cherry-pick した。
- st2 の GL_OUT_OF_MEMORY の元: libGLESv2 が DEPTH24_STENCIL8（D32S8 に落ちる）の texture を作るときの vkCmdCopyBufferToImage
  （depth aspect）で、実行器が buffer 側を D32S8 の形式（Y tile の R32_FLOAT、pitch 16）と記述し、`drv_i915_gfx_surface_write()` が
  pitch の 128 byte の境界で EINVAL（vk.log の `GPU copy 4x4 -> 4x4 ...: 3`、`command buffer stopped ... error 3`）。submit が EIO に
  なって libvulkan が device lost（-4）、以後の pipeline の作成と eglSwapBuffers が失敗し、描画が全て黒だった。
- 修正（`command.c`）: image の plane を aspect で選ぶ `i915_image_planes()`・`i915_image_plane_surface()`（depth・colour の main plane、
  stencil plane は Y tile の S8 = R8_UNORM）。buffer と image の copy は depth aspect の D32S8 を R32_SFLOAT、stencil aspect を R8_UNORM の
  linear の buffer として stencil plane と copy する。vkCmdCopyImage・vkCmdBlitImage は両側の aspect が名指す plane ごとに（
  `i915_image_copy_plane()`・`i915_image_blit_plane()`）。stencil の clear も plane の記述を使う。
- `i915_image_surface_write()`（`state.c`）: 単一 slice の QPitch を、stencil を持つ形式では depth plane の行（stencil_offset / pitch）
  に（以前は stencil plane を含む bytes / pitch。QPitch は mask されるので実害は無かったが、意味を正した）。
- st1 の compositor の停止（wlkill の直後）は st3 で再現しない（1 回、compositor は最後まで commit を続けた）。stencil と無関係に
  見えるが、1 回の観察なので未確定。

### 増分 5: multisample と resolve（2026-09-28）

- image（`image.c`）: 2x・4x の 2D・1 level・1 layer の image。colour は Y tile で sample ごとに tile 行の揃った slice（MSFMT_MSS、
  slice_rows 離れ）、depth・stencil は pixel の sample を平面に interleave（2x は横 2、4x は 2x2、`sample_width`・`sample_height`、
  isl の ISL_MSAA_LAYOUT_INTERLEAVED）。`drv_i915_gfx_image_slice()` は multisample の image を Y tile（`i915_gfx_surface.tiled`）で記述する。
- 描画（`state.c`・`draw.c`・`pipeline.c`）: pipeline の rasterizationSamples と pSampleMask、3DSTATE_MULTISAMPLE の sample 数、
  3DSTATE_SAMPLE_MASK、3DSTATE_RASTER の DX multisample rasterization、3DSTATE_SAMPLE_PATTERN に 2x・4x の標準の位置
  （Mesa intel_sample_positions.h）、render target の RENDER_SURFACE_STATE の Number of Multisamples・Y tile・Surface Array。
  executor の試験が手で作る pipeline（samples 0）は 1 sample を書く。
- resolve（`command.c`・`blit.c`）: vkCmdResolveImage（opcode 122、VkImageResolve は VkImageCopy と同じ wire）。新しい resolve kernel
  （IR から、4 つの sampled image を nearest で sample して平均、2x は各 sample を 2 回名指す）で sample の slice を平均する。
- clear: colour の clear（pass の begin・ClearAttachments・ClearColorImage）は全 sample の slice を埋める（`i915_fill_samples()`）。
  depth・stencil の矩形は sample の矩形へ（`i915_sample_rect()`）。
- device（`instance.c`）: framebuffer の sample 数 1・2・4、render target に使う（sampled・storage でない）optimal の 2D image の
  sampleCounts 1・2・4。D32S8・S8 に TRANSFER_SRC・DST の format feature。sampler2DMS（sampled の multisample）は未。
- host の fixture の stub に `drv_i915_gfx_resolve()` を足した（`plan/ws031/tests/i915-vk-render-stubs.inc`）。

### 後退（2026-09-28 に見つけた、増分 5 より前から）: p005 の egltest の fbo・es3

- p005 の 7 場面（`KEILAND_APP=egltest`）を増分 5 の後に走らせると fbo 12・es3 4（p005 の fix-egltest3 は全て 0）。fbo は FBO の
  中で blend の四角だけが描かれず（fbo-fbo の blend が背景の 0000ff）、窓へ FBO の texture を貼る fan が黒。es3 は D0〜D3（instanced、
  divisor）が背景のまま。glsl・glsl3・cube・formats・volumes は 0。executor の拒否・error は log に無い。
- 増分 5 を外した HEAD（8f36900f と main の merge）でも同じ数（`build/ws075-p006/st3-p005scenes`）: 増分 5 と無関係。
- libGLESv2・libEGL・libvulkan は p005 の後 rename だけ（rename を追った差分で 61 行の文字列）。GL_VERSION は p005 の 2.0 から 3.0 に
  （23622062 の vertexPipelineStoresAndAtomics）だが egltest は version で分岐しない。
- bisect: 23622062（VF の invalidate と feature）を外した run（`bisect1b`）でも fbo 12・es3 4 → 23622062 ではない。残りの候補は
  9c921466（MRT）・adf87fef（query）・a614b54e（SSBO）・62a10ad0（stencil）・main の kernel の変更（BUG-075 の inode/cache の discard、
  HAL の quiet console）。
- `bisect1`（同じ image の 1 回目）は wlkill の直後（ZWL frame 163、client の CLEANUP の後）で compositor が止まり egltest が走らな
  かった: [BUG-085](../../bugs/BUG-085.md)（st1 と同じ症状、間欠）。
- 実機の bisect（p005 の 7 場面、各 tree を自分の sysroot で build、log は `build/ws075-p006/bisect-*`）: adf87fef fbo 0、a614b54e
  fbo 0・es3 0、23622062 全て 0、62fe5e06 全て 0、31e4b072（この session の始めの main）全て 0。31e4b072 に stencil（8f36900f）を
  足すと fbo 12・es3 4。stencil の clear を止めても（diag-a）、stencil buffer を null にしても（diag-b）直らない。D32S8 の format の
  feature を消した run（diag-nod32s8）は egltest が接続せず判定できなかった（原因は未調査）。worktree の sysroot を作り直すと copy と
  byte 単位で同じで、sysroot は原因でない。
- 原因（p006 より前からある潜在の誤り）: `i915_command_op()` は op の list を再確保で倍にする。vkCmdBindVertexBuffers（offset）、
  vkCmdBindDescriptorSets（`i915_record_dynamic_offsets()` の dynamic offset）、vkCmdCopyImage・BlitImage（filter）の記録は、後の
  `i915_command_op()` をまたいで op の pointer を持っていた。記録の途中で list が伸びると、その書き込みは解放された古い list に行き、
  draw が dynamic UBO offset や vertex の offset を失う（es3 の形は uniform block の値が 7 の時だけ見えるので消え、fbo は blend の四角と
  texture の fan が消えた）。stencil の増分で vkCmdSetStencil* が pipeline の bind ごとに 3 つの op を記録するようになり、list の伸びる
  位置が変わって表に出た。
- 修正（`command.c`）: 記録は op の場所（list の index）を持ち、`i915_command_op_mark()`・`i915_command_op_at()` で引き直す。
  host の fixture resdispatch・pipe PASS（cmdbuf は既存の `test_blend_state` の assertion のまま）。
- 実機: fix1b-p005scenes は 7 場面全て 0（compositor は frame 2393 まで）。fix1-egltest6 は targets 0・blits 0・queries 0・feedback 1
  （drawn-from-captured は変わらず）。修正の 1 回目（fix1-p005scenes）は fbo の検査（fbo-fbo 0・fbo 0）の後、frame 614 で guest の全体が
  止まった（hang の報告なし、capture の frame と log が同時に終わる）。同じ image の再試行は最後まで動いた。BUG-085 に関係の可能性の
  ある観察として記録した。

### 増分 6: feedback の drawn-from-captured、dynamic の storage buffer、rasterizerDiscardEnable（2026-09-28）

- drawn-from-captured の原因: GPU の copy や cache ではなかった。実行器の vkGetPhysicalDeviceFormatProperties が全ての形式で
  `bufferFeatures` を 0 と答えていた（`i915_instance_format_features()`）。libGLESv2 の `draw_vertices()` は VERTEX_BUFFER の
  feature の無い形式の buffer object を device の buffer として bind せず、CPU 側の bytes（`attrib->buffer->data`）を stream へ
  写して描く。transform feedback の buffer は device の copy だけが新しい（`gpu_written`、map されるまで CPU の bytes は古い）ので、
  capture した位置の代わりに 0 の頂点を描き、四角が出なかった。map の検査（gles_buffer_fetch で device の copy を読む）は通る。
  i915 では libGLESv2 の全ての頂点が stream の copy を通っていた（Venus は feature を答えるので直接）。
- 修正（`instance.c`・`state.c`）: 頂点の fetcher の表（`i915_gfx_vertex_formats[]`）にある形式に `VERTEX_BUFFER_BIT` を答える
  （`drv_i915_gfx_vertex_format_supported()`）。以後 libGLESv2 は buffer object を直接 bind する（VF の invalidate と RT の flush は
  draw ごとに既にある）。host の fixture resdispatch の format の検査を合わせた。
- dynamic の storage buffer（`command.c`）: `i915_record_dynamic_offsets()` が `STORAGE_BUFFER_DYNAMIC` の binding にも offset を
  番号順に配る（Vulkan の順: set の順、binding 番号の順、uniform と storage を区別しない）。descriptor の slot の `dynamic` と
  push data の address への加算（`state.c`）は既に storage も扱っていた。
- rasterizerDiscardEnable（`pipeline.c`・`draw.c`・`genxml.h`）: pipeline の `rasterizer_discard`、draw の 3DSTATE_STREAMOUT の
  dword 1 bit 30（API Rendering Disable、anv の so.RenderingDisable）。stream output は無効のまま、vertex shader（と store）は走る。

## 検証

| 確認 | 結果 |
| --- | --- |
| host の vk の fixture spirv・lower・resdispatch・eu・compile・pipe | PASS（res・sync・cmdbuf は p005 から既存の失敗） |
| gentool（Mesa brw_disasm、`BRW_TOOLS=/home/awe/p014-c/mesa/build-asm/src/intel/compiler`） | PASS。MRT の fragment shader（location 0・1・3、discard あり）と storage buffer の vertex shader（load・store・predicate）も受ける（scratch で確認） |
| 実機 capture egltest6（`build/ws075-p006/`） | mrt1（MRT）: targets 16 → 7。q1（query）: queries 18 → **0**、targets 6。ssbo1: feedback 8 → 2。ssbo2（VF の invalidate と feature）: feedback **1**、targets 6、blits 10、queries 0。画面 `build/ws031-shots/ws075-p006-20260928-{q1,ssbo2}-sheet.png`。hang・fault なし |
| 実機 capture egltest6 st3（増分 4、`build/ws075-p006/st3`、この worktree の build） | targets 6 → **0**（stencil-complete・depth-stencil-texture-complete・draw-buffer-other も ok、stencil-inside/outside・depth-stencil-test の画素 ok）、blits 10（変化なし、multisample）、queries 0、feedback 1。capture の検査 pass（desktop_drawn・scenes_shown）。hang・fault・device lost なし。画面 `build/ws031-shots/ws075-p006-20260928-st3-sheet.png` |
| 実機 capture egltest6 ms1（増分 5、`build/ws075-p006/ms1`） | blits 10 → **0**（resolve-inside/outside・resolve-edge-mixed・depth-blit ok）、targets 0、queries 0、feedback 1（drawn-from-captured）。capture の検査 pass。hang・fault なし。画面 `build/ws031-shots/ws075-p006-20260928-ms1-sheet.png` |
| host の vk の fixture（増分 5） | spirv・lower・resdispatch・eu・compile・pipe PASS。res・sync・cmdbuf は既存の assertion で失敗（res: `opcode 78 routed to the res module`、sync: `unimplemented opcode 39`、cmdbuf: `test_blend_state` の push）。増分 5 の前の同じ fixture との比較は未実施 |
| 実機 capture egltest（p005 の 7 場面、回帰） | ms1-p005scenes（増分 5 あり）・st3-p005scenes（なし）・bisect1b（23622062 を外す）: glsl・glsl3・cube・formats・volumes 0、fbo 12、es3 4（上の後退） |
| 実機 capture（後退の修正、`build/ws075-p006/fix1b-p005scenes`・`fix1-egltest6`） | p005 の 7 場面 glsl・glsl3・fbo・cube・es3・formats・volumes 全て **0**。egltest6: targets 0・blits 0・queries 0・feedback 1。画面 `build/ws031-shots/ws075-p006-20260928-{fix1b-p005scenes,fix1-egltest6}-sheet.png`。fix1-p005scenes は frame 614 で停止（上） |
| 増分 6 の build と host の fixture（23caa415・082c0957） | image（`build/ws075-p006/img`＝egltest6、`img5`＝p005 の 7 場面、この worktree）の build は warning 0（log の 2 件は userland の noct の既存の warning）。host の fixture resdispatch・eu・compile・pipe PASS（resdispatch の format の検査を VERTEX_BUFFER の答えに合わせた）。cmd は増分 2 からの既存の link の失敗（fence.c の query が要る symbol）で、この増分と無関係 |
| 実機 capture（増分 6、2026-09-28 12:20〜12:38、5330 は電源の入れ直しの後 10.0.30.3、`I915_HOST=awe@10.0.30.3`） | image は worktree の `build/ws075-p006/{img,img5}`（warning 2 件は既存の noct）。**egltest6（r6b）: targets 0・blits 0・queries 0・feedback 0**（drawn-from-captured は直った）、capture の検査 pass（desktop_drawn・scenes_shown）。1 回目（r6）は i915 の start が `time_base_anomaly: 5`（kern_rtc_read_counter が使えない）で止まり、compositor は起動せず全場面 eglerror 0x3000。同じ image の再試行で動いた（host の起動直後の間欠、原因は未調査）。**p005 の 7 場面（r5）: glsl・glsl3・fbo（fbo-fbo 0）・cube・es3・formats・volumes 全て 0**、hang・fault なし。ただし capture の検査は scenes_shown が false: desktop と scene-00〜10 の画像が同じ hash（egltest の一つの場面の絵のまま）、scene-11 だけ違う。compositor は止まっていない（capture の frame は 1642 まで進む、perf の行は 5 秒ごとに 0 presents・約 83 submits・submit あたり gpu 約 34 ms）。画面の更新の遅れか停止かは未調査（BUG-085 に記録）。画面 `build/ws031-shots/ws075-p006-20260928-inc6-{egltest6,p005scenes}-sheet.png`。rasterizerDiscardEnable を直接見る検査は無い（回帰だけ） |
| QEMU | 未実施（i915 の実機の変更） |

## 中断（2026-09-28、main の wrap up）: 増分 4 の stencil（解決済み、上の増分 4）

（2026-09-28 追記: 増分 4 の commit で `wip.patch` は不要になり削除した。内容は git の履歴の 62a10ad0 と同じ。）

- stencil の実装（D32_SFLOAT_S8_UINT・S8_UINT、Y tile の separate stencil plane、3DSTATE_STENCIL_BUFFER、3DSTATE_WM_DEPTH_STENCIL の
  stencil、vkCmdSetStencil*、pass の stencilLoadOp と ClearAttachments の aspect、stencil の clear は Y tile の R8_UNORM に value/255）は
  build できる（commit 62a10ad0）が、実機で後退した: st1（`build/ws075-p006/st1`）は wlkill の直後の frame 194 で compositor が
  止まり、st2 は targets が 6 → 14（glerror 0x505、eglSwapBuffers の失敗、MRT の四角も黒）。原因は未調査。**62a10ad0 は revert し、
  差分は `plan/ws075/phase006/wip.patch`（show の形）に置いた。**
- 再開の手順: wip.patch を当てる（または 62a10ad0 を cherry-pick）→ targets の GL_OUT_OF_MEMORY の元を探す（D32S8 の image の
  作成・view・copy の拒否、image->bytes に stencil plane を足したことの影響（subresource layout、`i915_image_surface_write()` の
  単一 slice の QPitch が bytes/pitch）、`image_supported()` の D32S8）→ st1 の compositor の停止が再現するか（stencil の変更と
  無関係の可能性: wlkill の直後、BUG-077 の系統）→ 実機の egltest6。

## 残り（2026-09-28 の時点、増分 4 の後）

- stencil: 増分 4 で済み（st3）。D24_UNORM_S8_UINT は無く、libGLESv2 は D32S8 に落ちる。stencil の sampling（usampler の stencil
  texturing）と D32S8 の format feature の TRANSFER は未（copy の経路は増分 4 で通したので feature を足すのは multisample の増分で）。
- multisample の image と resolve: 増分 5 で済み（ms1、blits 0）。sampler2DMS の texelFetch（sampled の multisample、survey・glxtest の gl32）は未。
- texel buffer（samplerBuffer、survey）。
- feedback の drawn-from-captured: 原因（bufferFeatures が 0）を増分 6 で直し、実機の egltest6 で feedback 0 を確認した（2026-09-28、r6b）。p005 の 7 場面の capture の scenes_shown が false だった件（上の表）は未調査。以下は当時の再開の手順。再開: `CAPTURE=zdesktop-egltest KEILAND_APP=egltest6 BUILD=build/ws075-p006/img flock /tmp/i915-hw.lock plan/ws031/tests/vkloop-hw.sh zdesktop`（期待: targets 0・blits 0・queries 0・feedback 0）と、同じく `KEILAND_APP=egltest BUILD=build/ws075-p006/img5` で p005 の 7 場面が 0 のまま（libGLESv2 の全ての頂点が stream の copy から buffer の直接の bind に変わるので要る）。
- libGLESv2 の潜在の誤り（WS068 の側、未修正）: VERTEX_BUFFER の feature の無い形式の buffer object を CPU の bytes から stream に写す経路が、`gpu_written` の buffer を `gles_buffer_fetch()` せずに読む。i915 は増分 6 で feature を答えるので通らなくなったが、表に無い形式（例: fixed point の変換）では残る。
- targets の draw-buffer-other: st3 で ok（1282）。以前の INVALID_ENUM は stencil の対応の無い状態の残りの error だったと見られる。
- `egltest6` の guest の log: `ufs-cat.py` が zdesktop.log を sparse として読めない run がある（ssbo1）。そのときは mview.log を
  別に読む（`ufs-cat.py ... /var/log/mview.log`）。

## 完了の条件（2026-10-01 追記、範囲と「残り」から）

1. 今の tree（main）の image で、実機の passthrough の capture: `egltest6` の 4 場面 targets・blits・queries・feedback が failures 0、
   `egltest` の p005 の 7 場面（glsl・glsl3・fbo・cube・es3・formats・volumes）が failures 0、capture の検査 `desktop_drawn`・`scenes_shown` が true
   （増分 6 の r5 で `scenes_shown` が false だった件の原因を説明するか直す）。
2. texel buffer（samplerBuffer の texelFetch・textureSize）と sampler2DMS の texelFetch: host の survey（`plan/ws075/tests/shader-survey/run.sh`）の
   `gaps.txt` にこの 2 つが無い、または device の feature として断り、断ることを記録する（ws.md の受け入れ 2 の「断って記録」）。
3. 回帰: `sh plan/ws031/tests/run-vk-host-tests.sh` の全 10 個 PASS、test-hw の vkx 9/9・vke1 6/6・vke2 17/17・vkc 9/9、boot test PASS。

## 手順（2026-10-01 追記）

段の外（ws.md の「段の計画」）。main が Queue に入れたときだけ、10-10 より前に行う。手引きは [guide.md](../guide.md) の §5・§6。
`<W>` は `ws075-p006b` など、この試みの名前。

1. host の survey で今の不足を数える（2 の範囲）:
   ```
   sh plan/ws068/tests/glsl-host/run.sh
   sh plan/ws075/tests/shader-survey/run.sh build/<W>/shaders
   grep -iE 'buffer|ms|multisample' build/<W>/shaders/gaps.txt
   ```
2. 実機の capture（1 の範囲）。どちらも `vkloop-hw.sh` が lock の中で image を build するので、他の image の build と重ねない。host は今 `solaris10-man`:
   ```
   CAPTURE=zdesktop-egltest KEILAND_APP=egltest6 BUILD=build/<W>/img I915_HOST=solaris10-man flock /tmp/i915-hw.lock plan/ws031/tests/vkloop-hw.sh zdesktop > build/<W>/egltest6.log 2>&1
   cp -r /tmp/capture-last build/<W>/egltest6-capture; cp /tmp/zdesktop-guest-logs.txt build/<W>/egltest6-guest-logs.txt
   CAPTURE=zdesktop-egltest KEILAND_APP=egltest BUILD=build/<W>/img5 I915_HOST=solaris10-man flock /tmp/i915-hw.lock plan/ws031/tests/vkloop-hw.sh zdesktop > build/<W>/egltest.log 2>&1
   cp -r /tmp/capture-last build/<W>/egltest-capture; cp /tmp/zdesktop-guest-logs.txt build/<W>/egltest-guest-logs.txt
   ```
   `/tmp/capture-last` と `/tmp/zdesktop-guest-logs.txt` は次の run が上書きするので、各 run の直後に写す（lock を外した後に他の agent の run が始まり得る。
   lock の中で写したいなら `plan/ws075/tests/capture-hw.sh` の形に倣う）。読むもの: `*-capture/result.json`（`desktop_drawn`・`scenes_shown`）、
   guest の log（egltest の行の failures の数。guest の disk の log で、QEMU の serial ではない）、`*-capture/sheet.png`（ユーザーに見せる）。
3. 不足を直す（compiler: `src/drivers/gpu/i915/compiler/spirv.c`・`compile.c`、executor: `src/drivers/gpu/i915/render/image.c`・`descriptor.c`・`state.c`）。
   増分ごとに host の fixture（`sh plan/ws031/tests/run-vk-host-tests.sh "pipe cmdbuf compile"`）を先に通す。
4. 回帰（3 の範囲）: guide.md §5.3 と §6.4 の test-hw（`I915_HOST=solaris10-man`）、boot test（guide.md §5.4）。
5. 結果を「検証」の表に、passthrough と QEMU を分けて書く。

## 増分 7: uniform texel buffer（2026-10-07 夕、P1、q833）

- 範囲（Q1 の ACK）: p006 の残りのうち texel buffer（buffer view）。sampler2DMS は次の増分。render の compiler・実行器だけ、display は触らない。
- 以前は `vkCreateBufferView`（opcode 52）を実行器のどの module も持たず、stream ごと拒まれていた（libglesv2 は buffer texture と「黒い」texel buffer に buffer view を作る）。
- 実行器: `struct i915_gfx_buffer_view`（gfx.h）、`drv_i915_gfx_create_buffer_view`（memory.c、知らない buffer・texel buffer に無い format・buffer の外の offset は失敗）、destroy は generic（objects.c、session の close の解放の表にも）。descriptor の texel buffer view を binding に（descriptor.c、image と texel は互いに消す）。draw の binding table で texel の slot は SURFTYPE_BUFFER の surface state（state.c `i915_buffer_surface_write`、isl_buffer_fill_state_s の事実: 要素数 −1 を width 7・height 14・depth 11 bit に、pitch = texel −1、linear、identity の channel select）。sampler state は書かない（ld は使わない、heap は 0）。`drv_i915_gfx_texel_buffer_format`（state.c、R8・RG8・RGBA8 unorm/sint/uint・R32・RG32・RGB32・RGBA32 の float/sint/uint・RGBA16F）と、その format の `bufferFeatures` に `UNIFORM_TEXEL_BUFFER_BIT`（instance.c）。`GEN12_SURFTYPE_BUFFER 4`（intel/genxml.h、gen120.xml 141 行の値）。
- compiler: `OpImageFetch` が Dim Buffer の image を受ける（1 座標、v = 0、level 0 の ld、level・offset・arrayed は拒む）。
- 試験の直し（見つけた担当の直し）: `plan/ws031/tests/i915-vk-cmd-test.c` に `drv_i915_gfx_texel_buffer_format` の stand-in、`i915-vk-resdispatch-test.c` の bufferFeatures の期待に texel buffer の bit。

| コマンド | 結果 |
| --- | --- |
| `plan/ws031/tests/run-vk-host-tests.sh` の写し（trap の rm を外し、work を build/tmp に） | 10 個 × plain・ASan/UBSan すべて PASS（`WS031 vk host fixtures PASS`） |

未実施: texel buffer を実際に読む host の fixture（surface state の dword の検算）、vmunix の build、実機（GL の samplerBuffer の場面、5330）。VS の texel buffer は VS の sampled image と同じく実行器が PS だけなので未対応（backlog）。`textureSize(samplerBuffer)`（OpImageQuerySize の buffer）は未対応（backlog）。
