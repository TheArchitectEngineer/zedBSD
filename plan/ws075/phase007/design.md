<!-- awesome-plan project=zedbsd record=ws075-p007-design -->

# ws075-p007 設計: i915 のネイティブ Vulkan 実行器の geometry shader、gl_Layer と layered の描画、PrimitiveID

設計の文書（code は変えない）。p007 は着手前に p007a（compiler）と p007b（実行器）に分ける（Q1 の ACK）。
正本の規則: [AGENTS.md](../../../AGENTS.md)、[coding-style.md](../../coding-style.md)、[i915-rebuild-rules.md](../../ws031/i915-rebuild-rules.md)、[ws.md](../ws.md)。

## 0. 出典とライセンス

| 出典 | 使い方 |
| --- | --- |
| 今の i915 の source（`src/drivers/gpu/i915/`、この worktree の HEAD） | 事実は file:行で引く。推測の所は「未確認」の表（§12）に置く |
| Mesa 25.0.7（`build/mesa-tools/mesa-25.0.7/`、MIT） | **事実（field の位置、payload の形、URB の規則、device の上限）だけ**を取り、code は写さない。下の表の file と sha256 を引く。zedBSD の code は Zlib のまま。`intel/genxml.h` に足す定数は「値だけ」の転記で、既存の header と同じく出典の file・sha256・行を説明に書く（i915-rebuild-rules.md §1） |
| SPIR-V の仕様（Khronos） | opcode・ExecutionMode・BuiltIn の番号。既存の spirv.c と同じく「SPIR-V spec, section 3.x」と注記 |
| Vulkan の仕様 | 上限の最小値（§8 の判断に使う） |

Mesa 25.0.7 の引用 file と sha256（`sha256sum` で 2026-10-07 に計算）:

| file | sha256 |
| --- | --- |
| src/intel/compiler/brw_compile_gs.cpp | d5221b1591fedff0ccae444f354a6ae64feb5f54c5ab3036b3bc716de0facbcb |
| src/intel/compiler/brw_fs_nir.cpp | 2e6116f7818ad378a4ea4d0724b237cec7477940aaa0e19735b32a0c6a4920f8 |
| src/intel/compiler/brw_fs_visitor.cpp | b20de8d3a514c81de03f2de615abcabfa99c1d84e4c5d6496491116dd67e64ec |
| src/intel/compiler/brw_fs_thread_payload.cpp | be8f6811896159b367bd7a5d9c4566ffdc63ec45b729d76ce1dabd30f8765a73 |
| src/intel/compiler/brw_fs.cpp | 4127343a3140b3a67d4bf0d38e170ef1b473bfba8d50b8ff6984852146205e49 |
| src/intel/compiler/brw_vue_map.c | a9f28770a2a11782619082cd69bf21708f795a4716941191a31eb8fef72b6e98 |
| src/intel/compiler/brw_nir.c | 7cb2e82e04b4cf81b76441eb9e71ede3835f16350171a04dc350a24a14fc32c2 |
| src/intel/compiler/brw_lower_logical_sends.cpp | 8cb9e28f322fd80a3e144815d116337bf7c2f0e7f8b63c04819a54659cda1428 |
| src/intel/compiler/brw_eu.h | 87a58fd1a719122d81539607f0fb72cf0483486ed8aa5540c387c217d337fc1b |
| src/intel/compiler/brw_eu_defines.h | 12a919edc56a75efe68afffa55ca42278a0cd489efddc8a0915919be756d7abc |
| src/intel/compiler/brw_prim.h | ed0f82247b0ce0743ff1c6dff47a95c1100626916085c6fcac608f630b9d35a8 |
| src/intel/common/intel_urb_config.c | e1e7ddc142f020e49c21c27085fab34b80f192afec19c03d3a4426cc8cddc414 |
| src/intel/dev/intel_device_info.c | 1a3c7c6d87c4a60af5d1003096b4add0a34c4a02afc12b40a378cc060540afd8 |
| src/intel/vulkan/genX_pipeline.c | 9bf244df1284531767529980909176d09b72db037b6fdaa7f5874d997ebf7545 |
| src/intel/vulkan/genX_gfx_state.c | df9f4e31dcdbeda750a04313ef0f6a6cdc96fb60bfe1cf3daaab0c74804612b2 |
| src/intel/genxml/gen120.xml | e2452c7dd2d19f9c506f487ce98e938984b6bdf8a3ea2504c64afdd4facd542e |
| src/intel/genxml/gen110.xml（gen120.xml が import。3DSTATE_GS はここ） | 6598e556ffedf4fe051c78af3bb08030a39af865c76e2784dba5374646fce35d |
| src/intel/genxml/gen80.xml（3DSTATE_CLIP・3DSTATE_SBE_SWIZ） | 2962677cf69dc947345fd88bd7010427900160eb7a7b076463e6e8d28772439d |
| src/intel/genxml/gen70.xml（3D_Prim_Topo_Type） | dd7c942fc12afd2defdc435997ccdb5b48841d344f40625dbb2b4e746eca09ef |
| src/intel/genxml/gen60.xml（SF_OUTPUT_ATTRIBUTE_DETAIL） | 30fac841448b4239bbaedf92a77424ec054629d28f266c7190b3f592f4b9185c |
| src/compiler/nir/nir_gs_count_vertices.c | bb33993ff92c29296393c1103358195a88859292e03c244250ecbb2d49ce72e2 |

注: 今の `intel/genxml.h` の冒頭（30〜46 行）は Mesa main（ab691a1）の gen120.xml・gen110.xml の sha256 を書いており、25.0.7 の file とは sha256 が違う。p007 で足す定数は 25.0.7 の sha256 と行を別の段落で注記する（既存の「25.0.7 から足した」の注記の形に倣う）。

## 1. 範囲と受け入れ

### 1.1 目標

glxtest `--gl32` の `geometry-points`・`geometry-modes`・`layered`（`userland/x11/glxtest/gl32.c` 101〜167 行の 3 つの geometry shader、624〜760 行の検査）が i915 のネイティブ実行器で通ること。host では `plan/ws031/tests/run-vk-host-tests.sh` の fixture（compile・pipe・cmdbuf・resdispatch）に GS の場面を足し、命令列と state の dword を検べる。実機（5330、passthrough）は T1 が流す（5330 が戻ってから）。

### 1.2 p007a（compiler、`src/drivers/gpu/i915/compiler/`）

- SPIR-V の ExecutionModel Geometry、execution mode（入力の primitive 5 種、出力の primitive 3 種、OutputVertices、Invocations は 1 だけ）。
- OpEmitVertex・OpEndPrimitive。`gl_in[]`（Block の配列、member の BuiltIn Position）、per-vertex の配列の入力（location 付き）、gl_PrimitiveIDIn、出力の gl_Layer・gl_PrimitiveID（と Position・PointSize・located の varying）。
- Gen12 の GS の thread の payload と URB の読み書き（入力は pull、出力は頂点ごとの per-slot offset の URB write、control data header の cut bit、最後の頂点の数の write と EOT）。
- draw が programming に要る値を `struct i915_shader_binary` に出す。

### 1.3 p007b（実行器、`src/drivers/gpu/i915/render/`）

- pipeline の 3 stage（decode・compile・interface の照合・instruction window）。
- 3DSTATE_GS、3DSTATE_URB_ALLOC_GS / VS の分配、3DSTATE_CONSTANT_GS と GS の push data、SBE と CLIP を GS の出力の VUE から、SF の deref block、adjacency の topology。
- layered の描画: VkFramebuffer の `layers`、render target の surface の view extent、depth の layer、layered の clear（pass の loadOp と vkCmdClearAttachments）、`maxFramebufferLayers`。
- PrimitiveID: GS の出力（varying）と、GS の無い pipeline の fragment の gl_PrimitiveID（SBE_SWIZ の PRIM_ID）。
- device の feature `geometryShader` と `maxGeometry*` の limit。

### 1.4 範囲外（この Phase では作らない）

tessellation、transform feedback の GS、複数 stream、`gl_ViewportIndex`（`maxViewports` は 1）、geometry の instancing（Invocations > 1、GL 4.0）、GS の sampler（texture の read。VS と同じく binding table を持たない）、push model の入力（性能、§10）、`gl_in[].gl_PointSize` 以外の gl_in の member（ClipDistance）、3D texture の layered（libglesv2 も持たない）、display/ の file。

### 1.5 受け入れ（Phase 全体）

1. host: `sh plan/ws031/tests/run-vk-host-tests.sh` の 10 個が plain・ASan/UBSan で PASS（既存の失敗 res・sync・cmdbuf の `test_blend_state` は p006 の記録のとおり既存。新しい GS の検査は PASS）。`plan/ws031/tests/run-vk-gentool-test.sh`（Mesa の brw_disasm）が GS の kernel を受ける。
2. build: `make -j16 BUILD=build/<W> ZEDBSD_CONFIG=config/ci/config-amd64.mk build/<W>/vmunix` が warning 0。
3. 実機（T1、5330 の passthrough、QEMU の Venus ではない）: glxtest `--gl32` の `geometry-points`・`geometry-modes`・`layered` の check の行が PASS（green の四角）、hang・device lost 無し。回帰: test-hw の vkx・vke1・vke2・vkc、zdesktop の capture、boot test。
4. 通らない機能は device の feature・limit として断り、記録する（ws.md の受け入れ 2）。

## 2. 今の形（事実、file:行）

### 2.1 compiler

