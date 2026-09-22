# Vulkan executor の host fixture（run-vk-host-tests.sh）

`sh plan/ws031/tests/run-vk-host-tests.sh [名前...]` が既定一覧
`cmd spirv lower res resdispatch sync eu compile pipe cmdbuf` を通常 build と ASan/UBSan build で走らせる。

- `cmd spirv lower eu compile`: 試験する `.c` を fixture が `#include` する（従来どおり）。
- `res resdispatch pipe cmdbuf sync`: fixture を executor 本体と link する。`render/` のうち
  `draw.c`・`blit.c`（GPU で実行する部分）以外の全ファイルと `compiler/` を、kernel と同じく 1 ファイル 1 翻訳単位で
  build する（`render/*.c` の複数が `render/vulkan-codec.inc` の static 関数を持つため、1 翻訳単位にはまとめられない）。
  kernel 側の依存と GPU 実行は `i915-vk-render-stubs.inc` が与える:
  kern_malloc/calloc/free（生存 block を数え、失われた block も到達可能に保つ）、kern_logf（最後の行）、
  direct map（blob の物理アドレス = host pointer）、`drv_i915_gfx_draw`・`drv_i915_gfx_rect`・
  `drv_i915_gfx_session_close` の stand-in（呼ばれた内容を記録）、wire builder、slot 3 に reply blob を持つ session。
  旧 `i915-vk-e127-stubs.inc` はこれに置き換えた。

## 移した検査（旧 vk/ module → 新しい所有者）

| fixture | 旧 | 新 |
| --- | --- | --- |
| res | `vk/res.c` の memory/buffer/image API（GEM 確保・map）、image の surface state、descriptor API | wire 経由で `render/memory.c`・`image.c`・`descriptor.c`。memory の storage は libvulkan の blob（`drv_i915_render_blob_attach/detach`、`drv_i915_gfx_memory_cpu/va`）。surface state は `render/state.c` の `drv_i915_gfx_surface_write` |
| resdispatch | `vk/cmd.c` + `vk/res.c` の wire decode と reply 枠、`i915_vk_command_reply` | `render/vulkan.c` → `dispatch.c` → `objects.c`・`memory.c`・`image.c`。reply 領域は `drv_i915_render_transport_reply`、select/seek/version probe は `transport.c` |
| pipe | `vk/pipe.c` の shader module・graphics pipeline の wire decode、3DSTATE 発行 | `render/pipeline.c`・`pipeline-prepare.c`（executor の compiler で vkdemo shader を compile）。VS/PS の state は `render/state.c` の `drv_i915_gfx_emit_vertex_shader/pixel_shader` に、compile 済み kernel から出す |
| cmdbuf | `vk/cmdbuf.c` の記録（記録時に GPU 命令を書く）、lifecycle、vkQueueSubmit | `render/command.c`（操作列として記録し、submit で実行）。submit で GPU 経路へ渡る内容（clear の rect、draw の pipeline・vertex buffer・push constant・回数）を stand-in で確認。3DPRIMITIVE と MI_BATCH_BUFFER_END は `render/state.c` の `drv_i915_gfx_emit_primitive` |
| sync | `vk/sync.c` の fence・semaphore・query | fence は `render/fence.c`（opcode 35–38、latch）、semaphore は `render/sync.c` |
| cmd | 経路表の 35・42 を「sync 未移植」として検査 | sync の範囲は `fence.c` へ経路が付いた。35 は fence 作成、42 は `fence.c` が名乗って拒否する検査に変更 |

## 廃止した検査と理由

