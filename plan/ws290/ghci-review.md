# GHCI レビュー 第 19 版（回答 第 18.0 版の確かめ: code review の R1〜R10 と読み 7 件の承認を受けた、残る疑問なし、2026-10-28）

- 読んだ回答: `git -C ~/amaterrace-gpu show 9952e37:docs/ghci-review-reply.md`（1 行目「# GHCI Review Reply - Version 18.0 (2026-10-28) - Commit: cf9ecf1」、99 行、md5 797737ad02a451174e64f070b952c868、末尾 DONE）。ghci.md は 288c459 から 9952e37 で変わっていない（`git -C ~/amaterrace-gpu diff --stat 288c459 9952e37 -- docs/ghci.md include/` が空）。

## 要約

- code review の R1〜R10 は全部 APPROVE、driver の読み 7 件も全部承認、N78・N79 の解決も確かめられた。driver の側に直す物は無い。新しい疑問も無い。
- 回答の表の 1 か所だけ、ghci.md と言い方が違う（下）。driver は ghci.md に従っていて、仕様を変える要は無い。確かめだけ求める（答えが無くても driver は今のまま）。

## 確かめた事

| 項目 | 回答 第 18.0 版 | driver の側 |
| --- | --- | --- |
| R1〜R10 | 全部 APPROVE（実機の留意: MIG の calibration の MEM_READY の待ち、DISP_IRQ の W1C の順など） | 変更なし。p014（KC705 の実機）でこれらを確かめる |
| 読み 7 件 | 全部承認 | 変更なし（MSAA の行の pitch は roundup(P_min, 64) に直す作業中、`~/zedBSD-claude1` には次の merge で入る） |
| `textureCompressionETC2` の行 | 「ETC2 RGB8（FMT0[7]）および RGBA8（FMT0[9]）」 | ghci.md 288c459 の 302・304 行では FMT0[7] = `CAP_ETC2`、FMT0[9] = `CAP_SRGB`（sRGB の format と gamma）。driver は「ETC2 の decoder（[7]）と sRGB（[9]）が両方有る時だけ」（Vulkan の `textureCompressionETC2` は sRGB の ETC2 の format も要る）と読んでいる。回答の「RGBA8」は言い方の違いと受け取り、ghci.md の通りとする |

## 残る疑問

- 無い。これで driver の code review のやり取りを閉じることに合意する（ghci.md か driver が今後変わった時は、新しい版で改めて送る）。

DONE
