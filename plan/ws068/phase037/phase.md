<!-- awesome-plan project=zedbsd record=ws068p037 -->

# ws068-p037: desktop GL 3.3（timer query、dual-source blend、3.3 の context）

Phase ID: `ws068-p037`
Parent: [WS068](../ws.md)
Status: in-progress（実装済み・試験未実施。2026-10-29 q1682-i01 P2。cleared にしない）
Phase disposition: normal
保留の理由（旧）: 2026-09-27 ユーザー「OpenGL 3.2が問題なければ、それ以降のOpenGLはいったん保留して、i915の高度化に進んでください。」
（p033 の GL 3.2 は Venus で PASS）。
再開（2026-10-29）: ユーザー「作業の手が空いてしまったら、テストなしで実装だけ進める戦略で、残っているWSのphaseをまんべんなく進めましょう。」
（Q1 の依頼 q1682-i01 の引用）。WS068 は 2026-10-27 ユーザーの回答「継続」（空き時間だけ）。試験なし（QEMU・実機なし）で実装と
build・host の試験まで。branch は試験の後に Q1 が merge する。

## 範囲

- timer query（GL_ARB_timer_query）: `GL_TIME_ELAPSED` の query、`glQueryCounter(GL_TIMESTAMP)`、`glGetQueryObjecti64v`・
  `glGetQueryObjectui64v`、`glGetInteger64v(GL_TIMESTAMP)`、`glGetQueryiv(GL_QUERY_COUNTER_BITS)`。
- dual-source blend（GL_ARB_blend_func_extended）: GLSL の `layout(index = N)`、`glBindFragDataLocationIndexed`・
  `glGetFragDataIndex`、`GL_SRC1_COLOR`・`GL_SRC1_ALPHA`・`GL_ONE_MINUS_SRC1_*`、`GL_MAX_DUAL_SOURCE_DRAW_BUFFERS`。
- `glVertexAttribP{1,2,3,4}ui{,v}`（pointer の 2_10_10_10 の型は ES 3.0 で済み）。
- RGB10_A2UI の texture と描画（format は ES 3.0 で済み、試験で確かめる）。
- 3.3 の context（`glXCreateContextAttribsARB` 3.3、core と compatibility）: GL_VERSION「3.3 …」・GLSL「3.30」（`#version 330`、
  floatBitsToInt 等、`layout(location)` は compiler で済み）。

範囲外: 4.x（p034〜p036）。compatibility だけの `glVertexP*`・`glColorP*` 等の packed の固定機能の call。

## 設計（2026-09-27 の下書き。2026-10-29 に下の「実装」の通りに実装した）

- libegl: `zegl_display` に queue family の `timestampValidBits` と limits の `timestampPeriod`、features に `dualSrcBlend`。
  （libegl の device 作成の数行。WSI・present には触れない。）
- query.c: timestamp の query pool を occlusion の pool と別に持つ（slot の取得・reset は今の `query_slot_take` を pool ごとに）。
  GL_TIME_ELAPSED は begin・end で frame の command buffer に `vkCmdWriteTimestamp`（render pass の中でも可）、結果は
  (end − begin) を valid bits で mask して period を掛けた ns。`glQueryCounter` は 1 つ。`glGetInteger64v(GL_TIMESTAMP)` は
  今の frame を `query_finish` で submit・待ちしてから upload の queue で timestamp を書いて読む（同じ queue なので同じ時刻の系。
  QueryCounter の前後の順が保たれる）。`query_result` を 64 bit にし、uiv は切り詰める。valid bits が 0 の device は結果 0。
- GLSL: `layout(index = N)`（fragment の output だけ、0 か 1、desktop 3.30〜）を parse・check し、desktop の fragment の output に
  常に `Index` の decoration（既定 0）を出す。link の location の衝突の検査は (location, index) で。spirv.c が Index の word を反射し、
  program.c の `glBindFragDataLocationIndexed` がその word を書き換える（今の location の書き換えと同じ道）。
- draw.c: `draw_blend_factor` に SRC1 の 4 つ（VK_BLEND_FACTOR_SRC1_*）、glBlendFunc* の検査は desktop だけ受ける。
  dualSrcBlend の無い device は GL_MAX_DUAL_SOURCE_DRAW_BUFFERS 0 で SRC1 を GL_INVALID_ENUM。
