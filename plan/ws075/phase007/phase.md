<!-- awesome-plan project=zedbsd record=ws075-p007 -->

# ws075-p007: GL 3.2 の stage: geometry shader、gl_Layer と layered の描画、PrimitiveID（設計、p007a / p007b に分割）

Phase ID: `ws075-p007`
Parent: [WS075](../ws.md)
Status: planning（2026-10-07 設計と design-reviewer の反映済み、Q1 の ACK 待ち）（2026-10-07、P1。設計の文書 [design.md](design.md) を書いた。code は変えていない。着手前に p007a（compiler）と p007b（実行器）に分ける: Q1 の ACK）
Phase disposition: normal
Queue: none（設計だけ。実装の Queue は Q1 が p007a・p007b として入れる）
承認: 2026-09-27 ユーザー「…i915の高度化に進んでください。」、WS075 の計画（ws.md の p007 の行「着手前に分ける」）。2026-10-07 Q1 の ACK「p007 は着手前に分ける → p007a・p007b に分けて設計」。

## 範囲

設計の正本は [design.md](design.md)。要点:

- **p007a（compiler、`src/drivers/gpu/i915/compiler/`）**: SPIR-V の ExecutionModel Geometry、execution mode（入力の primitive 5 種、出力 3 種、OutputVertices、Invocations は 1 だけ）、OpEmitVertex・OpEndPrimitive、`gl_in[]`（Block の配列）、per-vertex の配列の入力、gl_PrimitiveIDIn、出力の gl_Layer・gl_PrimitiveID。Gen12 の GS の thread の payload（r1 出力 handle、r2 PrimitiveID、頂点ごとの ICP handle）、入力は pull（URB SIMD8 read）、出力は頂点ごとの per-slot offset の URB write、control data header の cut bit、最後に頂点の数の write と EOT。新 file `spirv-geometry.inc`・`compile-geometry.inc`（compute の前例）。
- **p007b（実行器、`src/drivers/gpu/i915/render/`）**: pipeline の 3 stage（decode・compile の順（VS → GS は VS の VUE map を受けて → FS）・interface の照合・instruction window の GS の枠）、3DSTATE_GS・3DSTATE_URB_ALLOC_GS（VS 21 / GS 6 chunk）・3DSTATE_CONSTANT_GS（slot の 0x2c00）、SF の deref PER_POLY、CLIP の Force Zero RTA Index、SBE/SBE_SWIZ を最後の stage の varying から、adjacency の topology（9〜12）、layered の描画（framebuffer の `layers`、render target の view extent、layer ごとの clear）、PrimitiveID（GS の varying / GS 無しは SBE_SWIZ の PRIM_ID）、feature `geometryShader` と `maxGeometry*`・`maxFramebufferLayers`。
- 範囲外: tessellation、transform feedback の GS、stream、ViewportIndex、Invocations > 1（a5 の候補）、GS の sampler、push model の入力、3D の layered、display/ の file、HAL・UAPI・toolchain の変更（要らない）。

## 増分（各 1〜2 時間。受け入れは design.md §4.6・§5.6）

