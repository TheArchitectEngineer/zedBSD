<!-- awesome-plan project=zedbsd record=ws068p024 -->

# ws068-p024: OpenGL ES 3.0 の API（1）: buffer・vertex array・uniform buffer

Phase ID: `ws068-p024`
Parent: [WS068](../ws.md)
Status: cleared（q495-i01、2026-09-27）
Phase disposition: normal
Queue: q495-i01
承認: 2026-09-27 ユーザーの自走の指示、WS068 の計画（ws068-p005「GLES 3.0」を p024〜p027 に分けた。desktop GL 3.0（p013）の前提）

## 範囲

- vertex array object（`glGenVertexArrays`・`glBindVertexArray`・`glDeleteVertexArrays`・`glIsVertexArray`）: 属性の配列と element buffer を
  object ごとに。
- buffer: `glMapBufferRange`・`glUnmapBuffer`・`glFlushMappedBufferRange`・`glGetBufferPointerv`、`glCopyBufferSubData`、target
  `GL_COPY_READ_BUFFER`・`GL_COPY_WRITE_BUFFER`・`GL_UNIFORM_BUFFER`（pixel pack/unpack と transform feedback の target は束縛だけ）、
  `glGetBufferParameteri64v`。
- instancing: `glDrawArraysInstanced`・`glDrawElementsInstanced`・`glVertexAttribDivisor`（divisor 1 は Vulkan の instance rate、それより
  大きいものは stream で展開）、`gl_InstanceID`。`glDrawRangeElements`。
- 整数の属性: `glVertexAttribIPointer`・`glVertexAttribI4i`/`ui`/`iv`/`uiv`・`glGetVertexAttribIiv`/`Iuiv`。整数の uniform:
  `glUniform{1,2,3,4}ui[v]`・`glGetUniformuiv`。
- uniform buffer: `glGetUniformBlockIndex`・`glUniformBlockBinding`・`glBindBufferBase`・`glBindBufferRange`・
  `glGetActiveUniformBlockiv`（DATA_SIZE・BINDING・NAME_LENGTH）・`glGetActiveUniformBlockName`、`glGetIntegeri_v` の束縛。compiler の
  link が block の名前・binding・std140 の大きさを渡し（`glsl_program` に足す）、link の「GLES 3.0 の API が要る」の拒否を外す。
  descriptor set に block の binding（32 + i）を足す。
- `glGetStringi(GL_EXTENSIONS)`、`glGetInteger64v`、primitive restart（`GL_PRIMITIVE_RESTART_FIXED_INDEX`）。
- block の member の反射（`glGetUniformIndices`・`glGetActiveUniformsiv`）は最小限（無ければ記録して後）。

## 受け入れ

1. build warning 0、変えた・新しい C の style-check 0。GLSL の host 試験（`plan/ws068/tests/glsl-host/run.sh`）PASS。
2. egltest の新しい場面（ES 3 の context、`#version 300 es`）: VAO 2 つの切り替え、instancing（divisor 1 と 2）、整数の属性、uniform buffer
   （std140 の block を glBindBufferRange で）、glMapBufferRange で書いた buffer が Venus の display と Wayland で PASS。
3. 回帰: egl-p020・egl-p022・egl-p023・egl-p008・x11-p005、boot test。i915 実機は未実施でよい。

## 結果（2026-09-27、q495-i01）

cleared。受け入れ 1〜3 を満たした（i915 実機は未実施）。

### 実装

- GLSL compiler（`glsl/glsl.h`・`glsl/link.c`）: `struct glsl_program` に名前付きの uniform block の一覧 `blocks`（名前・binding（32 から）・
  std140 の大きさ・読む stage・member の数）と、block の member の葉（`glsl_uniform_info` の `block`・`offset`・`array_stride`・
  `matrix_stride`・`row_major`。名前は GL の API の形: instance 名のある block は `Block.member`、無い block は `member`）を足した。
  block の上限は 24（binding 32〜55）。
