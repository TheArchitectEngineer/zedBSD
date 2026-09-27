<!-- awesome-plan project=zedbsd record=ws075p005 -->

# ws075-p005: texture の種類（compiler と実行器）

Phase ID: `ws075-p005`
Parent: [WS075](../ws.md)
Status: cleared（2026-09-28。増分 1〜5。実機の egltest の 7 場面が全て failures 0、vke1 6/6・vke2 17/17・vkx 9/9・vkc 9/9、capture の zdesktop・files PASS）
Phase disposition: normal
承認: 2026-09-27 ユーザー「…i915の高度化に進んでください。」、WS075 の計画（main の登録）。

## 範囲

1. compiler: texelFetch（OpImage・OpImageFetch）、textureSize・textureQueryLevels（OpImageQuerySize[Lod]・QueryLevels）、
   shadow（OpImageSampleDref*）、textureProj（OpImageSampleProj*）、textureGrad（Grad）、fine の y 微分（p004 の残り）、
   整数の sampler、1D・3D・cube・配列の sampler。
2. 実行器: 1D・3D・配列・cube の image と view、mip level・layer への描画（ws031-p030）、sampler の compare・border colour・
   unnormalized・anisotropy・w の address mode（ws031-p034）、形式（sRGB・RGBA8 の整数・R32）。
3. 後の増分: depth の image の sampling（Y tile の surface）、descriptor 配列・VS の sampled image（ws031-p035）、
   mirrored blit、egltest・glxtest の capture の場面（p004 からの follow-up）。

範囲外: MRT・query・texel/storage buffer・multisample（p006）、geometry・layered（p007）。

## 設計

### 増分 1: compiler の TEXTURE 命令（2026-09-28）

- IR: `I915_IR_TEXTURE`（`compiler/ir.h`）。sampler message の parameter を連続した value の列（src[0] から src[1] 個）で持ち、
  `component` に message type（Mesa brw_eu_defines.h の GFX5_SAMPLER_MESSAGE_* の番号）と header の texel offset。
  parameter の順は Mesa の `lower_sampler_logical_send()`（Gen12.0）: ref、bias/lod、座標（sample_d は各成分の後に 2 つの勾配）、
  ld は u v lod r。`I915_IR_DDY_FINE`。
- `compiler/spirv.c`: `i915_spirv_lower_texture()`（Dref・Proj・Grad、1D・3D・cube・配列、整数の結果）、`lower_fetch()`（offset は
  座標に足す、Mesa の lower_txf_offset）、`lower_query()`（resinfo）、`lower_image()`（OpImage）。今の 2D の float の sample は
  `lower_sample()` のまま（SAMPLE・SAMPLE_BIAS・SAMPLE_LOD）。1D の image は実行器で高さ 1 の 2D の surface なので、座標に v = 0 を
  足し、layer は r へ（Mesa は SURFTYPE_1D、ここは 2D）。実装する message: sample、sample_b、sample_l、sample_c、sample_d、
  sample_b_c、sample_l_c、sample_d_c（HSW の番号 20）、ld、resinfo。
- `compiler/compile.c`: TEXTURE の source は run（`i915_compile_source()`）。payload へ UD で bit のまま複写。fine の y 微分は Mesa の
  Gen11+ の `generate_ddy()` と同じ quad ごとの SIMD4 NoMask の add 2 つ（`drv_i915_eu_alu2_four()`、EU_EXEC_SIZE_4）。
  fill の上限を message の parameter の数へ。
- 確認: host で Mesa の brw_disasm が受け、組み直しても同じ（texture の種類の shader、3168 bytes）。

### 増分 2: 実行器の image の種類（2026-09-28）

- `render/image.c`: 1D・2D・3D、array layer、cube compatible を受ける。slice（layer か 3D の depth）ごとに mip layout 全体を
  `slice_rows`（layout の高さを縦の alignment 4 に丸めたもの、surface state の QPitch）ずつ下へ（isl の GFX4_2D、Gen9+ の 3D も
  同じ）。`drv_i915_gfx_image_slice()`・`drv_i915_gfx_image_slices()`。view は type・base layer・layer count を持つ。
  sampler は w の address mode・compare・border colour・unnormalized・anisotropy を持つ。形式: sRGB の RGBA8・BGRA8、RGBA8 の
  UINT・SINT、R32 の float・整数。
