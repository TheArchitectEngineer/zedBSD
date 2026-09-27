<!-- awesome-plan project=zedbsd record=ws068p031 -->

# ws068-p031: desktop GL 3.1（と shader の stage の要らない 3.2 の API）

Phase ID: `ws068-p031`
Parent: [WS068](../ws.md)
Status: cleared（2026-09-27。Venus で glx-p031 PASS、回帰 PASS。i915 実機は未実施）
Phase disposition: normal
承認: 2026-09-27 ユーザーの自走の指示（graphics が最優先）、design.md §6（2026-09-27 ユーザー「OpenGL 4.6もサポートできる範囲で」）、
WS068 の計画（p014 の分割）。試験は 2026-09-27 のユーザーの方針（amd64 のみ、phase の最後に）。

## 範囲

- texture buffer: `GL_TEXTURE_BUFFER` の texture、`glTexBuffer`（R・RG・RGBA の 8/16/32 bit の float・整数、RGB32 の 3 つ）、
  GLSL の `samplerBuffer`・`isamplerBuffer`・`usamplerBuffer` と `texelFetch`・`textureSize`（Vulkan の uniform texel buffer）。
- rectangle texture: `GL_TEXTURE_RECTANGLE`（level 0 だけ、既定の wrap は CLAMP_TO_EDGE、filter は LINEAR）、GLSL の
  `sampler2DRect`・`sampler2DRectShadow`・`isampler2DRect`・`usampler2DRect` と `texture`・`textureProj`・`texelFetch`・`textureSize`
  （SPIR-V は 2D の image、座標は `textureSize` で割って正規化。Vulkan は Rect の Dim を受けない）。
- `GL_PRIMITIVE_RESTART` と `glPrimitiveRestartIndex`（任意の index。CPU の index の展開で）。
- `glGetActiveUniformName`。
- GLSL 1.40（UBO の言語は p021 で済み）、context の版による `#version` の上限（3.0: 1.30、3.1: 1.40。1.4 の context は上限なし）。
- 3.1 の context（`glXCreateContextAttribsARB` 3.1、GL_VERSION「3.1 …」・GLSL「1.40」）と desktop の GL_EXTENSIONS
  （`GL_ARB_compatibility` と実装した ARB の名前。glGetStringi・GL_NUM_EXTENSIONS も）。3.0 の context もこの一覧。
- 3.2 の shader の stage の要らない API: `glDrawElementsBaseVertex`・`glDrawRangeElementsBaseVertex`・
  `glDrawElementsInstancedBaseVertex`、`glProvokingVertex`、`GL_DEPTH_CLAMP`（device の depthClamp）、
  `GL_TEXTURE_CUBE_MAP_SEAMLESS`（Vulkan は常に seamless、受けるだけ）。

範囲外（p033 へ）: geometry shader、multisample texture、core profile、3.3 の API。

## 設計

- rectangle texture: `GL_TEXTURE_RECTANGLE` を target に持つ texture（image は 2D と同じ、`rect_units`）。texture.c の
  `TEXTURE_TAKES_RECT` を image・storage・parameter の call が受け、mipmap は受けない。既定の sampling は LINEAR・CLAMP_TO_EDGE。
  framebuffer の `glFramebufferTexture2D(GL_TEXTURE_RECTANGLE)`（level 0 だけ）、libGL の glGetTexImage も。
- GLSL の rectangle sampler は SPIR-V の 2D の sampled image（Vulkan は Rect の Dim を受けない）。`texture`・`textureProj` の座標は
  `OpImageQuerySizeLod`（level 0）の大きさで割る。`texelFetch` は Lod 0、`textureSize` は level 0 の大きさ。
- buffer texture: `GL_TEXTURE_BUFFER` の texture（`buffer_units`）に `glTexBuffer` で buffer object と format（R・RG・RGBA の
  8/16/32、RGB32。device が uniform texel buffer で読めない format は零を読む）。buffer object の device の copy に
  `UNIFORM_TEXEL_BUFFER` の usage を足し、draw の時に `gles_texture_buffer_view` が buffer の device の copy と範囲に合う
  `VkBufferView` を作る（copy が変われば作り直し、古い view は frame の後に捨てる）。buffer の無い unit・種類の違う・空の buffer は
  零の 16 byte の buffer の view（float・int・uint）。buffer の削除は texture から外す。`glBindBuffer(GL_TEXTURE_BUFFER)` の target。
- GLSL の buffer sampler は SPIR-V の Buffer の Dim の OpTypeImage（sampled image でない、`SampledBuffer` の capability）、
  `texelFetch` は Lod 無しの OpImageFetch、`textureSize` は OpImageQuerySize。libGLESv2 の反射（spirv.c）は UniformConstant の
  Buffer の image を sampler とし、program の set layout は uniform texel buffer、draw の descriptor は texel buffer view。
