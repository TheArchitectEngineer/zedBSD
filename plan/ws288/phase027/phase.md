<!-- awesome-plan project=zedbsd record=ws288-p027 -->

# ws288-p027 MSAA（multisample の描画と sample する image）

- 親: [WS288](../ws.md)。設計: [design.md](../design.md) **§15 MSAA**（版 5、2026-10-25 P2）。review: [design-review-msaa.md](../design-review-msaa.md)。
- 状態: in-progress（2026-10-25 q1560-i01 P2: 実装の段。U44 の書き直し（版 5.1）は review 3 済み、S1 から）
- 依存: p009（part F の TU、特に multisample の sampled image の読みはこの Phase で）。
- 承認: 2026-10-25 ユーザー「了解です。SAはfuture workに回します。p009, msaaを優先して実装してください。そのあと、p010,p018,p019,p006,p011を実装します。KC705のDDR3をVRAMにしてPCIeのGPUボードにすることで、QEMUからパススルーで利用し、ゲストのzedBSDからVulkanを動作させることを、最初のゴールにします。必要なphaseは作成してください。」

## 範囲

- Vulkan 1.0 core の必須: framebuffer の color・depth・stencil で sample 数 1 と 4（`framebufferColorSampleCounts` など）、sampled image の 4 sample、`sampleRateShading` は任意（feature を確かめる）、resolve（render pass の resolve attachment）、`VkPipelineMultisampleStateCreateInfo`（sampleMask、alphaToCoverage は任意か確かめる）。
- 設計（最初に design.md へ節を足し design-reviewer を通す）: RX の sample ごとの coverage（標準の 4x の位置）、SU の fragment は pixel ごと（sample shading は任意なら後）、ROP の sample ごとの depth・stencil の test と color の書き込み、MS の image の配置（sample の並べ方）、resolve（ROP の帰りか BLT の kernel か）、TU の `texf` の sample の index（part F の P3 の案: rs2）、GHCI の STATE・CAPS・format の追加（draft）。
- 実装: RTL（RX・ROP・TU・必要なら BLT）、model（`rxmodel.py`・`tumodel.py`）、単体 TB、`atg_top` の scene（4x の三角形の縁、resolve、MS の image を texf で読む）、Verilator。

## 実装の段（design §15.17）

| 段 | 内容 | 依存 | LW | 状態 |
| --- | --- | --- | --- | --- |
| 設計 | design §15、design-reviewer（[review](../design-review-msaa.md)）、U43〜U46 | — | 3〜5 | in-progress（q1558-i01） |
| S1 | `msmodel.py`（BLT の resolve の期待を含む）、`gsi1model.py` の r6〜r9・`exp 0x105`、spec の更新 | 設計の review | 5 | planned |
| S2 | RX: `atg_rx_cov`・`atg_rx_bary`（新）、`atg_rx_scan`・`atg_rx_frag` | S1 | 7 | planned |
| S3 | SU: `atg_su_exp`（r6・0x105・zexp・d_s・`ex_ms`） | S1 | 3 | planned |
| S4 | ROP: `atg_rop_ms`（新）、`order`・`ds`・`wb` | S1 | 7 | planned |
| S5 | TU: MS の `texf`・RGB565 の `texf`（addr・cache・filter）・`texsz.s`（imm 3、SU の decode） | S1、p009 part F の merge | 5 | planned |
| S6 | `atg_top_msaa_tb` と scene、BLT の resolve を含む、Verilator、README、GHCI §12.11 | S2〜S5、S7 | 6 | planned |
| S7 | BLT の RESOLVE（§15.8 版 5.1）: `atg_blt` を wrapper に、`atg_blt_2d`（今の物の名前だけ）、`atg_blt_rsv`・`atg_blt_rsv_px`・`atg_blt_avg`、TB、spec、ghci §8.2 | S1 | 8 | planned |

## 受け入れ

design の MSAA の節（review 済み）。scene の 4x の color・depth と resolve の結果が model と word ごとに一致、`make lint` clean、`make sim` の PASS が減らない、`make verilator` PASS。板・timing は対象外。

## 記録

