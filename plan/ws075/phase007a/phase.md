<!-- awesome-plan project=zedbsd record=ws075-p007a -->

# ws075-p007a: GL 3.2 の stage の compiler（compiler/）: 増分 a1〜a5

Status: in-progress（q833、P1。2026-10-07 夜 a1・a2・a3 を実装と host 試験。ユーザーの決定で UCSI・DP alt mode を優先し、a4 の前で止めた。再開は下の「再開の情報」）
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

- 2026-10-07 夜 増分 a2（GS の codegen の読みと payload、EmitVertex は a3 まで refuse）:
  - 新 `compiler/compile-geometry.inc`（compile.c の末尾で include）: register の約束（r0、r1 出力 handle、r2 PrimitiveID、入力頂点の handle、push data、頂点数と cut bits は値の先頭 2 register、staged VUE と真下に書き込みの header 2 register）、interface（入力の頂点数の検査、located の per-vertex 入力の一覧、handle と push data の位置、出力頂点の 32 B 単位・control data（cut の時だけ max_vertices bit）・URB entry（32 KiB を越えれば refuse）の 64 B 単位）、prologue（`AND r(vue−2) = r1 & 0xFFFF`、頂点数・cut bits を 0）、`LOAD_VERTEX_INPUT`（producer の VUE の slot: Position 1、PointSize は header の dword 3、located は 2 + producer の varying の順位。定数の頂点はその handle の register、動的な頂点は vertex 0 の handle から CMP・SEL で channel ごとに選ぶ。SIMD8 URB read、rlen は読む component + 1、reply の register を bit のまま）、`LOAD_SYSTEM` PRIMITIVE_ID（r2）、terminate（`r127 = 書き込みの handle`、`r126 = 頂点数`、global 0・ex_mlen 1 の URB write + EOT）、describe（varying、`dispatch_grf_start`、GS の field）。
  - `compiler/compile.c`: `drv_i915_shader_compile_stage(ir, producer, out)`（`drv_i915_shader_compile` は producer NULL の wrapper。producer は GS だけ、VS の binary だけ）、state に producer（attempt をまたいで保つ）と GS の register・URB の field、`i915_compile_operands`（LOAD_VERTEX_INPUT の動的、EMIT/END の predicate）・`i915_compile_results`（EMIT/END は 0）、skip の解析（EMIT/END は region の中なら skip しない、region の後で garbage を読めば skip しない）、「VERTEX でなければ FRAGMENT」の分岐（payload_inputs、payload_end、VUE の staging と last_value_grf、prologue、terminate、describe、store_output の Position・varying・LAYER（header の dword 1 に UD の MOV））、GS の LOAD_INPUT は unsupported、EMIT/END は a3 まで unsupported。
  - `compiler/compiler.h`: binary に `vertices_in`・`output_topology`・`output_vertex_hwords`・`control_data_hwords`・`control_data_format`・`urb_entry_size`・`uses_primitive_id`・`writes_layer`、`drv_i915_shader_compile_stage` の宣言。
  - 試験: `compiler-shaders/noemit.geom`（EmitVertex の無い GS: 動的・定数の頂点の located 入力と gl_in、PrimitiveIDIn、Layer・PrimitiveID の書き込み）、`plan/ws031/tests/i915-vk-compile-test.c` の `test_geometry_reads`（producer 無し・location の無い producer は ENOTSUP、VS でない producer は EINVAL、points.geom は a3 まで ENOTSUP、binary の field、URB read 14 本・CMP・SENDC 無し・最後は global 0 の write + EOT、EU model に URB read と頂点数の write を足して 8 primitive を走らせ staged VUE・layer・PrimitiveID・頂点数 0・handle の mask を確かめる、scoreboard の検査）。`plan/ws031/tests/i915-vk-eudump.c` に geometry（GS の読む location を書く producer を作る）、`plan/ws031/tests/run-vk-gentool-test.sh` に `*.geom`（refuse-* と EmitVertex の 4 本は a3 まで飛ばす）、script の `mktemp` + `trap rm -rf` を `fresh_out build/tmp/ws031-gentool` に替えた（削除の規則、2026-10-06）。
  - design との差: `dispatch_grf_start` は push data の始まり（= 2 + PrimitiveID + vertices_in、Mesa の brw_compile_gs.cpp の `payload().num_regs` と VS の `COMPILE_PAYLOAD_GRF` と同じ）。design §4.4 の「+ push_regs」は誤り。URB read の rlen は design の 4 固定でなく読む component + 1（Mesa の emit_gs_input_load と同じ）。

| コマンド（a2） | 結果 |
| --- | --- |
| `python3 src/drivers/gpu/i915/tests/render/compiler-shaders/regenerate.py` | 成功、既存の `.spv` と kernel の `.inc` は不変 |
| `sh plan/ws031/tests/run-vk-host-tests.sh`（10 個） | plain・ASan/UBSan 全て PASS（scoreboard 5809 kernel） |
| `BRW_TOOLS=build/mesa-tools/build-asm/src/intel/compiler sh plan/ws031/tests/run-vk-gentool-test.sh` | PASS（noemit.geom: brw_disasm が `urb MsgDesc: offset N SIMD8 read mlen 1 rlen n` と最後の `SIMD8 write mlen 1 ex_mlen 1 EOT` を読み、再 assemble が一致） |
| `make -j16 BUILD=build/p1-k ZEDBSD_CONFIG=config/ci/config-amd64.mk build/p1-k/vmunix` | 成功 warning 0 |
| `git diff --check` | 問題無し |

