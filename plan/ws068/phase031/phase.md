<!-- awesome-plan project=zedbsd record=ws068p031 -->

# ws068-p031: desktop GL 3.1（と shader の stage の要らない 3.2 の API）

Phase ID: `ws068-p031`
Parent: [WS068](../ws.md)
Status: in-progress（2026-09-27 着手）
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

（実装の中で書き足す。）

## 判断が要る点（既定を選んで進める）

（なし）

## 検証

未実施。phase の最後に amd64 Venus で glx-p031（glxtest の `--gl31`）、glx-p013、x11-p004・p005、egl の回帰、GLSL の host 試験
（texture buffer・rectangle の shader を足す）、boot test。