| 事実 | 所 |
| --- | --- |
| stage は VERTEX・FRAGMENT・COMPUTE の 3 つ（`I915_STAGE_COUNT` 3） | `compiler/ir.h` 173〜178 |
| entry point は Vertex(0)・Fragment(4)・GLCompute(5) だけ、他は refuse「execution model other than Vertex / Fragment / GLCompute」 | `compiler/spirv.c` 307〜309、1314〜1340 |
| OpExecutionMode は compute の LocalSize だけ解釈、他は無視 | `spirv.c` 1301〜1304、`i915_spirv_declare_execution_mode` |
| OpCapability は無視（Geometry の capability は問題にならない） | `spirv.c` 1295〜1299 |
| Input の変数: location 付きだけ `PTR_INPUT`（`i915_spirv_add_io`）。location の無い Input（gl_in の Block の配列、gl_PrimitiveIDIn）は vertex / fragment / compute の builtin 以外「variable in a storage class that is not lowered」で refuse | `spirv.c` 1800〜1870 |
| 配列の interface 変数は「location を要素の数だけ取る」（`i915_spirv_add_io` 1940〜1956）。geometry の per-vertex の配列（外側の次元が頂点）はこの規則では誤る | `spirv.c` 1905〜1975 |
| access chain の dynamic index は 1 つまで、`MAX_DYNAMIC_ELEMENTS` 16 以下の配列。load は候補を全て読んで `SELECT` の連鎖で選ぶ（local の例 `i915_spirv_lower_load_local`） | `spirv.c` 2607〜2630、2930〜2960 |
| Input の load は scalar ごとに `I915_IR_LOAD_INPUT`（location・component） | `spirv.c` 3067〜3131 |
| Output の builtin は Position・PointSize だけ、他の builtin は refuse「store to an output that is neither located nor the Position or PointSize builtin」 | `spirv.c` 3628〜3647 |
| IR の location の予約: POSITION 0xFFFFFFFF、POINT_SIZE 0xFFFFFFFE、SHARED 0xFFFFFFFD、SECOND_COLOR 0xFFFFFFFC | `ir.h` 46〜52、115、121 |
| LOAD_SYSTEM の component は compute の 7 つ（`I915_IR_SYSTEM_COUNT` 7） | `ir.h` 78〜85 |
| 入力の location の予約: VERTEX_INDEX 64、INSTANCE_INDEX 65、FRONT_FACING 66、FRAG_COORD 67、POINT_COORD 68 | `compiler/compiler.h` 40〜65 |
| VS の register の約束: r0 header、r1 URB handle、r2.. push data、attribute 4 register ずつ。VUE は `[header][position][varyings 昇順]` を r(127 − 4·slots)..r126 に stage、最後の 2 slot の write が EOT（handle を r127 に写す）。header の dword 3 が point size（`COMPILE_VUE_POINT_SIZE`）、header は整数の 0 で埋める | `compiler/compile.c` 49〜103、165〜169、4528〜4537、4690〜4735 |
| URB write の descriptor `COMPILE_DESC_URB_WRITE(slot)` = 0x02080007 \| slot<<4（mlen 1、header、SIMD8 write opcode 7、global offset = slot）。data は split send の第 2 run（`COMPILE_EX_MLEN`） | `compile.c` 248〜253、266、4712〜4734 |
| 値の register は r16〜r95（payload が r15 を越えればその後ろ）。足りなければ spill、VS は gathered VUE | `compile.c` 144〜149、4352〜4395 |
| `i915_compile_interface` が入力・varying を集め payload の終わりを決める。`i915_compile_terminate` が stage ごとに終わりを出す | `compile.c` 4279〜4395、4542〜4615 |
| stage ごとの別 file の前例: `compile-compute.inc`（compile.c 4956 で include）、`spirv-compute.inc`（spirv.c 9447 で include） | `compiler/compile-compute.inc` 1〜40 |
| binary の field は VS/FS/CS の分だけ（`varying_locations`、`writes_point_size`、`dispatch_grf_start`、`scratch_bytes`、…） | `compiler/compiler.h` 117〜225 |
| EU encoder: `drv_i915_eu_send`（split send、ex_descriptor、EOT）、`_send_masked`（flag で predicate）、`_send_all`（NoMask）、IF/ENDIF/WHILE、`alu2_masked`、`cmp`、`select`、`flag_load` | `compiler/eu.h` 170〜200 |

### 2.2 実行器

| 事実 | 所 |
| --- | --- |
| pipeline は vertex と fragment の 2 stage。他の stage は「XXX pipeline stage 0x%x is not run」の log で捨てる | `render/pipeline.c` 596〜612、`render/gfx.h` 404〜410 |
| prepare は VS・FS を compile し、FS の入力の location が VS の varying にあるか照合（`i915_pipeline_kernels_fit`）。VS は sampler を持てない（binding table が無い） | `render/pipeline-prepare.c` 52〜115、370〜432 |
| instruction window: VS 0x0000、PS 0x4000、window 0xc000、32 window（kernel object 1.5 MiB）。`drv_i915_gfx_window` が vs/ps の code を写す | `render/heap.h` 93〜102、`render/draw.c` 246〜306 |
| draw ごとの batch: URB（`drv_i915_gfx_emit_urb`）、constants（VS と PS だけ、HS/DS/GS は空）、VS、HS/TE/DS を 0、STREAMOUT、**3DSTATE_GS を 0**、PRIMITIVE_REPLICATION 0、raster、PS、blend、depth、3DPRIMITIVE | `render/draw.c` 896〜1048（987〜1008）、`render/state.c` 1221〜1312 |
| URB: push constant 32 KiB を VS と PS に 16 KiB ずつ、VS は chunk 4 から `GEN12_URB_VS_BYTES`（3576×64）分の entry、HS/DS/GS は entry 0・start 5。entry 数は 8 の倍数に切り下げ | `state.c` 1221〜1258、`intel/genxml.h` 322〜330 |
| CLIP dword 3 = (1<<17) \| (2047<<6): Force Zero RTA Index Enable（bit 5）は 0 のまま → VUE header の layer が RTAI に使われる前提の値（VS は header を 0 で埋めるので layer 0） | `state.c` 1366〜1371 |
| SF dword 2 の deref block size は `GEN12_URB_DEREF_BLOCK_SIZE_32`（0） | `state.c` 1385 |
| topology: point/line/strip/tri/strip/fan だけ、adjacency と patch は 0 → `drv_i915_gfx_emit_vertex_input` が ENOTSUP「XXX unimplemented path: primitive topology」 | `state.c` 920〜944、1125〜1134 |
| SBE: 属性数 = varyings（GS が無いので VS の）、read offset 1（slot 2 から）、SBE_SWIZ は `i915_state_input_slot` で routing。PrimitiveID の override は無い | `state.c` 1594〜1655 |
| render target の surface: `i915_state_target_range` が **layer_count 1**（view の base_layer から 1 層）→ Render Target View Extent 0。`i915_image_surface_write` 自体は range の layer_count を view extent に書ける（2088〜2160） | `state.c` 2335〜2347、2088〜2160 |
| depth buffer は view の base_layer から image の全 slice（Depth = slices − 1） | `state.c` 1501〜1524 |
| framebuffer は width・height・views だけ保持、`layers` を捨てる | `render/render-pass.c` 161〜170、`gfx.h` 373〜381 |
| pass begin の clear と vkCmdClearAttachments は view の **最初の layer だけ**（「XXX: a view of several layers is written at its first layer only」、「The layers are not acted on」）。clear_attachment の op に layer の field が無い | `render/command.c` 1290、2157〜2178、1340〜1360、`gfx.h` 752〜765 |
| features: vertexPipelineStoresAndAtomics・logicOp・shaderInt16 だけ。`maxFramebufferLayers` 1、`maxGeometry*` は 0（未設定）、`maxViewports` 1 | `render/instance.c` 440〜447、387、364 |
| scratch の stage は VERTEX・PIXEL・COMPUTE の 3 つ（`scratch_per_thread[3]`） | `render/draw.h` 100〜103、`draw.c` 757〜865 |
| host の fixture の stub は `plan/ws031/tests/i915-vk-render-stubs.inc`（draw.c・blit.c を link しない） | `plan/ws031/tests/run-vk-host-tests.sh` 22〜35 |

### 2.3 libglesv2（GL 3.2 の経路が実行器に投げる物。WS068 p032・p033 で Venus では PASS 済み）

| 事実 | 所 |
| --- | --- |
| GS の SPIR-V: Capability Geometry、execution mode は InputPoints+（GLSL の primitive の順）、OutputPoints / OutputLineStrip / OutputTriangleStrip、OutputVertices、**Invocations 1** | `userland/desktop/libglesv2/glsl/emit.c` 1456〜1492 |
| gl_in は Block の配列（member の BuiltIn Position だけ、location 無し）。gl_PrimitiveIDIn / gl_PrimitiveID は BuiltIn PrimitiveId(7)、gl_Layer は BuiltIn Layer(9) の独立の変数。fragment の gl_PrimitiveID は Flat | `emit.c` 1331〜1365、1433〜1450、`glsl/emit.h` 178〜179 |
| gl_Position の書き換え（y の反転・z の範囲）は GS があれば GS の各 OpEmitVertex の前に入る（VS は書き換えない） | `libglesv2/spirv.c` 255〜300 |
| pipeline は VS・FS・GS の 3 stage（`VK_SHADER_STAGE_GEOMETRY_BIT`）、GS は FBO 用と窓用の 2 module | `libglesv2/draw.c` 2835〜2856、`program.c` 3127〜3133 |
| 描画先の clear は `vkCmdClearAttachments` で `rect.layerCount = target.layers`（layered の FBO は全 layer） | `draw.c` 555〜573 |
| layered の framebuffer: 2D 配列の level の全 layer / cube の 6 面の view、`VkFramebufferCreateInfo.layers` = 最少の layer 数 | `framebuffer.c` 1103〜1110、2393〜2394、2562〜2574 |
| device の `geometryShader` feature は読まない（GL 3.2 は常に名乗る） | `gles.c`・`program.c`・`draw.c` に `geometryShader` の参照無し（grep） |
| glxtest の 3 つの GS: points→triangle_strip 4 頂点（uniform の色）、lines_adjacency→triangle_strip 4 頂点（`gl_in.length()`、`gl_in[3]`）、triangles→triangle_strip 6 頂点（**2 重の loop、動的な `gl_in[i]`、`gl_Layer = layer`、EndPrimitive を loop の中で**） | `userland/x11/glxtest/gl32.c` 101〜167 |

## 3. ハードウェアの事実（Mesa 25.0.7 から。値だけ）

### 3.1 GS の thread の payload（SIMD8、1 channel = 1 入力 primitive）

`brw_fs_thread_payload.cpp` 103〜156（`gs_thread_payload`）:

| register | 内容 |
| --- | --- |
| r0 | thread header（scratch の dword 3・5 は VS と同じ） |
| r1 | 出力の URB handle（channel ごと。Gen12 は `& 0xFFFF`）。bits 31:27 は instance ID |
| r2（Include Primitive ID のとき） | 入力 primitive の ID（channel ごとの dword） |
| 次の vertices_in 個 | ICP handle: 頂点 v の入力 VUE の handle が register (base + v) の channel c の dword（Invocations 1 のとき。`emit_gs_input_load` 2741〜2745: 定数の頂点は register を選ぶ、動的は indirect） |
| その後 | push constant（`brw_fs.cpp` 1082: curb は payload の直後、URB data はその後 1115〜1117） |
| その後 | push model の入力（Vertex URB Entry Read Length の HWord × vertices_in。Mesa は 24 register を越えると read length を減らし pull にする。146〜155） |