- `render/state.c`: surface state は view の種類で 2D（配列は Surface Array）・3D・CUBE（6 面、Depth は cube の数 − 1）、Minimum
  Array Element、QPitch（isl_surface_state.c）。render target は view の level（MIP Count / LOD）と layer（Minimum Array
  Element、3D は slice）。sampler: shadow function（anv の vk_to_intel_shadow_compare_op）、cube override、anisotropy（EWA）、
  non-normalized、TCZ、border colour（dynamic state の 0x400 + 64 n の SAMPLER_BORDER_COLOR_STATE）。
- `render/command.c`・`draw.c`: copy・clear・blit・attachment は level と slice（layer か 3D の z）を回る。mip level 0 以外への
  draw の拒否を外した（draw の rectangle は level の大きさ）。
- 試験: vke1 に TEXKINDS（`tests/render/features.c`、`feature-shaders/texkinds.frag`）。2D 配列・3D・cube・1D・整数の
  texelFetch・depth compare・textureGrad（fine の y 微分）・textureSize・textureQueryLevels・textureProj・level の texelFetch。
  期待の画素は C の規則で作る（kernel の大きさの余裕のため）。LEVELDRAW: 2 level・3 layer の texture の level 1・layer 2 の
  view の framebuffer へ描き、その level・layer だけが書かれ、level 0 の layer 2 と level 1 の layer 1 は元の値のまま。

### 増分 3: depth の sampling、mirrored blit、egltest の場面（2026-09-28）

- depth（D32、Y tile）の image の sampling: surface state を R32_FLOAT・TILEMODE_YMAJOR に（isl は D32 の surface を R32_FLOAT で
  sample する）。実機は未検証（D32 を sample する試験が無い）。
- mirrored blit（ws031-p034）: blit の offset の組が逆順なら、その向きに鏡映（`drv_i915_gfx_rect()` の `linear` を flag に:
  `I915_GFX_RECT_LINEAR`・`_MIRROR_X`・`_MIRROR_Y`、source の角を逆に）。空の矩形は何もしない。実機は未検証。
- egltest の場面（p004 の follow-up）: `ZDESKTOP_APP=egltest`（`plan/ws031/tests/zdesktop/run-egltest.sh`）が egltest の
  glsl・glsl3・fbo・cube・es3・formats・volumes を zdesktop の窓で順に走らせ、各 scene の `EGLTEST CHECK run=<scene>` を
  viewer の log へ。capture の場面 `zdesktop-egltest`（`i915-capture.py`）は desktop と 5 秒ごとの 12 枚。
- 後へ: descriptor 配列と VS の sampled image（ws031-p035）は survey の 122 module に使うものが無い（VS の sampler は
  pipeline の準備で拒むまま、`pipeline-prepare.c`）。depth の image の copy（Y tile との変換）。

### 増分 4: libGLESv2 の要る実行器の不足（2026-09-28）

- binding の番号を 0〜63 に（`I915_GFX_MAX_BINDINGS` 8 → 64。libGLESv2 は uniform block を 32 から、capture を 48 に置く）。
  layout の binding は 32 個まで（`I915_GFX_MAX_LAYOUT_BINDINGS`）、dynamic uniform buffer は set ごとに 8 個まで
  （bind の op は binding と offset の組で持つ）。egl-d2 の es3 の `draw refused: set 0 binding 32 has no uniform buffer` の対処。
- 2D の depth の image の複数の layer（shadow の配列の texture）: layer ごとに Y tile の行（32 行）に揃えて置き、QPitch は
  その行数。3DSTATE_DEPTH_BUFFER は view の layer（Minimum Array Element）と Depth と QPitch を持つ。egl-d2 の
  `vkCreateImage refused: type 1 format 126 2x1x1 levels 1 layers 2` の対処。
