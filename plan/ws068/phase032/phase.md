<!-- awesome-plan project=zedbsd record=ws068p032 -->

# ws068-p032: GLSL compiler の geometry shader

Phase ID: `ws068-p032`
Parent: [WS068](../ws.md)
Status: cleared（2026-09-27。GLSL の host 試験 PASS（spirv-val、lavapipe の geometry の実行 2 件）。Venus の回帰 PASS）
Phase disposition: normal
承認: 2026-09-27 ユーザーの自走の指示（graphics が最優先）、design.md §6、WS068 の計画（p014 の分割）。試験は 2026-09-27 の
ユーザーの方針（amd64 のみ、phase の最後に）。

## 範囲

- GLSL 1.50 以上（desktop）の geometry の stage（`GLSL_STAGE_GEOMETRY`）: `layout(points | lines | lines_adjacency | triangles |
  triangles_adjacency) in;`、`layout(points | line_strip | triangle_strip, max_vertices = N) out;`、入力は配列（大きさ無しは
  入力の primitive の頂点の数）と入力の interface block の配列、`gl_in[]`（gl_Position・gl_PointSize）、`gl_PrimitiveIDIn`、
  出力の `gl_Position`・`gl_PointSize`・`gl_PrimitiveID`・`gl_Layer`、`EmitVertex()`・`EndPrimitive()`（geometry だけ）。
  fragment の `gl_PrimitiveID`（1.50）。
- SPIR-V: Geometry の capability と execution model、入力と出力の primitive・OutputVertices・Invocations 1 の execution mode、
  入力の配列の変数、`gl_in` は BuiltIn の member の Block の配列、OpEmitVertex・OpEndPrimitive。
- link: 頂点・geometry・fragment の 3 つ（`glsl_link_stages`）: vertex の出力 ↔ geometry の入力（配列の要素の型）、geometry の出力 ↔
  fragment の入力、uniform と uniform block は 3 つの stage で共有（block の stages の bit 2 が geometry）。`struct glsl_program` の
  `code[2]` を geometry の SPIR-V に。transform feedback の capture は geometry があれば断る（p033 以降）。
- host の試験: pass/・fail/ の geometry shader、3 stage の link と spirv-val、vk-run で `.geom` の試験（lavapipe で点を四角に
  広げる等）。

範囲外（p033）: libGLESv2 の API（glAttachShader の GL_GEOMETRY_SHADER、pipeline の geometry の stage、gl_Position の書き換えを
最後の stage へ）。

## 設計

- stage は `GLSL_STAGE_GEOMETRY`（2）。`struct glsl_program` の `code[GLSL_STAGE_GEOMETRY]`（無ければ NULL）。
- parse: layout の `points`・`lines`・`lines_adjacency`・`triangles`・`triangles_adjacency`・`line_strip`・`triangle_strip`、
  `max_vertices = N`（node の `primitive`・`max_vertices`）。check: 修飾子だけの宣言（`check_default_layout`）が shader の
  `geometry_input`・`geometry_vertices`・`geometry_output`・`max_vertices` を決め、`gl_in` をその頂点の数の配列にする。入力は配列
  （`check_geometry_input`: 大きさ無しは頂点の数、違う大きさは誤り、layout より前は誤り）、入力の block は instance の配列。
  main の後に layout の揃いを確かめる。varying は geometry では誤り。fragment だけの規則（出力の型、整数の入力の flat）は fragment だけに。
- built-in: `gl_in`（`gl_PerVertex` の block: gl_Position だけ。gl_PointSize は Vulkan の feature が要るので入れない）、
  `gl_PrimitiveIDIn`、出力の gl_Position・gl_PointSize・gl_PrimitiveID・gl_Layer、fragment の gl_PrimitiveID（1.50、Flat）。
  関数の stage の bit に geometry（`BI_GEOMETRY`、`BI_BOTH` は全ての stage）、`EmitVertex`・`EndPrimitive`（signature の `v` は void）。
- emit: Geometry の execution model と capability、Triangles 等の入力・OutputTriangleStrip 等の出力・OutputVertices・
  Invocations 1、`gl_in` の block の member の BuiltIn Position、PrimitiveId・Layer（Geometry の capability）、OpEmitVertex・
  OpEndPrimitive。fragment 以外の texture は explicit lod。入力の block の配列の member の flat。
- link: `glsl_link_stages(vertex, geometry, fragment, …)`（`glsl_link_captured` はその geometry 無し）。varying は producer と
  consumer の組ごと（geometry の入力は配列の要素の型で出力と照合）、uniform block の binding は stage の順に共有、block の
  `stages` の bit 2 が geometry。geometry があると transform feedback の capture は断る。`glsl_geometry_layout` が GL の名前の
  primitive と max_vertices を返す（p033 の draw の検査用）。
- libGLESv2 の `gles_spirv_position`（spirv.c）: OpEmitVertex を持つ module は各 OpEmitVertex の前で gl_Position を書き換える
  （p033 で geometry の stage を最後の stage として書き換える。vk-run はこれで試験）。

## 判断が要る点（既定を選んで進める）

（なし）

## 検証

| 確認 | 結果 |
| --- | --- |
| GLSL の host 試験（`plan/ws068/tests/glsl-host/run.sh`） | **PASS**: pass/ の `glsl150-geometry.vert`・`.geom`・`.frag`（配列の入力、入力の block の配列、gl_in.length()、gl_PrimitiveIDIn、gl_Layer、gl_PrimitiveID、EmitVertex・EndPrimitive の loop）の compile と 3 stage の link と spirv-val、fail/ の 5 件（layout 無し、vertex の EmitVertex、配列でない入力、大きさ違い、出力の primitive の誤り）、vk-run の `50-geometry`（三角形の通過と入力の値）・`51-geometry-expand`（最初の primitive から全体の四角）が lavapipe で期待の色。i915 の host 検査の組も PASS |
| build（lean image） | status 0。warning 0 |
| style-check | 変えた file 0（前も 0） |
| 回帰（Venus）egl-p019・p020・p024・p030・glx-p031 | PASS |
| boot test（`build/ws068-p032-regress/boot/login.png`） | PASS |
| i915 | 未実施（i915 の compiler は Geometry を受けない。F-023 の範囲） |

### 制限・移管

- `gl_in[].gl_PointSize`、`gl_ClipDistance`、geometry の `invocations`（4.0）、compatibility の gl_in の色は無い。
- transform feedback の capture は geometry があると断る。
- libGLESv2 の API（GL_GEOMETRY_SHADER、pipeline の geometry の stage、descriptor の stage の bit）は p033。