- primitive restart: `GL_PRIMITIVE_RESTART` と `glPrimitiveRestartIndex`。`draw_restart_index` が index を決める
  （GL_PRIMITIVE_RESTART_FIXED_INDEX が優先）。index の CPU の展開はそのまま。
- base vertex: `state->base_vertex` を立てて draw_primitives を呼び、`draw_indices` が restart でない index に足す。
- provoking vertex: fragment shader の SPIR-V に Flat の decoration があれば（`program->flat_inputs`）、GL の最後の頂点を
  provoking にするため primitive を回す（固定機能の flat と同じ `gles_expand` の道）。`glProvokingVertex(GL_FIRST_VERTEX_CONVENTION)`
  なら回さない（Vulkan と同じ）。これは OpenGL ES 3 の flat の varying の直し（ES も最後の頂点）でもある。output を capture する
  program は回さない（transform feedback は GL の頂点の順で記録する）。
- depth clamp: raster の `depth_clamp` → pipeline の depthClampEnable（device の depthClamp の時）。seamless は flag だけ。
- context の版: `glXCreateContextAttribsARB` は 3.1 の要求に 3.1、それ以外（1.0〜3.0）に 3.0。GL_VERSION「3.1 …」・GLSL「1.40」。
  fixed の hook に GLSL の上限（`glsl_version`、3.0: 130、3.1: 140、1.4: 無し）と extension の一覧（`extension_count`・
  `extension`）を足した。3.x の context の GL_EXTENSIONS は ARB の 23 名（GL_ARB_compatibility は 3.1 だけ）、1.4 は ES のまま。
- desktop の enum は gles.h に `#ifndef` で、`GL/gl.h` に宣言。

## 判断が要る点（既定を選んで進める）

- `glXCreateContextAttribsARB` は 3.1 を求められた時だけ 3.1 の context（GL_ARB_compatibility 付き）、1.0〜3.0 には 3.0 を返す
  （3.0 は以前の版と互換）。戻せる既定。

## 検証（QEMU の Venus。i915 実機は未実施）

image: `plan/ws068/tests/build-glsl-image.sh build/amd64`（main の b07a99df を merge した tree: desktop の WSI の変更を含む）。

| 確認 | 結果 |
| --- | --- |
| build（lean image） | status 0。warning 0 |
| style-check | 新しい file（`glxtest/gl31.c`・`gl31.h`）0、変えた file 0（前も 0） |
| GLSL の host 試験 | PASS（pass/ に `glsl140.vert`・`.frag`: samplerBuffer・isamplerBuffer・sampler2DRect・usampler2DRect・sampler2DRectShadow の texture・textureProj・texelFetch・textureSize、spirv-val と反射） |
| glx-p031（`plan/ws068/tests/glx-p031.sh`） | **PASS**: GL_VERSION「3.1 zedBSD (OpenGL ES 3.0 on Vulkan)」・GLSL「1.40」、extension 23（最初が GL_ARB_compatibility）、GLSL 1.50 の拒否、buffer texture（RGBA32F と R32I、buffer の変更が次の draw で見える）、rectangle texture（lookup・fetch・size・glGetTexImage・framebuffer の取り付け）、primitive restart（index 8）、base vertex の 3 つ、provoking vertex（既定の last と first）、depth clamp、seamless と glGetActiveUniformName。8 つの結果と固定機能の四角が緑 |
| 回帰 glx-p013・x11-p004・x11-p005 | PASS（gears.png を目で確かめた） |
| 回帰 egl-p002・p008・p010・p019・p020・p022〜p030 | 全て PASS。最初は egl-p030 が失敗（flat の回転が transform feedback の記録の順を変えた）、capture する program は回さないようにして PASS（p020・p024・glx-p031 も再試験 PASS） |
| boot test（`build/ws068-p031-regress/boot/login.png`） | PASS |
| i915 実機 | 未実施 |

画面: `build/ws068-shots/p031-20260927-gl31-glx.png`、`build/ws068-shots/p031-20260927-regress-gears.png`（main の tree）。

### 制限・移管

- rectangle texture の level 0 以外への glTexImage2D、REPEAT の wrap は断らない（使われない）。GLSL 1.10〜1.30 の
  `#extension GL_ARB_texture_rectangle` の sampler2DRect は無い（1.40 から）。
- buffer texture: `glTexBufferRange`（4.3）は無い。device が uniform texel buffer で読めない format（RGB32 は optional）は零を読む。
- GLSL の `#extension` で 3.0 の context が 1.40 の機能（UBO 等）を使う道は無い（GL_EXTENSIONS に ARB の名前は出す）。
- flat の varying を持ち output を capture する program の描画は Vulkan の provoking vertex（最初）のまま。
- i915 の実行器が Buffer の Dim・ImageQuery を受けるかは未確認（F-023 の範囲）。