- 2026-10-07 夜 増分 a3（EMIT・END・cut bits の write）:
  - `compiler/compile-geometry.inc`: EMIT（live = `count < max_vertices`、block の predicate があれば AND（design §14 B1 の guard）、f0.0 = live、staged VUE を 2 slot ずつ split send: src0 = handles と per-slot offset（mlen 2、bit 17）、src1 = slot（ex_mlen 4·n）、global = 2 + 2·control_hwords + slot、f0.0 で masked、後で f0.0 で count += 1・per-slot offset += 2·vertex_hwords（emit ごとの ADD の累計: design minor 3））、END（`bit = 1 << (count − 1)`、cut |= bit、predicate があれば f0.0 で masked）、cut bits の write（32 bit 以下は終わりに global 2・mlen 1・ex_mlen 1 で 1 回。32 を越えれば emit で count が 32 の倍数の時に前の dword を channel mask（bits 23:16）付き・data 4 回で書き（count ≠ 0 の channel だけ）cut を 0 に戻す、128 を越えれば per-slot offset（dword/4）も。終わりの write は count ≠ 0 の channel だけ: Mesa は 0 頂点でも書くが、その per-slot offset は entry の外を指し得るので守った）、URB write の descriptor を作る関数。split の位置は Mesa の brw_opt_split_sends が header の後で割るのと同じ（design §12 の 3 は Mesa の source で解消、実機は未）。
  - cut の reset は f0.0 で predicate する（f0.1 の predicate の AND と即値は Mesa の brw_asm が再 assemble で f0.0 と読む: disasm は f0.1 と読むので encoding は Mesa の disassembler と一致。実機で f0.1 の即値の命令を避けるため f0.0 に）。
  - 試験: `compiler-shaders/` に overflow.geom（max 3 で 5 emit）・emitif.geom（selection の中の emit/end、channel ごとの頂点数）・cut64.geom・cut160.geom・refuse-entry.geom（72 KiB の entry、compiler が ENOTSUP）。`i915-vk-compile-test.c` の EU model の URB write を GS の entry（per-slot offset・channel mask・未書き込みの印）に広げ、`test_geometry_emits` で points・adjacency・layers・varyings・overflow・emitif・cut64・cut160 を 8 primitive で走らせ、頂点数・cut bits の dword・各頂点の header（layer）・position・varying・PrimitiveID の varying を照合、vertex の write の descriptor（mlen 2、bit 17、global 4/6、predicated、EOT 無し）と cut の write（global 2、channel mask、128 bit 越えで per-slot）を数えた。gentool の skip（EmitVertex の 4 本）を外した。

| コマンド（a3） | 結果 |
| --- | --- |
| `python3 src/drivers/gpu/i915/tests/render/compiler-shaders/regenerate.py` | 成功、既存の `.spv` と kernel の `.inc` は不変 |
| `sh plan/ws031/tests/run-vk-host-tests.sh "compile spirv"`、`"pipe res"` | plain・ASan/UBSan PASS（a2 の 10 個の全体の run の後、compile.c の変更に関わる 4 つを再実行。cmd・lower・resdispatch・sync・eu・cmdbuf は a3 の後に未再実行） |
| `BRW_TOOLS=build/mesa-tools/build-asm/src/intel/compiler sh plan/ws031/tests/run-vk-gentool-test.sh` | PASS（GS 9 本を brw_disasm が受け、再 assemble が一致） |
| `make -j16 BUILD=build/p1-k ZEDBSD_CONFIG=config/ci/config-amd64.mk build/p1-k/vmunix` | 成功 warning 0 |
| `git diff --check` | 問題無し |

未実施: Mesa の compiler（brw_compile_gs）自身の GS の disasm との突き合わせ（Mesa の compiler を host で走らせる道具が無い。descriptor・offset・split の形は Mesa 25.0.7 の source（brw_fs_visitor.cpp の emit_urb_writes、brw_fs_nir.cpp の emit_gs_control_data_bits・emit_gs_vertex・emit_gs_end_primitive、brw_lower_logical_sends.cpp、brw_opt.cpp の split）で確かめた）。実機・QEMU は対象外（b5 で T1）。

### 再開の情報（2026-10-07 夜、ユーザーの決定で UCSI・DP alt mode を優先して中断）

- 済み: a1・a2・a3（host の試験は上）。a5（Invocations > 1）は後回し可のまま。
- 次: a4（diagnostic の文言、`I915_STAGE_COUNT` の網羅、spill と GS の共存の fixture（値を 90 個生かす GS）、`plan/ws075/tests/shader-survey` と `plan/ws068/tests/i915-shader-check` に geometry、WS068 の glsl150-geometry が通ること）→ p007b の b1・b2・b4・b5。
- 再開の前に: main の今を merge、`sh plan/ws031/tests/run-vk-host-tests.sh`（10 個全部）を一度流す。
- 未確認（実機で）: split send の vertex write、URB read の rlen < 4、f0.1 を predicate にした send（cut の write）、0 頂点の channel の扱い。
