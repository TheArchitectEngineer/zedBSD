# WS031 p008 計画: cmdbuf — command buffer → GEN バッチ変換・draw・投入

`vkCmd*` の記録を GEN 3D バッチへ変換し、`vkQueueSubmit` で WS029 の RCS0 request 経路へ投入する。おおまかな設計。

## Module と所有ファイル
- `src/drivers/gpu/i915/vk/cmdbuf.c`, `cmdbuf.h`
- fixture: `plan/ws031/tests/i915-vk-cmdbuf-test.c`
- 承認要（p007 と共有）: RCS 3D 有効化 hook、および WS029 の RCS0 への batch 投入 wrapper（`src/drivers/gpu/i915/i915.c` の native stream 投入 `i915_submit_stream` 相当の再利用/薄い wrapper）。差分を本 doc に添付。

## 実装する公開インタフェース（規約・正本）
`cmdbuf.h`:
- routing: `int i915_vk_cmdbuf_dispatch(struct i915_vk_session*, uint32_t op, struct i915_vk_reader*, struct i915_vk_writer*);`（command pool/buffer、`vkCmd*`、`vkQueueSubmit`、`vkBeginCommandBuffer` 等）
- command buffer: `int i915_vk_cmdbuf_begin(struct i915_vk_cmdbuf*);` `int i915_vk_cmdbuf_end(...);`
- 記録（op ごとに batch へ GEN を積む）: `i915_vk_cmd_bind_pipeline`, `i915_vk_cmd_bind_vertex_buffers`, `i915_vk_cmd_bind_descriptor_sets`, `i915_vk_cmd_push_constants`, `i915_vk_cmd_begin_render_pass`(clear), `i915_vk_cmd_end_render_pass`, `i915_vk_cmd_set_viewport/scissor`, `i915_vk_cmd_draw`.
- 投入: `int i915_vk_queue_submit(struct i915_vk_session*, struct i915_vk_cmdbuf *const*, uint32_t n, struct i915_vk_fence *fence);` — batch を WS029 RCS0 request として投入し、fence を seqno に結線（sync と協調）。

`vk-internal.h` の `struct i915_vk_batch`（p002 で定義）を command buffer が保持。

## 内部関数構成ガイド
render pass begin で RT/depth を bind し clear（blit か 3D clear）、bind 系で pipeline emit（`pipe`）・binding table/sampler pointer（`res`）・vertex buffer（`3DSTATE_VERTEX_BUFFERS`）・push constant を batch へ、draw で `3DPRIMITIVE`。batch 末尾に `MI_BATCH_BUFFER_END`。submit は batch GEM を PPGTT に見せて WS029 の RCS0 request（`MI_BATCH_BUFFER_START`）で実行、breadcrumb→fence。

## 依存
- 前段: p002（session/routing/batch 型）、p003 res（binding table/pointer、vertex/index buffer）、p007 pipe（emit）。
- WS029 core: RCS0 request 投入 wrapper（承認）、`drv_i915_gem_*`、`drv_i915_ppgtt_insert`、engine。
- p009 sync と fence を結線。

## 触れるファイル / 触れないファイル
- 触れる: `vk/cmdbuf.*`、fixture＋承認済み core hook。
- 触れない: 他モジュール `.c`、承認外 core、HAL、UAPI、libvulkan。

## 受け入れ条件と試験
- host fixture: 記録列（bind pipeline/vertex/descriptor→draw）が期待 GEN batch を生成、submit が WS029 request 経路（fixture の execlists emulator）を通り breadcrumb/fence が上がる。clear 動作。
- build 3 構成。実機での描画は p011（増分A）。

## 見積・制限
300 分。単一 RCS0・直列。増分A は 1 draw（三角形）＋clear。複数 draw/render pass・index draw は増分で。
