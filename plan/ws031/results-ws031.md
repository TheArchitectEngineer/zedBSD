# WS031 結果: i915 ネイティブ Vulkan 実行器（build-complete、2026-09-15）

libvulkan が drv_gpu UAPI へ送る Vulkan コマンドを、ホスト（QEMU/Venus/Mesa ANV）非依存で i915 カーネルドライバ内 native 実行器が GEN へ変換する経路を実装した。**全 12 phase を build-passing 基準で完了**（p011 の実機描画のみビッグバンテスト待ち）。Mesa（MIT）と Intel PRM の値・encoding を出典付き `.inc` へ転記し、論理は zedBSD 規約で新規実装。HAL 不変、UAPI 不変（既存 drv_gpu op 使用）。

## Phase 結果
| phase | module | 結果 |
| --- | --- | --- |
| p001 | 設計・契約枠 | cleared。外部設計＋12 契約ヘッダ |
| p002 | top+cmd | cleared。cmd decoder/object table/routing、drv_gpu 統合（capset/blob/map/command） |
| p003 | res | cleared。memory/buffer/image/sampler/descriptor、Gen12 surface state 転記 |
| p004 | spirv | cleared。SPIR-V→baseline IR、vkdemo vert/frag 検証 |
| p005 | eu | cleared。Gen12 EU エンコーダ（mesa-23.1.0 表形式転記）、field 照合 |
| p006 | compile | cleared。SPIR-V IR→GEN baseline codegen、vkdemo 通過 |
| p007 | pipe | cleared。3DSTATE_VS/PS/PIPELINE_SELECT emission |
| p008 | cmdbuf | cleared。record→batch、3DPRIMITIVE draw |
| p009 | sync | cleared。fence を engine seqno へ接続 |
| p010 | wsi | cleared。swapchain/present/display（modeset はビッグバン） |
| p011 | 統合 | build-passing 達成。実機描画（増分A/B/C）はビッグバンテスト |
| p012 | review | cleared。静的解析 0、回帰なし |

## 検証（agent-1、build-passing 基準）
- host fixture 8 種（cmd/spirv/res/sync/eu/compile/pipe/cmdbuf）通常＋ASan/UBSan PASS。
- i915 kernel build PASS（vmunix check、warning 0、FPU 不使用 `-mgeneral-regs-only` クリア）。
- 静的解析 gcc/clang 0 件。WS029 host fixture 全 PASS（回帰なし）。vk は I915 config で gate。git diff --check clean。
- コンパイラチェーン end-to-end: vkdemo の SPIR-V を parse→IR→GEN まで host 検証。

## 実装（すべて local、driver は Zlib、`.inc` は Mesa MIT 出典付き）
`src/drivers/gpu/i915/vk/`: vk-internal.h、vk.c、cmd.c、res.c、spirv.c、eu.c、compile.c、pipe.c、cmdbuf.c、sync.c、wsi.c、display.c、各 header、`linux/{surface-state,eu-encoding,3dstate}-gen12.inc`。i915.c/internal.h に drv_gpu 統合（capset/blob/map/command routing、session/attach）。vmunix.mk 配線。

## 制限・ビッグバンテスト（実機で確定）
実機ネイティブでの増分A（三角形）→B（texture+depth）→C（vkdemo）描画。EU の SWSB 依存・3-source/send operand・message descriptor、compile の GRF 規約、pipe の fixed-function fields、cmdbuf の RCS0 submission 実配線、wsi の display register programming、res_dispatch 等の Venus 精密 decode。値・encoding は Mesa 転記で配置は正、semantics は実機反復で確定する（テープアウト後ビッグバン方式）。
