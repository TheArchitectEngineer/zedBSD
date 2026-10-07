<!-- awesome-plan project=zedbsd record=ws075-p007a -->

# ws075-p007a: GL 3.2 の stage の compiler（compiler/）: 増分 a1〜a5

Status: in-progress（q833、P1。2026-10-07 夜 a1 を実装と host 試験）
Disposition: normal
Parent: [WS075](../ws.md)
設計: [phase007/design.md](../phase007/design.md)（§4・§14 が優先）、増分の表は [phase007/phase.md](../phase007/phase.md)。

## 承認

Q1（2026-10-07）: 判断 1〜8 を既定どおりで承認、Phase の ID は p007a・p007b で可。a1 の範囲の ACK（2026-10-07 夜、「以降の増分も表の範囲に収まるなら一言だけで進めてよい」）。

## 記録

- 2026-10-07 夜 増分 a1（SPIR-V の GS の読み、codegen は refuse のまま）:
  - `compiler/ir.h`: stage `I915_STAGE_GEOMETRY`（`I915_STAGE_COUNT` 4）、op `LOAD_VERTEX_INPUT`・`EMIT_VERTEX`・`END_PRIMITIVE`、location `I915_IR_LOCATION_LAYER`、`I915_IR_VERTEX_DYNAMIC`、`I915_IR_OUTPUT_*`（3D_Prim_Topo_Type の 1/3/5）、`I915_IR_MAX_OUTPUT_VERTICES` 256、system `PRIMITIVE_ID`（`I915_IR_SYSTEM_COUNT` 8）、ir の `vertices_in`・`output_topology`・`max_vertices`・`uses_end_primitive`・`uses_primitive_id`・`writes_layer`。
  - `compiler/compiler.h`: `I915_SHADER_LOCATION_PRIMITIVE_ID` 69。
  - 新 `compiler/spirv-geometry.inc`（spirv.c の末尾で include）: execution mode（入力 5 種・出力 3 種・OutputVertices 1〜256・Invocations 1 だけ、他の mode は refuse）、宣言の後の検査（入力・出力・OutputVertices が揃うこと）、入力の宣言（gl_PrimitiveIDIn は `PTR_SYSTEM`、gl_InvocationID と他の builtin は refuse、gl_in と located の配列は `PTR_VERTEX`、配列の長さ = 入力の頂点の数でなければ refuse、gl_in の member は全て builtin であること）、access chain の最初の index で頂点（定数・動的）、load は scalar ごとに `LOAD_VERTEX_INPUT`（gl_in の Position は location POSITION、PointSize は POINT_SIZE、他の member は refuse）、出力の builtin（Position、Layer → LAYER と `writes_layer`、PrimitiveId → location 69、PointSize・ViewportIndex は refuse: design §14 S3 と §4.5）、OpEmitVertex・OpEndPrimitive（block の predicate 付き。出力が points なら END は出さない）。
  - `compiler/spirv.c`: 分岐の追加（entry point、宣言の後の検査、fragment の gl_PrimitiveID を location 69 の Flat の入力に、geometry の入力の宣言、`i915_spirv_add_io` に型の引数（per-vertex は要素の型）、access chain、load、store の builtin、OpEmitVertex/OpEndPrimitive の dispatch）。`spirv-compute.inc`: geometry の execution mode を geometry の関数へ、`i915_spirv_system_component` に PrimitiveId。
  - `compiler/compile.c`: geometry の stage は ENOTSUP（a2 まで）。`render/pipeline-prepare.c`: log の stage の名前に "geometry"（GS の module を VS として渡した時の log が "fragment" と誤らないよう）。
  - 試験: `src/drivers/gpu/i915/tests/render/compiler-shaders/` に `points.geom`・`adjacency.geom`・`layers.geom`（glxtest の 3 つを `#version 450` に）、`varyings.geom`（WS068 の glsl150-geometry を Vulkan GLSL に: located の配列・block の配列・PrimitiveIDIn・PrimitiveID・Layer）、refuse の `refuse-invocations.geom`・`refuse-invocation-id.geom`・`refuse-viewport.geom`・`refuse-clip-distance.geom`・`refuse-length.spvasm`（手書き、spirv-as）、`primitive-id.frag`。`regenerate.py` は GS を `.spv` だけに出す（kernel の試験の `.inc` には入れない。再生成で `.inc` の差は mview の path の注記 1 行だけ: 古い `userland/desktop/mview` → 今の `userland/tests/mview`）。`plan/ws031/tests/i915-vk-spirv-test.c` に GS の 4 場面（field、LOAD_VERTEX_INPUT の頂点・location・component、EMIT/END の数と predicate、LAYER・PrimitiveId の store、LOAD_SYSTEM）、refuse 5 件と VS の OpEmitVertex、fragment の gl_PrimitiveID。
  - design との差: refuse の 5 件の「OpArrayLength」は ClipDistance の読みに替えた（gl_in.length() は GLSL の compiler が定数に畳み、OpArrayLength は storage buffer の run-time の配列にしか来ない。GS の storage buffer の OpArrayLength は他の stage と同じ扱い）。per-vertex の配列の長さ 0（run-time の配列）は Vulkan の Input に無いので受けない（長さ = 入力の頂点の数だけ）。

| コマンド | 結果 |
| --- | --- |
| `python3 src/drivers/gpu/i915/tests/render/compiler-shaders/regenerate.py`（shaderc 2025.2-1） | 成功、既存の `.spv` は不変 |
| `sh plan/ws031/tests/run-vk-host-tests.sh spirv` | plain・ASan/UBSan PASS |
| `sh plan/ws031/tests/run-vk-host-tests.sh`（10 個: cmd spirv lower res resdispatch sync eu compile pipe cmdbuf） | plain・ASan/UBSan 全て PASS（pipeline-prepare.c の log の変更の後に pipe を再度 PASS） |
| `git diff --check` | 問題無し |
| `make -j16 BUILD=build/p1-k ZEDBSD_CONFIG=config/ci/config-amd64.mk build/p1-k/vmunix` | 成功 warning 0 |

残り: a2（compile-geometry.inc: interface・payload・prologue・pull の入力・PrimitiveID・Layer/PrimitiveId の staged・`drv_i915_shader_compile_stage`・「VERTEX でなければ FRAGMENT」の分岐の洗い出し・頂点数 0 の EOT）→ a3 → a4（a5 は後回し可）。
