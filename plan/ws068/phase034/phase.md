<!-- awesome-plan project=zedbsd record=ws068p034 -->

# ws068-p034: tessellation（GL_ARB_tessellation_shader、GLSL の control・evaluation の stage）

Phase ID: `ws068-p034`
Parent: [WS068](../ws.md)
Status: in-progress（実装済み・試験未実施。2026-10-29 q1682-i01 P2。cleared にしない）
Phase disposition: normal
依存: p037
再開（2026-10-29）: ユーザー「作業の手が空いてしまったら、テストなしで実装だけ進める戦略で、残っているWSのphaseをまんべんなく進めましょう。」
（Q1 の依頼 q1682-i01 の引用）。試験なし（QEMU・実機なし）、build と host の試験まで。旧: 保留（2026-09-27 ユーザー「OpenGL 3.2が問題なければ、
それ以降のOpenGLはいったん保留して、i915の高度化に進んでください。」）。

## 範囲

- GLSL compiler: tessellation control（`GLSL_STAGE_TESS_CONTROL`）と evaluation（`GLSL_STAGE_TESS_EVALUATION`）の stage。
  desktop GLSL 4.00〜4.60 の `#version`（4.x の version mask は 3.30 の物に `GLSL_IN_400` と `GLSL_IN_TESS`）と、1.50・3.30 の
  `#extension GL_ARB_tessellation_shader`（macro も）。`patch` の修飾子、layout の `vertices = N`・`triangles`・`quads`・`isolines`・
  `equal_spacing`・`fractional_even_spacing`・`fractional_odd_spacing`・`cw`・`ccw`・`point_mode`。per-vertex の入出力は頂点の配列
  （大きさ無しの入力は gl_MaxPatchVertices = 32、control の出力は vertices）、per-vertex の in/out block の配列（patch の block は断る）。
  built-in: `gl_in[]`・`gl_out[]`（gl_PerVertex の gl_Position）、`gl_PatchVerticesIn`・`gl_PrimitiveID`・`gl_InvocationID`・
  `gl_TessLevelOuter[4]`・`gl_TessLevelInner[2]`・`gl_TessCoord`、evaluation の `gl_Position`・`gl_PointSize`、定数 `gl_MaxPatchVertices`・
  `gl_MaxTessGenLevel`、control の `barrier()`。
- SPIR-V: Tessellation の capability と execution model、OutputVertices、Triangles/Quads/Isolines、Spacing*、VertexOrder*、PointMode、
  BuiltIn の TessLevelOuter/Inner（Patch）・TessCoord・PatchVertices・InvocationId・PrimitiveId（tessellation では Geometry の capability を出さない）、
  patch の変数の Patch、control の barrier は `OpControlBarrier Workgroup Invocation None`。
  Vulkan 1.0 の domain の原点は左上（VK_KHR_maintenance2 が無い）なので、GL の ccw（既定）は Vulkan の VertexOrderCw に（Mesa の Zink と同じ）。
- link: `glsl_link_pipeline(vertex, tess_control, tess_evaluation, geometry, fragment, …)`（`glsl_link_stages` はその tessellation 無し）。
  隣り合う stage ごとに varying を結ぶ（per-vertex は配列の要素の型で、patch は patch 同士で照合、patch の location は per-vertex の後）。
  control だけ・evaluation だけは link の失敗（control 無しの既定の levels は未対応）。transform feedback の capture は tessellation があれば断る。
  `glsl_tess_layout` が GL の名前の layout を返す。
