# WS031 p010 計画: wsi — KMS/modeset・scanout・swapchain present

i915 に modeset を新設し、swapchain image を実 scanout する。native WSI（`VK_KHR_display`/`swapchain`）を実表示へ接続。WS029 f003（framebuffer 保持）の延長。おおまかな設計。

## Module と所有ファイル
- `src/drivers/gpu/i915/vk/wsi.c`, `wsi.h`（swapchain/WSI）
- `src/drivers/gpu/i915/vk/display.c`, `display.h`（KMS/modeset/plane）
- `src/drivers/gpu/i915/vk/linux/display-gen12.inc`（pipe/transcoder/PLL/DDI/plane register の dword layout・enum を Intel PRM / Mesa 相当から出典・SHA 付き転記）
- fixture: `plan/ws031/tests/i915-vk-wsi-test.c`

## 実装する公開インタフェース（規約・正本）
`display.h`:
- `int i915_vk_display_init(struct i915_vk_device*);` — 接続検出、EDID 読取、mode 列挙、eDP を pipe/transcoder/PLL/DDI で modeset。
- `int i915_vk_display_mode(struct i915_vk_device*, uint32_t *w, uint32_t *h, uint32_t *stride);`
- `int i915_vk_display_flip(struct i915_vk_device*, struct i915_vk_image *scanout);` — plane surface を image の GGTT オフセットへ設定し scanout。
- `void i915_vk_display_fini(struct i915_vk_device*);`

`wsi.h`:
- routing: `int i915_vk_wsi_dispatch(struct i915_vk_session*, uint32_t op, struct i915_vk_reader*, struct i915_vk_writer*);`（`VK_KHR_display`/`display_swapchain`/`surface`/`swapchain` 系 op）
- `i915_vk_swapchain_create/destroy`（swapchain image を GGTT 可視 surface として res 経由で確保）、`i915_vk_swapchain_acquire`（次 image index）、`i915_vk_swapchain_present`（`display_flip`＋fence 待ち）。

## 内部関数構成ガイド
modeset は eDP 1 枚に限定（PLL/DDI/transcoder/pipe/plane の最小シーケンス）。EDID から preferred mode。plane は GGTT オフセットの surface を指す（WS029 f003 のアパーチャ↔GGTT 関係を利用）。present は acquire した image を flip し、描画 fence の完了後に表示。console/graphics fallback（modeset 失敗時）を持つ。

## 依存
- 前段: p003 res（swapchain image = GGTT 可視 surface）、p009 sync（present の fence）。
- WS029 core: `drv_i915_read32/write32/wait32`、`drv_i915_ggtt_*`、WS029 f003 の framebuffer 保持知見。display register は vk/display が新規に扱う（core の scanout 保持ロジックは変更しない）。
- 後段: p011 統合。

## 触れるファイル / 触れないファイル
- 触れる: `vk/wsi.*`、`vk/display.*`、`vk/linux/display-gen12.inc`、fixture。
- 触れない: 他モジュール `.c`、WS029 core ロジック（uncore/ggtt の許可関数のみ呼ぶ）、HAL、UAPI、libvulkan。
- 注意: display register の駆動は本モジュールに閉じる。WS029 の ggtt scanout 保持（f003）とは独立に動く。

## 受け入れ条件と試験
- host fixture: modeset シーケンスの register 書込み値、mode 列挙、flip の plane surface 設定を照合（実 display は QEMU/fixture で MMIO を模擬）。
- 実機（Latitude 5330 ネイティブ）: eDP に単色/テスト画像を flip して表示（p011 の増分Aで描画と結合）。
- build 3 構成。

## 見積・制限
300 分。eDP 1 枚・単一 plane・preferred mode 固定。多出力・hotplug・DP MST は対象外。modeset の実機検証はユーザーのネイティブ起動が必要。

## 完了（build-passing 基準）
`vk/display.c`（display_init/mode/flip/fini、既定 mode 1920x1080、register sequence はビッグバン）、`vk/wsi.c`（swapchain＝res image リング、acquire round-robin、present＝fence wait＋display_flip、B8G8R8A8_UNORM）。kernel build PASS（link）、`i915_vk_device` に display mode フィールド追加。register programming（PLL/DDI/transcoder/pipe/plane）と実 scanout はビッグバンテスト。