- 実機: hw-egltest-3（binding 64 の直後）は compositor が最初の draw の前に止まった（draw state が実行の stack で 1368 byte に
  膨らんだのが疑い → dynamic offset を組で持って 616 byte に、ca4cab33）。hw-egltest-4: compositor は正常、es3 は failures
  9 → 4・glerror 0（UBO の binding 32 が通った）、formats は 14（GL_OUT_OF_MEMORY）、cube は setup の 0x506 のまま、volumes の
  行は出ず（場面の終わりが capture の終わりより後）。hang（BUG-077）は出ず。
- 未着手（次の再開点）: depth の image の copy（buffer ↔ Y tile。案: `i915_gfx_surface` の format が D16・D32 なら Y tile の
  R16_UNORM・R32_FLOAT の surface として rect で読み書き、texel の byte 数を format から）、egltest cube の
  COPY_BUFFER_TO_IMAGE の EINVAL、fbo の D16 の縞、GL_OUT_OF_MEMORY の元。

### 増分 5: depth の copy、形式、blend の feature、swizzle、instance rate（2026-09-28）

- depth の image の copy（buffer ↔ image）: `drv_i915_gfx_surface_write()` が D32・D16 の surface を Y tile の R32_FLOAT・R16_UNORM
  として書く（pitch は 128 byte の倍数、address は 4 KiB 揃え）。buffer の側は線形の R32_FLOAT・R16_UNORM。copy の texel の byte 数は
  形式から（今までは 4 固定）。D16・D32 に TRANSFER_SRC・DST の feature。image と image の copy も同じ surface で動く。
- 形式: R8_UNORM・R8G8_UNORM・R16G16B16A16_SFLOAT・B10G11R11_UFLOAT_PACK32（filter 可）、R32G32B32A32_SFLOAT（filter 不可、
  GLES と同じ）。`drv_i915_gfx_format_bytes()`（image.c、公開）、1・2 byte の形式の行は 4 byte に揃える（`i915_gfx_row_pitch()`）。
  level・layer の起点と subresource layout の横の offset も texel の byte 数で。egltest formats の GL_OUT_OF_MEMORY（14 件）の元は
  この形式の不足（vkCreateImage の拒否）。
- blend: 色の形式に `VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT` が無く、libGLESv2（framebuffer.c）は FBO の attachment を
  blend しない（opaque_mask）にしていた。egltest fbo の blend の不一致の元。filter 可の色の形式と RGBA32F に付けた。
- view の component swizzle を surface state の shader channel select に（`i915_gfx_view.channel_select`、render target は identity）。
  egltest formats の rg8-swizzle。
- instance rate の vertex binding: pipeline が binding の inputRate を持ち、3DSTATE_VF_INSTANCING で instancing enable・step rate 1
  （anv と同じ）。egltest es3 の D0〜D3（divisor 1・2。divisor 2 は libGLESv2 が stream に展開する）。
- depth の clear: D16 の word（1.0 は 0xffffffff）は float として NaN で、R32_FLOAT の view の fill では値が保たれない。egltest fbo の
  D16 の縞と depth の不一致の元。D16 の clear は Y tile の R16_UNORM の target へ depth を色として描く。D32 は従来どおり slice の
  R32_FLOAT の view を埋める（複数 layer の image で slice の行だけ、今までは image 全体の行で layer 0 以外は image の後ろへはみ出した）。
  一部の矩形の clear（vkCmdClearAttachments）は D32 も Y tile の surface へ描く（線形の view は tile の配置と合わない）。
- libGLESv2（texture.c）: まだ指定されていない level（cube の後の面）は copy の region にしない（extent 0 の region は Vulkan で無効）。
  egltest cube の COPY_BUFFER_TO_IMAGE の EINVAL の元。
- 実機の試験の kernel が AMD64_KERNEL_MAX_BYTES（16 MiB）を 28 KiB 超えた（.bss が 14 MB）。vkx の場面の state（約 240 KiB）を
  kernel の image の static から heap（kern_calloc）へ（`tests/render/executor.c`）。

## 検証

