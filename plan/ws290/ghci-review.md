# GHCI レビュー 第 9 版（ghci.md 89529de に対する疑問 N01〜N36、回答 第 8 版を反映、2026-10-28 02:50）

- 対象: `git -C /home/awe/amaterrace-gpu show 89529de:docs/ghci.md`（2677 行）と同じ commit の `include/ghci_cmd.h`。行の番号はこの版の物。差は `git diff 35b89af 89529de -- docs/ghci.md include/ghci_cmd.h` で読んだ（ghci.md の 3 行だけ、ghci_cmd.h は変わらない）。
- 読んだ回答: `docs/ghci-review-reply.md`（commit 5d5eb66）、1 行目「# GHCI Review Reply - Version 8.0 (2026-10-28) - Commit: 89529de (Target: N01〜N36)」、73 行、md5 b37ea3a850453597ad86a4e64ccf702b、末尾 DONE。Q1 が見た物と同じ。読み始めと読み終わり（02:50）で同じ。
- 回答の番号は、こちらの N の番号と合っている。

## 要約

- 判定（第 8 版の残り 3）: **今回の解決 3**（N12・N35・N36）。改訂の差で新しく生じた矛盾は無い（N37 以降は無し）。
- 残る疑問 **0 件: C 0・H 0・M 0・L 0**。**疑問は残っていない。**
- **driver の実装**: 文書の疑問による driver と shader の compiler の待ちは無い。

## 残る疑問

重さ: C＝その部分の driver が書けない、H＝その部分を書く前に答えが要る、M＝仮の扱いで進められるが答えが要る、L＝文書の整理。

| ID | 重さ | 判定 | 疑問 | 根拠 |
| --- | --- | --- | --- | --- |

## 解決した N

N01〜N36（今回: N12 は §11.3 1789 行が「strictly binary」で [LOCAL_BASE, LOCAL_BASE + LOCAL_SIZE) の外は全て host、「local DRAM aperture」の文は消え、bus の decode error は Kind 5 `GHCI_FAULT_KIND_AXI_ERROR`（§3.5 419 行と一致）、§7.3 960 行・970 行とも一致。N35 は §7.3 844 行が 0x12 で 717 行・1098 行・1668 行・ghci_cmd.h 34 行と一致。N36 は §12.9 2192 行が `texf` も形式ごとの規則（UNORM・SNORM は正規化した fp32）で返し、通らないのはフィルタ・LOD・深さの比較だけ）。

DONE
