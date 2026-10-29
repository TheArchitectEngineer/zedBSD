<!-- awesome-plan project=zedbsd record=ws068p036 -->

# ws068-p036: GL 4.x の残りと版の名乗り

Phase ID: `ws068-p036`
Parent: [WS068](../ws.md)
Status: 分割（2026-10-29 q1686-i01 P2、ユーザー「…phaseに分解して、目標を管理しながら、空いた時間に細々と取り組んで、WS完了に近づいていけるように、準備してください。」）。旧: in-progress（分割の案と a・c・i を実装、q1682-i01）

## 分割の索引（2026-10-29）

この phase.md は親の記録として残す（canceled にしないのは、a・c・d・g・i・j の実装の記録の本体がここにあるから）。下の表の a〜l を次の Phase に
分けた。実装済みの物は Venus で受け入れを確かめて cleared にした。

| 部分 | Phase |
| --- | --- |
| a. indirect draw | [ws068-p040](../phase040/phase.md)（cleared） |
| b. fp64 | [ws068-p047](../phase047/phase.md)（GLSL）、[ws068-p048](../phase048/phase.md)（GL の API） |
| c. sample shading | [ws068-p045](../phase045/phase.md)（実装済み、Venus の試験が残り） |
| d. texture gather・cube map 配列 | [ws068-p043](../phase043/phase.md)（cleared）、[ws068-p046](../phase046/phase.md)（cleared） |
| e. gpu_shader5・transform feedback 2/3 | [ws068-p049](../phase049/phase.md)、[ws068-p050](../phase050/phase.md)、[ws068-p051](../phase051/phase.md) |
| f. separate shader objects | [ws068-p053](../phase053/phase.md)、[ws068-p054](../phase054/phase.md) |
| g. vertex attrib binding | [ws068-p044](../phase044/phase.md)（cleared） |
| h. image load/store・atomic counter | p035 の分割（p056〜p058） |
| i. KHR_debug | [ws068-p041](../phase041/phase.md)（cleared） |
| j. DSA・clip control | [ws068-p042](../phase042/phase.md)（clip control、cleared）、[ws068-p064](../phase064/phase.md)・[ws068-p065](../phase065/phase.md)（DSA） |
| k. SPIR-V の shader | [ws068-p066](../phase066/phase.md)、[ws068-p067](../phase067/phase.md) |
| l. 版の名乗り（と各版の残りの extension） | 4.0 [p052](../phase052/phase.md)、4.1 [p055](../phase055/phase.md)、4.2 [p059](../phase059/phase.md)、4.3 [p061](../phase061/phase.md)・[p062](../phase062/phase.md)、4.4 [p063](../phase063/phase.md)、4.5 [p065](../phase065/phase.md)、4.6 [p068](../phase068/phase.md) |
Phase disposition: normal
依存: p034、p035
再開（2026-10-29）: ユーザー「作業の手が空いてしまったら、テストなしで実装だけ進める戦略で、残っているWSのphaseをまんべんなく進めましょう。」
（Q1 の依頼 q1682-i01 の引用）。ws.md の行の「着手前に更に分ける」に従い、下の表に分けた（案。Phase の ID を割り当てる時は Q1 が
ws.md に写す）。旧: 保留（2026-09-27、p037 と同じ）。

## 版の名乗りの方針

design.md §6: GL_VERSION は全てを実装した版を名乗り、上の版の機能は GL_ARB_* で個別に出す。4.0 を名乗るには下の 4.0 の行が全て要る
（fp64・sample shading・texture gather・cube map 配列・indirect draw・transform feedback 2/3・gpu_shader5 等）。それまでは 3.3 の context に
extension を足していく。device の feature が要る物（tessellation、fp64 の shaderFloat64、cube map 配列の imageCubeArray、sample shading の
sampleRateShading）は fixed.c の extension の表の features の欄（p034 で足した）で device ごとに出す。版の上限も device による
（p037 の `zgl_glx_version_limit` を拡げる）。

