# WS031 p005 計画: eu — Gen12 EU 命令・message エンコーダ

Gen12（Xe-LP）の EU 命令を byte 列へ encode する low-level emitter。値（bit layout・opcode・message descriptor）は出典付き `.inc`、ロジックは新規。おおまかな設計。

## Module と所有ファイル
- `src/drivers/gpu/i915/vk/eu.c`, `eu.h`
- `src/drivers/gpu/i915/vk/linux/eu-encoding.inc`（EU 命令 opcode/フィールド位置、compaction 不使用、`send` message descriptor。Intel PRM / Mesa `src/intel/compiler` の brw_eu_emit 相当を出典・SHA 付きで転記）
- fixture: `plan/ws031/tests/i915-vk-eu-test.c`
- 監査追記: `plan/ws031/i915-vk-license-audit.md` に参照 Mesa/PRM を追加。

## 実装する公開インタフェース（規約・正本）
`eu.h`:
- `struct i915_vk_eu_buf`（growable な 128bit 命令列。`kern_calloc` backing）: `i915_vk_eu_init/free`、`const uint32_t *i915_vk_eu_data(buf, size_t *bytes);`
- register 記述子 `struct i915_vk_eu_reg`（file=GRF/IMM/ARF、nr、subnr、type=F/D/UD、region=vstride/width/hstride）とヘルパ `i915_vk_eu_grf(nr)`,`i915_vk_eu_imm_f(bits)`,`i915_vk_eu_null()`。
- 命令 emit（SIMD8 既定、exec_size 引数）:
  - `void i915_vk_eu_mov(buf, dst, src);`
  - `void i915_vk_eu_alu(buf, enum op, dst, s0, s1);`（add/mul/sub/and/or/shl/...）
  - `void i915_vk_eu_mad(buf, dst, s0, s1, s2);`
  - `void i915_vk_eu_math(buf, enum func, dst, s0, s1);`（sin/cos/rsq/inv/sqrt）
  - `void i915_vk_eu_sel/cmp(...)`
  - `void i915_vk_eu_send(buf, dst, src, uint32_t sfid, uint32_t desc, uint32_t ex_desc, uint32_t mlen, uint32_t rlen, bool eot);`（sampler / RT write / URB は send で）
  - `void i915_vk_eu_nop(buf);` `void i915_vk_eu_eot(buf);`
- 命令選択（どの命令を出すか）は compile の責務。ここは「与えられた命令を正しく byte 化」だけ。

## 内部関数構成ガイド
128bit（4×u32）命令語の各フィールドを `.inc` の位置定義で組む、region/type エンコード、`send` の descriptor 合成、exec size/qtr control。compaction は使わない（実装単純化）。

## 依存
- 前段: p001（symbol 台帳の対象命令）。core 依存なし。
- 後段: p006 compile が emit を呼ぶ。

## 触れるファイル / 触れないファイル
- 触れる: `vk/eu.*`、`vk/linux/eu-encoding.inc`、`plan/ws031/tests/*`、license 監査 doc。
- 触れない: 他モジュール `.c`、core、HAL、UAPI、libvulkan。

## 受け入れ条件と試験
- host fixture: 代表命令（mov/add/mul/mad/math/send/nop/eot）を emit し、**既知の GEN byte 列**（PRM/Mesa 逆アセンブルで検証した期待値）と照合。境界（region、immediate、send descriptor）を確認。
- build 3 構成。実機なし（実 GPU 実行は増分Aで）。

## 見積・制限
300 分。SIMD8 固定、compaction 無し。命令集合は vkdemo に必要な最小から始め、増分で追加。

## 完了（build-passing 基準、field 照合済み）
`vk/eu.c`＋`vk/linux/eu-encoding-gen12.inc`（mesa-23.1.0 の表形式 brw_inst.h 12+ 列、brw_eu.c opcode_descs、brw_reg_type.c を転記、監査記録あり）。Gen12 128-bit 命令を 4 word で encode。mov(97)/add(64)/mul(65)/math(56)/nop(96)、SIMD8、dst/src0/src1 の reg file(2bit split)/type(F=9)/reg_nr/subreg/region、immediate(last word)。fixture が opcode/exec_size/dst・src reg_nr/is_imm/imm/math func の bit 配置を照合、通常＋ASan/UBSan PASS。
補正待ち（ビッグバンテスト）: 3-source(mad) と send の operand/descriptor、SWSB 依存 scoreboard。値は Mesa 転記なので配置は正、semantics は実機で確定。