| 増分 | 内容 | 依存 | 受け入れ（host） |
| --- | --- | --- | --- |
| a1 | IR・compiler.h の追加、`spirv-geometry.inc`（entry point、execution mode、gl_in・per-vertex 配列・PrimitiveIdIn・Layer/PrimitiveId、access chain の頂点、`LOAD_VERTEX_INPUT`、OpEmitVertex/OpEndPrimitive）。codegen は geometry を refuse のまま | — | spirv fixture: glxtest の 3 GS の parse の field と refuse 5 件 |
| a2 | `compile-geometry.inc`: interface・payload・prologue（r1 の handle は `& 0xFFFF` の写しを 1 回）、pull の入力（定数・動的の頂点）、PrimitiveID、Layer/PrimitiveId の staged、`drv_i915_shader_compile_stage(ir, producer, out)`、「VERTEX でなければ FRAGMENT」の分岐の全部の洗い出し（design.md §14 の S5 の一覧）と terminate の最小形（頂点数 0 の EOT） | a1 | compile fixture: EmitVertex の無い GS の binary の field、URB read の send、FS の経路（RT write の send）を通らないこと |
| a3 | EMIT（per-slot の write、predicate は `P && vertex_count < max_vertices`、offset は emit ごとの ADD の累計）、END（cut bits、>32 bit も: 判断 6 の改訂）、terminate（control の write、頂点数の EOT） | a2 | compile fixture: send の列（bit 17、global offset、mlen の field、EOT）、`if` の中・loop の中の EMIT、max_vertices+1 回 emit する GS で超えた分が mask される、64 頂点の cut。Mesa（brw_compile_gs）の disasm と URB の send の desc・offset・mlen・EOT の突き合わせ（`build/mesa-tools/build-asm/src/intel/compiler/brw_disasm`） |
| a4 | 整理: diagnostic、`I915_STAGE_COUNT` の網羅、spill との共存、shader-survey と i915-shader-check の geometry | a3 | spill の GS の fixture、survey の gaps 0 |
| a5（後回し可） | Invocations > 1（cut bits > 32 は a3 へ移した） | a3 | Invocations 2 の GS の fixture |
| b1 | pipeline の 3 stage、kernels の field、window 64 KiB（VS 0 / GS 0x4000 / PS 0x8000）、VS の上限の検査を `GS − VS` に（`pipeline-prepare.c` 379）、`drv_i915_gfx_window` の呼び出し（draw.c 543、compute.c 148、blit.c 341・582・593）、GS の push data の上限の検査、point size の source を最後の stage から、stub | a3 | pipe fixture: 3 stage の pipeline、既存の検査が不変 |
| b2 | draw の state: PUSH_CONSTANT_ALLOC（VS 8 / GS 8 / PS 16 KiB、固定）、URB の分配（`drv_i915_gfx_emit_urb` の blit.c 1001 も）、CONSTANT_GS、3DSTATE_GS、SF の deref、CLIP の RTAI、SBE、adjacency の topology、draw の topology の頂点の数 ≠ GS の入力の頂点の数なら draw を refuse | b1 | pipe fixture: 3DSTATE_GS・URB_ALLOC_GS・CLIP・SBE・VF_TOPOLOGY の dword、GS 無しの batch の差分が ALLOC と CLIP bit 5 だけ。vmunix warning 0 |
| b3（p007a と独立、先でよい） | layered: framebuffer の `layers`、target range の layer_count、depth・stencil の RTVE = layer_count − 1 と Depth、pass begin（framebuffer の layers の数）と ClearAttachments（`view->base_layer + rect.baseArrayLayer + l`）の layer ごとの fill、`maxFramebufferLayers` 2048 | — | cmdbuf fixture: layers 2 の clear が 2 fill、DEPTH_BUFFER の RTVE の dword、resdispatch: maxFramebufferLayers |
| b4 | PrimitiveID の SBE_SWIZ と 3DSTATE_SBE dword 1 の override（anv と同じ、GS 無し）、GS の scratch の stage、sampler・adjacency（GS 無し）の log、device の feature `geometryShader` と `maxGeometry*` を出す（compiler と実行器が揃った後） | b2、a3 | pipe fixture: SWIZ の PRIM_ID、spill する GS の dword 4〜5 |
| b5 | 試験の場面 `gl32`（`plan/ws031/tests/i915-capture.py`、glx-p033.sh の期待）、image の config、T1 への依頼文（単純な場面から: points→tristrip、adjacency、loop と layer。PrimitiveID の小さな場面も） | b2・b3・b4・a3（survey を使うなら a4） | T1 → Q1 の判定 |

## 受け入れ（Phase 全体）

1. host: `sh plan/ws031/tests/run-vk-host-tests.sh` の 10 個が plain・ASan/UBSan で PASS（2026-10-07 の main では既存の失敗は無い。失敗の集合が変わらず、新しい GS の検査が全て PASS）、`run-vk-gentool-test.sh` が GS の kernel を受ける。
2. build: vmunix（`config/ci/config-amd64.mk`）warning 0。
3. 実機（T1、5330 passthrough、QEMU の Venus ではない）: glxtest `--gl32` の `geometry-points`・`geometry-modes`・`layered` が PASS、hang・device lost 0。回帰: test-hw の vkx・vke1・vke2・vkc、zdesktop の capture、boot test。**T1 の結果を Q1 が判定するまで cleared にしない**（phase.md と queue.md に `test-wait（T1-NNN）`）。
4. 断る機能（Invocations > 1、cut bits > 32（a5 まで）、URB entry > 32 KiB、GS の sampler、GS 無しの adjacency）は log に理由を出し、ws.md の受け入れ 2 の「断って記録」に残す。

