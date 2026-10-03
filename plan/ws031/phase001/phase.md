# WS031 p001 計画: 設計固め — 外部設計の確定、header 枠、ライセンス監査、capset 方針

この Phase は実行モジュールを書かない。[外部設計書](../external-design.md)を確定し、`vk/` の**公開ヘッダの枠**（型と関数プロトタイプの空実装）、ファイル配置、ライセンス監査、capset 方針を用意して、p002 以降が「モジュール内の実装だけ」に集中できる状態にする。委譲の起点。

## Module と所有ファイル

- 新規（枠のみ、空実装可）: `src/drivers/gpu/i915/vk/vk.h`、`vk-internal.h`、`cmd.h`、`res.h`、`spirv.h`、`eu.h`、`compile.h`、`pipe.h`、`cmdbuf.h`、`sync.h`、`wsi.h`、`display.h`。
- 新規（計画・監査）: `plan/ws031/i915-vk-license-audit.md`、`plan/ws031/i915-vk-symbols.md`、`plan/ws031/phase001/`。
- 触れない: §7 の禁止一覧（HAL、`include/drivers/gpu.h`、WS029 core ロジック、libvulkan、他 driver）。core への結線は p002 以降。

## 実装する公開インタフェース（規約・正本）

外部設計書 §4 の各モジュール公開関数を、**プロトタイプとして確定**し各 `*.h` に置く。`vk-internal.h` に共有型を確定する:
- `struct i915_vk_device`、`struct i915_vk_session`、`struct i915_vk_reader/writer`、`enum i915_vk_object_kind`、handle 型、`enum i915_vk_stage`、`i915_vk_errno()` の対応表。
- 各モジュール `*.h` は §4.1–4.10 の関数プロトタイプ（引数/返り値を確定）。この Phase では本体は `return ENOSYS;` 等の枠でよい。以後、後段はこの header を契約として参照する。

確定にあたり **vkdemo が実際に使う Vulkan op / SPIR-V opcode / 3DSTATE / image・sampler 機能の最小台帳**を作り（`i915-vk-symbols.md`）、対象範囲を機能単位で固定する。libvulkan の `opcodes.h` と vkdemo の trace から op 集合を確定する。

## 内部関数構成ガイド

この Phase は枠のみ。各 header の関数群は外部設計書 §4 の粒度に合わせる。実体分割は各モジュールの Phase で決める。

## 依存

- 入力: 外部設計書、native-vulkan-design.md、WS029 の公開 API（§6 の core 関数一覧の存在確認）、libvulkan の `opcodes.h`/`context.c`（wire 形式・capset 要求の確認）。
- core 拡張 hook: この Phase では追加しない（p002 で入口結線を提案）。

## 決めること（未決事項の確定）

1. **capset 方針**: libvulkan を無改造で通すため i915 が広告する capset 内容。既存 Venus capset（156 byte）に整合させるか、native capset を定義して libvulkan に最小追加するか。既定は「i915 が必要最小 capability を広告、libvulkan 無改造」。
2. **in-kernel compiler の制約確定**: 作業メモリ（`kern_calloc`）、float 非使用（整数/固定小数で EU encode）、再入なし、上限（shader サイズ・GRF 数）。代替（userland compile）は不採用として記録（ユーザー指示は in-kernel）。
3. **SIMD 幅（8 固定）**、**tiling 既定（当初 linear、必要なら Y-tiled）**、**対象 SPIR-V subset**。
4. **ライセンス監査**: 参照する Mesa ファイル（`src/intel/compiler`、`src/intel/genxml` 等）と Intel PRM セクションを列挙し、Mesa 側は SPDX/permission notice を機械監査（`plan/ws031/tests/` に監査 script、WS029 の `fetch-linux-refs.sh` と同型）。転記は `vk/linux/*.inc` に出典・SHA・変換規則付き。

## 触れるファイル / 触れないファイル

- 触れる: 上記 `vk/*.h`（枠）、`plan/ws031/**`。
- 触れない: §7 の禁止一覧すべて。`.c` 本体（p002 以降）、core、libvulkan、HAL、UAPI。

## 受け入れ条件と試験

- 全 `vk/*.h` が単体でコンパイル（型と prototype が閉じている）。空枠を含むダミー TU で syntax チェック。
- ライセンス監査 script exit 0（参照 Mesa が MIT 系）。
- `i915-vk-symbols.md`（対象 op/opcode/3DSTATE/機能台帳）と capset・compiler 制約・SIMD/tiling の確定が文書化。
- build 配線の準備（vk source を並べるが未実装なので `AMD64_I915_SOURCES` への追加は p002 から、この Phase は header のみ）。

## 見積・制限

240 分。実行コードなし。実機なし。この Phase の成果は「後段が迷わず実装できる契約と枠」。未確定を自動 clear しない。
