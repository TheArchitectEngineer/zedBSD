<!-- awesome-plan project=zedbsd record=ws031-p020 -->
# ws031-p020: 設計: compiler の 16 bit の整数（p039）

Phase ID: `ws031-p020`
Parent: [WS031](../ws.md)
Status: cleared 候補（q833、P1、2026-10-07: 設計の第 2 版、design-reviewer の review（blocking 3・should-fix 4・minor 6）を反映、人の判断は無し）
範囲の決定（2026-10-07 ユーザー、Q1 経由のクリック）: 「Int16 今、Int64 は後の Phase」。p039 は Int16（`shaderInt16 = TRUE`、host の fixture）。Int64 は後の Phase（ws031-p051、32 bit の対の lowering）。Float64・Float16 は Future Work（Q1 が登録）。
根拠: 5330 の GPU（Gen12 LP、ADL-P）は 64 bit の float も整数も持たない（Mesa 25.0.7 `src/intel/dev/intel_device_info.c`: Gen12 LP の `has_64bit_float = false`・`has_64bit_int = false`。anv は Int64 を 32 bit の対で真似し、Float64 は softfp64）。Float16 は Vulkan 1.0 では `VK_KHR_shader_float16_int8` 無しに使えない（libvulkan は 1.0）。

## 1. 今の形

- `compiler/spirv.c`: `OpTypeInt` の width を記録するが、値を扱う全ての lowering は `i915_spirv_kind_components(…, SCALAR_INT)` を通り、width 32 だけを数える（8224〜）。16 bit の整数の値を使う命令は拒否される。`OpSConvert`（114）・`OpUConvert`（113）は無い。`OpCapability` は検べずに通す（1277）。
- IR の値は channel ごとに 32 bit の scalar。整数の命令は `IADD`・`ISUB`・`IMUL`・`INEG`・`UDIV`・`UMOD`・`IDIV`・`IREM`・`IAND`・`IOR`・`IXOR`・`INOT`・`SHL`・`SHR`・`ASR`・`ILT`・`IGE`・`ULT`・`UGE`・`IEQ`・`INE`、変換 `F2I`・`F2U`・`I2F`・`U2F`（`compiler/ir.h`）。

## 2. 設計

### 2.1 値の持ち方

- 16 bit の整数（int16・uint16、その vector）は IR の 32 bit の値 1 つに持つ。意味があるのは下位 16 bit で、上位 16 bit は不定（「汚れている」）としてよい。加減乗・論理積和排他・否定・左 shift は下位 16 bit を正しく作るので、結果を正規化しない（命令を増やさない）。
- 上位の bit が結果に効く命令の前でだけ、operand を広げる:
  - 符号付きに広げる（sign extend）: `SHL 16` → `ASR 16`。
  - 符号無しに広げる（zero extend）: `IAND 0xFFFF`。
- 定数（`OpConstant` の 16 bit）は 1 word で、SPIR-V の規則で上位は符号に従って埋まっている（そのまま使える）。

### 2.2 命令ごと

| 命令 | 16 bit の時 |
| --- | --- |
| `OpIAdd`・`OpISub`・`OpIMul`・`OpBitwiseAnd/Or/Xor`・`OpNot`・`OpSNegate`・`OpShiftLeftLogical` | そのまま（下位 16 bit が正しい） |
| `OpShiftRightArithmetic` | 被 shift 数を sign extend してから `ASR` |
| `OpShiftRightLogical` | 被 shift 数を zero extend してから `SHR` |
| shift 数（16 bit の時） | zero extend（16 以上は SPIR-V で未定義、汚れた上位で大きな数にしない） |
| `OpSDiv`・`OpSRem`・`OpSMod` | 両方を sign extend してから今の lowering |
| `OpUDiv`・`OpUMod` | 両方を zero extend してから |
| `OpSLessThan` ほか符号付きの比較 | 両方を sign extend |
| `OpULessThan` ほか符号無しの比較、`OpIEqual`・`OpINotEqual` | 両方を zero extend |
| `OpSConvert` | 16 → 32: sign extend。32 → 16・16 → 16: そのまま |
| `OpUConvert` | 16 → 32: zero extend。32 → 16・16 → 16: そのまま |
| `OpConvertSToF`（16 bit から） | sign extend して `I2F` |
| `OpConvertUToF`（16 bit から） | zero extend して `U2F` |
| `OpConvertFToS`・`OpConvertFToU`（16 bit へ） | 32 bit へ変換したまま（範囲外は SPIR-V で未定義） |
| `OpBitcast` | int16 ↔ uint16（同じ数の成分）はそのまま。`vec2` の 16 bit ↔ 32 bit の scalar（`vec4` ↔ `vec2` も）は pack: `(lo IAND 0xFFFF) IOR (hi SHL 16)`、unpack: lo = 値そのもの（汚れてよい）、hi = 値 `SHR 16` |
| `OpSelect`・`OpPhi`・`OpCompositeConstruct/Extract`・`OpVectorShuffle`・`OpCopyObject`・Function の変数の load/store | そのまま（bit を運ぶだけ） |