## 分割（案）

| 部分 | 中身 | 版 | 状態 |
| --- | --- | --- | --- |
| a. indirect draw | GL_ARB_draw_indirect: GL_DRAW_INDIRECT_BUFFER、glDrawArraysIndirect・glDrawElementsIndirect | 4.0 | **実装済み**（2026-10-29、下） |
| b. fp64 | GL_ARB_gpu_shader_fp64: GLSL の double・dvec・dmat、SPIR-V の Float64、glUniform*d、device の shaderFloat64 | 4.0 | 未着手 |
| c. sample shading | GL_ARB_sample_shading: glMinSampleShading・GL_SAMPLE_SHADING、gl_SampleID・gl_SamplePosition・gl_SampleMaskIn | 4.0 | **実装済み**（2026-10-29、下。gl_NumSamples は無い） |
| d. cube map 配列・texture gather | GL_ARB_texture_cube_map_array（samplerCubeArray、VK_IMAGE_VIEW_TYPE_CUBE_ARRAY）、GL_ARB_texture_gather（textureGather*） | 4.0 | texture gather は**実装済み**（2026-10-29 q1686-i01、下。shadow・rect・comp の引数は gpu_shader5 の e に）。cube map 配列は未着手 |
| e. transform feedback 2/3・gpu_shader5 の残り | 一時停止の object は済み（ES 3.0）、glDrawTransformFeedback、stream、gpu_shader5（bitfield・fma・indexing 等） | 4.0 | 未着手 |
| f. separate shader objects・program pipeline | GL_ARB_separate_shader_objects、glProgramUniform* | 4.1 | 未着手 |
| g. vertex attrib binding | GL_ARB_vertex_attrib_binding | 4.3 | **実装済み**（2026-10-29 q1686-i01、下） |
| h. image load/store・atomic counter | p035 の残り（GL_ARB_shader_image_load_store、GL_ARB_shader_atomic_counters） | 4.2 | 未着手（p035） |
| i. KHR_debug | glDebugMessageCallback・Insert・Control、object label、debug group | 4.3 | **実装済み**（2026-10-29、下） |
| j. DSA の部分・clip control | GL_ARB_direct_state_access の一部、GL_ARB_clip_control（gl_Position の書き換えと depth の範囲） | 4.5 | clip control は**実装済み**（2026-10-29 q1686-i01、下）。DSA は未着手 |
| k. SPIR-V の shader | GL_ARB_gl_spirv（glShaderBinary の SPIR-V と glSpecializeShader） | 4.6 | 未着手 |
| l. 版の名乗り | 4.0〜4.6 の context（libGLX の attributes、fixed.c の文字列・GLSL の上限、extension の表の版、`zgl_glx_version_limit` の device の条件） | — | 未着手（全て揃ってから） |

## 実装（2026-10-29）: a. indirect draw

- libGLESv2: `gles_state.draw_indirect_buffer`、buffer の target `GL_DRAW_INDIRECT_BUFFER`（libGL だけ）、削除で外す、glGet の
  `GL_DRAW_INDIRECT_BUFFER_BINDING`。
- libGL `gl40.c`（新）: `glDrawArraysIndirect`・`glDrawElementsIndirect`。command を CPU で読み（device が書いた buffer は
  `gles_buffer_fetch` で読み戻してから）、`glDrawArraysInstanced`・`glDrawElementsInstancedBaseVertex` に渡す。offset が 4 の倍数でなければ
  `GL_INVALID_VALUE`、buffer が無い・command が外なら `GL_INVALID_OPERATION`、element の draw は element buffer が要る。base instance は 4.0 の通り 0 とみなす。
- extension の表に `GL_ARB_draw_indirect`（3.1 以上）。`GL/gl.h` に宣言と定数。
- glxtest `--gl33` の 10 個目の outcome「draw-indirect」（command で緑の四角、binding の glGet、ずれた offset の拒否）。`glx-p037.sh` の期待のマスを増やした。

