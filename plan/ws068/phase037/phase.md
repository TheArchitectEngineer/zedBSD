<!-- awesome-plan project=zedbsd record=ws068p037 -->

# ws068-p037: desktop GL 3.3（timer query、dual-source blend、3.3 の context）

Phase ID: `ws068-p037`
Parent: [WS068](../ws.md)
Status: planned（保留、2026-09-27。コードは未着手）
Phase disposition: normal
保留の理由: 2026-09-27 ユーザー「OpenGL 3.2が問題なければ、それ以降のOpenGLはいったん保留して、i915の高度化に進んでください。」
（p033 の GL 3.2 は Venus で PASS）。再開はユーザーの指示で。

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

## 設計の下書き（再開の時の出発点。未実装）

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

## 判断が要る点

（なし）

## 検証

未実施（保留）。
