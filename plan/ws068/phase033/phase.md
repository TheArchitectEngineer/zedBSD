<!-- awesome-plan project=zedbsd record=ws068p033 -->

# ws068-p033: desktop GL 3.2（geometry shader、multisample texture、profile）

Phase ID: `ws068-p033`
Parent: [WS068](../ws.md)
Status: cleared（2026-09-27。Venus で glx-p033 PASS、回帰 PASS。i915 実機は未実施）
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

- GLSL（p032 で済み）: geometry の stage、`glsl_link_stages` の 3 stage の link。p033 で `sampler2DMS`・`isampler2DMS`・
  `usampler2DMS`（`GLSL_SAMPLER_MS`、SPIR-V は MS の 2D の sampled image）、`texelFetch(s, P, sample)` は Sample の operand の
  OpImageFetch、`textureSize(s)` は OpImageQuerySize（level 無し）。
- geometry shader（libGLESv2、libGL だけが受ける）: `GL_GEOMETRY_SHADER` の shader、attach・detach（program の 3 つ目の stage）。
  link は vertex・geometry・fragment の SPIR-V を `glsl_link_stages` で、`program_link_geometry` が geometry の block と uniform を
  program のものへ併合し、gl_Position の書き換え（window は y の反転、framebuffer object 用と 2 つの module）を geometry の
  各 OpEmitVertex の前に入れる（vertex の stage の gl_Position はその時は書き換えない）。descriptor の stage の bit に geometry、
  pipeline に 3 つの stage。spirv.c の反射は block の配列（gl_in）を飛ばす。`glGetProgramiv` の GL_GEOMETRY_VERTICES_OUT・
  INPUT_TYPE・OUTPUT_TYPE、GL_ATTACHED_SHADERS は 3 まで数える。
- adjacency の mode（GL_LINES_ADJACENCY 等 4 つ）は Vulkan の WITH_ADJACENCY の topology へそのまま。draw の mode が geometry の
  入力の primitive に合わなければ GL_INVALID_OPERATION（`draw_geometry_mode`）。
- layered の framebuffer: `glFramebufferTexture` は 2D・rectangle の level をそのまま、2D 配列の level の全ての layer・cube map の
  level の 6 面を layered の attachment（`GLES_LAYER_ALL`、2D_ARRAY の view）にする。framebuffer の layers は layered の
  attachment の最も少ない layer 数、layered と非 layered の混在は GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS。geometry の
  gl_Layer が layer を選ぶ。3D texture の slice の layered は無い。
- multisample texture: `glTexImage2DMultisample`・`glTexStorage2DMultisample`（GL_TEXTURE_2D_MULTISAMPLE、`ms_units`）。format は
  framebuffer が描ける colour か depth、samples は device の持つ数へ切り上げる（`gles_samples_for`）。CPU 側の texel は無く、image は
  作る時に零へ clear（`texture_clear_samples`）。sampler2DMS が読む unit に multisample texture が無ければ 1 texel の零の
  multisample texture（`gles_texture_black_ms`）。framebuffer への取り付け、blit の resolve は既存の道。glReadPixels・glGetTexImage は
  multisample を読まない。
- sample mask: `GL_SAMPLE_MASK`・`glSampleMaski`（word 0）を raster の状態に持ち pipeline の pSampleMask に。
  `glGetMultisamplefv(GL_SAMPLE_POSITION)` は Vulkan の標準の位置。
- profile と 3.2 の context: `glXCreateContextAttribsARB` の 3.2 は GLX_CONTEXT_PROFILE_MASK_ARB で core（既定）か compatibility。
  GL_VERSION「3.2 zedBSD (OpenGL ES 3.0 on Vulkan)」、GLSL「1.50」（`#version` の上限 150）、GL_CONTEXT_PROFILE_MASK。
  core は固定機能を断る: glBegin 等と固定機能の draw（program 0）、VAO 0 の draw は GL_INVALID_OPERATION。GL_EXTENSIONS は
  extension ごとの最初の版と compatibility だけの印の表から、版と profile の種類ごとに作る（core に GL_ARB_compatibility は無い。
  3.2 から GL_ARB_texture_multisample）。
- `glGetTexLevelParameteriv`・`fv`（大きさ、internal format、samples、fixed sample locations）を libGL の gl3.c に。

## 判断が要る点（既定を選んで進める）

- `glXCreateContextAttribsARB` の 3.2 の既定の profile は GLX_ARB_create_context_profile のとおり core。legacy の
  `glXCreateContext` は 1.4 のまま。戻せる既定。
- core の context でも GL_ARB_compatibility 以外の extension の名前は compatibility と同じ。

## 検証（QEMU の Venus。i915 実機は未実施）

image: `plan/ws068/tests/build-glsl-image.sh build/amd64`（main を merge した tree 117a0d70）。

| 確認 | 結果 |
| --- | --- |
| build（lean image） | status 0。warning 0（libGLESv2・libGL・glxtest） |
| style-check | 新しい file（`glxtest/gl32.c`・`gl32.h`）0、変えた file 0 |
| GLSL の host 試験 | PASS（p032 の geometry の pass・fail・exec と pass/ の `glsl150-ms.*`: sampler2DMS・isampler2DMS の texelFetch・textureSize、spirv-val。vk-run 21） |
| glx-p033（`plan/ws068/tests/glx-p033.sh`） | **PASS**: core の context（profile=1、GL_ARB_compatibility 無し、glBegin と VAO 0 の draw が 0x502）、compatibility の 3.2（GL_VERSION「3.2 …」・GLSL「1.50」）、geometry（point を四角へ、GL_LINES_ADJACENCY の draw、入力の primitive に合わない mode の拒否、program の問い合わせと attached shaders 3）、layered（2D 配列の 2 layer へ gl_Layer、layered と非 layered の混在の拒否）、multisample texture（4 samples、sampler2DMS の平均と blit の resolve）、sample mask（sample 0 だけ）と sample の位置、level の parameter。7 つの結果と固定機能の四角が緑。最初は multisample と sample mask が黒（試験の program の u_rect を設定していなかった。試験を直して PASS） |
| 回帰 glx-p013・glx-p031・x11-p004・x11-p005 | PASS（gears.png を目で確かめた） |
| 回帰 egl-p002・p008・p010・p019・p020・p022〜p030 | 全て PASS（egl-p024 は 1 回目に wayland の log の grep が行を見落とした。同じ image で再試験 PASS。p022・p023 は実行の bit が無いので sh で） |
| boot test（`build/ws068-p033-regress/boot/login.png`） | PASS |
| i915 実機 | 未実施 |

画面: `build/ws068-shots/p033-20260927-gl32-glx.png`、`build/ws068-shots/p033-20260927-regress-gears.png`。

### 制限・移管

- geometry shader: GL_ARB_geometry_shader4 の API（glProgramParameteri）と GLSL の `#extension` は無い。gl_in は gl_Position
  だけ（gl_PointSize・gl_ClipDistance は無い）。geometry の instancing（4.0）は p036。
- 3D texture の layered の attachment は無い（2D 配列と cube map だけ）。
- multisample texture の配列（GL_TEXTURE_2D_MULTISAMPLE_ARRAY）は無い。sample mask は word 0 だけ（samples は 32 まで）。
- glXCreateContextAttribsARB の共有（share_context）は未対応のまま。