| fixture | 検査 | 理由 |
| --- | --- | --- |
| res | descriptor pool が maxSets を超える確保を ENOSPC で拒否し、set の解放で枠が戻る | 新 `descriptor.c` の pool は設計上何も制限しない（set は小さな host object）。vkFreeDescriptorSets（78）は移植されておらず、経路表で未移植 module として拒否される（fixture はこの拒否を検査する） |
| res | memory が GEM object を確保し CPU map を返す | executor は memory の storage を確保しない。storage は libvulkan が export する blob で、blob attach の検査に置き換えた |
| pipe | GEM object 数で「失敗した pipeline が何も残さない」ことを見る | pipeline の kernel は host memory に置かれ GEM を使わない。生存 block 数で見る（下記の既知の漏れを参照） |
| cmdbuf | 記録時に batch へ 3DSTATE_VS/PS・3DPRIMITIVE が書かれる | 新方式は記録時に GPU 命令を書かず、submit 時に `draw.c` が書く。命令列は `state.c` の発行関数で、記録内容は draw stand-in で検査する |
| cmdbuf | pipeline 未 bind の draw が EINVAL | 拒否は `draw.c`（GPU 実行経路。session の GEM object と worker を使う）にあり、host に link しない |
| cmdbuf / sync | fence を RCS0 の seqno に arm し、engine の完了 seqno で signal / wait が timeout | 旧 `vk/sync.c` の arm/wait は未移植の wsi/cmdbuf からしか使われず、移植されていない。新 fence は submit が最後まで走った後に latch する |
| sync | query pool の作成・破棄 | query pool（47–49）は vkdemo から到達せず移植されていない。opcode 47 と vkWaitForFences（39）が `fence.c` で拒否されることを検査する |

`vk/wsi.c`・`vk/display.c` の検査は元から既定一覧に無い（S3 報告: libvulkan から到達しない）。

## p014 段階 A で足した検査

| fixture | 検査 |
| --- | --- |
| pipe | 実行器試験の shader（`src/drivers/gpu/i915/tests/render/shaders/place.vert` + `push.frag` の `.spv`）で、viewport/scissor を dynamic にした pipeline（viewport state は数だけ・配列なし、dynamic state に VIEWPORT/SCISSOR）を作る。`dynamic_viewport`/`dynamic_scissor` が立ち、fragment shader が byte 112 の push colour を読む（push 4 register、vertex は 1）pipeline が受理され、`3DSTATE_CONSTANT_VS`/`PS` が同じ block を buffer 3 から読み、`3DSTATE_PS` dword 6 の Push Constant Enable が立つ |
| cmdbuf | 3DPRIMITIVE の random access・start/base vertex・instance、`3DSTATE_INDEX_BUFFER`（MOCS・WORD/DWORD・L3 bypass disable・address・size、offset の境界と整列、UINT8 は ENOTSUP）。`vkCmdBindIndexBuffer`/`vkCmdSetViewport`/`vkCmdSetScissor`（index 1 は捨てる）/`vkCmdDrawIndexed` が draw stand-in に届く内容。`vkCmdCopyBuffer` が rect stand-in に渡す surface（4096 texel 行 + 端数行、R8G8B8A8、va）と、4 の倍数でない region・範囲外 region の失敗。200 操作の command buffer（list が伸びる）と 65537 操作での end の失敗。未実装 recording の検査は opcode 96 に変更 |

## p014 段階 B で足した検査（texture と mip）

| fixture | 検査 |
| --- | --- |
| res | 64x64・7 level の RGBA8 image が 2D mip layout（level 1 は level 0 の下、level 2 はその右、以降は level 2 の下、各 level は 4x4 texel 単位に切上げ）で pitch 256・100 行、要求 memory が chain 全体（28672）、level 3 の subresource layout（offset・size・row pitch）、5x3・3 level の端数、`drv_i915_gfx_image_level` の level 2・6 の va/寸法と level 7 の拒否。depth の 2 level と 64x64 の 8 level は ENOTSUP。view の levels（`VK_REMAINING_MIP_LEVELS` → 6）と、範囲外・0 level・末尾超えの拒否。sampler の mipmap mode・bias・LOD 範囲（float bit）と `SAMPLER_STATE`（MIPFILTER LINEAR/NEAREST、bias s4.8 正負、Max LOD の 14 clamp、Min LOD）、draw の texture `RENDER_SURFACE_STATE`（QPitch、MIP count 5・Surface Min LOD 1・mip tail start 7、level 0 の寸法と先頭 address）と render target（MIP count 0）、0 level の view で draw の state が EINVAL |
| cmdbuf | 32x32・6 level の image に `vkCmdCopyBufferToImage`（level 2）、同一 image の level 0→1 の linear `vkCmdBlitImage`、level 3 からの残り全 level の `vkCmdClearColorImage`（3 level を 1 回ずつ fill）、level 5 の `vkCmdCopyImageToBuffer` が rect stand-in に渡す surface（各 level の va・寸法・共有 pitch）。存在しない level 6 への copy で submit が失敗 |