- gl3.c: `glVertexAttribP*` は 2_10_10_10 を展開して `glVertexAttrib4f`（正規化は GL 4.2 の符号付きの式）。
- glx.c・fixed.c: 3.3 の版（profile は 3.2 以上）、文字列、GLSL の上限 330、extension の表に 3.3 の ARB 6 つ。
- 試験: glxtest `--gl33`（gl33.c）と `plan/ws068/tests/glx-p037.sh`、glsl-host の pass/ に index の shader、fail/ に vertex の index。

## 実装（2026-10-29、q1682-i01、P2）

- libegl（`zegl.h`・`vulkan.c`）: `zegl_display.timestamp_bits`（queue family の `timestampValidBits`）、features に `dualSrcBlend`。
- query.c: query pool を 2 つ（occlusion と timestamp。timestamp の pool は最初の timer query で作る、valid bits が 0 なら作らず結果 0）。
  slot の表・`query_slot_take` を pool ごとに。`GL_TIME_ELAPSED`（begin・end で draw surface の frame の command buffer に
  `vkCmdWriteTimestamp(BOTTOM_OF_PIPE)`、frame が開いていなければ `zegl_frame_begin` で開く）、`glQueryCounter(GL_TIMESTAMP)`、
  `glGetQueryObjecti64v`・`ui64v`（`uiv` は 32 bit に切る）、`glGetQueryiv(GL_QUERY_COUNTER_BITS)`（timer は valid bits、数の query は 32）、
  `glGetInteger64v(GL_TIMESTAMP)`（`gles_query_timestamp`: 今の frame を submit・待ちしてから upload の command buffer で timestamp）。
  結果は (end − begin) を valid bits で mask して `timestampPeriod` を掛けた ns。active の判定を `query_is_active` にまとめた。
- GLSL: `layout(index = N)`（desktop 3.30〜、fragment の output、location 必須、0 か 1）。index 1 の output にだけ `Index` の decoration を
  出す（index 0 は出さない: 既存の desktop の shader の SPIR-V を変えない）。link の衝突の検査は (location, index) で、index 1 は location 0 だけ。
- spirv.c: output の `Index` の word と id を反射（`gles_spirv_variable.id`・`index`・`index_word`）。
- program.c: `glBindFragDataLocationIndexed`・`glGetFragDataIndex`。bind の index が Index の word を書き換え、word の無い output が
  index 1 になる時は Location の OpDecorate の後に `OpDecorate %id Index 1` を挿す（`program_output_indexes`、fragment の word の書き換えの後の最後）。
  index 1 が location 0 以外なら link の失敗。
- draw.c・gles.c: `GL_SRC1_COLOR`・`GL_SRC1_ALPHA`・`GL_ONE_MINUS_SRC1_*` の blend factor。`glBlendFunc*` は desktop で device が
  `dualSrcBlend` を持つ時だけ受ける（他は `GL_INVALID_ENUM`）。`GL_MAX_DUAL_SOURCE_DRAW_BUFFERS` は 1（無い device は 0）。
  dual-source の blend で draw buffer 1 以降に書く draw は `GL_INVALID_OPERATION`。
- libGL `gl33.c`（新）: `glVertexAttribP{1,2,3,4}ui{,v}`（2_10_10_10 を展開、符号付きの正規化は GL 4.2 の式）。exports.map と `GL/gl.h` に 3.3 の宣言と定数。
- 版: libGLX `glx.c` が 3.3 の context を受ける（profile は 3.2 以上。device が dualSrcBlend を持たなければ拒む、下の「判断」）。libGL `fixed.c` が「3.3 …」・GLSL「3.30」・GLSL の上限 330、
  extension に 3.3 の ARB 6 つ（blend_func_extended・explicit_attrib_location・shader_bit_encoding・texture_rgb10_a2ui・timer_query・
  vertex_type_2_10_10_10_rev）。
- 試験の program: glxtest `--gl33`（`userland/x11/glxtest/gl33.c`: 版と extension、TIME_ELAPSED、QueryCounter と GL_TIMESTAMP の順、
  dual-source（layout と bind）、P4ui（unsigned）と P3ui（signed）、RGB10_A2UI の FBO の clear と読み戻し）と `plan/ws068/tests/glx-p037.sh`。
  glxtest の「later-refused」の検査は 3.3 が受かるようになったので 4.7（無い版）の拒否に替えた（glx-p013・p031・p033 の期待の行は不変）。
