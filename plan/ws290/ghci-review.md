# GHCI レビュー 第 17 版（ghci.md d9252e4 と include/ghci_cmd.h の属性の register の食い違い、2026-10-28）

- 対象: `git -C /home/awe/amaterrace-gpu show d9252e4:docs/ghci.md` §12.10.3「1. Vertex Shader Input Conventions」と、同じ commit の `include/ghci_cmd.h` 172 行、`fw/cp/pipeline.c` 751〜760 行。
- 前の版: 第 16 版（N01〜N77 全部解決、回答 第 16.0 版で Signoff）。この版は、driver の実装（GSI1 の compiler）で見つけた食い違い 1 件だけ。

## 要約

- 新しい疑問 1 件（N78）。driver は正本の ghci.md（累積の base_i）に従って実装済み。header と firmware を ghci.md に合わせるか、ghci.md を直すかの確かめを求める。

## 疑問

| ID | 重さ | 食い違い | driver の扱い（仮） | 求める答え |
| --- | --- | --- | --- | --- |
| N78 | M | ghci.md §12.10.3: 有効な attribute slot i の値は register r[base_i .. base_i + C_i − 1]、base_0 = 0、base_{i+1} = base_i + C_i（C_i は format の成分の数）、合計 60 以下。`include/ghci_cmd.h` 172 行: `GHCI_VS_REG_ATTR(loc, comp)` = 4 × loc + comp（location ごとに 4 register）。`fw/cp/pipeline.c` 751〜760 行は pipeline の記録の `attr[i].dst` を `GHCI_S_ATTR_DST + i` に書く（CP が PSO から base_i を計算する §12.10.3 の「Autonomous Hardware Configuration」と別の形） | 正本の ghci.md の累積の base_i で compile する（vec4 でない属性が並ぶと header の式と register が違う）。header は照合にだけ使う | (1) 正しいのは ghci.md の累積か、header の 4 × loc + comp か。(2) ghci.md が正しいなら、`ghci_cmd.h` の `GHCI_VS_REG_ATTR` と firmware の ATTR_DST の設定を直す予定か（実機 KC705 での確かめの前に合わせたい） |

DONE
