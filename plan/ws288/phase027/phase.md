<!-- awesome-plan project=zedbsd record=ws288-p027 -->

# ws288-p027 MSAA（multisample の描画と sample する image）

- 親: [WS288](../ws.md)。設計: [design.md](../design.md) **§15 MSAA**（版 5、2026-10-25 P2）。review: [design-review-msaa.md](../design-review-msaa.md)。
- 状態: in-progress（2026-10-25 q1558-i01 P2: 設計の段。実装の段 S1〜S6 は未着手）
- 依存: p009（part F の TU、特に multisample の sampled image の読みはこの Phase で）。
- 承認: 2026-10-25 ユーザー「了解です。SAはfuture workに回します。p009, msaaを優先して実装してください。そのあと、p010,p018,p019,p006,p011を実装します。KC705のDDR3をVRAMにしてPCIeのGPUボードにすることで、QEMUからパススルーで利用し、ゲストのzedBSDからVulkanを動作させることを、最初のゴールにします。必要なphaseは作成してください。」

## 範囲

- Vulkan 1.0 core の必須: framebuffer の color・depth・stencil で sample 数 1 と 4（`framebufferColorSampleCounts` など）、sampled image の 4 sample、`sampleRateShading` は任意（feature を確かめる）、resolve（render pass の resolve attachment）、`VkPipelineMultisampleStateCreateInfo`（sampleMask、alphaToCoverage は任意か確かめる）。
- 設計（最初に design.md へ節を足し design-reviewer を通す）: RX の sample ごとの coverage（標準の 4x の位置）、SU の fragment は pixel ごと（sample shading は任意なら後）、ROP の sample ごとの depth・stencil の test と color の書き込み、MS の image の配置（sample の並べ方）、resolve（ROP の帰りか BLT の kernel か）、TU の `texf` の sample の index（part F の P3 の案: rs2）、GHCI の STATE・CAPS・format の追加（draft）。
- 実装: RTL（RX・ROP・TU・必要なら BLT）、model（`rxmodel.py`・`tumodel.py`）、単体 TB、`atg_top` の scene（4x の三角形の縁、resolve、MS の image を texf で読む）、Verilator。

## 実装の段（design §15.17）

| 段 | 内容 | 依存 | LW | 状態 |
| --- | --- | --- | --- | --- |
| 設計 | design §15、design-reviewer、U43〜U46 | — | 3〜5 | in-progress（q1558-i01） |
| S1 | `msmodel.py`、`gsi1model.py` の r6〜r9・`exp 0x105`、spec の更新 | 設計の review | 4 | planned |
| S2 | RX: `atg_rx_cov`・`atg_rx_bary`（新）、`atg_rx_scan`・`atg_rx_frag` | S1 | 7 | planned |
| S3 | SU: `atg_su_exp`（r6・0x105・zexp・d_s・`ex_ms`） | S1 | 3 | planned |
| S4 | ROP: `atg_rop_ms`（新）、`order`・`ds`・`wb` | S1 | 7 | planned |
| S5 | TU: MS の `texf`・`texsz.s` | S1、p009 part F の merge | 2 | planned |
| S6 | `atg_top_msaa_tb` と scene、resolve4、Verilator、README、GHCI §12.11 | S2〜S5 | 6 | planned |

## 受け入れ

design の MSAA の節（review 済み）。scene の 4x の color・depth と resolve の結果が model と word ごとに一致、`make lint` clean、`make sim` の PASS が減らない、`make verilator` PASS。板・timing は対象外。

## 記録

- 2026-10-25 01:08 Q1: 新設（part E の P3 の調べで、multisample の sampled image が ROP の MSAA に依存すると分かった）。
- 2026-10-25 01:37 P2（q1558-i01、設計の段）: design.md 版 5 §15 MSAA を書いた（Vulkan 1.0 core の要求の表は docs.vulkan.org の文面から、RX の sample の coverage、fragment の入力 r6〜r9、SU の `exp 0x105`、ROP の `atg_rop_ms`、sample-interleaved の配置、resolve = firmware の内蔵の draw、TU の `texf` の rs2 = sample（P3 の TEX_IMAGE[15:12] = log2(samples)）、GHCI の draft §12.11、段 S1〜S6 = 29 LW、U43〜U46）。amaterrace-gpu の worktree p2-p027 の `docs/ghci.md` に CAPS[29] と §12.11（draft）。次は design-reviewer。
