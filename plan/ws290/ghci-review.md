# GHCI レビュー 第 17 版（ghci.md d9252e4 の食い違い 2 件: 属性の register（N78）と multisample の行の pitch（N79）、2026-10-28）

- 対象: `git -C /home/awe/amaterrace-gpu show d9252e4:docs/ghci.md` §12.10.3「1. Vertex Shader Input Conventions」と、同じ commit の `include/ghci_cmd.h` 172 行、`fw/cp/pipeline.c` 751〜760 行。
- 前の版: 第 16 版（N01〜N77 全部解決、回答 第 16.0 版で Signoff）。この版は、driver の実装（GSI1 の compiler）で見つけた食い違い 1 件だけ。

## 要約

- 新しい疑問 2 件（N78・N79）。N78: driver は正本の ghci.md（累積の base_i）に従って実装済み。header と firmware を ghci.md に合わせるか、ghci.md を直すかの確かめを求める。N79: ghci.md の中の 2 か所が食い違う。driver は仮に §12.11 の P を使う。

## 疑問

| ID | 重さ | 食い違い | driver の扱い（仮） | 求める答え |
| --- | --- | --- | --- | --- |
| N78 | M | ghci.md §12.10.3: 有効な attribute slot i の値は register r[base_i .. base_i + C_i − 1]、base_0 = 0、base_{i+1} = base_i + C_i（C_i は format の成分の数）、合計 60 以下。`include/ghci_cmd.h` 172 行: `GHCI_VS_REG_ATTR(loc, comp)` = 4 × loc + comp（location ごとに 4 register）。`fw/cp/pipeline.c` 751〜760 行は pipeline の記録の `attr[i].dst` を `GHCI_S_ATTR_DST + i` に書く（CP が PSO から base_i を計算する §12.10.3 の「Autonomous Hardware Configuration」と別の形） | 正本の ghci.md の累積の base_i で compile する（vec4 でない属性が並ぶと header の式と register が違う）。header は照合にだけ使う | (1) 正しいのは ghci.md の累積か、header の 4 × loc + comp か。(2) ghci.md が正しいなら、`ghci_cmd.h` の `GHCI_VS_REG_ATTR` と firmware の ATTR_DST の設定を直す予定か（実機 KC705 での確かめの前に合わせたい） |
| N79 | M | §8.4（1396 行）「color, depth/stencil, and resolve attachments are strictly linear row layouts with 64-byte aligned `row_pitch_bytes`」と §12.11（2501〜2505 行）「Sample s of pixel (x, y) is at base + layer × layer stride + y × P + (x × S + s) × ps … P = roundup(width, 2) × S × ps … The render target and depth strides of a multisampled draw are P」。P は 64 byte に揃わない事がある（例: 幅 10・4 sample・4 byte で P = 160） | multisample の image の行の pitch は §12.11 の P とし、render pass の attachment の記述と texture descriptor の `row_pitch_bytes` に P を書く（仮） | (1) multisample の attachment の行の pitch は P か、64 byte に揃えた値か。(2) 記述の `row_pitch_bytes` を hardware は使うのか（P を自分で計算して記述の値を無視するのか）。(3) multisample の texture（`texf` で sample を読む）の行も P か |

DONE
