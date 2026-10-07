<!-- awesome-plan project=zedbsd record=ws031-p032 -->
# ws031-p032: executor: blend の logic op と dual source

Phase ID: `ws031-p032`
Parent: [WS031](../ws.md)
Status: in-progress（q833、P1、2026-10-07: logic op は実装と host の試験 PASS、dual source は実装中）
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

## dual source

（実装中）
