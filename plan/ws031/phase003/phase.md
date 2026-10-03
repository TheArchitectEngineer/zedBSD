# WS031 p003 計画: res — memory/buffer/image/sampler/descriptor → i915 資源・state

Vulkan の資源系オブジェクトを i915 の GEM/GGTT/PPGTT と surface/sampler/binding state へ対応付ける。おおまかな設計。

## Module と所有ファイル
- `src/drivers/gpu/i915/vk/res.c`, `res.h`
- fixture: `plan/ws031/tests/i915-vk-res-test.c`
- 転記が要れば `vk/linux/surface-state-*.inc`（`RENDER_SURFACE_STATE`/`SAMPLER_STATE` の dword layout、出典付き）。

## 実装する公開インタフェース（規約・正本）
`res.h`:
- routing: `int i915_vk_res_dispatch(struct i915_vk_session *s, uint32_t op, struct i915_vk_reader *r, struct i915_vk_writer *reply);`（memory/buffer/image/image view/sampler/descriptor 系 op）
- memory: `int i915_vk_memory_alloc(struct i915_vk_session*, uint64_t size, uint32_t flags, struct i915_vk_memory **out);` / `void i915_vk_memory_free(...);` / `int i915_vk_memory_map(struct i915_vk_memory*, void **cpu);`
- buffer: `int i915_vk_buffer_create(...);` `int i915_vk_buffer_bind(struct i915_vk_buffer*, struct i915_vk_memory*, uint64_t offset);` `void i915_vk_buffer_destroy(...);`
- image: `int i915_vk_image_create(struct i915_vk_session*, const struct i915_vk_image_info*, struct i915_vk_image **out);` `int i915_vk_image_bind(...);` `void i915_vk_image_destroy(...);` `int i915_vk_image_view_create(...);`
- sampler: `int i915_vk_sampler_create(struct i915_vk_session*, const struct i915_vk_sampler_info*, struct i915_vk_sampler **out);`
- descriptor: `i915_vk_dsl_create/destroy`（set layout）、`i915_vk_dpool_create/destroy`、`i915_vk_dset_alloc/free`、`int i915_vk_dset_update(struct i915_vk_dset*, const struct i915_vk_write_dset*, uint32_t n);`
- **state 供給（pipe/cmdbuf が消費）**: `const uint32_t *i915_vk_image_surface_state(const struct i915_vk_image_view*);`（RENDER_SURFACE_STATE dword）、`const uint32_t *i915_vk_sampler_state(const struct i915_vk_sampler*);`、`int i915_vk_build_binding_table(struct i915_vk_session*, struct i915_vk_dset *const*, uint32_t n_sets, uint32_t *bt_offset, uint32_t *samp_offset);`（state heap に surface/sampler を並べ offset を返す）。
- state heap（GGTT 可視の dynamic state / surface state 領域）の確保は res が所有し、offset を返す。

`vk-internal.h` へ追加（p003 が確定）: `struct i915_vk_memory/buffer/image/image_view/sampler/dsl/dpool/dset`、`struct i915_vk_image_info`（extent/format/tiling/usage）、`struct i915_vk_sampler_info`、`struct i915_vk_write_dset`。

## 内部関数構成ガイド
format→GEN surface format 変換、tiling（当初 linear、Y-tiled は増分Bで）、RENDER_SURFACE_STATE/SAMPLER_STATE の dword 構築、descriptor→binding table の surface/sampler heap 配置、host-visible memory の CPU view。

## 依存
- 前段: p002（object table、reader/writer、session）。
- WS029 core: `drv_i915_gem_create/destroy/bind_vm/unbind_vm/read/write`、`drv_i915_ggtt_alloc/free/insert/clear`、`drv_i915_ppgtt_insert/clear`。
- 後段: pipe/cmdbuf が surface/sampler/binding table を参照。

## 触れるファイル / 触れないファイル
- 触れる: `vk/res.*`、`vk/linux/surface-state-*.inc`、fixture。
- 触れない: 他モジュール `.c`、core（許可関数呼び出しのみ、編集しない）、HAL、UAPI、libvulkan。