Mesa は「常に VUE handle を含める」（134 行: pull を常に使えるように）。

### 3.2 GS の URB entry（出力）と頂点の write

| 事実 | 出典 |
| --- | --- |
| 出力 VUE の slot: 0 = header（dword 0 予約、**dword 1 = Layer、dword 2 = Viewport、dword 3 = PointSize**）、1 = Position、2.. = varying。Layer・Viewport は header に入り slot を取らない | `brw_vue_map.c` 82〜86・116〜117、`brw_nir.c` 541〜549、`brw_fs_visitor.cpp` 157〜187（header の MOV） |
| 出力頂点の大きさ: `output_vertex_size_hwords = ALIGN(num_slots × 16, 32) / 32`（32 B の倍数。最大 62×16 = 992 B）。3DSTATE_GS の Output Vertex Size = hwords × 2 − 1（16 B 単位 − 1） | `brw_compile_gs.cpp` 224〜275、`genX_pipeline.c` 1469 |
| URB entry の並び（static でない頂点数のとき）: **先頭 1 HWord（32 B）に頂点の数**、次に control data header（hwords 分）、その後に頂点が output_vertex_size ずつ。entry size = ALIGN(合計, 64)/64（64 B 単位）。最大 32 KiB（`GFX7_MAX_GS_URB_ENTRY_SIZE_BYTES` = 512×64）、越えると compile 失敗 | `brw_compile_gs.cpp` 307〜329、`brw_eu_defines.h` 1498 |
| 頂点 n の write: global offset = 2 × control_hwords + 2（OWord 単位、dynamic のとき）、**per-slot offset = n × output_vertex_size_hwords × 2**（OWord）。slot を 2 つ（8 register）ずつ、GS の write は EOT にしない | `brw_fs_visitor.cpp` 69〜101、233〜258 |
| control data: 出力が points なら format SID で bits/vertex 0（stream 0 だけ）、それ以外は format CUT で bits/vertex = EndPrimitive を使えば 1、使わなければ 0。header bits = vertices_out × bits/vertex、hwords = ALIGN(bits, 256)/256 | `brw_compile_gs.cpp` 189〜222 |
| EndPrimitive: `cut_bits \|= 1 << ((vertex_count − 1) & 31)`（SHL は src1 の下位 5 bit だけを見る） | `brw_fs_nir.cpp` 2351〜2408 |
| header ≤ 32 bit: thread の最後に 1 回、global offset 2（OWord）、data 1 register（per-slot・channel mask 無し）。> 32 bit: 32 bit 溜まるごとに per-slot offset（dword_index/4）と channel mask（1 << (dword_index % 4) を bits 23:16）付きで data を 4 回複製して write し、cut_bits を 0 に戻す。≤ 128 bit なら per-slot 無し | `brw_fs_nir.cpp` 2410〜2553、2622〜2678 |
| thread の終わり（dynamic）: data = 最終の頂点数（1 register）、global offset 0、EOT | `brw_compile_gs.cpp` 35〜70 |
| 頂点数が static になるのは全経路で定数のとき（loop・分岐で変わると −1） | `nir_gs_count_vertices.c` 56〜110 |
| EmitVertex の後の出力の値は未定義（Mesa は live range のために register を作り直すだけ） | `brw_fs_nir.cpp` 3483〜3488 |

### 3.3 URB の message

`brw_eu.h` 316〜327（`brw_urb_desc`）、`brw_eu_defines.h` 1476〜1477、`brw_lower_logical_sends.cpp` 35〜75・137〜191:

| 項目 | 値 |
| --- | --- |
| descriptor | bits 3:0 opcode（SIMD8 write **7**、SIMD8 read **8**）、bits 14:4 global offset（OWord）、bit 15 channel mask present、bit 17 per-slot offset present。mlen・rlen・header present は共通の message descriptor（今の `COMPILE_DESC_URB_WRITE` が mlen 1・header を 0x02080000 に持つ） |
| write の payload | handle（1 register）、[per-slot offsets（1 register、channel ごとの OWord offset）]、[channel mask（1 register）]、data（1〜8 register）。Mesa は 1 つの連続 payload（mlen = 全部）にする。今の VS は handle を src0、data を src1（ex_mlen）の split send で書いていて動いている |
| read の payload・reply | handle（1 register）[、per-slot offsets]、rlen = 読む dword 数 × 1 register（`size_written`）: slot 1 つ（vec4）は 4 register、register c が component c | 

### 3.4 3DSTATE_GS（gen110.xml 623〜688、10 dword）

| dword | bit | field | 値（anv `genX_pipeline.c` 1451〜1489） |
| --- | --- | --- | --- |
| 1〜2 | 38〜95 | Kernel Start Pointer | GS kernel の window 内 offset（VS と同じ形、dword 1 は offset そのもの） |
| 3 | 0〜5 | Expected Vertex Count | vertices_in |
| 3 | 16 | Floating Point Mode | 0（IEEE） |
| 3 | 18〜25 | Binding Table Entry Count | 0 |
| 3 | 27〜29 | Sampler Count | 0 |
| 3 | 30 / 31 | Vector Mask Enable / Single Program Flow | 0 / 0 |
| 4 | 0〜3 | Per-Thread Scratch Space | VS と同じ符号化（`i915_state_scratch`） |
| 4〜5 | 10〜63 | Scratch Space Base Pointer | 同上 |
| 6 | 0〜3 と 29〜30 | Dispatch GRF Start Register For URB Data（[3:0] と [5:4]） | 固定 payload + push_regs（§4.4） |
| 6 | 4〜9 | Vertex URB Entry Read Offset | 0 |
| 6 | 10 | Include Vertex Handles | 1 |
| 6 | 11〜16 | Vertex URB Entry Read Length | 0（pull だけ、§4.4） |
| 6 | 17〜22 | Output Topology | 3D_Prim_Topo_Type: POINTLIST 1、LINESTRIP 3、TRISTRIP 5 |
| 6 | 23〜28 | Output Vertex Size | hwords × 2 − 1 |
| 7 | 0 | Enable | 1 |
| 7 | 1 | Discard Adjacency | 0 |
| 7 | 2 / 3 | Reorder Mode / Hint | 0 |
| 7 | 4 | Include Primitive ID | kernel が PrimitiveId を読むとき 1 |
| 7 | 5〜9 | Invocations Increment Value | 0 |
| 7 | 10 | Statistics Enable | 1 |
| 7 | 11〜12 | Dispatch Mode | SIMD8 = **3** |
| 7 | 13〜14 | Default Stream Id | 0 |
| 7 | 15〜19 | Instance Control | invocations − 1 = 0 |
| 7 | 20〜23 | Control Data Header Size | hwords |
| 8 | 0〜8 | Maximum Number of Threads | max_gs_threads − 1 = **335**（Gen12 `GFX12_HW_INFO` max_gs_threads 336、`intel_device_info.c` 961） |
| 8 | 16〜26 / 30 | Static Output Vertex Count / Static Output | 0 / 0（常に dynamic、§4.4） |
| 8 | 31 | Control Data Format | CUT 0 / SID 1 |
| 9 | 0〜7 / 8〜15 | Cull / Clip Test Enable Bitmask | 0 |
| 9 | 16〜20 / 21〜26 | Vertex URB Entry Output Length / Output Read Offset | 0（anv は設定しない。VS の dword 8 と同じ扱い） |

### 3.5 URB の分配（`intel_urb_config.c` 63〜295、gen120.xml 821〜856）

- 3DSTATE_URB_ALLOC_GS（0x785B、3 dword）: dword 1 bits 0〜9 entry size（64 B 単位 − 1）、bits 10〜17 start slice0、bits 21〜28 start slice N（chunk = 8 KiB 単位）；dword 2 bits 0〜15 entries slice0、16〜31 entries slice N。VS と同じ形（今の `drv_i915_gfx_emit_urb`）。
- GS の最小 entry は 2（「DUAL_OBJECT のため」、133 行）。entry size < 9 のとき entry 数は 8 の倍数（103〜113 行）。Gen12 の GS の最大 entry 1548、VS 3576（`intel_device_info.c` 973〜976）。
- 配置は pipeline の順（push constant、VS、HS、DS、GS）、無効 stage は start を先頭に（246〜255 行）。
- SF の URB deref block size: Gen12 で **GS が最後の stage なら PER_POLY**（257〜290 行）。
- Gen12 の L3 config の URB は 32 KiB 単位の表（`intel_l3_config.c` 143〜147）で、compute engine のため bank ごとに 4 KiB 引く（`intel_urb_config.c` 73〜90）。今の i915 は総量を直接持たず「push 32 KiB + 3576 entry × 64 B」を VS に与えて動いている（§2.2）。p007 はこの範囲の中で VS と GS を分け、総量は増やさない。

### 3.6 その他の state

| 事実 | 出典 |
| --- | --- |
| 3DSTATE_CLIP dword 3 bit 5（start 101）Force Zero RTA Index Enable: 最後の vertex stage が Layer を書かなければ 1（Vulkan 1.0.45「Layer を書かなければ layer 0」） | gen80.xml 581、`genX_pipeline.c` 878〜886 |
| 3D_Prim_Topo_Type: LINELIST_ADJ 9、LINESTRIP_ADJ 10、TRILIST_ADJ 11、TRISTRIP_ADJ 12。anv は Vulkan の WITH_ADJACENCY をそのまま写す | gen70.xml、`brw_prim.h` 34〜37、`genX_gfx_state.c` 128〜138 |
| 3DSTATE_SBE dword 1: bits 0〜4 Primitive ID Override Attribute Select、bits 16〜19 Component Override X/Y/Z/W。anv は前の stage が PrimitiveID を書かず FS が読むときに設定。SBE_SWIZ の attribute（16 bit）: bits 0〜4 Source Attribute、9〜10 Constant Source（PRIM_ID = **3**）、12〜15 Component Override X..W。anv は VUE に無い attribute（slot −1）に PRIM_ID の constant source を使う | gen80.xml 1147〜1153、gen60.xml 287〜306、`genX_pipeline.c` 704〜754 |
| SBE の read offset は VS でも GS でも slot 2 から（Mesa は 2 slot 引く） | `genX_pipeline.c` 722〜742 |
| RENDER_SURFACE_STATE の render target: Minimum Array Element = view の base layer、Render Target View Extent = layer 数 − 1（今の `i915_image_surface_write` が range から書く） | `state.c` 2156〜2160 |