## 実装（2026-10-29）: i. KHR_debug

- libGL `debug.c`（新）: `glDebugMessageCallback`・`glDebugMessageControl`（source・type・severity の bit、id の規則 64 まで、GL_DONT_CARE）・
  `glDebugMessageInsert`・`glGetDebugMessageLog`（callback が無い時の 16 件の log）・`glPushDebugGroup`・`glPopDebugGroup`（深さ 64、
  group ごとの control の state、push・pop の message、GL_STACK_OVERFLOW・UNDERFLOW）・`glObjectLabel`・`glGetObjectLabel`（各 namespace の
  名前を確かめる）・`glObjectPtrLabel`・`glGetObjectPtrLabel`（sync）・`glGetPointerv`（callback と user param）。`GL_DEBUG_OUTPUT`（debug の
  context で既定 on）・`GL_DEBUG_OUTPUT_SYNCHRONOUS` は fixed の capability、glGet の `GL_MAX_DEBUG_*`・`GL_DEBUG_LOGGED_MESSAGES`・
  `GL_DEBUG_NEXT_LOGGED_MESSAGE_LENGTH`・`GL_DEBUG_GROUP_STACK_DEPTH`・`GL_MAX_LABEL_LENGTH`。既定で low の severity は出さない。
- libGLESv2: `gles_fixed_hooks.error`（gles_error が毎回呼ぶ）で GL の誤りを API・ERROR・HIGH の message に（id は誤りの enum）。
  hook は context の state を作らない（作る時の誤りで再帰しない）。出力は常に同期（message を作った call の中で callback）。
- extension の表に `GL_KHR_debug`（3.0 以上）。`GL/gl.h` に GLDEBUGPROC と宣言・定数。glxtest `--gl33` の 11 個目の outcome「debug」
  （marker・誤り・group の push と pop を callback が聞く、group の深さ、buffer の label、callback 無しの log）。glx-p037.sh の期待のマスは 12 全て緑。
- 制限: 消した object の label は残る（同じ名前の次の object が引き継ぐ）。shader compiler の message は出さない。

## 実装（2026-10-29）: c. sample shading

- GLSL: version mask の `GLSL_IN_SAMPLE`（4.00 以上、1.50・3.30 は `#extension GL_ARB_sample_shading`、macro も）。fragment の
  `gl_SampleID`・`gl_SamplePosition`・`gl_SampleMask[1]`（出力）、4.00 以上は `gl_SampleMaskIn[1]`。SPIR-V の SampleId・SamplePosition
  （SampleRateShading の capability）・SampleMask、整数の入力は Flat。gl_SamplePosition は向きを変えない（多 sample の target は FBO だけで、
  FBO は GL の行のまま。窓は 1 sample）。
- libGLESv2/libGL: `glMinSampleShading`（[0, 1] に clamp、device の sampleRateShading が無ければ `GL_INVALID_OPERATION`）、
  `GL_SAMPLE_SHADING` の capability（fixed の hook、device が持つ時）、glGet の `GL_MIN_SAMPLE_SHADING_VALUE`、pipeline の
  `sampleShadingEnable`・`minSampleShading`（多 sample の target で fraction が 0 より大きい時、key の raster に入れる）。
- extension の表に `GL_ARB_sample_shading`（device の feature の欄 `FIXED_FEATURE_SAMPLE_SHADING`）。`GL/gl.h` に宣言と定数。
- glsl-host: `pass/samples330ext.*`（spirv-val）、`fail/sample-330.frag`（extension 無しで gl_SampleID は無い）。

## 実装（2026-10-29 q1686-i01）: j. clip control

