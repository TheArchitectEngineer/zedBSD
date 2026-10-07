<!-- awesome-plan project=zedbsd record=ws031-p019 -->
# ws031-p019: 設計: executor の未実装機能（正常系: p032・p033）

Phase ID: `ws031-p019`
Parent: [WS031](../ws.md)
Status: cleared 候補（q833、P1、2026-10-07: 設計の第 2 版、design-reviewer の review（blocking 2・should-fix 9・minor 11）を反映。dual source の feature の公開だけ人の判断（§5）、他は実装へ）
範囲の決定: 2026-10-07 Q1（q833）: 正常系だけ、実機の確認は対象外、host の試験で確かめる。p032（blend の logic op・dual source）と p033（image view の format の読み替え・component swizzle）を実装する。p036（UBO の dataport）・p037（tiling）は実機の確認が要るので後。

## 1. 今の形（2026-10-07 の tree を読んだ）

| 項目 | 今 | 場所 |
| --- | --- | --- |
| logic op | pipeline が `logicOpEnable` を持つと `XXX ... does not apply it` と log し、無視する。feature `logicOp` は答えていない（0） | `render/pipeline.c` 739、`render/instance.c` の `i915_instance_features` |
| dual source | factor が `SRC1_*` の式は blend を切る（anv の「shader が第 2 source を書かない時」と同じ）。compiler は Location 0 の Index 1 の出力を知らない。feature `dualSrcBlend` は 0、`maxFragmentDualSrcAttachments` は 0 | `render/state.c` の `i915_blend_equation`、`compiler/spirv.c`・`compile.c` |
| view の format | `vkCreateImageView` は `view->format` を記録するが、texture と render target の `RENDER_SURFACE_STATE` の SURFACE_FORMAT は **image の format** から作る。`MUTABLE_FORMAT` の view（例: UNORM の image を SRGB で見る）は読み替えられない | `render/image.c` 236、`render/state.c` の `i915_image_surface_write`（1979〜）・target（2828 の整数の判定も image の format） |
| swizzle | 4 成分の `VkComponentSwizzle` を shader channel select に直して `view->channel_select` に持ち、texture の RSS の dword 7 に書く。ただし「0 なら identity」の判定のため、4 成分とも `ZERO` の view（channel select がちょうど 0）は identity になる（`XXX`） | `render/image.c` 245・151、`render/state.c` 2084 |

## 2. 設計

### 2.1 logic op（p032 の 1）

- 出典: Mesa 25.0.7 `src/intel/genxml/gen80.xml` の `BLEND_STATE_ENTRY`（Gen12 も同じ、`gen120.xml` は `gen110.xml`→`gen90.xml`→`gen80.xml` を import）: dword 1 の bit 27..30 が `Logic Op Function`（`3D_Logic_Op_Function`）、bit 31 が `Logic Op Enable`。値は `gen40.xml` の `3D_Logic_Op_Function`（CLEAR 0、NOR 1、AND_INVERTED 2、COPY_INVERTED 3、AND_REVERSE 4、INVERT 5、XOR 6、NAND 7、AND 8、EQUIV 9、NOOP 10、OR_INVERTED 11、COPY 12、OR_REVERSE 13、OR 14、SET 15）。対応は anv の `vk_to_intel_logic_op`（`src/intel/vulkan/genX_gfx_state.c` 87）。
- `intel/genxml.h` に `GEN12_BLEND_LOGIC_OP_ENABLE (1U << 31)`・`GEN12_BLEND_LOGIC_OP_FUNCTION_SHIFT 27U`（dword 1 の）と、`VkLogicOp` → 値の表を `render/state.c` に（anv の表の写し、出典の comment）。
- pipeline: `i915_gfx_decode_blend` が `logic_op_enable`・`logic_op` を pipeline に持つ（XXX の log を消す）。
- BLEND_STATE: logic op が有効な時、**全ての attachment の blend を切り**（Vulkan「blending of all attachments is treated as if it were disabled」、PRM「LogicOp と Color Buffer Blending を同時に有効にすると UNDEFINED」）、target ごとに、format が float か sRGB でなければ dword 1 に Logic Op Enable と Function（anv の `ignores_logic_op`: float と sRGB の target は logic op を素通し、Vulkan「Logical operations are not applied to floating-point or sRGB format color attachments」）。`3DSTATE_PS_BLEND` の Color Buffer Blend Enable も 0（attachment 0 の BLEND_STATE と揃える、既存の約束 state.c 1684）。
- target の format の判定: target の **view の format**（2.3 の後）で、float（`SFLOAT`）・sRGB を見る小さな関数（`i915_state_format_ignores_logic_op`）。
- feature: `logicOp = VK_TRUE`。
- 試験（host、`run-vk-host-tests.sh` の `pipe`・`cmdbuf`）: logic op の pipeline の decode（enable・op）、draw の BLEND_STATE の dword 1 に XOR（6）と Enable、blend の bit が 0、`3DSTATE_PS_BLEND` の blend が 0、float の target（R32G32B32A32_SFLOAT）では Logic Op Enable が 0、feature の答え。