## 4. p007a: compiler の設計

### 4.1 責務と境界

compiler は device に触れず、SPIR-V → IR → EU の語と「draw が programming に要る値」を返す（`compiler.h` 8〜15 の約束のまま）。GS で新しく要る境界:

- GS の入力 slot は **前の stage（VS）の VUE の並び**で決まる（§3.1 の pull の global offset）。compiler 単体では知れないので、pipeline の prepare が VS の binary の `varying_locations` を GS の compile に渡す（§4.4 の interface）。これは Mesa が link で VS の出力と GS の入力を揃えるのと同じ役を、i915 では prepare が担う。
- 3 stage の照合（VS の出力 ⊇ GS の入力、GS の出力 ⊇ FS の入力）は p007b の prepare（§5.1）。

file の配置（`compile-compute.inc`・`spirv-compute.inc` の前例、i915-rebuild-rules §1「使う header だけ include」、coding-style §2 の順）:

| file | 内容 |
| --- | --- |
| `compiler/ir.h` | stage `I915_STAGE_GEOMETRY`、op `EMIT_VERTEX`・`END_PRIMITIVE`・`LOAD_VERTEX_INPUT`、location `LAYER`、system `PRIMITIVE_ID`、`struct i915_shader_ir` の geometry の field |
| `compiler/compiler.h` | location `I915_SHADER_LOCATION_PRIMITIVE_ID`、binary の geometry の field、`drv_i915_shader_compile_stage()` |
| `compiler/spirv-geometry.inc`（新、spirv.c の末尾で include） | entry point・execution mode・gl_in・per-vertex 配列・PrimitiveIdIn・Layer/PrimitiveId の出力・OpEmitVertex/OpEndPrimitive の parse |
| `compiler/compile-geometry.inc`（新、compile.c の末尾で include） | GS の payload・interface・prologue・入力の read・emit/end/terminate |
| `compiler/spirv.c`・`compile.c` | 分岐の追加だけ（entry point、変数の宣言、access chain、load、store、interface、prologue、terminate、instruction の dispatch） |

### 4.2 IR の拡張（`ir.h`）

| 追加 | 意味 |
| --- | --- |
| `I915_STAGE_GEOMETRY = 3`、`I915_STAGE_COUNT = 4` | stage。既存の `I915_STAGE_*` の値は変えない |
| `I915_IR_LOAD_VERTEX_INPUT` | dst = 入力 primitive の頂点の input `location`・component `component`。頂点は `immediate` が bit 31 を持たなければ `immediate` の番号（定数）、持てば src[0] の整数の値（動的、0〜vertices_in−1 の外は未定義）。geometry だけ |
| `I915_IR_EMIT_VERTEX` | 今の出力の値で 1 頂点を出す（gl_Position・gl_Layer・PointSize・varying）。`component` 1 のとき src[0] の Boolean の channel だけ（selection の中）。geometry だけ |
| `I915_IR_END_PRIMITIVE` | 今の strip を終える。`component` 1 のとき src[0] の predicate。出力が points なら parser が出さない |
| `I915_IR_LOCATION_LAYER 0xFFFFFFFBU` | STORE_OUTPUT の location: VUE header の dword 1（整数）。geometry だけ |
| `I915_IR_SYSTEM_PRIMITIVE_ID 7U`、`I915_IR_SYSTEM_COUNT 8U` | LOAD_SYSTEM の component: gl_PrimitiveIDIn（payload の r2） |
| `struct i915_shader_ir` に `uint32_t vertices_in; uint32_t output_topology; uint32_t max_vertices; uint32_t uses_end_primitive;` | execution mode から。`output_topology` は 3D_Prim_Topo_Type の値（1/3/5） |

`compiler.h`: `#define I915_SHADER_LOCATION_PRIMITIVE_ID 69U`（GS の出力の varying と FS の入力の予約 location。64〜68 の次）。

### 4.3 SPIR-V の parse（`spirv-geometry.inc` と spirv.c の分岐）

番号（SPIR-V spec 3.3・3.6・3.21）: ExecutionModel Geometry 3；ExecutionMode Invocations 0、InputPoints 19、InputLines 20、InputLinesAdjacency 21、Triangles 22、InputTrianglesAdjacency 23、OutputVertices 26、OutputPoints 27、OutputLineStrip 28、OutputTriangleStrip 29；BuiltIn PrimitiveId 7、InvocationId 8、Layer 9、ViewportIndex 10；OpEmitVertex 218、OpEndPrimitive 219。

| 入力 | 扱い |
| --- | --- |
| OpEntryPoint Geometry | `ir->stage = I915_STAGE_GEOMETRY`（`i915_spirv_declare_entry_point` に分岐） |
| OpExecutionMode | 入力の primitive → `vertices_in` 1/2/4/3/6；出力 → `output_topology`；OutputVertices → `max_vertices`（1〜256、0 は refuse）；Invocations は 1 だけ（他は refuse「geometry invocations other than one」）。parse の終わりに入力・出力・OutputVertices がそろっていなければ refuse |
| Input の変数、location 無し、pointee が「Block の struct の配列」（gl_in） | `PTR_INPUT` の新種 `PTR_VERTEX_BLOCK`: 配列の長さは `vertices_in`（0 = 大きさ無しも可、違えば refuse）。member の BuiltIn Position → slot 1、PointSize → slot 0 の component 3、他の member は refuse |
| Input の変数、location 付き、pointee が配列 | geometry では**外側の配列が頂点**: `i915_spirv_add_io` は要素の型で location を登録（配列の長さは vertices_in か 0）。`ptr_kind` は `PTR_INPUT` に「頂点を選ぶ」印（record に `vertex` と `vertex_dynamic`） |
| Input の BuiltIn PrimitiveId（gl_PrimitiveIDIn） | `PTR_SYSTEM`（compute の `i915_spirv_declare_system` と同じ道）で `I915_IR_SYSTEM_PRIMITIVE_ID`。`ir` に `uses_primitive_id`（binary へ） |
| Input の BuiltIn InvocationId | refuse（Invocations 1 なので定数 0 にしてもよいが、使う shader は無い。refuse を既定、§8） |
| Output の BuiltIn Layer | `STORE_OUTPUT` location `I915_IR_LOCATION_LAYER` component 0（整数 bit のまま）。`ir` に `writes_layer` |
| Output の BuiltIn PrimitiveId | 通常の located output と同じ経路で location `I915_SHADER_LOCATION_PRIMITIVE_ID` component 0（整数 bit のまま、varying の slot）。fragment の BuiltIn PrimitiveId は入力の同じ location（Flat） |
| Output の BuiltIn ViewportIndex | refuse（`maxViewports` 1） |
| OpAccessChain on gl_in / per-vertex 配列 | 最初の index が頂点: 定数なら `record->vertex`、動的なら `record->vertex_dynamic = 値`（`MAX_DYNAMIC_ELEMENTS` 16 ≥ 6 を満たす）。残りの index は今の `i915_spirv_chain_scalars`（member → slot、component） |
| OpLoad from そのような pointer | scalar ごとに `LOAD_VERTEX_INPUT`（location/component は今の `i915_spirv_lower_load_input` と同じ、`immediate` = 頂点 or bit 31 \| 0 と src[0] = 動的 index）。geometry の入力は raw（補間しない）なので型の制限は VS と同じ（bit のまま） |
| `gl_in.length()`（OpArrayLength ではなく定数。GLSL compiler は定数に畳む: p032 の試験「gl_in.length()」が通っている） | 何もしない。OpArrayLength が来たら refuse |
| OpEmitVertex | `EMIT_VERTEX`、今の block の predicate があれば `component` 1・src[0]（`I915_IR_STORE_STORAGE` の形） |
| OpEndPrimitive | 出力が points でなければ `END_PRIMITIVE`（同じ predicate の形）、`uses_end_primitive = 1`。points なら無視 |
| OpReturn 以後 | 何もしない（thread の終わりは codegen）。`gles_spirv_position` の書き換えは OpEmitVertex の前に Position の store を入れる（§2.3）ので、EMIT が最後に store された値を使う形で合う |

selection の中の EMIT: parser は if-convert するので、両側が走り predicate だけ違う。EMIT の URB write と頂点数の増加を predicate で mask すれば正しい（§4.4）。loop の中の EMIT: loop の変数の MOVE と同じく「program の順の最後の store」が staged の値なので、loop の各回で staged register の値を書くだけでよい。

### 4.4 code generation（`compile-geometry.inc` と compile.c の分岐）

register の約束（compile.c 49〜103 の表に足す）:

| 範囲 | 内容 |
| --- | --- |
| r0 | thread header |
| r1 | 出力 URB handle（channel ごと） |
| r2（`uses_primitive_id` のとき） | primitive ID |
| 次の vertices_in 個（`COMPILE_GS_HANDLE_GRF` = 2 + uses_primitive_id） | 入力頂点 v の ICP handle（register base + v） |
| その後 push_regs 個 | push data（push constant、block、storage の address）: `payload_inputs` = base + vertices_in + push_regs は今の `i915_compile_instruction` の規則（1732〜1737）に分岐を足す |
| Dispatch GRF Start | base + vertices_in + push_regs（push model の入力は無いので何も来ない。VS の `vs_grf_start` = 2 + push_regs の規則と同じ考え。Mesa は curb の後に URB data を置く（`brw_fs.cpp` 1115〜1117）） |
| 値 | r16（payload が越えればその後ろ）〜 staged VUE の手前 |
| `COMPILE_GS_HEADER_GRF` = vue_grf − 2 | 頂点の write の header: r(vue_grf−2) = r1 の写し（prologue で 1 回、NoMask）、r(vue_grf−1) = per-slot offset（emit ごとに計算） |
| staged VUE | VS と同じ r(127 − 4·slots)..r126: slot 0 header（dword 0 = 0、dword 1 = layer、dword 2 = 0、dword 3 = point size）、slot 1 = position、2.. = varying 昇順 |
| r127 | 最後の write（頂点数、EOT）の handle の写し。r126 を頂点数の data に使う（staged VUE はもう要らない） |
| `vertex_count`・`cut_bits` | 値の register から 2 つ固定で取る（spill させない、IR の値ではない）。prologue で 0 |

