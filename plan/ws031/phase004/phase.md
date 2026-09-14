# WS031 p004 計画: spirv — SPIR-V パーサ → baseline IR

SPIR-V を、compile が消費する baseline IR へ変換する。最適化なし。おおまかな設計。

## Module と所有ファイル
- `src/drivers/gpu/i915/vk/spirv.c`, `spirv.h`
- fixture: `plan/ws031/tests/i915-vk-spirv-test.c`（入力に vkdemo の `cuboid.vert.spv`/`cuboid.frag.spv` を使用）

## 実装する公開インタフェース（規約・正本）
`spirv.h`:
- `int i915_vk_spirv_parse(const uint32_t *code, size_t words, struct i915_vk_shader_ir **out);`
- `void i915_vk_spirv_free(struct i915_vk_shader_ir *ir);`
- IR 型（compile が読む正本）:
  - `struct i915_vk_shader_ir { enum i915_vk_stage stage; entry 情報; struct i915_vk_io *inputs; *outputs; struct i915_vk_uniform *push/*bindings; struct i915_vk_inst *insts; uint32_t n_insts; uint32_t n_values; };`
  - `struct i915_vk_io { uint32_t location; enum type; uint32_t components; };`（varying/attribute）
  - `struct i915_vk_uniform { uint32_t set, binding; enum kind（push_constant / sampled_image / uniform）; offset/size; };`
  - `struct i915_vk_inst { enum i915_vk_op op; uint32_t dst; uint32_t src[4]; uint32_t imm; uint32_t swizzle; };`（SSA。op = fadd/fmul/fsub/fmad/dot/rsq/sin/cos/compose/extract/load_input/store_output/load_push/sample/... の最小集合）

## 内部関数構成ガイド
SPIR-V header 検査、`OpEntryPoint`/`OpName`/decoration（location/binding/set）収集、type/constant table、`OpFunction` 本体を SSA 命令へ写す、`OpAccessChain`+`OpLoad/OpStore` を input/output/push/binding 参照へ解決、`OpImageSampleImplicitLod` を sample op へ、算術/`OpCompositeConstruct`/`OpVectorShuffle` を対応 op へ。制御流れ命令が現れたら EINVAL（vkdemo は straight-line）。未対応 opcode は明示 EINVAL。

## 依存
- 前段: p001（stage 型、symbol 台帳の対象 opcode）。core 依存なし（純データ変換）。
- 後段: p006 compile が IR を消費。IR 型はこの doc が正本。

## 触れるファイル / 触れないファイル
- 触れる: `vk/spirv.*`、fixture。
- 触れない: 他モジュール `.c`、core、HAL、UAPI、libvulkan。

## 受け入れ条件と試験
- host fixture: vkdemo の 2 shader `.spv` を parse し、期待する input/output/uniform 一覧と命令数・種類を照合。未対応 opcode 入力で EINVAL。メモリは `kern_calloc`、`i915_vk_spirv_free` で解放（ASan/UBSan clean）。
- build 3 構成。実機なし。

## 見積・制限
240 分。in-kernel、float 演算を parser では行わない（定数は bit で保持）。制御流れ・拡張命令は対象外（増分C で必要分のみ追加）。

## 完了（host 検証済み）
`vk/spirv.c` 実装。SPIR-V header 検証、宣言 pass（型/decoration/interface 変数/定数/pointer storage）、body pass（access-chain/load/store を input/output/push 参照へ解決、算術/extinst sin/cos/rsq/sample/compose/extract を IR へ）。gl_PerVertex は Location 無しで builtin 判別。fixture `i915-vk-spirv-test.c` が vkdemo の cuboid.vert/frag.spv を parse し interface（vert: in vec3@0/vec2@1、out vec2@0；frag: in vec2@0、out vec4@0、sampler set0/binding0）と命令種を照合、garbage 拒否、リーク無し。通常＋ASan/UBSan PASS。SPIR-V opcode は Khronos 公開仕様由来。
