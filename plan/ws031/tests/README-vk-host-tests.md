# Vulkan executor の host fixture（run-vk-host-tests.sh）

`sh plan/ws031/tests/run-vk-host-tests.sh [名前...]` が既定一覧
`cmd spirv lower res resdispatch sync eu compile pipe cmdbuf` を通常 build と ASan/UBSan build で走らせる。

- `cmd spirv lower eu compile`: 試験する `.c` を fixture が `#include` する（従来どおり）。
- `res resdispatch pipe cmdbuf sync`: fixture を executor 本体と link する。`render/` のうち
  `draw.c`・`blit.c`（GPU で実行する部分）以外の全ファイルと `compiler/` を、kernel と同じく 1 ファイル 1 翻訳単位で
  build する（`render/*.c` の複数が `data/vulkan-codec.inc` の static 関数を持つため、1 翻訳単位にはまとめられない）。
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