- libGL `gl45.c`（新）: `glClipControl`（origin は GL_LOWER_LEFT・GL_UPPER_LEFT、depth は GL_NEGATIVE_ONE_TO_ONE・GL_ZERO_TO_ONE、他は
  GL_INVALID_ENUM で状態を変えない）。glGet の `GL_CLIP_ORIGIN`・`GL_CLIP_DEPTH_MODE`（libGL だけ）。extension の表に `GL_ARB_clip_control`（3.0 以上）。
- libGLESv2: pipeline の key の raster に `clip_control`（GLES_CLIP_UPPER_LEFT・GLES_CLIP_ZERO_TO_ONE）。GL_UPPER_LEFT は最後の stage の
  y の反転を窓と FBO で入れ替え（窓は反転しない module、FBO は反転する module）、front face も同じ反転に合わせる（GL は UPPER_LEFT で
  面積の符号を反転するので、clip 空間の回りで表裏が決まるまま。Mesa の st_atom_rasterizer が ClipOrigin で front_ccw を反転するのと同じ、と
  記憶から。仕様の原文は未確認）。GL_ZERO_TO_ONE は `gles_spirv_position` の新しい引数 half_depth を 0 にした module（z を (z+w)/2 に
  しない）。最後の stage（geometry、無ければ tessellation evaluation、無ければ vertex）の書き換え前の code を program に保ち
  （`last_code`）、ZERO_TO_ONE の module を初めての draw で作る（`gles_program_zero_module`、window 用・FBO 用の 2 つ、unlink で捨てる）。
  gl_FragCoord・viewport・scissor は窓の座標のままなので変えない。
- 試験: glxtest `--gl33` の 12 個目の outcome「clip-control」（既定値の glGet、悪い origin の拒否と状態の保持、UPPER_LEFT で上半分の
  四角が下の行に出ること、UPPER_LEFT で back の culling でも表のまま、ZERO_TO_ONE で depth -0.5 が clip され 0.5 が残ること、既定で -0.5 が
  残ること）。spirv-host に z を保つ書き換えの spirv-val と FMul が 1 つ減る検査。

## 実装（2026-10-29 q1686-i01）: d の一部. texture gather

- GLSL: version mask の `GLSL_IN_GATHER`（4.00 以上、1.30〜3.30 は `#extension GL_ARB_texture_gather`、macro も）。`textureGather`
  （gsampler2D・gsampler2DArray・gsamplerCube）と `textureGatherOffset`（gsampler2D・gsampler2DArray、定数の offset は ConstOffset）、
  SPIR-V の OpImageGather（component 0）。version mask の extension の組み合わせを 1.10〜3.30 の全ての版に OR するように整理した。
- libGL: extension の表に `GL_ARB_texture_gather`（3.0 以上）、glGet の `GL_MIN/MAX_PROGRAM_TEXTURE_GATHER_OFFSET`（device の
  min/maxTexelGatherOffset）と `GL_MAX_PROGRAM_TEXTURE_GATHER_COMPONENTS_ARB`（4）。
- 範囲外（GL_ARB_gpu_shader5 の物、e に残す）: comp の引数、shadow sampler の gather（refZ）、2DRect の gather、textureGatherOffsets。
- 試験: glsl-host の `pass/gather400.*`、`fail/gather-330.frag`、exec の `62-texture-gather`（lavapipe で 2x2 の texture の red の 4 texel と
  offset -1 の clamp）。glxtest `--gl33` の 13 個目の outcome「texture-gather」。

## 実装（2026-10-29 q1686-i01）: g. vertex attrib binding