入力の read（pull、`LOAD_VERTEX_INPUT`）:

1. handle: 定数の頂点 v は register (HANDLE_GRF + v) をそのまま src0 に。動的なら temporary に handle[0] を MOV し、v = 1..vertices_in−1 について `CMP f0.0 index == v` → `SEL temporary = handle[v]`（最大 5 組）。
2. slot: location の slot は「producer の VUE map」から: Position は 1、PointSize は 0（component 3）、located は `2 + rank`（producer の `varying_locations` の中の順位）。無ければ compile は ENOTSUP（prepare が先に照合するので普通は起きない）。
3. `send`（SFID URB 6）: src0 = handle、descriptor = opcode 8 \| slot << 4 \| header present \| mlen 1 \| rlen 4（reply は 4 register、register c = component c。§3.3）。読むのが 1 component でも 4 register を reply に取る（temporaries 4 つ）。同じ頂点・slot の 4 component を 1 message にまとめるのは後の最適化（§10）。
4. dst = reply の component の register を MOV（bit のまま）。

emit（`EMIT_VERTEX`）:

1. per-slot offset: r(vue_grf−1) = vertex_count × (output_vertex_size_hwords × 2)（`mul` の UW の即値。§12 の確認事項）。
2. staged VUE の slot を 2 つずつ write: src0 = r(vue_grf−2)（handle と offset の 2 register）、src1 = staged slot（ex_mlen 4·count）、descriptor = 今の `COMPILE_DESC_URB_WRITE(global)` に **mlen 2・bit 17（per-slot present）**を足した物、global = 2 + 2 × control_hwords + first slot（OWord）。predicate があれば `drv_i915_eu_send_masked`（f0.0 を src[0] の Boolean から `drv_i915_eu_flag_load` 相当の CMP で作る）。
3. vertex_count += 1（predicate があれば `alu2_masked`）。
4. staged の値はそのまま（GLSL は未定義とするので、残しても誤りではない）。

end（`END_PRIMITIVE`、cut format のとき）: `temp = vertex_count + 0xffffffff`、`mask = 1 << temp`、`cut_bits |= mask`（predicate があれば masked）。header が > 32 bit の形は増分 a5（§4.6）までは compile 時に refuse（OutputVertices > 32 かつ uses_end_primitive）。

terminate（`i915_compile_terminate` の geometry の分岐）:

1. cut bits があれば（control_hwords > 0）: src0 = r1、src1 = cut_bits（ex_mlen 1）、descriptor = write \| global 2（OWord）、EOT 無し。
2. `MOV r127 = r1`、`MOV r126 = vertex_count`、`send` src0 = r127、src1 = r126（ex_mlen 1）、global 0、**EOT**。（static な頂点数を使わず常に dynamic: 頂点数は loop や分岐で変わる glxtest の shader に要り、static の場合も同じ経路で正しい。Mesa の dynamic の経路と同じ並び。）

interface（`i915_compile_interface` の geometry の分岐）:

- varying は VS と同じ規則で `varyings` に集める（Position・LAYER・POINT_SIZE を除く STORE_OUTPUT の location、昇順。`I915_SHADER_LOCATION_PRIMITIVE_ID` 69 は user の 0〜63 の後に来る）。
- `output_vertex_size_hwords = ALIGN((2 + varying_count) × 16, 32) / 32`、`control_bits_per_vertex`（points なら 0、それ以外は uses_end_primitive）、`control_hwords = ALIGN(max_vertices × bits, 256)/256`、`urb_entry_size = ALIGN(32 + 32·control_hwords + 32·hwords·max_vertices, 64)/64`；32 KiB を越えれば unsupported（Mesa と同じ）。
- payload の終わりは base + vertices_in + push_regs。staged VUE の下に header の 2 register を取るので `last_value_grf` は vue_grf − 3 まで。vertex_count・cut_bits の 2 register を値の最初の 2 つから取る（`first_value_grf += 2`）。
- 足りなければ `out_of_registers`（spill の victim を選ぶ今の仕組みに任せる。gathered VUE は GS では使わない → `late_vue` を立てずに refuse せず spill だけで続ける。それでも足りなければ unsupported）。

binary（`compiler.h` に足す field、draw が読む）:

| field | 意味 |
| --- | --- |
| `vertices_in` | Expected Vertex Count |
| `output_topology` | 3DSTATE_GS Output Topology（1/3/5） |
| `output_vertex_hwords` | Output Vertex Size = ×2 − 1 |
| `control_data_hwords`、`control_data_format` | Control Data Header Size、Control Data Format（CUT 0 / SID 1） |
| `urb_entry_size` | 3DSTATE_URB_ALLOC_GS の entry size（64 B 単位） |
| `uses_primitive_id` | Include Primitive ID |
| `writes_layer` | CLIP の Force Zero RTA Index Enable を 0 に |
| 既存 `dispatch_grf_start`・`push_regs`・`blocks`・`varying_count`・`varying_locations`・`writes_point_size`・`scratch_bytes`・`input_count`・`input_locations` | VS と同じ意味（`input_locations` は per-vertex の location、prepare の照合用） |

compile の口: `int drv_i915_shader_compile_stage(const struct i915_shader_ir *ir, const struct i915_shader_binary *producer, struct i915_shader_binary **out);`（producer は GS のとき VS の binary、他は NULL）。既存の `drv_i915_shader_compile(ir, out)` は producer NULL の wrapper として残す（host の fixture と rectangle kernel が使う）。

### 4.5 限界と拒否（log に理由を出す。ws.md の受け入れ 2「断って記録」）

| 条件 | 扱い |
| --- | --- |
| Invocations > 1、InvocationId、ViewportIndex、ClipDistance、gl_in の Position・PointSize 以外の member、stream | parser の refuse（diagnostic に理由） |
| OutputVertices > 32 かつ EndPrimitive を使う（control header > 32 bit） | a5 まで refuse。a5 で Mesa の per-slot + channel mask の形 |
| URB entry > 32 KiB（例: 256 頂点 × 18 slot） | compile が ENOTSUP |
| sampler を使う GS | prepare が refuse（VS と同じ「binding table が無い」） |
| spill が scratch の上限を越える | 既存の規則 |

### 4.6 増分（各 1〜2 時間）と受け入れ

| 増分 | 内容 | 受け入れ（host） |
| --- | --- | --- |
| a1 | ir.h・compiler.h の追加、`spirv-geometry.inc`: entry point・execution mode・変数（gl_in、per-vertex 配列、PrimitiveIdIn、Layer/PrimitiveId の出力）・access chain の頂点・load（`LOAD_VERTEX_INPUT`）・OpEmitVertex/OpEndPrimitive。codegen は geometry を「未対応」で refuse のまま | `i915-vk-spirv-test`: glxtest の 3 GS（glslc で `.geom` から作った語、§7）を parse して `vertices_in`・`output_topology`・`max_vertices`・`uses_end_primitive`・EMIT/END の数・`LOAD_VERTEX_INPUT` の頂点（定数/動的）・`writes_layer` を確かめる。refuse の 5 件（Invocations 2、ViewportIndex、7 頂点の配列、InvocationId、OpArrayLength） |
| a2 | `compile-geometry.inc`: interface・payload・prologue（handle の写し、vertex_count・cut_bits）、`LOAD_VERTEX_INPUT` の pull（定数・動的）、`LOAD_SYSTEM` PRIMITIVE_ID、`STORE_OUTPUT` LAYER/PRIMITIVE_ID の staged、`drv_i915_shader_compile_stage` の producer map | `i915-vk-compile-test`: points.geom の binary の field（hwords・control・entry size・grf start）、URB read の send の数と descriptor（opcode 8、slot、rlen 4）、動的の頂点の CMP/SEL の数（layers.geom）。gentool（brw_disasm）が受ける |
| a3 | EMIT（per-slot の write、masked）、END（cut bits）、terminate（control の write、頂点数の EOT）、`out_of_registers` の扱い | compile fixture: EMIT ごとの send（bit 17、mlen 2、global offset = 2 + 2·control）、END の SHL/OR、最後の 2 send（global 2、global 0 + EOT）。gentool。adjacency.geom（selection の中の store）と layers.geom（loop の中の EMIT/END と動的 gl_in） |
| a4 | 整理: diagnostic の文言、`I915_STAGE_COUNT` を使う配列・switch の網羅、spill と GS の共存（値を 90 個生かす GS を fixture で作り spill を通す）、`plan/ws075/tests/shader-survey` と `plan/ws068/tests/i915-shader-check` に geometry の stage を足す（WS068 の `pass/glsl150-geometry.geom` が通る） | compile fixture の spill の GS、survey の `gaps.txt` に geometry の不足が無い |
| a5（後回し可、§8 の判断） | control header > 32 bit（per-slot offset + channel mask、32 bit ごとの write、cut_bits の reset）、Invocations > 1（Instance Control、handle の dword 選択、InvocationId） | compile fixture: 64 頂点の GS で write の数と mask |

## 5. p007b: 実行器の設計

### 5.1 pipeline の 3 stage

