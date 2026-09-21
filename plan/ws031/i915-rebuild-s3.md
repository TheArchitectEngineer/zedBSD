# i915 再構築 S2/S3 — compiler と render の分担・名前

[実施計画](i915-rebuild-plan.md)、[共通規則](i915-rebuild-rules.md)。旧ソース: `src/drivers/gpu/i915-old/vk/`。

## 1. 名前（並行作業のために固定する）

| 旧 | 新 | 所有 |
| --- | --- | --- |
| `struct i915_vk_reader` / `i915_vk_writer` / `i915_vk_arena` | `struct i915_wire_reader` / `i915_wire_writer` / `i915_wire_arena` | S3a `render/internal.h` |
| `struct i915_vk_device` / `i915_vk_session` / `i915_vk_batch` | `struct i915_render_device` / `i915_render_session` / `i915_render_batch` | S3a `render/internal.h` |
| `i915_vk_read_u32/u64/handle/array`、`i915_vk_reply_u32/u64/blob` | `drv_i915_wire_read_u32/u64/handle/array`、`drv_i915_wire_reply_u32/u64/bytes` | S3a `render/codec.h` |
| `i915_vk_object_table_create/destroy`、`i915_vk_obj_insert/lookup/remove` | `drv_i915_object_table_create/destroy`、`drv_i915_object_insert/lookup/remove` | S3a `render/object.h` |
| `drv_i915_vk_attach/detach/open/close/command/blob_attach/blob_detach`、capset | `drv_i915_render_attach/detach/open/close/execute/blob_attach/blob_detach/get_capset` | S3a `render/render.h` |
| `i915_vkc_*`（生成 codec） | `i915_vkc_*` のまま（static 生成物。`data/vulkan-codec.inc`） | S3a |
| `struct gfx_X`（gfx.h の全型） | `struct i915_gfx_X` | S3b `render/gfx.h` |
| `i915_vk_gfx_obj_dispatch` / `i915_vk_gfx_rec_dispatch` | `drv_i915_gfx_obj_dispatch` / `drv_i915_gfx_rec_dispatch`（引数は旧のまま、型だけ新名） | S3b / S3c |
| `i915_vk_gfx_X`（その他の gfx 関数） | `drv_i915_gfx_X` | 定義した側 |
| compiler | `drv_i915_shader_parse`、`drv_i915_shader_compile`、`struct i915_shader_binary`、`struct i915_shader_ir`（`compiler/compiler.h`、`compiler/ir.h`） | S2 |

構造体のフィールド名は変えない。

## 2. ファイルの分担

| 担当 | 旧 | 新 |
| --- | --- | --- |
| S2 | `spirv.c`、`compile.c`、`eu.c`、`linux/eu-encoding-gen12.inc` | `compiler/` |
| S3a 入口・転送 | `vk.c`、`cmd.c`、`vkc.c`、`vkc.h`、`codec-generated.inc`、`inst.c`、`vk-internal.h` の核の型 | `render/render.h`、`render/internal.h`、`render/vulkan.c`、`render/transport.c`、`render/dispatch.c`、`render/codec.c/.h`、`data/vulkan-codec.inc`、`render/object.c/.h`、`render/instance.c/.h`。ops 側（`command.c`、`resource.c`、`session.c`、`i915.c`）の XXX を render 呼出しに置換 |
| S3b 物 | `gfx-obj.c`、`gfx.h` の型 | `render/gfx.h`、`render/memory.c`、`render/image.c`、`render/descriptor.c`、`render/pipeline.c`（object 部分）、`render/render-pass.c`、`render/sync.c` |
| S3c 記録・描画 | `gfx-rec.c`、`gfx-draw.c` | `render/command.c`、`render/state.c`、`render/draw.c`、`render/blit.c`、`render/batch.c`、`render/math.c`、`render/pipeline-prepare.c`（統合時に pipeline.c へ） |
| 判断待ち | `res.c`、`pipe.c`、`cmdbuf.c`、`sync.c`、`wsi.c`、`display.c`（E-127 以前の module） | S3a が「本番（libvulkan の vkdemo 経路）で実際に処理している opcode」を調べ、報告する。移植は報告後に決める |