### 2.2 dual source（p032 の 2）

- Vulkan: fragment shader が `Location 0` `Index 0` と `Location 0` `Index 1` に書き、blend の factor が `SRC1_*` を使う。`maxFragmentDualSrcAttachments` は 1（attachment 0 だけ）。
- compiler（`compiler/spirv.c`）: 出力の変数の `Index` の decoration（SPIR-V の Decoration 32、`OpDecorate %var Index 1`）を読み、Location 0 Index 1 を「第 2 の色」として記録する（今の location の表に 1 つ足す: `fs_second_color`）。Index 1 が Location 0 以外、または Index 1 があるのに Location 1 以上にも書く shader は ENOTSUP（Vulkan の制限: dual source は attachment 0 だけ）。
- compiler（`compile.c`）: 第 2 の色がある fragment shader は、location 0 の色を r120..r123、第 2 の色を r124..r127 に置き（payload が src0 の RGBA、続いて src1 の RGBA で連続する。今の location 0 は r124..r127、location 1 は r120..r123 で、dual source では location 1 は使わない）、render target write を 1 つ: message control を `SIMD8 dual source subspan01`（2、Mesa `brw_eu_defines.h` 1304 `BRW_DATAPORT_RENDER_TARGET_WRITE_SIMD8_DUAL_SOURCE_SUBSPAN01`、今の単一 source は 4）、message length 8（今は 4）、binding table entry と render target index は location 0 の物、last で thread を終わる。discard の shader は今と同じく f1.0 で predicate。Gen11+ は header 無し（Mesa `lower_fb_write_logical_send`: header は ver < 11 だけ）。TGL の SIMD16・SIMD32 の dual source の hang（Mesa `brw_compile_fs.cpp` 151〜、Wa_14017468336）は、この compiler が SIMD8 だけなので当たらない。
- kernel の情報: compile の結果に `dual_source`（第 2 の色を書く）を足し、pipeline が持つ。
- state（`render/state.c`）: `i915_blend_equation` の「第 2 source を読む式は blend しない」を、pipeline の fragment shader が `dual_source` の時は blend する（anv の `has_fs_dual_src`）。shader が書かない時は今のまま blend を切る。
- feature: `dualSrcBlend = VK_TRUE`、property `maxFragmentDualSrcAttachments = 1`。
- 試験（host）: `compile` の EU model に dual source の SENDC（descriptor の message control 2・mlen 8・payload 8 register）を足し、Location 0 の Index 0・1 に書く shader（新しい `dual.frag`、`compiler-shaders` に GLSL と `.spv`）を 8 channel で実行し、payload の 8 register が 2 つの色と bit 一致。`spirv`: Index の decoration の decode と、Index 1 の Location 1 は ENOTSUP。`pipe`/`cmdbuf`: dual source の pipeline で BLEND_STATE の factor が SRC1_COLOR（`GEN12_BLENDFACTOR_SRC1_COLOR`）のまま blend が有効、dual source でない shader では今と同じく切る。encoder の往復（`run-vk-gentool-test.sh`、Mesa の `brw_disasm`）は Mesa の tool がある時だけ（無い host では未実施と書く）。

### 2.3 view の format の読み替え（p033 の 1）

- texture: `i915_image_surface_write` の `range` に view の format を足し（`range->format`、0 なら image の format）、SURFACE_FORMAT と texel の大きさの判定はその format から。読み替えは texel の byte 数が同じ物だけ（`MUTABLE_FORMAT` の互換は「同じ class」= 同じ texel の大きさ。違えば今の image の format のまま、log）。depth・stencil の image は読み替えない（view は同じ format しか取らない）。
- render target: target の surface state と「整数の target か」（state.c 2828）・logic op の判定を、target の view の format から。swapchain の UNORM の image を SRGB の view で描く形（`MUTABLE_FORMAT`）が SRGB の書き込みになる。
- `vkCreateImageView` の検べ: image に `MUTABLE_FORMAT` が無いのに format が違う view は Vulkan の誤用（VU）で、正常系の外。今は受けて読み替える（異常系の扱いは backlog）。
- 試験（host、`res`・`cmdbuf`）: R8G8B8A8_UNORM の image（MUTABLE）に R8G8B8A8_SRGB の view: texture の RSS の SURFACE_FORMAT が SRGB の値、target も同じ。R8G8B8A8_UNORM を R32_UINT で見る（同じ 4 byte）: 整数の target として blend が切れる。byte 数の違う view（R16_UNORM）は image の format のまま。

