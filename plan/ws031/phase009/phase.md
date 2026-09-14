# WS031 p009 計画: sync — fence/semaphore/query/timeline → completion 接続

Vulkan の同期プリミティブを WS029 の seqno/HWSP breadcrumb と `drv_gpu_complete`/`retire_waitq` に結線する。おおまかな設計。

## Module と所有ファイル
- `src/drivers/gpu/i915/vk/sync.c`, `sync.h`
- fixture: `plan/ws031/tests/i915-vk-sync-test.c`

## 実装する公開インタフェース（規約・正本）
`sync.h`:
- routing: `int i915_vk_sync_dispatch(struct i915_vk_session*, uint32_t op, struct i915_vk_reader*, struct i915_vk_writer*);`（fence/semaphore/event/query pool 系 op、`vkWaitForFences`/`vkGetFenceStatus`/`vkQueueWaitIdle`/`vkDeviceWaitIdle`）
- fence: `int i915_vk_fence_create(struct i915_vk_session*, bool signaled, struct i915_vk_fence **out);` `void i915_vk_fence_destroy(...);` `int i915_vk_fence_reset(...);` `int i915_vk_fence_wait(struct i915_vk_fence*, uint64_t timeout_ns);` `int i915_vk_fence_status(struct i915_vk_fence*);`
- **cmdbuf 連携**: `int i915_vk_fence_arm(struct i915_vk_fence*, uint32_t engine, uint32_t target_seqno);` — submit が返す seqno を fence に結びつける。
- semaphore: `i915_vk_semaphore_create/destroy` と queue submit の wait/signal（当初は同一 queue 直列なので no-op に近い。timeline は増分C）。
- query pool: `i915_vk_query_pool_create/destroy`、`i915_vk_query_*`（occlusion/timestamp。vkdemo 最小なら timestamp のみ）。

## 内部関数構成ガイド
fence を engine の retire（HWSP seqno）に紐付け、`retire_waitq` で wait、`i915_vk_fence_status` は seqno 比較。`drv_gpu_complete` 経路との整合（WS029 の request 完了 callback で fence を signal）。

## 依存
- 前段: p008 cmdbuf（submit が seqno を返す→`i915_vk_fence_arm`）、p002 session。
- WS029 core: engine seqno/`retire_waitq`、`drv_gpu_complete`。
- 後段: p011 統合で使用。

## 触れるファイル / 触れないファイル
- 触れる: `vk/sync.*`、fixture。
- 触れない: 他モジュール `.c`、core（許可関数のみ）、HAL、UAPI、libvulkan。

## 受け入れ条件と試験
- host fixture: fence を armed→emulator が seqno を進める→wait 解除・status signaled、timeout 動作、reset。semaphore/query の生成・破棄。
- build 3 構成。実機は p011。

## 見積・制限
180 分。単一 queue 直列前提。timeline semaphore と複雑な query は増分C/後続。
