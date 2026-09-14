# WS031 p001 決定事項

外部設計（[external-design.md](../external-design.md)）に対し、p001 で確定した実装前提。後段はこれを参照する。

## 1. capset 方針
i915 の native 実行器は、libvulkan（WS030）を**無改造で通す**ことを既定とする。libvulkan は `GPU_GET_CAPSET` で Venus 互換の capset（≥156 byte、`context.c` の閾値）を要求するため、i915 は `drv_i915_vk_attach` で**その形に整合する capset を広告**する。native 固有 capability の追加が必要と判明した場合のみ、p002 または p011 で libvulkan に最小追加する（その Phase だけが libvulkan を触る）。capset の具体フィールドは p002 で decoder と併せて確定する。

## 2. in-kernel compiler の制約
- 配置: `src/drivers/gpu/i915/vk/`（kernel）。ユーザー指示どおり in-kernel。userland compile 案は不採用（記録のみ）。
- 作業メモリ: `kern_calloc`/`kern_free`。関数内の大きな自動配列は使わない。
- 浮動小数: kernel は FPU を使わないため、compiler は**整数演算で shader の float 定数を bit として扱い**、演算は EU 命令として GPU 側で実行する（compiler 自身は float 演算しない）。
- 再入なし・単一スレッド前提。1 shader ずつコンパイル。
- 上限: shader サイズ・GRF 数・命令数に上限を設け、超過は EINVAL。baseline なので spill は最小/未対応（不足時 EINVAL で差し戻し）。

## 3. GEN ターゲット・SIMD・tiling
- ターゲット: Gen12 Xe-LP（Alder Lake-P、`8086:46a8`）。
- SIMD 幅: fragment/vertex とも **SIMD8 固定**（baseline）。
- tiling: 当初 **linear**。texture の Y-tiled は増分Bで必要になれば追加。

## 4. 対象 Vulkan subset（rough 台帳）
`vkdemo` が実際に使う経路のみを対象化する。詳細台帳は `plan/ws031/i915-vk-symbols.md` に p002〜で肉付けするが、rough には:
- op: instance/device/queue、memory/buffer/image/image view/sampler、descriptor set layout/pool/set/update、shader module、pipeline layout/pipeline、command pool/buffer、`vkCmdBindPipeline/BindVertexBuffers/BindDescriptorSets/PushConstants/BeginRenderPass/Draw/EndRenderPass`、`vkQueueSubmit`、fence、swapchain/display。op 番号は libvulkan `opcodes.h`（147 op）を正本に参照。
- SPIR-V: straight-line、算術（fadd/fmul/fsub/fmad/dot）、`sin/cos/rsq`（GEN math）、compose/extract、push constant、varying、`OpImageSampleImplicitLod`。制御流れ・拡張命令は対象外（必要分のみ増分Cで追加）。
- 3DSTATE: VF/VS/PS/PS_EXTRA/SBE/WM/URB/VIEWPORT/RENDER TARGET、depth（増分B/C）。

## 5. ライセンス方針と監査タイミング（運用調整）
- 参照: Mesa（`src/intel/compiler`＝EU encoding、`src/intel/genxml`＝3DSTATE/surface/sampler、`src/intel/isl`＝tiling、いずれ MIT 系）と Intel 公開 PRM。ロジックは新規、値・テーブル・encoding は出典付き `.inc` へ転記。
- **調整**: 外部設計では p001 でライセンス監査を実行する想定だったが、Mesa の転記は p005（eu）・p007（pipe）・p010（wsi）で発生するため、機械監査（SPDX/permission notice の判定と SHA 記録）は**各転記 Phase が `.inc` 生成の直前に実行**する（`plan/ws031/tests/fetch-mesa-refs.sh` を WS029 の `fetch-linux-refs.sh` と同型で用意）。p001 では方針と参照範囲の確定に留める。転記が MIT 以外に触れれば当該 Phase を止める。

## 6. ファイル配置（確定）
`src/drivers/gpu/i915/vk/` に各モジュールの `.c`/`.h`、`vk/linux/` に転記 `.inc`。ヘッダ枠（契約）は本 Phase で作成済み（`vk-internal.h` と 11 モジュール header、全て単体コンパイル確認）。build 配線（`vmunix.mk` への source 追加）は p002 から。
