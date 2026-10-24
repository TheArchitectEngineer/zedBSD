<!-- awesome-plan project=zedbsd record=ws288-p027 -->

# ws288-p027 MSAA（multisample の描画と sample する image）

- 親: [WS288](../ws.md)。設計: [design.md](../design.md)（MSAA の節は無い。この Phase の最初に足す）。
- 状態: planned（2026-10-25 01:08 Q1 新設）
- 依存: p009（part F の TU、特に multisample の sampled image の読みはこの Phase で）。
- 承認: 2026-10-25 ユーザー「了解です。SAはfuture workに回します。p009, msaaを優先して実装してください。そのあと、p010,p018,p019,p006,p011を実装します。KC705のDDR3をVRAMにしてPCIeのGPUボードにすることで、QEMUからパススルーで利用し、ゲストのzedBSDからVulkanを動作させることを、最初のゴールにします。必要なphaseは作成してください。」

## 範囲

- Vulkan 1.0 core の必須: framebuffer の color・depth・stencil で sample 数 1 と 4（`framebufferColorSampleCounts` など）、sampled image の 4 sample、`sampleRateShading` は任意（feature を確かめる）、resolve（render pass の resolve attachment）、`VkPipelineMultisampleStateCreateInfo`（sampleMask、alphaToCoverage は任意か確かめる）。
- 設計（最初に design.md へ節を足し design-reviewer を通す）: RX の sample ごとの coverage（標準の 4x の位置）、SU の fragment は pixel ごと（sample shading は任意なら後）、ROP の sample ごとの depth・stencil の test と color の書き込み、MS の image の配置（sample の並べ方）、resolve（ROP の帰りか BLT の kernel か）、TU の `texf` の sample の index（part F の P3 の案: rs2）、GHCI の STATE・CAPS・format の追加（draft）。
- 実装: RTL（RX・ROP・TU・必要なら BLT）、model（`rxmodel.py`・`tumodel.py`）、単体 TB、`atg_top` の scene（4x の三角形の縁、resolve、MS の image を texf で読む）、Verilator。

## 受け入れ

design の MSAA の節（review 済み）。scene の 4x の color・depth と resolve の結果が model と word ごとに一致、`make lint` clean、`make sim` の PASS が減らない、`make verilator` PASS。板・timing は対象外。

## 記録

- 2026-10-25 01:08 Q1: 新設（part E の P3 の調べで、multisample の sampled image が ROP の MSAA に依存すると分かった）。
