<!-- awesome-plan project=zedbsd record=ws075p005 -->

# ws075-p005: texture の種類（compiler と実行器）

Phase ID: `ws075-p005`
Parent: [WS075](../ws.md)
Status: in-progress（2026-09-28〜）
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

## 検証

| 確認 | 結果 |
| --- | --- |
| host の vk の fixture spirv・lower・eu・compile・pipe・resdispatch | PASS（res は sampler の期待の語を cube override に合わせた） |
| host の fixture cmd・res・sync・cmdbuf | 既存の失敗（routing・sync・SetLineWidth の log の期待が古い。p005 の変更の前から。p010 へ） |
| 実機 vke1（TEXKINDS を含む） | 実行中 |

## Follow-up

- depth の image（Y tile）の sampling、descriptor 配列、VS の sampled image、mirrored blit、egltest・glxtest の場面。
- 3D の image の blit は最初の depth だけ。layered rendering（view の複数の layer への描画）は p007。