## 人の判断（既定の案つき。design.md §8）

| # | 点 | 既定 |
| --- | --- | --- |
| 1 | instruction window 48 → 64 KiB、GS に 16 KiB（kernel object +512 KiB） | 採る |
| 2 | PUSH_CONSTANT_ALLOC を常に VS 8 / GS 8 / PS 16 KiB（GS 無しの draw の dword も変わる） | 採る（draw ごとの変更の stall の要否が未確認） |
| 3 | 断る機能の limit の報告（`maxGeometryShaderInvocations` 32、`maxGeometryOutputVertices` 256） | Vulkan の最小値を報告し compile で refuse・log・記録 |
| 4 | GS 無しの adjacency の topology | 今のまま refuse（backlog） |
| 5 | 入力は pull だけ | pull だけ（push は Future Work） |
| 6 | a5 を p007 に入れるか | cut bits > 32 は a3 に入れる（GL の `GL_MAX_GEOMETRY_OUTPUT_VERTICES` は Vulkan の limit そのまま、`gles.c` 2537）。Invocations > 1 だけ後回し可 |
| 7 | b3 を p007a より先に流す | 可 |
| 8 | fixture の GS の SPIR-V は glslc で | 採る（WS068 の compiler の出力は i915-shader-check で別に） |

## Q1 の判断（2026-10-07）

- 人の判断 1〜8 は技術の判断として Q1 が既定どおりで承認（ユーザーの UAT・製品の判断に当たらない）。Phase の ID は p007a・p007b（ws051-p002b と同じ形）で可。

## review の反映（2026-10-07、design-reviewer: blocking 2・should-fix 11・minor 8）

design.md §14 に全部を書いた（本文より §14 が優先）。上の増分の表と判断 6 はそれに合わせて直した。依存: p007 は p006 の成果のうち「増分 5 の MS と増分 7・8 の texel buffer・sampler2DMS の source」（main に統合済み）だけを使い、p006 の実機の clearance は待たない（scoped dependency）。Phase の ID（p007a・p007b は ID の規則 `wsNNN-pNNN` に合わない）を分けるかは Q1 が決める。

## 未確認（design.md §12）

URB SIMD8 read の descriptor（rlen・header）、per-slot offset の `mul` の即値、2 register の header + ex_mlen の split send を hardware が受けるか、ALLOC の変更の stall、GS 無しの adjacency の挙動、GS があり PrimitiveID を書かないときの PRIM_ID、Read Length 0 + Include Vertex Handles、URB の総量、3DSTATE_GS dword 9 の 0、view extent による RTAI の書き分け、libvulkan の wire が geometry の stage と layers を運ぶこと。それぞれ確かめ方を design.md に書いた。

## 出典とライセンス

Mesa 25.0.7（MIT）の file と sha256 は design.md §0 の表。事実（field の位置、payload の形、URB の規則、device の上限）だけを取り、code は写さない。`intel/genxml.h` に足す定数は値だけの転記で、file・行・sha256 を注記する。zedBSD の code は Zlib。SPIR-V の番号は Khronos の仕様。

## 記録

| 日付 | 内容 |
| --- | --- |
| 2026-10-07 | P1（kernel-and-driver-designer）が設計: [design.md](design.md)。読んだ物: compiler の `spirv.c`・`compile.c`・`compiler.h`・`ir.h`・`eu.h`・`compile-compute.inc`、render の `pipeline.c`・`pipeline-prepare.c`・`state.c`・`state.h`・`draw.c`・`draw.h`・`heap.h`・`gfx.h`・`command.c`・`render-pass.c`・`instance.c`、`intel/genxml.h`、libglesv2 の `glsl/emit.c`・`spirv.c`・`draw.c`・`program.c`・`framebuffer.c`、glxtest の `gl32.c`、host の fixture と runner、Mesa 25.0.7 の §0 の file。command は読むだけ（`grep`・`sed`・`sha256sum`）。build・試験は未実施（設計の Phase）。commit は P1 が行う |

### 制限・移管

- 実機の証拠は無い（5330 が戻ってから T1）。QEMU（Venus）では i915 の GS は動かない。
- a5（cut bits > 32、Invocations > 1）と push model の入力、URB の比例配分は Future Work の候補（design.md §10）。
