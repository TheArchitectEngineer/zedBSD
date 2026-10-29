<!-- awesome-plan project=zedbsd record=ws068p036 -->

# ws068-p036: GL 4.x の残りと版の名乗り

Phase ID: `ws068-p036`
Parent: [WS068](../ws.md)
Status: in-progress（分割の案と最初の一部だけ実装・試験未実施。2026-10-29 q1682-i01 P2。cleared にしない）
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
| c. sample shading | GL_ARB_sample_shading: glMinSampleShading・GL_SAMPLE_SHADING、gl_SampleID・gl_SamplePosition・gl_SampleMaskIn | 4.0 | 未着手 |
| d. cube map 配列・texture gather | GL_ARB_texture_cube_map_array（samplerCubeArray、VK_IMAGE_VIEW_TYPE_CUBE_ARRAY）、GL_ARB_texture_gather（textureGather*） | 4.0 | 未着手 |
| e. transform feedback 2/3・gpu_shader5 の残り | 一時停止の object は済み（ES 3.0）、glDrawTransformFeedback、stream、gpu_shader5（bitfield・fma・indexing 等） | 4.0 | 未着手 |
| f. separate shader objects・program pipeline | GL_ARB_separate_shader_objects、glProgramUniform* | 4.1 | 未着手 |
| g. vertex attrib binding | GL_ARB_vertex_attrib_binding | 4.3 | 未着手 |
| h. image load/store・atomic counter | p035 の残り（GL_ARB_shader_image_load_store、GL_ARB_shader_atomic_counters） | 4.2 | 未着手（p035） |
| i. KHR_debug | glDebugMessageCallback・Insert・Control、object label、debug group | 4.3 | 未着手 |
| j. DSA の部分・clip control | GL_ARB_direct_state_access の一部、GL_ARB_clip_control（gl_Position の書き換えと depth の範囲） | 4.5 | 未着手 |
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

## 判断（推しで進めた）

- indirect の command は GPU の vkCmdDrawIndirect にせず CPU で読む（draw の index の展開・flat の並べ替え・feedback の数えが CPU で count を要るため）。
  compute が書いた command は frame の待ちが入る。

## 検証

- build（2026-10-29）: amd64・arm64・riscv64 の libGL・libGLESv2・libEGL、amd64 の glxtest → exit 0、warning 0。
- 未実施: Venus の glx-p037（draw-indirect の outcome）、host の試験（GL の draw の道は host で走らない）。
