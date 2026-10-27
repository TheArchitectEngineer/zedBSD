# GHCI レビュー 第 8 版（ghci.md 35b89af に対する疑問 N01〜N36、回答 第 7 版を反映、2026-10-28 02:45）

- 対象: `git -C /home/awe/amaterrace-gpu show 35b89af:docs/ghci.md`（2677 行）と同じ commit の `include/ghci_cmd.h`。行の番号はこの版の物。差は `git diff bc3b4e6 35b89af -- docs/ghci.md include/ghci_cmd.h` で読んだ。
- 読んだ回答: `docs/ghci-review-reply.md`（commit 17e3d0d）、1 行目「# GHCI Review Reply - Version 7.0 (2026-10-28) - Commit: 35b89af (Target: N01〜N34)」、95 行、md5 d5bd5fa9542d06e7bb8eeffbd96110be、末尾 DONE。読み始め（02:44）と読み終わり（02:45）で同じ。
- 回答の番号は、こちらの N の番号と合っている。

## 要約

- 判定（第 7 版の残り 4）: **今回の解決 3**（N14・N33・N34）、一部 1（N12、回答と本文の食い違いを含む）。
  改訂で新しく生じた矛盾 2: N35（`CMD_SESSION_OPEN` の opcode）、N36（`texf` の「raw」と形式ごとの戻り値）。
- 残る疑問 **3 件: C 0・H 0・M 0・L 3**。
- **driver の実装**: 残る疑問は全て文書の整理で、driver と shader の compiler は答えを待たずに書ける（N12 は driver が使わない番地の扱い、N35 は opcode の表の 0x12 で読める、N36 は形式ごとの規則で読める）。
- 前回と同じく、回答が挙げた本文の行に無い事は解決に数えていない。

## 残る疑問

重さ: C＝その部分の driver が書けない、H＝その部分を書く前に答えが要る、M＝仮の扱いで進められるが答えが要る、L＝文書の整理。

| ID | 重さ | 判定 | 疑問 | 根拠（ghci.md 35b89af・回答 第 7 版） |
| --- | --- | --- | --- | --- |
| N12 | L | 一部・食い違い | HOST_LIMIT の下への制約（32 bit の DMA で IOMMU 無し）と CMD_WAIT_FENCE への同じ振り分けは本文に入った。残り: (1) 回答は「LOCAL_BASE + LOCAL_SIZE 以上の番地は host へ行かず fault」と言い、§11.3 1789 行も「≥ LOCAL_BASE + LOCAL_SIZE within the local DRAM aperture are invalid」と書くが、同じ 1789 行の前の文と §7.3 960 行・970 行は「[LOCAL_BASE, LOCAL_BASE + LOCAL_SIZE) の外は host」。「local DRAM aperture」の範囲は本文のどこで決まるか。LOCAL_BASE + LOCAL_SIZE 以上のある番地が host へ行くか fault かは何で決まるか。(2) 1789 行は fault を「Kind 4」とし、回答は「Kind 4 `GHCI_FAULT_KIND_AXI_ERROR`」と言うが、§3.5 の FAULT_INFO の表では 4 は `WATCHDOG`、`AXI_ERROR` は 5。どちらの Kind か。 | §7.3 960 行・970 行、§11.3 1789 行、§3.5 418〜419 行。回答 N12 (1) |
| N35 | L | 新規 | §7.3 844 行（dynamic state の初期化の契機）は `CMD_SESSION_OPEN` を「0x01」と書き、回答 N14 も「opcode 0x01」と言う。opcode の表（717 行）・1098 行・1668 行は 0x12。844 行の 0x01 は 0x12 の誤りか。 | 717 行・844 行・1098 行・1668 行。回答 N14 (1) |
| N36 | L | 新規 | §12.9 の「Return values written to rd..rd+3 depend on format data type」（2186 行）の下に、UNORM は [0, 1] の fp32 などの規則と並んで「`texf` returns raw texel values」（2192 行）がある。`texf` で UNORM・SNORM の形式を読んだ時、rd..rd+3 は 2187〜2188 行の正規化した fp32 か、格納されたままのビット列か（「raw」はフィルタと深さの比較を通らない意味だけか）。 | §12.9 2155 行・2186〜2192 行。回答 N34 |

## 解決した N

N01〜N11・N13〜N34（今回: N14 は初期値への復帰が電源投入・CTRL.RESET・`CMD_SESSION_OPEN`、submission では戻らず、dynamic state は session ごとで `CMD_BIND_SESSION` で切り替わる、§7.3 844〜845 行。N33 は §12.11 2286 行が `compute_lds_and_flags[16]`（dw15）で §8.2 1224 行と一致。N34 は UNORM [0, 1]・SNORM [-1, 1]・float はそのまま・UINT/SINT は整数のビット列・欠けた成分は (0, 0, 1)、§12.9 2155 行・2186〜2191 行）。

DONE