| 確認 | 結果 |
| --- | --- |
| host の vk の fixture spirv・lower・eu・compile・pipe・resdispatch | PASS（res は sampler の語を cube override に、resdispatch は D16・depth の sampling・3D の報告に合わせた） |
| host の fixture cmd・res・sync・cmdbuf | 既存の失敗（routing・sync・記録の log の期待が他の変更で古い。p005 の前から。cmdbuf は SetLineWidth を SetDepthBounds に替えたが、先にまだ古い期待。p010 へ） |
| gentool（Mesa brw_disasm/brw_asm）: feature の shader を足した 44+ 個 | PASS（texkinds.frag 387 命令、texops.frag 140 命令） |
| shader survey（`plan/ws075/tests/shader-survey/run.sh`、規則を p005 に合わせた） | 122 module 中 gap は 29 → **13**、残りは MRT・geometry・buffer texture・multisample（p006・p007）。compiler の拒否と survey の gap は一致 |
| 実機 vke1（`build/ws075-p005/hw-vke1-2`） | **PASS 6/6**（BLEND・UBO・TEX3・TEXOPS・**TEXKINDS**・**LEVELDRAW**） |
| 実機 vke2・vkx・vkc（`build/ws075-p005/r1-*`） | **PASS 17/17・9/9・9/9** |
| 実機 capture zdesktop（`r1-zdesktop`） | FAIL: BUG-077（mview の接続の直後に render engine の停止、device lost） |
| 実機 capture zdesktop-egltest（`hw-egltest-1`・`-2`） | glsl・glsl3 は CHECK failures=0（実機の i915 で GLES 2・3 の場面が初めて通った）。fbo: run 1 は FBO が incomplete（D16 が無かった）→ D16 を足した run 2 は complete、ただし depth と blend の 3 画素が違う。cube: `CUBE setup glerror=0x506`。es3・formats・volumes は BUG-077 の停止で未到達 |
| host の vk の fixture spirv・lower・resdispatch・eu・compile・pipe（増分 5 の後） | PASS（resdispatch は depth の TRANSFER・blend・RGBA16F・RGBA32F の feature に合わせた） |
| 実機 capture zdesktop-egltest（`build/ws075-p005/fix-egltest1`〜`3`） | run 1（増分 5 の途中）: cube 0・formats 1・volumes 0・es3 4・fbo 6。run 2: es3・formats・volumes・cube 0、fbo 4（D16 の depth）。**run 3: glsl・glsl3・fbo・cube・es3・formats・volumes の全てが failures 0・glerror 0**。画面 `build/ws031-shots/ws075-p005-20260928-{fix-egltest1,egltest2,egltest3}-sheet.png`。capture の検査 `scenes_shown` は FAIL（desktop の shot に既に egltest の窓があり、差分が出ない: 場面の判定は log の CHECK 行で行う。harness の follow-up） |
| 実機 vke1・vke2・vkx・vkc（`build/ws075-p005/fix-*`、増分 5 の後） | **PASS 6/6・17/17・9/9・9/9** |
| 実機 capture zdesktop（`build/ws075-bug077/fix-zdesktop1`）・files（`fix-files1`〜`3`） | PASS（6/6、9/9）。BUG-077 の停止なし |
| QEMU | 未実施（i915 の実機の変更） |

## Follow-up

- 2026-09-28 の clear の時点: 下の egltest fbo・cube の follow-up は増分 5 で解決。残り: descriptor 配列と VS の sampled image
  （ws031-p035、survey の module に使うものが無い）、mipmap の depth の image、D16 の mip の halign 8、capture の `scenes_shown` の
  判定（harness）。

- egltest fbo: D16 の FBO で depth の試験が全部落ちる（depth-front・behind が背景）、blend が効かない。cube: FBO の setup で
  GL_INVALID_FRAMEBUFFER_OPERATION。BUG-077 の後に調べる。

- depth の image（Y tile）の sampling、descriptor 配列、VS の sampled image、mirrored blit、egltest・glxtest の場面。
- 3D の image の blit は最初の depth だけ。layered rendering（view の複数の layer への描画）は p007。