| 変更 | 所 |
| --- | --- |
| `struct i915_gfx_pipeline` に `struct i915_gfx_shader *geometry;` と `struct i915_shader_binary *gs_binary;` | `gfx.h` 408〜410 の隣 |
| decode: `VK_SHADER_STAGE_GEOMETRY_BIT` を `pipeline->geometry` に（他の stage は今の log のまま） | `pipeline.c` 602〜610 |
| prepare: VS を compile → GS があれば `drv_i915_shader_compile_stage(gs_ir, vs_binary)` → FS。照合: GS の `input_locations` ⊆ VS の `varying_locations`（Position・PointSize は常にある）、FS の入力 ⊆ **最後の stage**（GS があれば GS）の `varying_locations`（`I915_SHADER_LOCATION_POINT_COORD` と、GS 無しの `I915_SHADER_LOCATION_PRIMITIVE_ID` は除く、§5.4）。GS の `sampler_count != 0` は refuse（VS と同じ）。GS の `code_bytes` は window の GS の枠に収まること。log の行に GS の数字を足す | `pipeline-prepare.c` 52〜115、370〜432 |
| release: `gs_binary` を free | `pipeline-prepare.c` 185〜200 |
| instruction window: **window を 64 KiB に**（`I915_GFX_INSTRUCTION_BYTES` 0x10000）、VS 0x0000（16 KiB）、**GS 0x4000（16 KiB、新 `I915_GFX_GS_KERNEL`）**、PS 0x8000（32 KiB、`I915_GFX_PS_KERNEL` を変更）。kernel object は 32 × 64 KiB = 2 MiB（+512 KiB）。`drv_i915_gfx_window` に gs_code/gs_bytes（NULL で無し）。rectangle・resolve の kernel は NULL を渡す。PS の上限（32 KiB）は変わらない | `heap.h` 93〜102、`draw.c` 246〜306、`draw.c` 543〜553 |
| `struct i915_gfx_kernels` に GS の field: `gs_code/gs_bytes`、`gs_grf_start`、`gs_push_regs`・`gs_push`、`gs_vertices_in`、`gs_output_topology`、`gs_output_vertex_hwords`、`gs_control_hwords`、`gs_control_format`、`gs_urb_entry_size`、`gs_primitive_id`、`gs_writes_layer`、`gs_scratch_bytes`・`gs_scratch_offset`。`varyings`・`ps_input_slots` は最後の stage の binary から（`drv_i915_gfx_pipeline_kernels`） | `state.h` 52〜140、`state.c` の `drv_i915_gfx_pipeline_kernels` |

### 5.2 draw の state（GS があるとき。無ければ今の dword のまま: 回帰の差分 0 を host の fixture で確かめる）

| packet | 変更 |
| --- | --- |
| 3DSTATE_PUSH_CONSTANT_ALLOC_* | **常に** VS 8 KiB（offset 0）、GS 8 KiB（offset 8）、PS 16 KiB（offset 16）に変える（HS/DS は 0）。各 stage の push data は 1 KiB 以下（`I915_GFX_PUSH_DATA_BYTES`）なので足りる。値を draw ごとに変えない（§12: ALLOC の変更に stall が要るかは未確認。値を固定して問題を避ける）。GS の無い draw の dword が変わるので pipe fixture と vkx の回帰で確かめる |
| 3DSTATE_URB_ALLOC_VS / GS | `drv_i915_gfx_emit_urb(batch, vs_entry_size, gs_entry_size)`。GS 無し（gs_entry_size 0）: 今のまま。GS 有り: 今の VS の領域（chunk 4 から、3576×64 B ＝ 27 chunk の整数部）を **VS 21 chunk、GS 6 chunk（48 KiB）** に分け、GS は start = 4 + 21、entries = min(48 KiB / (size×64), 1548) を size < 9 なら 8 の倍数に切り下げ；2 未満なら ENOTSUP（entry が 32 KiB 近いとき）。VS は 21 chunk の中で今と同じ計算。HS/DS は 0 のまま |
| 3DSTATE_CONSTANT_GS | `drv_i915_gfx_emit_constants` に GS の va/regs を足す。GS の push data は slot の **0x2c00**（新 `I915_GFX_GS_PUSH_BUFFER`、0x400。rectangle の頂点 0x2800〜0x2890 の後、scratch 0x3000 の前） |
| 3DSTATE_GS | 新 `drv_i915_gfx_emit_geometry_shader(batch, kernels)`（§3.4 の表の値）。無ければ今の `drv_i915_batch_zero` |
| 3DSTATE_SF dword 2 | GS 有りは `GEN12_URB_DEREF_BLOCK_SIZE_PER_POLY`（§3.5）、無しは今の 32 |
| 3DSTATE_CLIP dword 3 | `Force Zero RTA Index Enable`（bit 5）= 最後の stage が layer を書かなければ 1。**GS 無しの draw でも 1 に変わる**（VS は header を 0 で埋めるので結果は同じ。anv と同じ値にする。回帰で確かめる） |
| 3DSTATE_SBE / SBE_SWIZ | 属性の数・routing を最後の stage の varying から（kernels の `varyings`・`ps_input_slots` を prepare が GS から作る）。read offset 1 のまま |
| 3DSTATE_BINDING_TABLE_POINTERS_GS | 0 のまま（GS は sampler を持たない） |
| 3DSTATE_VF_TOPOLOGY / 3DPRIMITIVE | `drv_i915_gfx_topology` に LINE_LIST/STRIP/TRIANGLE_LIST/STRIP の WITH_ADJACENCY → 9/10/11/12。**GS の無い pipeline の adjacency は今のまま refuse**（§8） |
| scratch | GS が spill するとき: `scratch_per_thread`・`scratch_offset` に GEOMETRY の stage（thread id は max_gs_threads 336）、3DSTATE_GS の dword 4〜5。増分 b4 までは `gs_scratch_bytes != 0` を ENOTSUP で refuse |

### 5.3 layered の描画

| 変更 | 所 |
| --- | --- |
| `struct i915_gfx_framebuffer` に `layers`（`VkFramebufferCreateInfo.layers`、0 は 1） | `render-pass.c` 161〜170、`gfx.h` 373〜381 |
| render target の range: `i915_state_target_range` の `layer_count` を view の `layer_count`（0 は 1）に → Render Target View Extent = layers − 1、Surface Array。3D の view は今の規則（1 slice） | `state.c` 2335〜2347 |
| depth: 今の「view の base_layer から image の全 slice」のまま（view の layer 範囲に絞るのは任意。RTAI が範囲外なら書かれないだけ） | `state.c` 1501〜1524 |
| pass begin の loadOp CLEAR: view の layer ごとに `i915_image_surface(view->image, level, base_layer + l)` で fill（今は最初の layer だけ）。stencil の clear も同じ loop | `command.c` 2305〜2360、2157〜2178 |
| vkCmdClearAttachments: op に `base_layer`・`layer_count`（VkClearRect）を足し、実行で layer ごとに fill | `command.c` 1340〜1360、2380〜2450、`gfx.h` 752〜765 |
| limits: `maxFramebufferLayers` 2048（view extent 11 bit = 2047 + 1、`maxImageArrayLayers` と同じ） | `instance.c` 387 |

GS が `gl_Layer` に書く値は VUE header の dword 1 → clipper の RTAI → render target の Minimum Array Element + RTAI の layer に書かれる（isl の surface の形は今の `i915_image_surface_write` のまま）。

### 5.4 PrimitiveID

| 場合 | 扱い |
| --- | --- |
| GS が `gl_PrimitiveID` を書き、FS が読む | varying の location 69 として通常の routing（prepare の照合で一致） |
| GS が無く FS が読む | prepare は location 69 を照合から除き、`kernels->ps_primitive_id_mask`（bit n = FS の入力 n）を立てる。SBE_SWIZ のその attribute の 16 bit を Constant Source PRIM_ID（3 << 9）と Component Override X..W（0xF << 12）にする（anv の slot −1 の形、§3.6） |
| GS があり書かず FS が読む | 同じく SBE_SWIZ の PRIM_ID（§12: GS があるときの PRIM_ID の値は未確認。glxtest は使わない） |

### 5.5 features と limits（`instance.c`）

`geometryShader = VK_TRUE`。limits: `maxGeometryShaderInvocations` 32（§8 の判断 3）、`maxGeometryInputComponents` 64、`maxGeometryOutputComponents` 64（16 varying × 4、`maxVertexOutputComponents` と同じ）、`maxGeometryOutputVertices` 256、`maxGeometryTotalOutputComponents` 1024（Vulkan の最小値）。compiler が断る組（§4.5）は log に出し、ws.md の受け入れ 2 の記録に残す。

### 5.6 増分（各 1〜2 時間）と受け入れ

| 増分 | 内容 | 受け入れ（host） |
| --- | --- | --- |
| b1 | pipeline の 3 stage: decode、prepare（compile の順、照合、log）、kernels の field、window の layout（64 KiB、GS の枠）、`drv_i915_gfx_window` の gs、stub の更新 | `i915-vk-pipe-test`: wire で VS+GS+FS の pipeline を作り `kernels_ready`、`gs_binary` の field、GS 無しの pipeline の既存の検査が変わらない。cmd・res の stub が link する |
| b2 | draw の state: PUSH_CONSTANT_ALLOC の固定の分配、URB の分配、CONSTANT_GS と 0x2c00、3DSTATE_GS、SF の deref、CLIP の RTAI、SBE/SWIZ を最後の stage から、adjacency の topology、`drv_i915_gfx_emit_urb` の署名 | pipe fixture: batch の 3DSTATE_GS の dword 6〜8（enable、SIMD8、topology、vertex size、control、handles、expected count、max threads 335）、URB_ALLOC_GS（start 25、entries ≥ 2・8 の倍数）、CLIP bit 5、SBE の属性数と SWIZ、VF_TOPOLOGY 9/11。GS 無しの batch は PUSH_CONSTANT_ALLOC と CLIP bit 5 以外が前と同じ dword（fixture で前後の dump を比べる）。vmunix の build warning 0 |
| b3（p007a と独立。先に流してよい） | layered: framebuffer の layers、target range の layer_count、pass begin と ClearAttachments の layer ごとの fill、`maxFramebufferLayers` | `i915-vk-cmdbuf-test`: layers 2 の framebuffer の ClearAttachments（layerCount 2）が 2 つの fill op に、pass begin の clear が 2 layer。`i915-vk-resdispatch-test`: limits・features |
| b4 | PrimitiveID の SBE_SWIZ（GS 無し）、GS の scratch（stage の追加、3DSTATE_GS dword 4〜5）、GS の sampler の refuse の log、adjacency の GS 無しの log | pipe fixture: FS だけが PrimitiveId を読む pipeline で SWIZ の attribute = 0x9600 \| …（PRIM_ID と override）、spill する GS で dword 4〜5 |
| b5 | 試験の場面: `plan/ws031/tests/i915-capture.py` に `gl32` の場面（x11 の場面に倣い `glxtest --gl32` を走らせ、guest の log の GLXTEST GL32 check の行と四角の色を `plan/ws068/tests/glx-p033.sh` の期待で判定）、image の config は `plan/ws075/tests/config-test-hw.mk` に glxtest を足す（§7）。T1 への依頼文（image の作り方、流す試験、合否） | 場面が Venus（QEMU）の image で動くことは T1 の回帰で（i915 の判定ではない）。実機は T1 → Q1 の判定 |

## 6. 資源・並行性・失敗と回復

