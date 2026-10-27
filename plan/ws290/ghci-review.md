# GHCI レビュー 第 12 版（ghci.md cf8be08 に対する疑問 N01〜N46、回答 第 11.0 版を反映、疑問は残っていない、2026-10-28 03:47）

- 対象: `git -C /home/awe/amaterrace-gpu show cf8be08:docs/ghci.md`（2782 行）と同じ commit の `include/ghci_cmd.h`・`include/gsi1_isa.h`。差は `git diff 1a1fa85 cf8be08 -- docs/ghci.md include/` で読んだ（ghci.md の 1 行と gsi1_isa.h の注記だけ。main の先頭 4188a4c は回答の file だけの差）。
- 読んだ回答: `docs/ghci-review-reply.md`（commit 4188a4c）、1 行目「# GHCI Review Reply - Version 11.0 (2026-10-28) - Commit: cf8be08 (N45 & N46 Fully Resolved)」、36 行、md5 a2a7366a6b03bca1f99f869161499587、末尾 DONE。
- 回答の番号は、こちらの N の番号と合っている。

## 要約

- 判定（第 11 版の残り 2）: **今回の解決 2**（N45・N46）。改訂の差で新しく生じた矛盾は無い。
- 残る疑問 **0 件: C 0・H 0・M 0・L 0**。**疑問は残っていない。**
- **driver の実装**: 文書の疑問による driver と shader の compiler の待ちは無い。zedBSD の driver は ghci.md cf8be08 を正本に設計（版 4）を仕上げる。

## 残る疑問

重さ: C＝その部分の driver が書けない、H＝その部分を書く前に答えが要る、M＝仮の扱いで進められるが答えが要る、L＝文書の整理。

| ID | 重さ | 判定 | 疑問 | 根拠 |
| --- | --- | --- | --- | --- |

## 解決した N

- N01〜N44（第 11 版まで）。
- N45（今回）: `include/gsi1_isa.h` の冒頭と header・interface 表の定義に「pre-1.0、GHCI 1.0 では使わない」を明記、flag の注記を `DEVICE_CAPS`・`EXT_CAP_FEATURE`・PSO dw15 の名前に、`lda` の注記から「A GPU with CAPS[30]」を除いた（推しの「明記」の側で受けた）。
- N46（今回）: 2298 行から `VK_ERROR_INITIALIZATION_FAILED` を除いた。

## 参考（疑問ではない、返信は不要）

- `include/gsi1_isa.h`（cf8be08）382 行の `GSI1_TEXSZ_SAMPLES` の注記に旧名「CAPS[29]」が 1 か所残る（`DEVICE_CAPS[31]` `CAP_MSAA_4X` の事）。次に header を直す時についでに。

DONE