- glsl-host: `pass/dual330.{vert,frag}`、`fail/index-{vertex,range,no-location,es}`、反射の index の検査（3d）、i915 の受け入れに dual330。

## 判断（Q1 の指示で直した、2026-10-29）

1. Q1「GL 3.3 の仕様（Table 23.x の implementation dependent values）では MAX_DUAL_SOURCE_DRAW_BUFFERS の最小は 1 なので、dualSrcBlend の無い
   device で 3.3 を名乗ると仕様に合わない。dualSrcBlend が無い device では 3.2 を上限にし（context の作成で 3.3 を求められたら拒む）、ある device
   だけ 3.3 にして。」→ libGL に `zgl_glx_version_limit(EGLDisplay)`（`zgl-glx.h` の private な口、exports.map）: 初期化した display の device が
   `dualSrcBlend` を持てば 33、無ければ 32。libGLX の `glx_context` が EGL の display を作った後に版がそれを超える context を拒む（NULL）。
   （最初の案は「無い device でも 3.3、MAX_DUAL_SOURCE 0」だった。）
   device の `dualSrcBlend`（2026-10-29 の source の読み。実機・QEMU では未確かめ）:

   | GPU（driver） | dualSrcBlend | GL の上限 | 根拠 |
   | --- | --- | --- | --- |
   | Venus（QEMU、host の GPU を転送） | host の driver による（host の Mesa の radv・anv・lavapipe は出す見込み、未確かめ） | 3.3（出せば） | `src/drivers/gpu/venus/` は host の features をそのまま返す |
   | i915 | 出さない | 3.2 | `src/drivers/gpu/i915/vk/vk-instance.c` の features は vertexPipelineStoresAndAtomics・logicOp・shaderInt16・geometryShader だけ |
   | nvrtx | 出さない | 3.2 | `src/drivers/gpu/nvrtx/vk/vk-device.c` の `device_features` は全て off |
   | amdrdna | 出さない | 3.2 | `src/drivers/gpu/amdrdna/vk/vk-device.c` の `device_features` は全て off |
   | powervr（VF2） | 出さない | 3.2 | `src/drivers/gpu/powervr/vk/vk-device.c` は全て off |
   | bcm2711（Pi 4） | 出さない | 3.2 | `src/drivers/gpu/bcm2711/vk/vulkan-query.c` は 0 から、optional の feature を出さない |
   | mali | 出さない | 3.2 | `src/drivers/gpu/mali/vk/vk-device.c` は全て off |
   | ghci（KC705） | caps の `GHCI_HAS_DUAL_SOURCE` の時 | 3.3（caps による） | `src/drivers/gpu/ghci/vk/vk-caps.c` |

   extension の表は静的で、3.3 の context だけが `GL_ARB_blend_func_extended` を名乗る（3.3 の context は dualSrcBlend のある device だけ）。
2. timestamp を書かない queue（valid bits 0）では timer の結果は 0、`GL_QUERY_COUNTER_BITS` は 0。
3. desktop の fragment output の `Index 0` は出さない（他の backend の compiler の差を増やさない）。index 1 の bind は decoration を挿して実現。

## 検証

- build（2026-10-29、版の上限の直しの後にもう一度）: amd64（config-amd64）・arm64（config-rpi4）・riscv64（config-visionfive2）で `dynamic/libGL.so`・`libGLESv2.so`・
  `libEGL.so`・`libGLX.so`・`bin/glxtest` → exit 0、`warning:` 0（各 arch の sysroot は worktree の中で作り直し、`GL/gl.h` の変更を含む）。
  `git diff --check` 問題なし。
- host: `sh plan/ws068/tests/glsl-host/run.sh build/glsl-host-p037` → `glsl-host: PASS`（compile・expect・link と spirv-val・反射の
  index（dual330: `out factor location=0 index=1`、`out colour location=0 index=0`）・i915 の受け入れ（dual330 を含む）・lavapipe の 24 試験）。
- 未実施: Venus の `glx-p037.sh`（glxtest --gl33）と回帰（glx-p013・p031・p033、egl-p019〜p030）、i915 実機。
  `program_output_indexes`（Index の挿入）と timer query は host で走らせていない（Vulkan の device の上の libGL が要る）。