- libGLESv2: `gles_attrib` に attribute が読む binding point（既定は自分の index）と relative offset、各 index の binding point の
  buffer・offset・stride・divisor・`binding_exact`（glBindVertexBuffer の stride 0 は本当に 0、glVertexAttribPointer の 0 は詰めた並び）。
  attribute の `buffer`・`pointer`・`stride`・`divisor` は binding point から写した値（`gles_vertex_binding_apply`）で、draw の経路は
  そのまま（stride 0 で `zero_stride` の時だけ詰めない）。glVertexAttribPointer・IPointer・Divisor は仕様の通り自分の binding point を
  設定して写す（OpenGL ES では各 attribute が自分の binding point だけを読むので見える挙動は変わらない）。VAO は gles_attrib ごと保存されるので
  binding point も VAO の状態。buffer の削除で binding point の buffer も外す。immediate mode（glEnd）は 4 つでなく全 attribute を保存・復元する。
- libGL `gl43.c`（新）: `glBindVertexBuffer`・`glVertexAttribFormat`・`glVertexAttribIFormat`・`glVertexAttribBinding`・
  `glVertexBindingDivisor`（index・offset・stride（2048 まで）・relative offset（2047 まで）・型の検査、core profile の VAO 0 は
  GL_INVALID_OPERATION）。glGetIntegeri_v・glGetInteger64i_v の `GL_VERTEX_BINDING_BUFFER/OFFSET/STRIDE/DIVISOR`、glGetVertexAttribiv の
  `GL_VERTEX_ATTRIB_BINDING`・`GL_VERTEX_ATTRIB_RELATIVE_OFFSET`、glGet の `GL_MAX_VERTEX_ATTRIB_BINDINGS`（16）・
  `GL_MAX_VERTEX_ATTRIB_RELATIVE_OFFSET`（2047）。extension の表に `GL_ARB_vertex_attrib_binding`（3.0 以上）。
- 制限: GL_VERTEX_ATTRIB_ARRAY_STRIDE は binding point の stride を返す（4.3 は glVertexAttribPointer の値を attribute の状態に持つ）。
  GL_VERTEX_BINDING_STRIDE は glVertexAttribPointer の 0 を 0 のまま返す（4.3 は詰めた実効値）。
- 試験: glxtest `--gl33` の 14 個目の outcome「attrib-binding」（1 つの binding point の interleave の 2 attribute、stride 0 の binding point
  の 1 要素を全頂点が読む、状態の glGet）。

## 判断（推しで進めた）

- indirect の command は GPU の vkCmdDrawIndirect にせず CPU で読む（draw の index の展開・flat の並べ替え・feedback の数えが CPU で count を要るため）。
  compute が書いた command は frame の待ちが入る。

## 検証

- host（2026-10-29、c の後）: `sh plan/ws068/tests/glsl-host/run.sh build/glsl-host-p036c` → `glsl-host: PASS`。
- build（2026-10-29、c の後）: amd64・arm64・riscv64 の libGL・libGLESv2・libEGL・libGLX・glxtest → exit 0、warning 0（sysroot を作り直し）。
- 未実施: Venus の glx-p037（draw-indirect・debug の outcome）、host の試験（libGL は host で走らない）。

## 検証（2026-10-29 q1686-i01 P2）

- build: amd64・arm64・riscv64 の libGL・libGLESv2・libEGL・glxtest → exit 0、warning 0（sysroot を作り直し）。
- host: `sh plan/ws068/tests/spirv-host/run.sh build/tmp/q1686-spirv-host` → PASS（z を保つ書き換え）、
  `sh plan/ws068/tests/glsl-host/run.sh build/tmp/q1686-glsl-host4` → PASS（gather の exec、cube map 配列の pass/fail）。
- Venus `build/tmp/q1686-v4`: `venus-all: PASS`。glx-p037 の outcome draw-indirect・debug・clip-control・texture-gather・attrib-binding が緑
  （cube map 配列の前の tree）。
- Venus `build/tmp/q1686-v5`（cube map 配列を含む最終の tree、image 92b3c60d6c9c83f5）: `venus-all: PASS`（16 本）。glx-p037 の outcome cube-map-array は skipped でなく実行して緑（Venus の device は imageCubeArray を持つ）、他の outcome も全て緑、`GLXTEST GL33 ready failures=0`。
