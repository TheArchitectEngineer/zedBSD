<!-- awesome-plan project=zedbsd record=ws031-p032 -->
# ws031-p032: executor: blend の logic op と dual source

Phase ID: `ws031-p032`
Parent: [WS031](../ws.md)
Status: cleared 候補（q833、P1、2026-10-07: logic op・dual source とも実装と host の試験 PASS。dual source の feature は実機まで 0（ユーザー））
設計: [p019](../phase019/phase.md) §2.1・§2.2・§7（S1〜S4・S6・M5〜M11）
ユーザーの決定（2026-10-07、Q1 経由のクリック）: logic op は feature を有効に。dual source は実装し、feature（`dualSrcBlend`・`maxFragmentDualSrcAttachments`）は実機で確かめるまで 0。

## logic op（2026-10-07、P1）

| 部分 | file | 中身 |
| --- | --- | --- |
| 定義 | `intel/genxml.h` | `GEN12_BLEND_LOGIC_OP_ENABLE`（entry の dword 1 の bit 31）・`GEN12_BLEND_LOGIC_OP_FUNCTION_SHIFT`（27、Mesa genxml gen80.xml の bit 59..62・63） |
| pipeline | `render/gfx.h`（`logic_op_enable`・`logic_op`）、`render/pipeline.c` | decode で持つ（XXX の log を消した） |
| state | `render/state.c` | `i915_blend_equation` は logic op の時 blend しない（BLEND_STATE と 3DSTATE_PS_BLEND が揃う）。BLEND_STATE の各 entry の dword 1 に Logic Op Enable と Function を、blend の有無・整数の target に関わらず、float（SFLOAT・UFLOAT）と sRGB でない target に（`i915_state_target_format`・`i915_state_format_takes_logic_op`）。`VkLogicOp` → 値は anv の `vk_to_intel_logic_op` の表（16 以上は COPY） |
| feature | `render/instance.c` | `logicOp = VK_TRUE` |
| 試験 | `plan/ws031/tests/i915-vk-cmdbuf-test.c`（`test_logic_op`） | AND（VK 1 → 8）が dword 1 に、blend を求める pipeline でも entry の dword 0 の bit 31（blend）が 0 で dword 1 の bit 31（logic op）が 1、PS_BLEND の blend が 0、16 個と範囲外が anv の表どおり、R8G8B8A8_UINT の target に COPY（12）、SRGB の target は無し、logic op を外すと blend が戻る |

確認（host、2026-10-07）: WS031 の host fixture の全部（通常と ASan/UBSan、rm を除いて手で）PASS。`make -j16 disk-image` exit 0、自前の warning 0。
未実施: 実機（対象外）、pipeline の wire の decode の fixture（`pipe` に logic op の record を足していない、decode は 2 行）。

## dual source（2026-10-07、P1）

| 部分 | file | 中身 |
| --- | --- | --- |
| SPIR-V | `compiler/spirv.c` | decoration `Index`（32）を読む（今まで拒否）。Index 1 の出力は IR の `I915_IR_LOCATION_SECOND_COLOR`（`compiler/ir.h`、既存の値と衝突しない 0xFFFFFFFC）へ。fragment の Location 0 でない Index 1、1 location を超える Index 1 は拒否 |
| compile | `compiler/compile.c`・`compiler.h` | 第 2 の色は r120..r123（location 1 の register、dual source の時 location 1 は書かれない）、location 0 は今の r124..r127。prologue の 0 埋めにも入る。終わりは render target 0 への SIMD8 dual source の書き込み 1 つ（split send: src0 r124 mlen 4、src1 r120 ex_mlen 4、descriptor `0x08031200` = Mesa の `brw_message_desc` と `brw_fb_write_desc` の式、message control 2、last）。discard は f1.0 の predicate。第 2 の色があって location 0 以外にも書く、または location 0 が無い shader は unsupported。binary に `dual_source` |
| pipeline・state | `render/gfx.h`（`dual_source`）、`pipeline-prepare.c`、`state.c` | kernel の印を pipeline に写し、SRC1 の factor の式は印のある pipeline だけ blend（無ければ今どおり切る、anv の `has_fs_dual_src`） |
| feature | `render/instance.c` | **変えない**（`dualSrcBlend`・`maxFragmentDualSrcAttachments` は 0、実機で確かめるまで、ユーザー 2026-10-07） |
| 試験 | `src/drivers/gpu/i915/tests/render/compiler-shaders/dual.frag`・`dual-alone.frag`（と `.spv`、glslc `--target-env=vulkan1.1 --target-spv=spv1.0 -O0`）、`plan/ws031/tests/i915-vk-compile-test.c`（`test_dual_source`）、`i915-vk-cmdbuf-test.c`（`test_logic_op` の末尾）、`run-vk-gentool-test.sh`（`dual-alone` は飛ばす） | SENDC が 1 つで EOT、descriptor が Mesa の式の値と一致、ex_mlen 4・RT index 0、src0 r124・src1 r120、EU model で 2 つの色が bit 一致。第 2 の色だけの shader は compile が失敗。SRC1 の式は `dual_source` の pipeline だけ blend |

確認（host、2026-10-07）: WS031 の host fixture の全部（通常と ASan/UBSan）PASS。`make -j16 disk-image` exit 0、自前の warning 0。
未実施: Mesa の disassembler（gentool）での確かめ（この host に build が無い）、実機（対象外、feature は 0）。`regenerate.py` の一覧には足していない（GPU の vkc の期待値が要る、実機の再開の後）。