- 所有と寿命: `gs_binary` は pipeline が持ち `drv_i915_gfx_pipeline_release` で free（VS・FS と同じ）。kernel の語は window に写され、window の世代の規則（`draw.c` 262〜300）で置き直される。URB は draw ごとに分配し直す（今の VS の entry size が draw ごとに変わる実績に倣う）。
- 並行性: 新しい共有状態は無い。pipeline の prepare は create の中（session の thread）、draw の記録は「1 session に 1 thread」（`draw.h` 36〜44）。object の表の mutex（p025）は変えない。
- 割込み・DMA: 変更無し（GS は render engine の同じ batch）。
- 失敗: compile の refuse → `vkCreateGraphicsPipelines` が error（libglesv2 は「the device refused a shader」で link 失敗、glxtest の outcome が赤になるが止まらない）。draw の ENOTSUP（topology、URB の entry 不足、scratch）は今の「draw refused」の log と同じ道。GPU の hang（誤った 3DSTATE_GS・URB）は engine の reset（BUG-077 の修正）で device lost に落ちる → host の fixture と gentool を先に通し、実機は T1 が流す（hang したら Q1 に報告）。
- 回帰の隔離: GS の無い pipeline の batch の dword は PUSH_CONSTANT_ALLOC と CLIP bit 5 以外変えない（b2 の fixture で差分を確かめる）。

## 7. 試験計画

| 層 | 何を | 道具 |
| --- | --- | --- |
| host（増分ごと） | spirv・compile・pipe・cmdbuf・resdispatch の fixture に GS の場面（§4.6・§5.6） | `sh plan/ws031/tests/run-vk-host-tests.sh "spirv compile pipe cmdbuf resdispatch"`（plain と ASan/UBSan）。GS の SPIR-V は `src/drivers/gpu/i915/tests/render/compiler-shaders/regenerate.py`（glslc、`--target-env=vulkan1.0`）に `points.geom`・`adjacency.geom`・`layers.geom`（glxtest の 3 つを `#version 450` にした物）と対の vert/frag を足し `compiler-shaders-gen.inc` に出す |
| host（kernel の妥当性） | Mesa の brw_disasm が GS の kernel を受ける | `plan/ws031/tests/run-vk-gentool-test.sh`（`BRW_TOOLS=/home/awe/p014-c/mesa/build-asm/src/intel/compiler`、p006 の記録） |
| host（GL の shader） | WS068 の GLSL compiler が出す GS（`plan/ws068/tests/glsl-host/pass/glsl150-geometry.geom` と glxtest の 3 つ）を i915 の parser・compiler が受ける | `plan/ws068/tests/i915-shader-check/run.sh` に geometry の mode（a4）、`plan/ws075/tests/shader-survey/run.sh` |
| build | vmunix warning 0 | `make -j16 BUILD=build/<W> ZEDBSD_CONFIG=config/ci/config-amd64.mk build/<W>/vmunix` |
| QEMU（T1） | Venus では i915 の GS は動かない。boot test と Venus の glx-p033 の回帰（libglesv2 を変えないので変わらないはず）だけ | `plan/tools/boot-test.sh`、`plan/ws068/tests/glx-p033.sh` |
| 実機（T1、5330 passthrough、`flock /tmp/i915-hw.lock`） | `glxtest --gl32` の gl32 の場面（b5）: geometry-points・geometry-modes・layered の check が PASS、multisample-texture・sample-mask は p006 の増分 8 の確認を兼ねる。回帰: `plan/ws075/tests/test-hw.sh` の vkx・vke1・vke2・vkc、zdesktop の capture（`CAPTURE=zdesktop`）、boot test | 依頼は Q1 経由で `plan/agents/T1/requests.md`。結果の判定は Q1 |

QEMU の証拠と実機の証拠は分けて書く。実機は 5330 が戻るまで未実施。

## 8. 人の判断が要る点（既定の案つき）

| # | 点 | 既定 | 代案 |
| --- | --- | --- | --- |
| 1 | instruction window を 48 KiB → 64 KiB にし GS に 16 KiB の枠を取る（kernel object +512 KiB） | **採る**（PS の 32 KiB を減らさない。`heap.h` の定数と stub だけ） | PS の 32 KiB を 16+16 に割る（大きい fragment shader が入らなくなる危険） |
| 2 | PUSH_CONSTANT_ALLOC を常に VS 8 / GS 8 / PS 16 KiB に（GS の無い draw の dword も変わる） | **採る**（draw ごとに変えると stall の要否が未確認） | GS のある draw だけ変える |
| 3 | 断る機能の limit の報告: `maxGeometryShaderInvocations`（Vulkan の最小 32、実装は 1）、`maxGeometryOutputVertices` 256（entry 32 KiB の上限と a5 までの cut bits 32 の上限） | **Vulkan の最小値を報告し、compile で refuse して log に出し、ws.md の受け入れ 2 に「断って記録」**（GL 3.2 は invocations を使えない。libglesv2 は feature を読まない） | 実装の値（1・32）を報告する（Vulkan 非準拠だが正直） |
| 4 | GS の無い pipeline の adjacency の topology | **今のまま refuse（log）**、backlog（Vulkan は許す。hardware が GS 無しで adjacency を捨てるかは未確認） | 9〜12 をそのまま通す（実機で確かめてから） |
| 5 | 入力は pull だけ（push model を作らない） | **pull だけ**（register の予算が見える、動的な頂点が SEL で済む。Mesa も 24 register を越えると pull） | push（性能、後の最適化。§10） |
| 6 | a5（cut bits > 32、Invocations > 1）を p007 の中でやるか | **p007a の最後に a5 を置き、時間が無ければ Future Work に移す**（glxtest は要らない） | p007 に入れない |
| 7 | b3（layered の clear・framebuffer の layers）を p007a より先に流す | **先に流してよい**（compiler に依存しない） | 順にする |
| 8 | fixture の GS の SPIR-V は glslc で作る（regenerate.py の前例）。WS068 の compiler の出力は i915-shader-check で別に確かめる | **採る** | WS068 の compiler の出力だけを fixture にする（host の試験が libglesv2 の build に依存する） |
| 9 | HAL・UAPI・toolchain は変えない | **変えない**（必要になる点は無い） | — |

## 9. 選ばなかった案と理由

| 案 | 理由 |
| --- | --- |
| GS の入力 slot を GS 単体で決める（GS の入力の集合 = VS の出力の集合を強制） | VS が GS の読まない varying を書く普通の shader を断ることになる。prepare が VS の map を渡す方が Mesa の link と同じ意味で、変更は compile の口 1 つ |
| static な頂点数（Static Output）を使う | glxtest の layers.geom は loop で頂点数が変わる。dynamic の経路は static の shader でも正しく、場合分けが 1 つ減る |
| gathered VUE（VS の `late_vue`）を GS にも | EMIT が shader の途中に何度も来るので「最後に 1 回集める」形が合わない。spill が register 不足を吸収する |
| control data を Mesa と同じく常に per-slot + channel mask で書く | header ≤ 32 bit なら 1 回の write で済む（Mesa も同じ分岐）。> 32 bit は a5 |
| GS の kernel を PS の 32 KiB から切り出す | 判断 1 |
| framebuffer の `layers` を view の layer 数から推定する | VkFramebufferCreateInfo が持つ値をそのまま持つ方が単純で正しい |

## 10. 後の最適化（Future Work の候補、p007 では作らない）

- push model の入力（Vertex URB Entry Read Length > 0、`brw_fs_thread_payload.cpp` 142〜155 の 24 register の規則）。
- 同じ頂点・slot の 4 component を 1 つの URB read にまとめる、reply の register を直接値にする。
- static な頂点数のときの「最後の write に EOT を付ける」（Mesa の `mark_last_urb_write_with_eot`）。
- URB の分配を GS の entry size に応じて変える（Mesa の `intel_get_urb_config` の wants の比例配分）。p007 は VS 21 / GS 6 chunk の固定（§5.2）。

## 11. コーディング規約・転記の扱い

- 新 file は Zlib の header と file の説明（coding-style §13）、static 関数の 1 行の前方宣言、段落ごとの comment、`return error;` で終わらない形（i915-rebuild-rules §1）。`.inc` は compile.c・spirv.c の末尾の include（前例と同じ）。
- `intel/genxml.h` に足す定数: 3DSTATE_GS の field の shift・値（§3.4）、URB_ALLOC_GS、CLIP の bit 5、SBE の PrimitiveID override の bit、SBE_SWIZ の attribute の bit（gen60.xml）、3DPRIM の ADJ 4 つ、GS の max threads 335、GS の max entries 1548。それぞれに 25.0.7 の file・行・sha256 を注記（既存の形）。
- Mesa の C++ の構造・comment・関数名は写さない。説明に Linux/Mesa の関数名を書くのは「処理の意味の説明に限る」（rules §1）。

## 12. 未確認（実装の増分で確かめる。確かめ方つき）

| # | 事項 | 確かめ方 |
| --- | --- | --- |
| 1 | URB SIMD8 read の descriptor（header present の bit、rlen 4 で register c = component c） | a2 で gentool（brw_disasm）の出力を Mesa の `tools/refvk.c` の同じ shader の disasm と比べる（p006 の手順） |
| 2 | `mul` の UW 即値による per-slot offset の encoding（`drv_i915_eu_alu2` の MUL が UW 即値を受けるか） | a3 で gentool。駄目なら既存の IMUL の lowering を使う |
| 3 | 2 register の header（handle + per-slot offset）を src0、data を ex_mlen の split send にして hardware が受けるか（Mesa は 1 つの連続 payload） | gentool は受ける見込み。実機は T1。駄目なら連続 payload（header の 2 register の直後に staged slot を置く配置に変える: vue_grf−2・vue_grf−1 の直後が staged なので連続になる） |
| 4 | PUSH_CONSTANT_ALLOC・URB_ALLOC の値を draw の間で変えるときの stall の要否（anv の運用） | 値を固定して避ける（判断 2）。URB は今も draw ごとに変わっている |
| 5 | GS 無しで adjacency の topology を流したときの hardware の挙動 | 判断 4（refuse のまま） |
| 6 | GS があり PrimitiveID を書かないときの SBE_SWIZ PRIM_ID の値 | glxtest は使わない。b4 の後に実機で確かめられる場面があれば |
| 7 | Vertex URB Entry Read Length 0 + Include Vertex Handles の組の妥当性 | Mesa は lines_adjacency の pull で同じ組を出す（`brw_fs_thread_payload.cpp` 151〜155 の計算で 0 になる）。実機で確かめる |
| 8 | URB の総量（§3.5）と VS 21 / GS 6 chunk の分配が L3 config の URB に収まること | 今の VS の領域の中で分けるので総量は増えない。entry の不足は ENOTSUP で見える |
| 9 | `3DSTATE_VS` と同じく 3DSTATE_GS の dword 9（Output Read Offset/Length）を 0 にしてよいこと | anv の `emit_3dstate_gs` が設定しない（`genX_pipeline.c` 1451〜1499 に無い）。実機で確かめる |
| 10 | `i915_image_surface_write` の view extent（layer_count − 1）で RTAI の書き分けが働くこと（今は RT の range が 1 層なので未観測） | b3 の実機（layered の check） |
| 11 | libvulkan の wire が geometry の stage と `VkFramebufferCreateInfo.layers`・`VkClearRect` の layer をそのまま運ぶこと | `pipeline.c` 596〜610 と `render-pass.c` 146 の decoder は generic（codec）なので運ぶ見込み。b1・b3 の host fixture で確かめる |