- libGLESv2/libGL: `GL_TESS_CONTROL_SHADER`・`GL_TESS_EVALUATION_SHADER`（libGL、device の `tessellationShader` の時だけ）の shader・attach・
  detach・`GL_ATTACHED_SHADERS`・glGetAttachedShaders、link（tessellation の stage の uniform と block の merge、control の module、evaluation の
  window 用と FBO 用の module。geometry が無ければ evaluation の gl_Position を書き換え、vertex は書き換えない）、`glGetProgramiv` の
  `GL_TESS_CONTROL_OUTPUT_VERTICES`・`GL_TESS_GEN_MODE`・`_SPACING`・`_VERTEX_ORDER`・`_POINT_MODE`。draw: `GL_PATCHES` → `PATCH_LIST`、
  pipeline の tessellation の stage と `patchControlPoints`（pipeline の key に入れる）、tessellation の program は GL_PATCHES だけ・無い program は
  GL_PATCHES を断る（`GL_INVALID_OPERATION`）、flat の provoking の並べ替えをしない。`glPatchParameteri(GL_PATCH_VERTICES)`（1〜min(32, device)）、
  `glPatchParameterfv`（既定の levels を glGet のために保つ）、glGet の `GL_PATCH_VERTICES`・`GL_MAX_PATCH_VERTICES`・`GL_MAX_TESS_*`・
  `GL_PATCH_DEFAULT_*_LEVEL`。
- libGL の extension: `GL_ARB_tessellation_shader`（3.2 以上の context、device が tessellationShader を持つ時）。extension の表に device の
  feature の欄を足し、結んだ文字列の cache を (version, profile, features) の組の表にした（p036 の 4.x にも使う）。`GL/gl.h` に 4.0 の
  tessellation の定数と glPatchParameter*。
- 試験の program: glxtest `--gl33` の 9 つ目の outcome「tessellation」（extension がある時: 4 頂点の patch を quad で分けて赤い FBO を緑に、
  layout の glGet、三角形の draw の拒否、GL_PATCH_VERTICES。無い時は skipped で緑）。`glx-p037.sh` の期待のマスを 1 つ増やした。
- glsl-host: `pass/tessellation400.*`（4.00、per-vertex と patch の varying、barrier）・`pass/tessellation-levels330ext.*`（3.30 + extension、
  per-vertex の block、uniform の levels、cw・fractional_odd）、`fail/tess-*` 7 件、exec の `60-tessellation`・`61-tessellation-levels`
  （lavapipe で描いて色を照合）。`glsl-test` と `vk-run` が `.tesc`・`.tese` を読む。

範囲外: control 無しの evaluation（既定の levels）、patch の interface block、`gl_ClipDistance`・`gl_PointSize` の gl_in/gl_out、
4.0 の context の版（p036）、i915 等の native の compiler の tessellation（libvulkan 側）。

## 判断（推しで進めた）

1. control 無しの evaluation shader は link の失敗（GL 4.0 は既定の levels で許す）。control を合成するには name の照合を SPIR-V の側で作る
   必要があり、この Phase では見送った。`glPatchParameterfv` の値は保つだけ。
2. `#version 400`〜`460` を受ける（4.x だけの機能は無いものは誤りのまま）。libGL の 3.3 の context の GLSL の上限は 330 のままなので、
   3.3 の context で使えるのは `#version 330` + `#extension GL_ARB_tessellation_shader`。
3. domain の原点の違いは vertex order を入れ替えて吸収（VK_KHR_maintenance2 は libvulkan に無い）。

## 検証

- build（2026-10-29）: amd64・arm64・riscv64 の `libGL.so`・`libGLESv2.so`・`libEGL.so` → exit 0、warning 0（sysroot を作り直し）。
  amd64 の `bin/glxtest` → exit 0、warning 0。
- host: `sh plan/ws068/tests/glsl-host/run.sh build/glsl-host-p034` → `glsl-host: PASS`（tessellation の pass 8 本の compile、fail 7 件、
  2 組の link と spirv-val（tesc・tese・書き換えた tese）、vk-run 26 試験（`60-tessellation`・`61-tessellation-levels` が lavapipe で期待の色））。
- 未実施: Venus の glxtest `--gl33`（tessellation の outcome）と回帰、i915（i915 の libvulkan は tessellationShader を出さない: extension は名乗らない）。
  vertex order の入れ替えは culling 無しの試験だけで、向きは確かめていない。
