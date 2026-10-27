# GHCI レビュー 第 11 版（ghci.md 1a1fa85 に対する疑問 N45・N46、回答 第 10.0 版を反映、2026-10-28 03:45）

- 対象: `git -C /home/awe/amaterrace-gpu show 1a1fa85:docs/ghci.md`（2782 行）と同じ commit の `include/ghci_cmd.h`・`include/gsi1_isa.h`。行の番号はこの版の物。差は `git diff bc7df9e 1a1fa85 -- docs/ghci.md include/` で読んだ（main の先頭 e4355be は回答の file だけの差）。
- 読んだ回答: `docs/ghci-review-reply.md`（commit e4355be）、1 行目「# GHCI Review Reply - Version 10.0 (2026-10-28) - Commit: 1a1fa85 (N37〜N45 Fully Resolved)」、83 行、md5 f049d30c5f73d422ac96d7a637cf744e、末尾 DONE。
- 回答の番号は、こちらの N の番号と合っている。

## 要約

- 判定（第 10 版の N37〜N45）: **今回の解決 8**（N37〜N44）、**一部解決 1**（N45: ghci.md は直ったが `include/gsi1_isa.h` が残る）。新しい疑問 1（N46）。
- 残る疑問 **2 件: C 0・H 0・M 0・L 2**。
- **driver の実装**: 文書の疑問による driver と shader の compiler の待ちは無い（残りは文書と header の整理だけ。driver は include を写さず、値は ghci.md から `gsi1-abi.h` に起こす）。

## 残る疑問

重さ: C＝その部分の driver が書けない、H＝その部分を書く前に答えが要る、M＝仮の扱いで進められるが答えが要る、L＝文書の整理。

| ID | 重さ | 判定 | 疑問 | 根拠 | 推し |
| --- | --- | --- | --- | --- | --- |
| N45 | L | 一部解決 | ghci.md の古い参照は全部直った。回答の (5)「`include/gsi1_isa.h` を §12.10.3 と完全に整合・同期」は未了: 1a1fa85 の gsi1_isa.h は、廃止した program の header（`GSI1_MAGIC`・`GSI1_HEADER_WORDS`・`GSI1_H_*`、10・22〜38 行）、interface 表と kind 1〜14（`GSI1_IF_*`・`GSI1_KIND_*`、67〜90 行と各 kind の word の定義）、旧 CAPS の注記（54・55・58 行の CAPS[28]・[24]・[25]、`lda` の注記（197 行）の「A GPU with CAPS[30]」）を持ったまま | gsi1_isa.h 10〜90 行、196〜198 行、ghci.md 2265 行 | header・interface 表・kind の定義を消し（または「pre-1.0、GHCI 1.0 では使わない」と明記し）、CAPS[n] を `DEVICE_CAPS`・`EXT_CAP_FEATURE` の名前に直す。varying の 64 word は capability に依らない（§12.10.3）ので「A GPU with CAPS[30]」を消す |
| N46 | L | 新 | 2298 行「the driver must reject pipeline creation (`VK_ERROR_INITIALIZATION_FAILED`)」: `VK_ERROR_INITIALIZATION_FAILED` は Vulkan 1.0 の `vkCreateGraphicsPipelines` が返せる値に無い（返せるのは `VK_ERROR_OUT_OF_HOST_MEMORY`・`VK_ERROR_OUT_OF_DEVICE_MEMORY`）。返す値は Vulkan の driver の側の事で、GHCI が決める事でもない | 2298 行 | 「the driver must reject pipeline creation」までにし、Vulkan の戻り値の名前を消す（zedBSD の driver は `VK_ERROR_OUT_OF_DEVICE_MEMORY` を返して log を出す） |

## 解決した N

- N01〜N36（第 9 版まで）。
- N37〜N44（今回）: N37 は §12.10.3 全体で解決。N38 は 2287〜2300 行（slot の有効は format ≠ 0、register は有効な slot の成分の数の累積和から詰める、合計 60 以下、`ATTR_DST[i]` は CP が `CMD_BIND_PIPELINE` で自律に組み driver は何もしない）。N39 は 2309〜2310 行（`used_regs_vs` ≥ 62 の時だけ r60・r61、r62 は hardware が書かない）。N40 は POINT_SIZE が PT に入らず、`lda` の word 0〜3 が clip の POS（2322・2330・2358 行、gsi1_isa.h の注記も一致）。N41 は 2341〜2343 行（vertex k は b_k の頂点、Vulkan の順、vertex 0 が provoking、line の vertex 2・point の vertex 1・2 は未定義）。N42 は 2332〜2339 行（Vulkan の clip の体積・割り算・viewport の式・depth clamp）と POINT_SIZE は 1 固定。N43 は 2368・2373・2466 行と ghci_cmd.h・gsi1_isa.h の `& 7`・`GSI1_COLOR_ATTACHMENTS 8`（export 0x100〜0x173 は DEPTH 0x104・SAMPLE_MASK 0x105・SRC1 0x108〜0x10B と重ならない）、MRT は CAP_ROP_PACK に依らない。N44 は 1426 行が 0x108〜0x10B。
- N45 の ghci.md の部分: 2133 行の参照（§12.10.2）、§12.12 の interface kinds の段落の削除、2259 行の参照（§12.10.3）、§12.5・§12.6・§12.11 の CAPS[n] の置き換え、§12.1 の CAPS[25]（1918 行、予約、実行は kind 1）。

DONE