- 16 bit の値を memory の interface（UBO・SSBO・push constant・input・output）に置くことは、Int16 の capability だけでは許されない（`StorageBuffer16BitAccess` などの拡張が要る）。今どおり拒否する。
- `OpIAddCarry` などの拡張の算術、bit 操作（`OpBitCount` など）は、32 bit で無い物は 16 bit でも無い（拒否のまま）。

### 2.3 実装の形

- `spirv.c` に `i915_spirv_int_width(parser, type_id)`（scalar か vector の整数の width、16 か 32）と、`i915_spirv_extend16(parser, value, is_signed)`（2.1 の 2 命令か 1 命令、32 bit の値はそのまま）を足す。
- `i915_spirv_kind_components` の `SCALAR_INT` は width 16 も数える（Boolean・float は今どおり）。
- 2.2 の各 lowering（`i915_spirv_lower_integer`・`_integer_unary`・比較・`_convert`・`_bitcast`）で、operand の型が 16 bit なら必要な所に extend を挟む。`OpSConvert`・`OpUConvert` の lowering を新しく足す。
- feature: `shaderInt16 = VK_TRUE`（`render/instance.c` の `i915_instance_features`）。

### 2.4 試験（host）

- `compile`（EU model）: `int16.frag`（`compiler-shaders`、GLSL の `GL_EXT_shader_explicit_arithmetic_types_int16`）が flat の 32 bit の整数の input から int16・uint16 を作り、加減乗除・剰余・shift（算術・論理）・比較（符号付き・無し）・変換（S・U・float）・pack の bitcast を計算して float で出す。8 channel ずつ、値を C で同じ規則（16 bit の wrap）で計算した参照と bit 一致。負の数・0x8000・0xFFFF の境界を channel に入れる。
- `lower`（IR の interpreter）: 同じ shader を IR で評価して参照と一致（lower の fixture が評価できる範囲で）。
- `spirv`: 16 bit の値の memory の interface（16 bit の UBO の member）が拒否のまま。
- feature の答え（`resdispatch` が features を見ているなら）。

## 3. 触る file

`src/drivers/gpu/i915/compiler/spirv.c`、`render/instance.c`、`src/drivers/gpu/i915/tests/render/compiler-shaders/int16.frag`・`.spv`、`plan/ws031/tests/i915-vk-compile-test.c`（と lower）。HAL・UAPI・libvulkan は触らない（feature はそのまま渡る）。

## 4. 人間の判断

無し（範囲はユーザーが決めた。命令の意味は SPIR-V の仕様に従う）。

## 5. Int64（後の Phase、ws031-p051）の覚え

- 値を 32 bit の対（lo・hi）にし、IR の値を 2 つ使う。加減は carry（`ULT` で作る）、乗算は 32 x 32 → 64 の部分積（今の 32 bit の乗算の上位を取る lowering を使う）、shift は対をまたぐ、比較は hi の後に lo、変換は対と float の間（Mesa の `nir_lower_int64` が手本）。