## p014 段階 C で足した検査（SPIR-V compiler）

| fixture | 検査 |
| --- | --- |
| lower | IR interpreter を bit 単位（float と Boolean）に拡張。`src/drivers/gpu/i915/tests/render/compiler-shaders/*.spv` と mview の 3 shader（`userland/base/mview/shaders/`）を C で評価した GLSL と比較: GLSL.std.450 の abs/floor/fract/sqrt/exp2/log2/pow/inversesqrt/min/max/clamp/mix/normalize と a / b（11 入力）、比較 12 種（FOrd*/FUnord*、NaN を片側・両側、手組み SPIR-V）、`&&`/`||`（phi）、`?:`（分岐と local）、入れ子の if/else と discard を 64x64 の全画素、分岐内 return（leak）、vertex の normalize/max/clamp（OpConstantComposite）、mview.vert・mview.frag・cutout.frag（alpha 0.5 未満で discard）。拒否は FMod・OpLoopMerge・後方分岐・OpSwitch・vertex の OpKill |
| compile | EU model を bit 単位・flag register・predication（SEL は選択、他は mask）・CMP・SEL・AND/OR/NOT・RNDD/FRC・abs・math（INV/SQRT/LOG/EXP）・sampler（偽 texture）・predicate 付き SENDC（書いた channel を記録）に拡張。上の shader を SPIR-V → IR → EU 語で 8 channel ずつ実行し参照と bit 一致: 数学 4 本 x 32 回、compare（NaN channel 入り）、branch（64x64、dispatch 内で分岐が割れる 86 回）、discard（全 channel 破棄 256 回・一部 96 回、dispatch mask 0xA5 でも未 dispatch channel を書かない）、cells/vsmath の vertex、mview.vert（payload r2..r17、値は r19 から、payload 不変）・mview.frag・cutout.frag。vkdemo の kernel は段階 C 前と byte 一致 |
| eu | CMP（cond・flag・predicate）、discard の predicate 付き CMP、SEL（cond / predicate）、AND/OR/NOT/RNDD/FRC、abs、flag load（SIMD1 NoMask、f1.0 ← r1.7 UW）、math LOG/EXP、predicate 付き SENDC の各 field と拒否 |
| pipe | 「lower できない shader」を OpFDiv（lower されるようになった）から OpFMod に変更 |

`run-vk-gentool-test.sh` は mview と compiler-shaders の全 `.spv` も判定する。gentool（Mesa main）が無い環境では
`BRW_TOOLS=<Mesa 25.0 の build>/src/intel/compiler` で `brw_disasm`/`brw_asm` の往復に切り替わる（判定は同じ）。

## fixture が固定している既知の問題（production は変更していない）

- `render/pipeline.c`: kernel の準備に失敗した vkCreateGraphicsPipelines は、公開しなかった pipeline の record を
  解放しない（XXX "happy path only"）。pipe fixture は 1 block が残ることを確かめたうえで回収する。
- descriptor set を解放する経路が無い（78 は拒否、pool の破棄も session の close も set を解放しない）。
  res fixture は set を自分で外して解放する。
- `drv_i915_gfx_create_reply` などの応答本体は、command header の reply 要求 flag が 0 でも書かれる
  （echo の opcode だけが flag に従う）。libvulkan は object command を常に flag 1 で送る（`vulkan_command_begin`）ので
  現状は到達しない。fixture は flag 0 を void command（vkFreeMemory）でだけ使う。

## analyzer

`sh plan/ws031/tests/run-vk-analyzer.sh` は `render/*.c` と `compiler/*.c` を gcc -fanalyzer（kernel 設定）にかけ、
compile できないか警告が 1 件でもあれば失敗する。出力は `plan/ws031/phase012/analyzer-gcc.log`。