- libGLESv2:
  - header を `GLES3/gl3.h` に（`gl2ext.h` と併せて）。
  - buffer（`buffer.c`）: target `GL_COPY_READ_BUFFER`・`GL_COPY_WRITE_BUFFER`・`GL_UNIFORM_BUFFER`・`GL_PIXEL_PACK_BUFFER`・
    `GL_PIXEL_UNPACK_BUFFER`・`GL_TRANSFORM_FEEDBACK_BUFFER`（pixel と transform feedback は束縛だけ）、`glMapBufferRange`（CPU の
    bytes をそのまま返す。unmap と `glFlushMappedBufferRange` で device の copy を stale に）・`glUnmapBuffer`・`glGetBufferPointerv`・
    `glCopyBufferSubData`・`glGetBufferParameteri64v`（MAPPED・ACCESS_FLAGS・MAP_OFFSET・MAP_LENGTH も）、`glBindBufferBase`・
    `glBindBufferRange`（uniform は 24、transform feedback は 4 の添字付きの束縛点）、`glGetIntegeri_v`・`glGetInteger64i_v`。
    device の copy に `VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT`。
  - vertex array object（`buffer.c`）: `glGenVertexArrays`・`glDeleteVertexArrays`・`glBindVertexArray`・`glIsVertexArray`。bind で
    context の属性の配列と element buffer を前の object へ保存し新しい object のものを読む（現在値は context のもので保つ）。
    buffer の削除はすべての vertex array から外す。VAO 0 以外で client の配列は `GL_INVALID_OPERATION`（OpenGL ES のみ）。
  - draw（`draw.c`）: `glDrawArraysInstanced`・`glDrawElementsInstanced`・`glDrawRangeElements`・`glVertexAttribDivisor`（divisor 1 は
    Vulkan の instance rate、2 以上は stream に instance ごとに展開。`gles_vertex_layout.rates` を pipeline の key に）、
    `glVertexAttribIPointer`・`glVertexAttribI4i`/`ui`/`iv`/`uiv`・`glGetVertexAttribIiv`/`Iuiv`（SINT・UINT の format。shader の入力と
    符号が違う整数と device の fetch できない format は 32 bit に変換）、`glVertexAttribPointer` の `GL_INT`・`GL_UNSIGNED_INT`・
    `GL_HALF_FLOAT`・`GL_INT_2_10_10_10_REV`・`GL_UNSIGNED_INT_2_10_10_10_REV`、属性の現在値は shader の入力の型の format で。
    primitive restart（`GL_PRIMITIVE_RESTART_FIXED_INDEX`: 型の最大値で index を切り、切れ目ごとに list に展開してつなぐ）。
    名前付きの uniform block は束縛点の buffer の範囲を descriptor（`VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`、binding 32 から）に。
    buffer が無い・範囲が block より小さいと `GL_INVALID_OPERATION`。
  - program（`program.c`）: link の「uniform blocks need the OpenGL ES 3.0 API」の拒否を外した。block を binding で記録し（SPIR-V の
    binary は型の名前だけ、GLSL は compiler の一覧で名前・大きさ・stage・member）、descriptor set layout に足す。
    `glGetUniformBlockIndex`・`glGetActiveUniformBlockiv`（BINDING・DATA_SIZE・NAME_LENGTH・ACTIVE_UNIFORMS・ACTIVE_UNIFORM_INDICES・
    REFERENCED_BY_*）・`glGetActiveUniformBlockName`・`glUniformBlockBinding`・`glGetUniformIndices`・`glGetActiveUniformsiv`（TYPE・
    SIZE・NAME_LENGTH・BLOCK_INDEX・OFFSET・ARRAY_STRIDE・MATRIX_STRIDE・IS_ROW_MAJOR）、`glGetProgramiv` の ACTIVE_UNIFORM_BLOCKS と
    ACTIVE_UNIFORM_BLOCK_MAX_NAME_LENGTH。block の member は location を持たない（`glGetUniformLocation` は -1）。
    `glUniform{1,2,3,4}ui[v]`・`glGetUniformuiv`、非正方の `glUniformMatrix{2x3,3x2,2x4,4x2,3x4,4x3}fv`、transpose（OpenGL ES 3 と
    desktop GL）。`glGetUniformiv` は float を経ずに読む。
  - spirv.c: GL の型の名前（`gles_gl_type`: uint の vector、非正方の matrix）、名前付き block の型の名前。
  - gles.c: `glGetStringi`（`GL_EXTENSIONS`）・`GL_NUM_EXTENSIONS`・`glGetInteger64v`（MAX_ELEMENT_INDEX・MAX_SERVER_WAIT_TIMEOUT）、
    OpenGL ES 3 の整数の状態（VERTEX_ARRAY_BINDING、各 buffer の束縛、MAX_UNIFORM_BUFFER_BINDINGS・MAX_UNIFORM_BLOCK_SIZE・
    MAX_*_UNIFORM_BLOCKS・UNIFORM_BUFFER_OFFSET_ALIGNMENT・MAX_ELEMENT_INDEX・MAX_ELEMENTS_* ほか）、`glEnable` の
    `GL_PRIMITIVE_RESTART_FIXED_INDEX`。属性の初期値（size 4・GL_FLOAT）。
  - exports.map に 48 の関数。GL_VERSION は「OpenGL ES 2.0」のまま（ES 3.0 を名乗るのは p027）。