## 7. review の反映（2026-10-07、design-reviewer、第 2 版。§2・§2.4 をこのとおり読み替える）

| 指摘 | 反映 |
| --- | --- |
| B1 `kind_components(SCALAR_INT)` を広げると約 30 の呼び出し元（UBO・SSBO・push・input・output・switch・image query・GLSL.std.450）が 16 bit を 32 bit として通す | `i915_spirv_int_components` は 32 bit のまま。16 bit も数える別の helper（`i915_spirv_int_components_any`）を、確かめた呼び出し元だけで使う: 整数・単項・比較・変換・bitcast・select・phi・construct・extract・insert・shuffle・copy・local の load/store・定数・GLSL.std.450 の整数。宣言（`i915_spirv_declare_variable`）で、Input・Output・Uniform・StorageBuffer・PushConstant の変数の型に 16 bit の整数があれば ENOTSUP（理由つき、深さの上限のある再帰） |
| B2 glslc は 16 bit の動的な index を出す（Function の配列・UBO・shared の access chain） | `chain_block`・`chain_dynamic`・`chain_shared` で、16 bit の index を sign extend してから `dynamic_index` に（SPIR-V の index は符号付き、Mesa も `nir_i2iN`）。`OpSwitch` の selector が 16 bit なら zero extend し literal を 16 bit に（glslang は出さないが他の producer）。image の座標・LOD の 16 bit は拒否のまま（width の検べを明示） |
| B3 GLSL.std.450 の SAbs・SSign・SMin・SMax・SClamp・UMin・UMax・UClamp | 符号付きは全 operand を sign extend、符号無しは zero extend してから今の lowering |
| S1 shift の operand の width は別々（base と count） | base の width は結果の型、count は自分の型。count は extend しない（EU・interpreter・EU model とも count & 31、16 以上は未定義）。他の二項と比較は、operand の width が結果・互いと違えば拒否 |
| S2 Workgroup の int16（`shaderInt16` は Function・Private・Workgroup を許す） | 技術の選択として対応する: shared の 16 bit の整数は 4 byte の slot（shared の配置は実装の定義、外から見えない）。load は word、store は word（汚れた上位も）、atomic は今どおり 32 bit だけ。Private は 32 bit でも拒否のまま（記録） |
| S3 試験が汚れた operand を作らないと extend の抜けを見逃す | generality-shaders の形（pixel の座標の input、32 bit の語の出力、`regenerate.py` の Python の参照）で `int16.frag`。上位が非自明な値の切り詰め（`int16_t(0x12348000)`）、wrap（`0x7FFFs + 1s`）、各消費者（除算・剰余・SMod・SHR・ASR・比較の符号付き・無し・符号の混ざった IEqual・S/U→F・min/max/clamp/abs/sign・local と UBO の動的な index・pack の bitcast）に汚れた operand を少なくとも 1 channel。拒否の試験は `i915-vk-lower-test.c` の `test_refusals`（手組みの module）: UBO の offset 0 の member・SSBO の store・push・Input・Output |
| S4 ws.md と合わない | ws.md: p020 は「p039（Int16）の設計」、p039 は「16 bit の整数」、新しい ws031-p051（Int64、32 bit の対）を planned で足す。p042・p043 の設計は別（p020 には無い） |
| M1 定数 | 定数も汚れうる値として扱い、`->constant` を直に読む所（switch の literal・texel offset・定数の index）は型の width で mask か extend |
| M2 同じ width の SConvert・UConvert | 不正な SPIR-V として EINVAL |
| M3 bitcast | 32 bit の側は int・float の scalar か vec2。合計の bit 数が合わなければ拒否。成分 0 が下位 |
| M4 費用 | 16 と 0xFFFF の定数は `shared_constant` で共有、extend した値は id ごとに 1 度だけ（cache）。SEXT16 の IR の命令は作らない |
| M5 | `i915_spirv_extend16(parser, value, type_id, is_signed)`: 型の width が 32 なら値をそのまま |
| M6 | 実機の vke2 は WS の終わりに T1（実機の再開の後）。今回は host だけ |
