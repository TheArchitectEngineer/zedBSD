# GHCI レビュー 第 6 版（ghci.md fcd97e1 に対する疑問 N01〜N33、回答 第 5 版を反映、2026-10-28 02:29）

- 対象: `git -C /home/awe/amaterrace-gpu show fcd97e1:docs/ghci.md`（2664 行）と同じ commit の `include/ghci_cmd.h`（754 行）。行の番号はこの版の物。差は `git diff 7153329 fcd97e1 -- docs/ghci.md include/ghci_cmd.h` で読んだ。
- 読んだ回答: `docs/ghci-review-reply.md`（commit 268989e）、1 行目「# GHCI Review Reply - Version 5.0 (2026-10-28) - Commit: fcd97e1 (Target: N01〜N33)」、196 行、md5 b1df1a860bae641e28936dbd64c367b8、末尾 DONE。読み始め（02:26）と読み終わり（02:29）で同じ。
- 回答の番号は、こちらの N の番号と合っている。

## 要約

- 判定（第 5 版の残り 11）: **今回の解決 4**（N03・N13・N23・N32）、一部 7。
  回答と本文の食い違い 4（回答が「直した」と言う箇所が本文・header に残る: N16 §12.9 の CAPS[26]、N21 §3.4、N25 §13.2 の plane 0 の取り込み、N12 header の GhciCmdFlip）。
  改訂で新しく生じた矛盾は、該当の N の行に入れた（N16 の MEM_CACHE_CFG [7:0] に 256 が入らない、N33 の `flags[16]`）。新しい番号（N34〜）は無い。
- 残る疑問 **7 件: C 0・H 0・M 4・L 3**。**C・H は残らない**。N02・N14 は H から M に（slot の引き方・packet の形が本文で決まり、残りは上書き・初期値・優先の扱い）、N25・N33 は M から L に下げた。
- **driver の実装**: 残る疑問は全て仮の扱いで書ける。答え待ちで仮の扱いになるのは、96 byte を超える push constant（N02）、dynamic state の初期値と PSO との優先（N14）、MMU を通らない番地の 4 GiB より上の扱い（N12）。
- 前回と同じく、回答が挙げた本文の行に無い事は解決に数えていない。

## 残る疑問

重さ: C＝その部分の driver が書けない、H＝その部分を書く前に答えが要る、M＝仮の扱いで進められるが答えが要る、L＝文書の整理。

