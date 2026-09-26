<!-- awesome-plan project=zedbsd record=ws068p024 -->

# ws068-p024: OpenGL ES 3.0 の API（1）: buffer・vertex array・uniform buffer

Phase ID: `ws068-p024`
Parent: [WS068](../ws.md)
Status: in-progress（q495-i01）
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