### 2.4 swizzle の「全て ZERO」（p033 の 2）

- `range` に `channel_select_set`（view から来た時 1）を足し、texture は view の channel select をそのまま書く（4 成分 ZERO の 0 も）。render target と、view の無い surface（blit・copy・storage の既存の呼び出し）は今の identity。`image.c` の `XXX` の comment を消す。
- 試験（host、`cmdbuf`）: 4 成分 ZERO の view で texture の RSS の dword 7 の channel select が 0、identity の view で 4/5/6/7、R と B を入れ替えた view で 6/5/4/7。

### 2.5 image の create flags と usage の照合（p016 の残り）

- p016 の「image create flags と input/transient attachment の usage を format 特性の回答で照合」は、`vkGetPhysicalDeviceImageFormatProperties` が create flags を読まない（`instance.c` の XXX）こと。正常系としては `MUTABLE_FORMAT`・`CUBE_COMPATIBLE` を受ける（今も受けている）ので、答えを変える必要は無い。input attachment・transient attachment の usage は executor が実装していない（subpass の input attachment は無い）ので今の「対応しない usage は NOT_SUPPORTED」が正しい。**この設計では変えない**（異常系・誤用の照合は backlog）。

## 3. 触る file

| file | 変更 |
| --- | --- |
| `src/drivers/gpu/i915/intel/genxml.h` | logic op の 2 つの定義、RT write の message control の dual source の値 |
| `src/drivers/gpu/i915/render/pipeline.c`・`pipeline.h` | logic op の decode、kernel の `dual_source` を持つ |
| `src/drivers/gpu/i915/render/state.c` | BLEND_STATE の logic op、dual source の式、view の format・channel select の range |
| `src/drivers/gpu/i915/render/image.c` | `XXX` の comment、view の format の記録（今もある）の使い方 |
| `src/drivers/gpu/i915/render/instance.c` | feature `logicOp`・`dualSrcBlend`、property `maxFragmentDualSrcAttachments` |
| `src/drivers/gpu/i915/compiler/spirv.c`・`compile.c`・`compiler.h` | Index の decoration、dual source の payload と message |
| `src/drivers/gpu/i915/tests/render/compiler-shaders/dual.frag`・`.spv`、`plan/ws031/tests/i915-vk-*-test.c` | 試験 |

触らない: HAL・UAPI（`include/hal/hal.h`・`include/uapi/`）、libvulkan（feature・property の答えは executor の reply をそのまま渡す。確かめる）、display・WSI。

## 4. 確かめ（p032・p033 の実装で）

- `sh plan/ws031/tests/run-vk-host-tests.sh`（既定の一覧、通常と ASan/UBSan）が PASS。足した検査は 2.1〜2.4 の各「試験」。
- `make disk-image`（vmunix の i915）が warning 0。
- 実機（5330）は対象外（Q1、2026-10-07）。QEMU は Venus の経路で i915 の executor を通らないので、T1 の試験は無い（build の確かめだけ）。

## 5. 人間の判断が要る点

§7 の末尾（dual source の feature の公開）。他は無し。

## 6. 積み残し（backlog へ）

- logic op・dual source の実機での色の確かめ（5330、実機の試験の再開の後）。
- `MUTABLE_FORMAT` の無い image の別 format の view を拒むこと（VU の検べ）、`vkGetPhysicalDeviceImageFormatProperties` の create flags（2.5）。
- dual source の SIMD16 以上（この compiler は SIMD8 だけ）。

## 7. review の反映（2026-10-07、design-reviewer、第 2 版）

