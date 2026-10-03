# WS031 p007 計画: pipe — Gen12 3D パイプライン state emission

compile の shader binary と固定機能状態から、draw 時に発行する `3DSTATE_*` を構築・発行する。おおまかな設計。

## Module と所有ファイル
- `src/drivers/gpu/i915/vk/pipe.c`, `pipe.h`
- `src/drivers/gpu/i915/vk/linux/3dstate-gen12.inc`（各 `3DSTATE_*` の dword layout・enum を Intel PRM / Mesa `src/intel/genxml`(gen12.xml) 相当から出典・SHA 付き転記）
- fixture: `plan/ws031/tests/i915-vk-pipe-test.c`
- 承認要（p008 と共有）: `src/drivers/gpu/i915/{lrc.c,engine.c,internal.h}` の RCS 3D 有効化 hook（`CTX_R_PWR_CLK_STATE`、3D 用 LRC state）。差分を本 doc に添付し承認後に最小変更。

## 実装する公開インタフェース（規約・正本）
`pipe.h`:
- routing: `int i915_vk_pipe_dispatch(struct i915_vk_session*, uint32_t op, struct i915_vk_reader*, struct i915_vk_writer*);`（pipeline layout/pipeline/shader module/render pass/framebuffer 系 op）
- `int i915_vk_pipeline_create(struct i915_vk_session*, const struct i915_vk_pipeline_info*, struct i915_vk_pipeline **out);`（VS/FS の `i915_vk_shader_binary`、vertex input、RT/depth format、topology を受ける）
- `void i915_vk_pipeline_destroy(struct i915_vk_pipeline*);`
- `void i915_vk_pipeline_emit(struct i915_vk_pipeline*, struct i915_vk_batch*);` — 保持した `3DSTATE_*` dword を batch へ。
- `void i915_vk_pipe_emit_base(struct i915_vk_session*, struct i915_vk_batch*);` — `PIPELINE_SELECT`(3D)、STATE_BASE_ADDRESS、共通 URB/VF 初期化（draw 前に 1 回）。

`vk-internal.h` へ追加: `struct i915_vk_pipeline`（保持する 3DSTATE dword 群、shader binary 参照、URB 割当）、`struct i915_vk_pipeline_info`。

## 内部関数構成ガイド
`3DSTATE_VS/PS/PS_EXTRA/SBE/WM`（shader kernel pointer/GRF/thread）、`3DSTATE_URB_{VS,HS,DS,GS,PS}`（URB 割当）、`3DSTATE_VF_*`/`3DSTATE_VERTEX_ELEMENTS`（vertex input）、`3DSTATE_DEPTH_BUFFER`/`3DSTATE_PS_BLEND`（増分B/C）、`3DSTATE_VIEWPORT_*`、binding table pointers / sampler state pointers（res の offset を使う）。値は `.inc`、組み立てロジックは新規。

## 依存
- 前段: p006 compile（shader binary）、p003 res（surface/sampler state、binding table offset、state base）。
- WS029 core: RCS 3D 有効化 hook（承認）、`drv_i915_gem_*`。
- 後段: p008 cmdbuf が emit を draw 記録で呼ぶ。

## 触れるファイル / 触れないファイル
- 触れる: `vk/pipe.*`、`vk/linux/3dstate-gen12.inc`、fixture＋承認済み core hook。
- 触れない: 他モジュール `.c`、承認外の core、HAL、UAPI、libvulkan。

## 受け入れ条件と試験
- host fixture: 増分A の最小 pipeline を作り `3DSTATE_*` の dword を**既知値**と照合（shader pointer/GRF/URB/VF/RT）。増分B/C で depth/sampler pointer を追加照合。
- build 3 構成。実機での実行は p008/p011。

## 見積・制限
360 分。増分A は VF＋VS＋PS＋RT の最小。depth/blend/sampler pointer は増分B/C。RCS 3D hook の core 差分は最小・既存挙動不変。

## 完了（build-passing 基準、host 検証済み）
`vk/pipe.c`＋`vk/linux/3dstate-gen12.inc`（gen120.xml の 3DSTATE command opcode/length を転記）。pipeline_create（VS/FS binary の kernel VA・GRF 保持）、pipeline_emit（3DSTATE_VS 0x7810＋kernel ptr＋GRF、3DSTATE_PS 0x7820）、pipe_emit_base（PIPELINE_SELECT 3D、STATE_BASE_ADDRESS）。fixture が batch 内の command header と kernel pointer/GRF を照合、通常＋ASan/UBSan PASS。
補正待ち: URB/VF/SBE/WM/depth/viewport 等の fixed-function fields、STATE_BASE の実 address。command 列と shader pointer は確定、fields は実機で補正。
