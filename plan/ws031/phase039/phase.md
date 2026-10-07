<!-- awesome-plan project=zedbsd record=ws031-p039 -->
# ws031-p039: compiler: 16 bit の整数（Int16）

Phase ID: `ws031-p039`
Parent: [WS031](../ws.md)
Status: cleared 候補（q833、P1、2026-10-07: 実装と host の試験 PASS。実機は対象外）
設計: [p020](../phase020/phase.md) §2・§7。範囲（2026-10-07 ユーザー）: Int16 だけ、Int64 は ws031-p051、Float64・Float16 は Future Work（F-080）。

## 実装（2026-10-07、P1、`compiler/spirv.c`・`spirv-compute.inc`・`render/instance.c`）

- 16 bit の整数は 32 bit の IR の値に持ち、上位 16 bit は不定。`i915_spirv_kind_components` は 16 bit の整数も数え、宣言で Input・Output・PushConstant・Uniform・StorageBuffer の変数の型に 16 bit の整数があれば拒否（`i915_spirv_type_has_int16`、"16-bit integer in an input, output, push constant or buffer"）。switch の selector・image の結果・query・PackHalf は 32 bit だけ（明示の検べ）。
- `i915_spirv_extend16`（sign: SHL 16・ASR 16、zero: IAND 0xFFFF、定数は共有）を上位が効く所で: 除算・剰余・SMod（sign）、UDiv・UMod（zero）、ASR（被 shift 数を sign）・SHR（zero）、比較（符号付きの順序は sign、他と等否は zero）、S/U → F、GLSL.std.450 の SAbs・SSign・SMin・SMax・SClamp（sign）と UMin・UMax・UClamp（zero）、access chain の 16 bit の動的な index（sign、Function・UBO・shared）。shift の count は extend しない（& 31）。二項・比較の operand の width の不一致は拒否。
- `OpSConvert`・`OpUConvert`（新）: 16 → 32 は extend、32 → 16 は名前だけ、同じ width は EINVAL。`OpBitcast`: 合計の bit が同じ物、`vec2` の 16 bit ↔ 32 bit の scalar（成分 0 が下位）の pack・unpack。
- shared の 16 bit の整数は 4 byte の slot（atomic は 32 bit だけ）。
- feature `shaderInt16 = VK_TRUE`。

## 試験（host）

| 試験 | 中身 |
| --- | --- |
| `int16.frag`（generality-shaders、`regenerate.py` に `int16_pixel` の参照） | 32 bit の境界の値（0x12348000・0x7ffe0005 など上位の非自明な値）を 16 bit に切り詰めた operand で、16 の操作（S/U の除算・剰余、3 つの shift、wrap の乗加減、S/U の min・max、比較 8 種（符号の混ざった等否を含む）、S/U → F、abs・clamp・sign、pack32・unpack16 と汚れた 16 bit の index）。IR の interpreter（`lower`）と EU model（`compile`）で 4096 pixel が bit 一致 |
| 変異の確かめ | `i915_spirv_extend16` を何もしない形にすると `lower` が FAIL（試験が extend の抜けを見つける） |
| 拒否（`plan/ws031/tests/int16/*.frag`、`compile` の `test_int16_refusals`） | UBO の offset 0 の member・SSBO の store・push・input・output の 16 bit が ENOTSUP |
| 回帰 | WS031 の host fixture の全部（通常と ASan/UBSan）PASS、他の generality の data は不変（`regenerate.py` は既存の spv を変えない） |

`make -j16 disk-image` exit 0、自前の warning 0。
未実施: 実機の vke2（kernel の `generality.c` は int16 の step を流していない、実機の再開の後）、shared の 16 bit の compute の試験、規約の見直し（p048）。
