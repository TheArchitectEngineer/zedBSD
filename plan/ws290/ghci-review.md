# GHCI レビュー 第 7 版（ghci.md bc3b4e6 に対する疑問 N01〜N34、回答 第 6 版を反映、2026-10-28 02:38）

- 対象: `git -C /home/awe/amaterrace-gpu show bc3b4e6:docs/ghci.md`（2669 行）と同じ commit の `include/ghci_cmd.h`。行の番号はこの版の物。差は `git diff fcd97e1 bc3b4e6 -- docs/ghci.md include/ghci_cmd.h` で読んだ。
- 読んだ回答: `docs/ghci-review-reply.md`（commit 5613e82）、1 行目「# GHCI Review Reply - Version 6.0 (2026-10-28) - Commit: bc3b4e6 (Target: N01〜N33)」、127 行、md5 b19904cdac56083fae5d97fc6407869f、末尾 DONE。読み始め（02:36）と読み終わり（02:38）で同じ。
- 回答の番号は、こちらの N の番号と合っている。

## 要約

- 判定（第 6 版の残り 7）: **今回の解決 4**（N02・N16・N21・N25）、一部 3（N12・N14・N33、いずれも L に下げた）。
  回答と本文の食い違い 1（N33: 回答は centroid の bit を本文で直したと言うが §12.11 2278 行は今も `flags[16]`）。
  改訂で新しく生じた矛盾 1: N34（§12.9 の format の戻り値）。
- 残る疑問 **4 件: C 0・H 0・M 1・L 3**。
- **driver の実装**: 残る疑問は全て仮の扱いで書ける。答え待ちで仮の扱いになるのは、SNORM・整数・float の texture を `texl` が返す値（N34、shader の compiler に効く）だけ。
- 前回と同じく、回答が挙げた本文の行に無い事は解決に数えていない。

## 残る疑問

重さ: C＝その部分の driver が書けない、H＝その部分を書く前に答えが要る、M＝仮の扱いで進められるが答えが要る、L＝文書の整理。

| ID | 重さ | 判定 | 疑問 | 根拠（ghci.md bc3b4e6・回答 第 6 版） |
| --- | --- | --- | --- | --- |
| N34 | M | 新規 | §12.9 は読める format を §8.5.1 の表に委ね、「UNORM RGBA8, BGRA8, RGB565, and floating-point formats return [0, 1] normalized values」とする。§8.5.1 の Sampled の列には SNORM（R8_SNORM など）、UINT・SINT（R8_UINT・R32_SINT など）、SFLOAT（R16_SFLOAT・R32_SFLOAT など）がある。(1) float の format は [0, 1] に丸めるのか、格納された値をそのまま返すのか。(2) SNORM は [-1, 1] か。(3) UINT・SINT を `texl`・`texf` で読んだ時、rd..rd+3 は fp32（2152〜2153 行）に変換した値か、整数のビット列か。(4) R・RG の format で欠けた成分（G・B・A）は何を返すか。 | §12.9 2152〜2154 行・2184 行、§8.5.1 1434 行・1437 行・1438 行・1453 行・1458 行。回答 N33 (3) |
| N12 | L | 一部 | `fence_addr` の振り分け（§7.3 959 行）と FLIP の 32 bit は決まった。残り: (1) 959 行は「[LOCAL_BASE, LOCAL_BASE + LOCAL_SIZE) の外は全て host」、§11.3 1787 行は「LOCAL_BASE 以上は local」で、LOCAL_BASE + LOCAL_SIZE 以上の 32 bit の番地の行き先が二つの節で違う。どちらが正か。(2) 959 行は「HOST_LIMIT に制限されない」、1787 行は「IOMMU が無く 32 bit の DMA の system では host の番地は HOST_LIMIT より下」。後者の system で `fence_addr` も HOST_LIMIT より下に限るか。(3) 959 行の振り分けは CMD_WAIT_FENCE の `fence_addr` にも同じく当てはまるか（959 行は CMD_FENCE の節にだけある）。 | §7.3 959 行・962〜966 行、§6.0 620 行、§11.3 1787 行。回答 N12 (1) |
| N14 | L | 一部 | 初期値（stencil の mask 0xFF・reference 0、depth bias 0、blend constants 1.0）、PSO との後勝ち、`CAP_MRT_BLEND` = 0 の fault は §7.3 に入った。残り: 「Initial / Reset Defaults」はいつこの値に戻るか（電源投入・CTRL.RESET だけか、ring の submission・session の切替・CMD_BIND_PIPELINE でも戻るか）。戻らないなら、ある session の dynamic state は次の session の draw に残るか。 | §7.3 843〜846 行。回答 N14 (1)(2) |
| N33 | L | 一部・食い違い | alpha to one は §12.11 から消え（回路が無い）、`texsz.s`・`texsz.l` は descriptor の field を返すと決まった。残り: §12.11 2278 行は今も centroid を「`flags[16]` `fs_centroid`」と書く（`flags` は dw8 で、`fs_centroid` は dw15 [16]。2250 行・2281 行・2305 行は dw15）。回答は本文で明確にしたと言う: 2278 行も dw15 に直すか。 | §12.11 2278 行・2281 行・2305 行、§12.10 2250 行、§8.2 1215 行（dw8）・1222 行（dw15）。回答 N33 (2) |

## 解決した N

N01〜N11・N13・N15〜N32（今回: N02 は TU が CMD_BIND_DESCRIPTOR_TABLE で受けた SET_TABLE_ADDR[0..1] を使い c24〜c31 の上書きに影響されない・c[32..511] は CMD_LOAD_CONST・texture を set 0・sampler を set 1 に束ねる、§8.4 1413〜1418 行。N16 は MEM_CACHE_CFG [15:0] が L2 の KiB・§12.9 の draft と CAPS[26] の撤廃、§11.3 1789 行・§12.9。N21 は §3.4 405 行が DISP_IRQ_STATUS の名前。N25 は §13.2 2520 行が IMMEDIATE を含む）。

DONE