| 指摘 | 反映（上の節を次のとおり読み替える） |
| --- | --- |
| B1 swizzle: memset・calloc で作った view（kernel の GPU 試験 `tests/render/*.c`、cmdbuf の fixture）の channel select は 0 で、全部 ZERO になる | 「view から来た」印は `struct i915_gfx_view` に置き（`swizzle_set`）、`drv_i915_gfx_create_image_view` だけが立てる。印の無い view は今の identity。試験に「memset の view は 4/5/6/7」を足す |
| B2 attachment の clear（LOAD_OP_CLEAR・vkCmdClearAttachments、`i915_attachment_surface` → `i915_image_surface`）が image の format | `i915_attachment_surface` も 2.3 と同じ規則で view の format にする（depth・stencil は除く）。cmdbuf の試験で rect の stand-in が SRGB の format を受ける |
| S1 XOR は VK と HW の値が同じ | 試験は AND（VK 1 → HW 8）・COPY（3 → 12）、表の 16 個を anv の表と照合 |
| S2 logic op の bit を blends に縛らない | entry の dword 1 の logic op の bit は、blend の有無・整数の target に関わらず書き、float・sRGB の target でだけ止める。試験に attachment 1 が blendEnable=FALSE の形と R8G8B8A8_UINT の target |
| S3 float の判定 | SFLOAT と UFLOAT（`B10G11R11_UFLOAT_PACK32`）の両方 |
| S4 dual source の register | Mesa と同じ split send: src0 = r124..r127（location 0 は今のまま）、src1 = r120..r123（第 2 の色、location 1 は禁止なので空き）。desc は message control 2・mlen 4（`0x08031200`、last は `|0x1000`）、ex_desc は `COMPILE_EX_MLEN(4) | COMPILE_EX_RT_INDEX(0)`。location 0 を動かさない |
| S5 IR への渡し方 | `i915_spirv_id` に `has_index`・`index` を足し decoration 32 を読む（今は「not interpreted」で拒否）。IR の専用の location（既存の値と衝突しない）で第 2 の色を表し、compile の interface の走査・store・prologue の 0 埋めで扱う。Index 1 だけで Index 0 の無い shader は ENOTSUP（Mesa も両方を要る） |
| S6 EU model は独立の証拠にならない | descriptor の各 field を Mesa の `brw_fb_write_desc` の式で作った期待値と照合する試験を足す。gentool（brw_disasm）は今の host に無い |
| S7 fixture に framebuffer・render pass が無い | cmdbuf の fixture に pass と framebuffer を作る（kernel の GPU 試験 `executor.c` 642〜660 の形）。view の無い target は今の `target->format` |
| S8 順序 | p033（view の format、`i915_state_target_format()`）→ p032（logic op はそれを使う）の順に実装する |
| S9 p033 の範囲 | §2.5 のとおり usage の照合は変えない（範囲の変更、ws.md の p033 の行に記録） |
| M1 R16_UNORM は対応外 | 試験は R8G8_UNORM（2 byte）、byte 数が 0 の format は読み替えない |
| M2 texture の range は memset されていない | 新しい field を明示に代入 |
| M3 `i915_image_surface_write` の呼び出し元は texture と target だけ | 記述を直す（blit・copy・storage は `drv_i915_gfx_surface_write`） |
| M4 depth と color の同じ byte 数 | image と view のどちらかが depth・stencil なら読み替えない。圧縮 format と BLOCK_TEXEL_VIEW_COMPATIBLE は executor に無い |
| M5 compile.c は genxml.h を見ない | dual source の message control は compile.c の `COMPILE_DESC_RT_WRITE` の隣 |
| M6 property の場所 | `maxFragmentDualSrcAttachments` は `i915_instance_limits`、feature は `i915_instance_features`。libvulkan はそのまま渡す（確認済み） |
| M7 dual source の印は pipeline の field に写す | `drv_i915_gfx_emit_ps_blend` は pipeline しか受けない |
| M8 PS_BLEND と整数の target の食い違い（既存）、整数の target に blendEnable は誤用 | R32_UINT の view の試験は blendEnable=FALSE で。既存の食い違いは backlog |
| M9 VkLogicOp の範囲 | 16 以上は COPY |
| M10 `dual.frag.spv` | `regenerate.py` の一覧には足さない（GPU の vkc の期待値が要るため、実機の再開の後）。spv は host の compile 試験だけ |
| M11 logic op の enable と blend の enable は同じ bit 31 で dword が違う | 試験で dword 0 の bit 31 が 0、dword 1 の bit 31 が 1 を両方 |

人の判断（§5 の改め）: **dual source の feature（`dualSrcBlend`・`maxFragmentDualSrcAttachments`）を実機の確認なしで公開してよいか**。新しい RT write の message が実機の GPU に流れ、descriptor に誤りがあれば 5330 が hang する恐れがある（独立の disassembler の確認も今の host では動かない）。案: compiler と state は実装するが、feature は実機で確かめるまで 0 のままにする（Q1 へ）。logic op は state の bit だけで hang の恐れが無いので公開する。