## 13. 敵対的レビュー（自己）

| # | 指摘 | 対応 |
| --- | --- | --- |
| 1 | 「GS の入力 slot を prepare が渡す」は compile の口を変える。host の fixture（compile・lower・gentool）は `drv_i915_shader_compile` を呼ぶので壊れないか | wrapper を残す（§4.4）。GS の fixture だけ新しい口を使う |
| 2 | if-convert された selection の両側で EMIT が走ると、predicate の無い channel まで頂点を出すのではないか | EMIT は predicate で mask した send と masked の ADD にする（§4.4）。parser が `component` 1 と src[0] に predicate を付ける（STORE_STORAGE と同じ型）。fixture の adjacency.geom（`if` の中の store）は store だけなので、EMIT が `if` の中にある fixture を a3 に足す |
| 3 | loop の中で EMIT した後、同じ staged register に次の store が入る前に別の channel が…（SIMD の整合） | 全 channel が同じ命令列を走り、staged register は channel ごと（SIMD8 の 1 dword/channel）。頂点数・offset も channel ごと。整合する |
| 4 | vertex_count・cut_bits を値 register から 2 つ固定で取ると、spill の victim の選び方（`last_use`）がこれらを知らない | IR の値ではないので victim にならない。`first_value_grf` を 2 進めるだけ（§4.4） |
| 5 | 頂点数の最後の write が r126 を data に使うが、r126 は staged VUE の一部で、その時点の値は EOT 後に要らない。しかし cut bits の write（直前）の data は cut_bits の register で r112..r127 の制約は無い（EOT ではない）→ 問題無し | 記載のとおり |
| 6 | glxtest の layers.geom は `gl_Layer = layer` を loop の中で書く。staged header の dword 1 への MOV は program 順で最後の store が残るので、EMIT の時点の値は直前の store。正しい | — |
| 7 | GS の無い draw の CLIP bit 5 と PUSH_CONSTANT_ALLOC を変えるのは「無関係な変更を壊さない」に反しないか | 値は anv と同じ側に寄せる。b2 で前後の dword の差分を fixture で固定し、実機の回帰（vkx・vke・zdesktop）で確かめる。差分を避けたいなら判断 2 の代案 |
| 8 | 3DSTATE_GS の Maximum Number of Threads 335 は GT2 の値。5330（46a8）は ADL-P GT2（`intel_device_info_adl_gt2` = `GFX12_GT_FEATURES(2)`、`GFX12_HW_INFO` の 336）で合う。heap.h の `I915_GFX_MAX_VS_THREADS` と同じ「1 target 固定」の XXX を付ける | 記載 |
| 9 | URB entry が 32 KiB に近い GS（256 頂点 × 大きい VUE）は 48 KiB の GS 領域で entry 1 → refuse。glxtest は 6 頂点なので関係無いが、GL の app が当たり得る | log に理由を出し記録。領域の分配を「GS の entry size に応じて chunk を増やす」のは後の改良に書く（§10 に追加しておく） |
| 10 | `gl_in[i].gl_Position` の動的 index の SEL 連鎖は handle の選択だけで、読みは 1 message。4 component を 4 register の reply で取るので temporaries が 4 つ要る。register 不足の shader で spill が増える | 許容（glxtest は小さい）。§10 の最適化 |
| 11 | per-vertex の located 入力（`in vec4 v[]`）の `i915_spirv_add_io` の変更は VS/FS の配列の入力（location を要素ごとに取る）を壊してはならない | geometry の stage のときだけ外側の配列を頂点とする分岐（§4.3）。a1 の fixture に VS の配列入力の既存の検査（`vin16.vert` 等、lower fixture）が含まれ回帰する |
| 12 | libglesv2 の `gles_spirv_position` は OpEmitVertex の前に Position の store を入れる。その store は「gl_Position の block」経由（PTR_OUTPUT_BLOCK）なので、GS でも今の Position の store の経路で足りるか | p032 の記録では GS の gl_Position は独立の builtin 変数（emit.c 1309〜の表の形。gl_PerVertex の block は gl_in 側だけ）。どちらでも今の `i915_spirv_lower_store_output` が受ける（3628〜3647） |
| 13 | host の fixture の cmdbuf は既存の失敗（`test_blend_state`）がある。b3 の検査を足しても PASS にならず受け入れに使えない | b3 の検査は cmdbuf の既存の失敗より前に置き、個別に通る形にする。既存の失敗は p006 の記録どおり別件（直すなら Q1 に path を報告） |
| 14 | 実機が無い間に p007b を merge すると、GS を使う app（無い）以外への影響は判断 2・CLIP の 2 点だけ。それでも実機の回帰まで「cleared」にしない | phase.md の受け入れに「実機は T1 の結果を Q1 が判定するまで cleared にしない」を書く |
| 15 | design は Mesa 25.0.7 の file 行を引くが、genxml.h の既存の sha256 は別の Mesa（main）の物。転記のとき file を取り違えない | §0 の注記。新しい定数は 25.0.7 の sha256 と行を書く |

## 14. design-reviewer の review の反映（2026-10-07、この節が本文より優先）

| 指摘 | 反映 |
| --- | --- |
| B1 EmitVertex が max_vertices を越えて URB の外へ書く | a3 の EMIT の predicate を `P && vertex_count < max_vertices`（Mesa の `nir_lower_gs_intrinsics.c` 30〜55 と同じ guard、kernel の中の compiler は未定義の動作を GPU の破損にしない）。fixture に max_vertices+1 回の emit |
| B2 増分の依存と受け入れ | a2 の受け入れは EmitVertex の無い GS と terminate の最小形（頂点数 0 の EOT）、b1 の依存は a3、b5 の依存に a3・b4（survey なら a4）（phase.md の表） |
| S1 r1 の handle の mask | a2 の prologue で `AND r(vue_grf−2), r1, 0xFFFF`（NoMask）を 1 回、§4.4 の r1 の使用（301・303・324・325 行）は全てこの写し（Mesa `brw_fs_thread_payload.cpp` 114） |
| S2 layered の depth・stencil の RTVE | b3 で 3DSTATE_DEPTH_BUFFER（`state.c` 1514〜1524）と stencil（3171 付近）の Render Target View Extent = layer_count − 1、Depth を合わせる（isl `isl_emit_depth_stencil.c` 148・164） |
| S3 point size の source | b1 で `vs_point_size` を最後の stage の binary から。`shaderTessellationAndGeometryPointSize` は出さない（FALSE）ので GS の PointSize の store は refuse、GS がある時の幅は state の 1.0 |
| S4 PrimitiveID の SBE | b4 で SBE_SWIZ と 3DSTATE_SBE dword 1 の Primitive ID Override（bits 0〜4、16〜19）の両方（anv `genX_pipeline.c` 714〜718・747〜754）、期待値は 0xF600 \| source。実機の場面は b5 の依頼に足す |
| S5 「VERTEX でなければ FRAGMENT」の分岐 | a2 で次を GS の扱いに: `compile.c` 4507 の prologue、4343〜4349 の payload_end、2378 の point size、2384 以降の staged store、4555〜4610 の terminate、3725〜3810 の skip の解析（EMIT・END は STORE_STORAGE と同じく P の下の predicated）、891・1028 付近の operands・results の表 |
| S6 topology と GS の入力の不一致 | b2 で draw の topology の頂点の数 ≠ `gs_vertices_in` なら ENOTSUP と「draw refused」の log |
| S7 window の VS の上限と呼び出し | b1（phase.md の表） |
| S8 feature と limit の増分 | `geometryShader`・`maxGeometry*` は b4（a3 の後）、`maxFramebufferLayers` だけ b3 |
| S9 cut bits > 32 | 判断 6 を改訂し a3 に入れる。Invocations > 1 だけ a5。GS の sampler は refuse のまま、GL の `GL_MAX_GEOMETRY_TEXTURE_IMAGE_UNITS` との矛盾は ws.md の「断って記録」に書く |
| S10 道具の path | brw の道具は `build/mesa-tools/build-asm/src/intel/compiler/{brw_asm,brw_disasm}`、`refvk.c` は `plan/ws031/handover/tools/refvk.c`（Mesa の build が要るかは未確認）、regenerate.py は `--target-env=vulkan1.1 --target-spv=spv1.0`。a3 の受け入れに Mesa の disasm との URB の send の突き合わせ |
| S11 p006 への依存と受け入れの文 | phase.md の「review の反映」（scoped dependency、受け入れ 1 の文）。Phase の ID は Q1 |
| minor 1 mlen | emit 2 は mlen の field（bits 28:25）を 2 に置き換える（OR しない） |
| minor 2 ClearAttachments の layer | b3（phase.md の表） |
| minor 3 per-slot offset | emit ごとの ADD の累計（同じ predicate）、`mul` の UW 即値は使わない |
| minor 4 window の費用 | session ごとに +512 KiB（`draw.c` 104）、EOT の詰めの clear（`state.c` 797）も 64 KiB に |
| minor 5 gl_in の member | 宣言でなく access で refuse（glslc の gl_PerVertex は ClipDistance・CullDistance を持つ） |
| minor 6 GS 無しの adjacency | 判断 4 の代案の根拠: anv は GS の有無に関わらず 9〜12 を出す（`genX_gfx_state.c` 128〜138） |
| minor 7 実機の試験の並べ方 | b5（phase.md の表） |
| minor 8 GS の push data の上限 | b1（phase.md の表） |