## 受け入れ条件と試験
- host fixture: memory/buffer/image/sampler/descriptor 生成、bind、surface/sampler state dword が既知値、binding table 配置、CPU view 書込み反映、破棄で GEM 解放。
- build 3 構成。実機なし。

## 見積・制限
300 分。増分Aは RT 用 image と linear のみで足りる。texture/depth の image・sampler・descriptor は増分Bで拡張。

## 進捗（メモリ経路＝libvulkan open 可能に）
i915.c に `i915_blob_create`（GEM 確保→session VM bind→resource として追跡、GPU_BLOB_MAPPABLE|SHAREABLE 検証）と `i915_resource_map`（GEM の system-RAM を CPU view で公開）を実装、drv_gpu ops テーブルに配線、`I915_CAPABILITIES` に `GPU_CAP_BLOB|GPU_CAP_MAPPING` 追加。これで libvulkan の必須 capability（RESOURCE/TRANSFER/COMMAND/CAPSET/BLOB/MAPPING）が揃い、open とメモリ確保/map が成立する。UAPI 変更なし（既存 op を使用）。kernel build PASS、WS029 host fixture 全 PASS（backend の capability assert を BLOB|MAPPING で更新）、vk fixture PASS。
残（p003 clear まで）: res.c の Vulkan command handler（buffer/image/sampler/descriptor、object 対応付け）、Gen12 の RENDER_SURFACE_STATE/SAMPLER_STATE 転記（Mesa/PRM、ライセンス監査 gate）と binding table。増分A は RT image と linear のみ、sampler/descriptor は増分B。

## 進捗（object lifecycle 検証済み、surface encoding は Mesa 待ち）
`vk/res.c` 実装: memory（GEM 確保＋session PPGTT bind＋CPU view）、buffer（memory range bind）、image/image view（extent/format/tiling＋surface state）、sampler。res fixture `i915-vk-res-test.c` が WS029 実 GEM/PPGTT 上で memory/buffer/image lifecycle と surface state 構造（extent packing）を検証、通常＋ASan/UBSan PASS。kernel build で res.o clean（FPU 制約クリア）。
残: RENDER_SURFACE_STATE/SAMPLER_STATE の Gen12 正確 encoding（Mesa genxml 転記＝ユーザー取得待ち、実機検証待ち）、descriptor（dsl/dpool/dset/binding table＝増分B）、res_dispatch の Venus command 精密 decode（p011）。

## 進捗（descriptor 管理も検証済み、増分B）
`vk/res.c` に descriptor 追加: dsl（binding/type 記録）、dpool（max_sets 制限）、dset（alloc/free で pool slot 管理）、dset_update（binding ごとの view/sampler 記録）。res fixture に descriptor テスト追加（pool 上限 ENOSPC、free で slot 復帰、update 記録）、通常＋ASan/UBSan PASS、kernel build clean。
残: build_binding_table と surface/sampler state の Gen12 正確 encoding（Mesa genxml 転記＝clone 完了待ち）、res_dispatch の Venus 精密 decode（p011）。

## 完了（build-passing 基準、増分A スコープ）
Gen12 surface state を転記・正式化。`vk/linux/surface-state-gen12.inc`（gen120.xml の RENDER_SURFACE_STATE bit 位置・Surface Type/Tile Mode enum、isl.h の ISL_FORMAT 値を転記、監査記録あり）。res の surface_state が dword0（SURFTYPE_2D/format/tile）・dword2（extent-1）・dword3（pitch-1）・dword8-9（bind で base）を encode、res fixture が各 dword を照合 PASS。kernel build PASS。format 値は isl.h から取得（手写しの取り違え回避）。
残（増分B/p011）: SAMPLER_STATE encoding、build_binding_table（state heap＋BINDING_TABLE_STATE）、res_dispatch の Venus decode。実機描画はビッグバンテスト。