| ID | 重さ | 判定 | 疑問 | 根拠（ghci.md fcd97e1・回答 第 5 版） |
| --- | --- | --- | --- | --- |
| N02 | M | 一部 | 重なりは「後に書いた方が残る」、`texl` の slot k は `set0_va + k × 32`・`set1_va + k × 16` で `binding_offset_bytes` を使わない、組の index は廃止、と決まった。残り: (1) `maxPushConstantsSize` は 128 byte で、128 byte の push constant は c24〜c31 を上書きする。この時 Texture Unit が使う `set0_va`・`set1_va` は、c24〜c27 の値か、CP が CMD_BIND_DESCRIPTOR_TABLE で受けた値か（前者なら上書きの後の `texl` は何を読むか）。shader が set の基を読む c[32..511] には、誰がどの packet で書くか。(2) Texture Unit は set 0 を texture だけ、set 1 を sampler だけの slot 順の表として読む。Vulkan の 4 つの set（`maxBoundDescriptorSets` 4）は hardware の set 0〜3 に 1 対 1 に対応せず、UMD が texture・sampler の表を組み直して set 0・1 に束ねる、で正しいか。 | §8.4 1313〜1331 行・1406〜1412 行、§8.6 1514 行・1517 行、§12.10.2 2252〜2262 行。回答 N02 |
| N14 | M | 一部 | CMD_SET_DYNAMIC_STATE の `state_id` と payload、stride（0 は全 vertex が同じ要素、`0xFFFFFFFF` は PSO の既定値）は決まった。残り: stencil の mask・reference、depth bias の値、blend constants は、最初の CMD_SET_DYNAMIC_STATE の前・CMD_BIND_PIPELINE の後に何の値か（BIND_PIPELINE は dynamic state を初期化するか）。PSO の dw9 の line width と `state_id` 0x04、dw11 の blend と `state_id` 0x07〜0x0E は、どちらが効くか（後に書いた方か）。`CAP_MRT_BLEND` = 0 の GPU に 0x07〜0x0E を送った時は。 | §7.3 816〜842 行、§8.2 1211 行・1213 行・1238 行。回答 N14 |
| N12 | M | 一部・食い違い | §11.2 の flag の表、VM_MAP の host の phys は PA_BITS まで、CMD_FLIP の base_hi・record_pa_hi は 0、は本文に入った。残り: §11.3 の HOST_LIMIT は「HOST_LIMIT より下は host、LOCAL_BASE 以上は local」と番地で振り分けるが、MMU を通らない 64 bit の番地（`fence_addr`、§13.2 の scanout の「host の DMA 番地 / IOVA」）が 4 GiB より上の host memory の時、どちらへ行くか（PTE には L bit があるが、これらには無い）。`fence_addr` は HOST_LIMIT より下に限るか。header の GhciCmdFlip は今も base・record_pa を「64-bit physical」と書く（回答は header も直したと言う）。 | §11.3 1778 行、§6.0 620 行、§7.3 947 行・1146〜1155 行、§11.2 1709 行、§13.2 2503 行、ghci_cmd.h 740〜746 行。回答 N12 |
| N16 | M | 一部・食い違い・新規 | MEM_CACHE_CFG は §11.3・§4.1 に入り、§12.1 は .3 の形・depth compare・gather を全ての構成で使えるとした。残り: (1) MEM_CACHE_CFG [7:0] は L2 の KiB で値に 256 を挙げるが、8 bit に 256 は入らない: 256 KiB はどう表すか。(2) §12.9 は今も .3 の形・texel offset・`texsz.d`・depth compare・gather・`texqlod` を「draft, CAPS[26]」とし、FFILT の段は CAPS[26] を float の filter の報告に挙げる（回答は §12.1 を直したと言うが §12.9 は変わっていない）: CAPS[26] は何で、これらは GHCI 1.0 で使えるか（draft の印は残るか）。 | §11.3 1780 行、§11.5.1 1824 行、§12.1 1904 行、§12.9 2146 行・2164 行・2168 行・2179 行・2193 行・2201 行・2229 行。回答 N16 |
| N25 | L | 一部・食い違い | record の 16 byte、record_pa の 32 bit、VSYNC_WAIT と IMMEDIATE の優先、display_id は §7.3 で決まった。残り: §13.2 は今も「plane 0 は次の vsync で取る、ENABLE が 0 の時だけ直ちに」で IMMEDIATE が無い。§13.2 も §7.3 に合わせるか。 | §7.3 1150 行・1156〜1160 行、§13.2 2514〜2515 行。回答 N25 |
| N21 | L | 一部・食い違い | DISP_ERROR（bit 5）、§9.2 の手順、§13.2 の事象の名前は直った。残り: §3.4 は今も事象を「VBLANK, scanout flip take completed, FIFO underflow error, hotplug」と呼ぶ（回答は §3.4 も直したと言う）。 | §3.4 405 行、§13.2 2526〜2532 行。回答 N21 |
| N33 | L | 一部・新規 | PSO が正本で MS_CTRL・SAMPLE_KILL は CP が設定する、sample shading は使える、TEX_IMAGE・TEX_SAMPLER の field の対応は §12.11 に入った。残り: (1) §12.11 は per-sample の処理に「alpha to one」を挙げるが、PSO にそれを選ぶ bit が無い（旧 MS_CTRL[9]）: 常に効くのか、効かないのか。(2) §12.11 2272 行は centroid を「`flags[16]` `fs_centroid`」と書くが、`fs_centroid` は dw15 [16] で dw8 の `flags` ではない。(3) §12.9 は今も TEX_IMAGE を引く（2166 行の「TEX_IMAGE[15:12] = 2」は log2 で、§12.11 の対応先 `sample_count` は 1・2・4。2175 行の「formats are those of TEX_IMAGE (section 8.4)」は RGBA8・BGRA8・depth だけ）: `texl` などが読める format は §8.5.1 の表で決まる、で正しいか。 | §12.11 2272 行・2275 行・2278〜2282 行・2315 行、§8.2 1210 行（dw8）・1217 行（dw15）、§12.9 2162 行・2166 行・2175 行。回答 N33 |

## 解決した N

N01・N03（表が 0 の時 attachment 0 は常に `color_clear_rgba`、format で解釈、1〜7 は 0、§8.4）・N04〜N11・N13（各 image の packet に pitch・format・tiled、driver が slice の番地を計算、level 0 の pitch と tiled の tile 行の pitch、§7.3・§8.3）・N15・N17〜N20・N22・N23（border の表は `state_id` 0x10 の session の VA、§7.3・§8.3）・N24・N26〜N31・N32（BEGIN_RENDER_PASS の flags と PSO dw8 [11] が header と一致、ghci_cmd.h 182 行・590 行）。

DONE
