# WS031 p006 計画: compile — baseline IR → GEN codegen

spirv の IR を、eu の emit を使って GEN バイナリへ 1 対 1・最適化なしで lower する。おおまかな設計。

## Module と所有ファイル
- `src/drivers/gpu/i915/vk/compile.c`, `compile.h`
- fixture: `plan/ws031/tests/i915-vk-compile-test.c`

## 実装する公開インタフェース（規約・正本）
`compile.h`:
- `int i915_vk_compile(struct i915_vk_device *vk, const struct i915_vk_shader_ir *ir, struct i915_vk_shader_binary **out);`
- `void i915_vk_shader_binary_free(struct i915_vk_shader_binary *);`
- `struct i915_vk_shader_binary`（pipe/cmdbuf が消費する正本）:
  - GEN バイナリを収めた GEM object（`struct i915_vk_memory *code` または GEM handle）＋ code bytes／entry offset
  - `uint32_t grf_used; uint32_t simd; uint32_t threads;`
  - VS: `struct { uint32_t location; uint32_t urb_offset; } *outputs; n;`（varying→URB 配置）、入力 attribute→GRF payload 対応
  - FS: RT 出力位置、入力 varying の SBE 受け、push constant 配置
  - `uint32_t bindings[]`（sampler/surface index 使用表）

## 内部関数構成ガイド
SSA 値→GRF の素朴割当（値ごとに 1 GRF、足りなければ増やす。spill は最小/未対応で EINVAL）、IR 命令→eu emit の 1 対 1 lower、swizzle/compose は mov 列、sin/cos/rsq は eu_math、sample は payload 構築＋eu_send（sampler）、VS 出力は URB write（send）、FS 出力は RT write（send, eot）。push constant は push 定数領域から mov。thread payload レイアウト（VS: VUE handle/attr、FS: pixel/barycentric）。

## 依存
- 前段: p004 spirv（IR 型が正本）、p005 eu（emit と reg 型）。
- WS029 core: GEN バイナリを置く GEM は res 経由または `drv_i915_gem_create`（p003 の memory を使うのが望ましい）。
- 後段: p007 pipe / p008 cmdbuf がバイナリと配置情報を使う。

## 触れるファイル / 触れないファイル
- 触れる: `vk/compile.*`、fixture。
- 触れない: 他モジュール `.c`（`spirv.h`/`eu.h`/`res.h` は include）、core（許可関数のみ）、HAL、UAPI、libvulkan。

## 受け入れ条件と試験
- host fixture: (1) passthrough VS＋定数色 FS（増分A）を compile し、期待 GEN 命令列の要点（RT write, eot 等）を照合。(2) vkdemo VS/FS（増分C）を compile し、sample/math/push を含むことを確認。GRF 割当が破綻しない。ASan/UBSan clean。
- build 3 構成。実機での実行は p008/p011。

## 見積・制限
360 分。最適化なし・SIMD8・spill 最小。増分A（定数 FS）→B（sample）→C（math/push）の順で対応命令を広げる。

## 完了（build-passing 基準、host 検証済み）
`vk/compile.c` 実装。SSA値→GRF 素朴割当（値ごとに 1 GRF、16 から）、IR op を eu emit へ 1 対 1 lower（load_input/push/store_output は mov、fadd/fsub→add、fmul/dot→mul、mad、sin/cos/rsq→math、sample→send、compose/extract→mov）、末尾に output 送信＋EOT の send。binary は encode 済み word を保持（caller が GEM 配置）。fixture が vkdemo frag/vert を parse→compile し非空 binary・SEND 終端・MATH(vert) を照合、通常＋ASan/UBSan PASS。
補正待ち: payload/push/output の GRF 規約、message descriptor、SWSB、negate（fsub）、dot の多成分。値は Mesa 転記で配置は正、semantics は実機で確定。
