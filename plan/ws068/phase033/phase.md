<!-- awesome-plan project=zedbsd record=ws068p033 -->

# ws068-p033: desktop GL 3.2（geometry shader、multisample texture、profile）

Phase ID: `ws068-p033`
Parent: [WS068](../ws.md)
Status: in-progress（2026-09-27 着手）
Phase disposition: normal
承認: 2026-09-27 ユーザーの自走の指示（graphics が最優先）、design.md §6、WS068 の計画（p014 の分割）。試験は 2026-09-27 の
ユーザーの方針（amd64 のみ、phase の最後に）。

## 範囲

- geometry shader（libGLESv2 の source、libGL だけが受ける）: `glCreateShader(GL_GEOMETRY_SHADER)`、attach・detach、
  `glsl_link_stages` の 3 stage の link、geometry の SPIR-V の反射と uniform の併合、gl_Position の書き換えを geometry の stage へ、
  pipeline の geometry の stage、descriptor の stage の bit、`glGetProgramiv` の GL_GEOMETRY_VERTICES_OUT・INPUT_TYPE・OUTPUT_TYPE、
  adjacency の draw の mode（GL_LINES_ADJACENCY 等）、draw の mode と geometry の入力の primitive の不一致は GL_INVALID_OPERATION。
- layered の framebuffer: `glFramebufferTexture`（2D 配列・3D・cube map の level の全ての layer）と geometry の gl_Layer。
- multisample texture: `glTexImage2DMultisample`・`glTexStorage2DMultisample`（GL_TEXTURE_2D_MULTISAMPLE）、framebuffer の取り付け、
  GLSL の `sampler2DMS`・`isampler2DMS`・`usampler2DMS` と `texelFetch(s, P, sample)`・`textureSize`、`GL_SAMPLE_MASK`・
  `glSampleMaski`、`glGetMultisamplefv(GL_SAMPLE_POSITION)`。
- profile: 3.2 の core は固定機能（glBegin 等の固定機能の draw、行列等）を GL_INVALID_OPERATION で断る。compatibility は今のまま。
- 3.2 の context（GL_VERSION「3.2 …」・GLSL「1.50」、GL_CONTEXT_PROFILE_MASK）、GL_EXTENSIONS に geometry・multisample の ARB。

範囲外（p037）: 3.3 の API と context。

## 設計

（実装の中で書き足す。）

## 判断が要る点（既定を選んで進める）

（なし）

## 検証

未実施。phase の最後に amd64 Venus で glx-p033（glxtest の `--gl32`）、glx-p013・p031、x11-p004・p005、egl の回帰、
GLSL の host 試験、boot test。