- 2026-10-25 01:08 Q1: 新設（part E の P3 の調べで、multisample の sampled image が ROP の MSAA に依存すると分かった）。
- 2026-10-25 01:12 Q1: 設計の段を P2（kernel-and-driver-designer）に（q1558）。part F（P3）と並行。実装の段は設計の review と part F の merge の後。
- 2026-10-25 01:37 P2（q1558-i01、設計の段）: design.md 版 5 §15 MSAA を書いた（Vulkan 1.0 core の要求の表は docs.vulkan.org の文面から、RX の sample の coverage、fragment の入力 r6〜r9、SU の `exp 0x105`、ROP の `atg_rop_ms`、sample-interleaved の配置、resolve = firmware の内蔵の draw、TU の `texf` の rs2 = sample（P3 の TEX_IMAGE[15:12] = log2(samples)）、GHCI の draft §12.11、段 S1〜S6 = 29 LW、U43〜U46）。amaterrace-gpu の worktree p2-p027 の `docs/ghci.md` に CAPS[29] と §12.11（draft）。次は design-reviewer。
- 2026-10-25 02:02 P2: design-reviewer の review 1（18 件: High 2・Medium 8・Low 8、Critical 0）に全部対応（[design-review-msaa.md](../design-review-msaa.md)）。主な変更: RGB565 の `texf`（S5）、resolve の時間を RX の律速で直した（1080p ≈1.0 s @ 100 MHz）、帯域を line の粒度で、`exp 0x105` は最後の書き込みが勝つ、exp の書き手の分離、depth だけの shader は `exp ~0 → 0x105`、TU の slot を予約せず STATE を保存・復元、`texsz.s` の decode、試験を LINE_BYTES 32・64・16 bpp・奇数の幅などに広げた。段の合計 31 LW。
- 2026-10-25 02:21 P2: review 2（新しい 18 件: High 2・Medium 6・Low 10、review 1 の不十分 9 件を含む）に対応（[design-review-msaa.md](../design-review-msaa.md) の review 2）。主な変更: `texsz.s` を imm 3 に（P3 の part F が imm 2 を `texsz.d` に使った、p3-p009f 3ec9aed で確かめた）、resolve の後は firmware の写しの state を dirty にして出し直す（STATE・constant の読み戻しをやめた）、全 fragment program の終わりに `movi rT, -1; exp rT, 0x105`、RGB565 の `texf` は cache・filter も、d_s は三角形ごとに 1 回、受け入れを PASS の行・MUT・selftest・vec-diff・timeout で機械的に。段の合計 32 LW（S5 5 LW）。ws.md の p010・p018・p019 の依存の列に p027 を足すのは Q1 に依頼。設計の段の成果は review 済み、U43〜U46 はユーザーの判断待ち。
- 2026-10-25 02:23 Q1: 設計の段の merge（zedBSD の agent/p2-p027 d661579e8 を main に）。amaterrace-gpu の `docs/ghci.md` の案（p2-p027 f393310）は P3 の part F の ghci.md の書き直しの後に merge する。U43〜U46 はユーザーに提示。実装の段 S1〜S6（32 LW）は part F の merge と U 項目の回答の後。CAPS[29]・format 4・texsz の imm 3 の予約を P3 に伝えた。
- 2026-10-25 02:32 ユーザーの回答（design §15.20 の冒頭）: U43・U45・U46 は推し、**U44 はあるべき形の hardware の resolve**（firmware の内蔵の draw の案は採らない）。実装の段の最初に §15 の resolve の節を書き直して review する。
- 2026-10-25 03:24 P2（q1560-i01、実装の段）: amaterrace-gpu の p2-p027 に main 5a7a5c4 を merge（a928541、衝突無し）。**U44 の書き直し**: design §15.8 版 5.1 = resolve は BLT の op RESOLVE（BLT_OP [2:0] = 4、STATE 0x00B BLT_RESOLVE: samples・平均の format（0 UNORM8・1 UNORM565・2 SAMPLE0、3〜5 の sRGB・float は render output がその format を書く時に足す）・tile の dst）、専用の streaming の engine `atg_blt_rsv`（読みの credit と line の FIFO、1 cycle に 1 group、組み立ての buffer）、既存の 2D は `atg_blt_2d` に名前だけ変え `atg_blt` は wrapper。ROP の道・新しい block・draw の案と比べた表は §15.8。Vulkan の refpage（vkCmdResolveImage・VkImageResolve）で 1.0 は color だけ、整数は 1 sample、重なり禁止を確かめた。ghci.md §8.2・§12.11（6bee646）。design-reviewer の review 3（20 件: High 2・Medium 8・Low 10）に全部対応（[review 3](../design-review-msaa.md)、ghci 2803b65）。段 S7（8 LW）を足し、S1 は 5 LW、計 41 LW。§2.4.4 の 122 と §8 の p027 の LW は §15 の外なので Q1 に依頼。