- egltest `--scene=es3`（`userland/desktop/egltest/es3.c`）: OpenGL ES 3 の context、`#version 300 es`。VAO 4 つ（A: glDrawRangeElements と
  整数の現在値、B: interleave の buffer の unsigned byte を int の入力へ（変換）、C: restart index で切った triangle strip と int の配列、
  D: glDrawElementsInstanced の 4 instance（gl_InstanceID、divisor 1 の色と divisor 2 の gain））。色は std140 の block（Palette、
  glBindBufferRange で offset alignment の位置、glMapBufferRange で書き glCopyBufferSubData で足す）と fragment の instance 名付きの
  block（Tint、glUniformBlockBinding で束縛点 2）から、uint の uniform が 7 のときだけ。start で API の報告を 33 項目確かめる。
- 試験: `plan/ws068/tests/egl-p024.sh`（display と Wayland の画面の 9 点、readback、API の検査）。GLSL の host 試験に block の反射の
  検査（blocks330 の block の大きさ・member の offset・stride）を足した。p022 で `gles_spirv_position` の引数が増えたのに追従して
  いなかった host の試験（`spirv-host/main.c`・`glsl-host/vk-run.c`）を直した（flip 1）。host の shim に GLES3。

### 検証

- build（lean image、`plan/ws068/tests/build-glsl-image.sh build/amd64`）: 我々の source の warning 0（openssl の外部の既存の warning のみ）。
  main（WS036 の zedbsd7 の toolchain）を merge した後に clean build で再確認。
- style-check: libGLESv2（glsl/ を含む）・libEGL・egltest・glsl-test.c すべて 0。§14 を読んで確かめた（3 節以上の条件は 1 節 1 行、
  三項演算子は使わない、成功の return を最後に）。
- GLSL の host 試験（`plan/ws068/tests/glsl-host/run.sh`）PASS（compile・expect・link・blocks・i915・lavapipe の 19 試験）。
- Venus（QEMU）: egl-p024 PASS（display と Wayland の画面の 9 点、readback failures=0、API の検査 failures=0。画面を目で確かめた）。
  回帰: egl-p008・egl-p019・egl-p020・egl-p022・egl-p023・x11-p005 PASS（gears.png を目で確かめた）、boot test PASS
  （`build/ws068-p024-boot/login.png`）。いずれも main の merge 後の image。
- i915 実機: 未実施（名前付き block の descriptor・instance rate・整数の属性は F-023 の範囲で未確認）。

### 制限・移管

- `glMapBufferRange` の `GL_MAP_UNSYNCHRONIZED_BIT` は同期する map と同じ（CPU の bytes を渡すので待たない）。
- SPIR-V の binary（glShaderBinary）の名前付き block は大きさ 0（不明）・member 無し（名前は型の OpName）。
- 名前付き block の member の名前が 63 文字を超えると切れる（`GLES_NAME`）。
- `GL_MAX_VERTEX_UNIFORM_BLOCKS`・`FRAGMENT` は 12、`COMBINED` は 24。stage ごとの 12 の上限は link で検査しない（device の
  上限を超えると pipeline の作成で失敗する）。
- pixel pack/unpack buffer の束縛は受けるが、glTexImage・glReadPixels はまだ読まない（ws068-p028 の範囲）。transform feedback の束縛点は
  束縛だけ（p027）。
