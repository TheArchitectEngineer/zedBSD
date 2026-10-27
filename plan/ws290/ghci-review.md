# GHCI レビュー 第 3 版（ghci.md 382dd77 に対する疑問 N01〜N30、回答 第 2 版を反映、2026-10-27 20:15）

- 対象: `git -C /home/awe/amaterrace-gpu show 382dd77:docs/ghci.md`（2463 行）。行の番号はこの版の物。
- 読んだ回答: `docs/ghci-review-reply.md`（commit 915d97c）、1 行目「# GHCI Review Reply - Version 2.0 (2026-10-27) - Commit: 382dd77 (Target: N01〜N29)」、296 行、md5 b04c8acd9c5f9ce6c1e1dcb9fc6d42a4、末尾 DONE。読んでいる間に変わっていない。
- 回答の N10〜N19 は、こちらの N10〜N19 と別の質問（viewport・HOST_LIMIT・COPY_BUFFER・format 表・dual-source・timestamp・mesh・ray tracing・caps の優先・EXT_CAP_FMT0）に答えている。こちらの番号の質問として読み直してほしい。

## 要約

- 判定（N01〜N29）: **解決 4**（N05・N20・N22・N29）、一部 15（うち回答と ghci.md の食い違い 6: N03・N09・N19・N21・N25・N26）、未解決 10（うち本文の中の矛盾 3: N01・N06・N16、回答と食い違い 1: N23）。新しい疑問 1（N30）。
- 残る疑問 **26 件: C 3・H 9・M 11・L 3**。
- **driver の実装は、KMD の基盤の範囲なら始められる**: register の初期化、ring・doorbell・ring の折り返し、fence の待ち、IRQ（§3.4 の bit 0〜3・5）、session の fault の回収（§3.3・§10）、SESSION_OPEN/CLOSE・BIND_SESSION・VM_MAP/UNMAP の発行、COPY_BUFFER・FILL_BUFFER。reset の手順・CP_HEAP の拡張・FLIP は M・H の答え待ちで、仮の扱いで書ける。
- **shader・descriptor・render pass・描画・user の command の受け付けは、まだ始められない**（C の 3 件）。
- 次の回答も、こちらの N の番号と ghci.md の直した節と行を添えてほしい。回答だけにある事は解決に数えない。

## 残る疑問

重さ: C＝その部分の driver が書けない、H＝その部分を書く前に答えが要る、M＝仮の扱いで進められるが答えが要る、L＝文書の整理。

| ID | 重さ | 判定 | 疑問 | 根拠（ghci.md 382dd77・回答 第 2 版） |
| --- | --- | --- | --- | --- |
| N01 | C | 未解決（本文の中で矛盾） | §12.10.1 は register の数（`used_regs`）・`lds_bytes`・workgroup size・flags（centroid・depth export・discard）・varying の数を PSO で渡すと言うが、§8.2 の `GhciPipelineState` にそれらの field は無い（dw13〜15 は reserved）。CMD_DISPATCH も group 数だけ。これらはどの構造体のどの field か。「最大 64 本（r0..r62）」はどちらか。 | §12.10.1 2031〜2044 行、§8.2 1055〜1100 行、§7.3 CMD_DISPATCH 864〜870 行。回答 N01 |
| N02 | C | 一部 | interface table は消え、descriptor table を直に引く形に一本化された。残り: `texl` などの slot は sub[3:0]（0〜15）だが、slot から set・binding・sampler へどう対応するか。型の混ざる table で `binding × descriptor_size_bytes`（texture 32・sampler/buffer 16）はどう決まるか。uniform・storage buffer を読む命令の列（descriptor の base を読む命令と `lw`/`ld.64`）。dynamic offset を足すのは driver か shader か。§8.4 は今も `tex ld`・`gsi1_load`。 | §8.4 1157〜1169 行・1223〜1231 行、§12.9 1939〜1943 行、§12.5 1816 行、§12.10.2 2045〜2061 行。回答 N02 |
| N03 | C | 一部・食い違い | `GhciRenderPassDesc` に depth/stencil・resolve・大きさ・sample 数が入った。残り: `image_va_lo` などは 32 bit だけ（回答は 64 bit の lo/hi と言う）。`layers` は uint8_t で「1..2048」。`row_pitch_bytes` は 16 bit で 16384×4 byte が入らない。clear は `color_clear_rgba` の 1 語（回答は clear_color_0..3 の 4 語）で、整数・float の format の値は。`clear_color_1_7_va_lo` の表の形と上位 32 bit。attachment の layer・level の指定。resolve は 1 個だけ。LoadOp=Clear と clear_flags はどちらが効くか。§7.2 は今も 64 byte。 | §8.4 1171〜1222 行、§7.3 846〜862 行、§7.2 0x2B。回答 N03 |
| N04 | M | 一部 | 二次での禁止に FENCE・WAIT_FENCE・WRITE_TIMESTAMP が入り、kind 3 に決まった。残り: §6.0 は WRITE_TIMESTAMP を全部禁止、§7.3 は flags[0]=1 だけ禁止。Vulkan は二次の中の vkCmdWriteTimestamp を許す: flags[0]=0 は許されるか。入れ子は §7.3 で `GHCI_FAULT_BAD_PROGRAM`（kind が無い）。 | §6.0 586〜592 行、§7.3 889 行・917〜924 行。回答 N04 |
| N06 | H | 未解決（本文の中で矛盾） | §3.3 は「client が一つだけの時その fault で FAULTED にならない」、同じ §3.3 と §10 は「開いた client の session が全部 fault したら FAULTED」。client が一つの時はどちらか。 | §3.3 372 行・381 行、§10 1403 行。回答 N06 |
| N07 | M | 一部 | session 0 は SESSION_OPEN が要らず、VM_MAP(session_id=0) で map すると決まった。残り: 「reset の後 session 0 の root を CP_HEAP から取る」が、電源の投入の後は CP_HEAP がまだ 0（App. A は step 4 で設定）。session 0 の root はいつ確保されるか、CP_HEAP を設定した後に何か要るか。 | §6.0 610〜615 行、App. A 2449〜2452 行。回答 N07 |
| N08 | M | 一部 | §5.2 に手順が入った。残り: §5.2 は「page tables・session descriptors は有効のまま」と言い、同時に SESSION_ACTIVE を消し SESSION_OPEN をやり直させる: 古い page table の CP_HEAP の page は戻るか。App. A の末尾は今も IRQ_GLOBAL・WDT_EN を戻さない順で、「fence を error で完了（§10）」と言うが §10 に無い。 | §5.2 552〜562 行、App. A 2459〜2463 行。回答 N08 |
| N09 | H | 一部・食い違い | 本文は RUN=0 で止めて LIMIT のすぐ後に続く連続の page を足す手順、回答は「動作中いつでも LIMIT を書ける」。どちらか。続く page が取れない時（heap の移動・別の範囲の追加）は。RUN=0 の後 RPTR と session は保たれるか。失敗した VM_MAP の後ろの packet は実行済みか捨てられたか。止まった session（SESSION_ACTIVE=0）へ VM_MAP を出し直してよいか。CP_HEAP は 32 bit の PA: 4 GiB より下に限るか、discrete で local に置けるか。 | §11.2 1486〜1506 行、§3.3 361〜372 行。回答 N09 |
| N10 | H | 未解決 | （回答の N10 は viewport の別の質問）map 済みの範囲への VM_MAP、部分の UNMAP、未 map の UNMAP、開いた session への SESSION_OPEN、root の確保の失敗、閉じた session への VM_MAP は何になるか（kind、止まる session）。KMD の packet の fault は session 0 の fault（device 全体）か。CAP_PROT=0 の PROTECTED は §4.1 で VK_ERROR_FEATURE_NOT_PRESENT、§11.6 で kind 2: どちらか。 | §4.1 509〜512 行、§7.3 947〜979 行、§11.6 1660〜1680 行 |
| N11 | H | 未解決 | （回答の N11 は HOST_LIMIT の別の質問）fence_addr は §6.0 で host だけ、§7.3・§9.1 で「または local DRAM」。WAIT_FENCE は「host/GPU address」。CMD_FENCE の flags は §7.3 で [1] writeback、§9.1 で [1] MEM_WRITE・[2] HOST_FLUSH。どれが正か。 | §6.0 605 行、§7.3 895〜911 行、§9.1 1378〜1379 行 |
| N12 | H | 一部 | HOST_LIMIT は PA の振り分けの境界と決まった。残り: HOST_LIMIT・LOCAL_BASE・CP_HEAP は 32 bit、PA_BITS は 40 以上: 4 GiB より上の host の memory・local はどう表すか。VM_FLAG_LOCAL の phys は「LOCAL_BASE + offset」の offset か絶対の PA か。FB_BASE・FB_NEXT の register は 1 語（32 bit）だが §6.0 は 64 bit の PA と言う。 | §11.2 1484 行、§11.3 1596〜1600 行、§3.2.2 282 行、§13.2 2275〜2276 行、§6.0 608 行。回答 N11 |
| N13 | H | 未解決 | （回答の N13 は format 表）image の配置: texture の flags[0] Tiled の tile の形、mip level 1 以降の位置、array・3D の layer との順、D24S8・D32S8 の stencil の置き場。buffer→image の command（COPY_IMAGE は image→image）。COPY_IMAGE（8 dw）・BLIT・RESOLVE_IMAGE の payload、COPY_BUFFER の flags と align。 | §8.3 1110〜1126 行、§8.5.2 1313〜1326 行、§7.2 0x40〜0x44、§7.3 926〜945 行 |
| N14 | H | 一部 | dual-source の export と、enum の値が Vulkan と同じことは決まった。残り: PSO に stencil の fail/pass/depthFail op・front/back の別・reference、attachment ごとの blend と write mask、logic op の種類、MSAA（sample 数・mask・alpha to coverage）、line width、TRIANGLE_FAN・adjacency・patch が無い（§8.4 は VkLogicOp・VkStencilOp が PSO にあると言う）。vertex の stride は PSO で 8 bit×binding 0〜3、§8.6 で 12 bit・4095、BIND_VERTEX_BUFFERS で 32 bit: どれが効くか。 | §8.2 1062〜1083 行、§8.4 1252〜1254 行、§8.6 1355 行、§7.3 751〜760 行。回答 N14 |
| N15 | H | 未解決 | （回答の N15 は timestamp）§11.5 は今も DRAW・LOAD_PROGRAM・INVALIDATE（1.0 に無い）で cache を無効にすると言う。host が code・PSO・descriptor を書き換えた時、何が I cache・texture・data cache・CP の PSO の cache を無効にするか（BARRIER の INVAL_L1 だけか）。 | §11.5 1621〜1632 行、§7.3 872〜882 行 |
| N16 | H | 未解決（本文の中で矛盾） | §3.2.1 は「ISA_ID は VEND+0x10」と言うが、§4.1 で VEND+0x10 は GCLK_HZ。§11.5.1 は今も CAPS2[15:8]〜[31:28]、§12 は CAPS[19]・[24]〜[30]、§16 は CAPS・CAPS2 を挙げる。今はどの register のどの bit・field か。 | §3.2.1 271 行、§4.1 480 行、§11.5.1 1642〜1650 行、§12.1 1690 行、§12.5 1829〜1841 行、§12.9、§16 2437〜2438 行 |
| N17 | M | 一部 | occlusion の結果（va + index×8 に 64 bit）は決まった。残り: query_pool_va は lo の 32 bit だけ。availability・precise の扱い。8 dw の DRAW_INDIRECT（count buffer）に flags の語が無い: indexed をどう示すか。indirect の dispatch の command が無い。 | §7.3 825〜844 行・999〜1004 行。回答 N15 |
| N18 | M | 未解決 | （回答の N18 は caps の優先）CAP_PROT=0・CAP_HW_WALKER=0 の時、VM_MAP（guard は CAP_HW_WALKER）・SESSION_OPEN（Mandatory）は何をするか、GPU の番地は flat の物理か。walker があり PROT が無い GPU は session ごとの VA か。VM_MAP の COHERENT・WC と §11.4 の関係。 | §7.2 0x10〜0x14、§11.6 1660〜1664 行、§11.4 |
| N19 | M | 一部・食い違い | EXT_CAP_FMT0 の bit は本文で [6:0] BC・[7] ETC2・[8] ASTC（予約）・[9] sRGB・[10]〜[11] FFILT、回答で sRGB 16・FP16 17・FP32 18・ASTC あり: どちらか。本文 302〜305 行に [31:13] と [31:19] の予約と maxColorAttachments の行が重複。maxColorAttachments は §3.2.2 で MAX_MRT、§8.6 で FMT_LIMITS[3:0]。FMT_OPTIMAL の bit に DEPTH_STENCIL_ATTACHMENT・BLEND・VERTEX_BUFFER が無く、§4.1 は COLOR_ATTACHMENT_BIT を bit 7 と言う。 | §3.2.2 291〜305 行、§4.1 486〜490 行・505 行、§8.6 1356 行。回答 N19 |
| N21 | M | 一部・食い違い | §3.4 は [0] FENCE・[1] FAULT・[2] WATCHDOG・[3] DISPLAY・[5] SOFT になった。回答は [1] RING_EMPTY・[2] RING_FAULT・[3] DISP_VSYNC・[4] WDT_TIMEOUT: 本文を正とするか。§6.2 の framing の誤りは今も「`GHCI_FAULT_BAD_HEADER`, kind 2」（§3.4 は kind 3）。§9.2 は IRQ_DISP と DISP2 STATUS 0x214 で、§13.2 の DISP2 の事象の register と合うか。 | §3.4 384〜404 行、§6.2 653 行、§9.2 1385 行、§13.2 2274 行。回答 N21 |
| N23 | M | 未解決・食い違い | texture の format は「Format code (RGBA8 …)」、render pass は「VkFormat (1..109) / 内部の ID (0x01..0x0E)」: 1〜14 は両方に重なる。GPU はどちらを解釈するか（D32 などは 109 を超える）。sampler の compare_op・filter の値、border は RGBA8 だけ（整数・float の border）。回答の field（max_anisotropy・border_color_idx）は本文の §8.3 に無い。 | §8.3 1110〜1144 行、§8.4 1179 行。回答 N23 |
| N24 | M | 一部 | `ld.64`・`sd.64` の encoding が入った。残り: LDS は §8.6 で 16384（KC705 は 4096、Vulkan 1.0 の最小は 16384）、回答の CAP_LDS_SIZE は本文に無い: KC705 で compute を出すか、値をどこで読むか。MSAA の 2x（§8.5.2・§8.6）と §12.11（1 か 4）。§8.6 の maxStorageBufferRange は「32-bit GPU VA」。 | §8.6 1328〜1364 行、§8.5.2 1325 行、§12.11 2065 行。回答 N24 |
| N25 | M | 一部・食い違い | FLIP の base は 64 bit になった。本文は dw1 plane・dw4 value・header の flags[0]、回答は dw1 display_id・dw4 flags（vsync・take）: どちらか。FLIP から record の番地が消えたが、§13.2 は今も「flip の record の番地に {value, FRAME_COUNT} を書く」: record は無くなったか。mode は MODE の表だけか（EDID を読む経路）。 | §7.3 1013〜1019 行、§13.2 2325〜2334 行。回答 N25 |
| N26 | M | 一部・食い違い | reset の値 0x10000000 と、command の実行中だけ数えることは決まった。残り: 数えるのは packet ごと（packet の始めで 0 に戻るか）か累計か。WAIT_FENCE・FLIP の待ちは数えるか。満了の「halts」は device の FAULTED か session の fault か、IRQ_FAULT も立つか。`wdt_hold` は本文で bus の待ち、回答で CTRL.HALT: どちらか。 | §3.1 224 行、§10 1404〜1411 行。回答 N26 |
| N27 | L | 未解決 | VM_UNMAP は「TLB の無効化の後で RPTR が進む」（§11.2）、§6.4 は「引数を写した時に進む」。回答の「UNMAP の後の FENCE で page を再利用」は本文に無い。 | §6.4 663〜670 行、§11.2 1533〜1538 行。回答 N27 |
| N28 | L | 一部 | job tag・BUFFER_GRANT・§3.4 の重複・BEGIN_RENDER_PASS の opcode は直った。残り: §7.3 の「0x31 / 0x30」（0x30 は DISPATCH）・「0x34 / 0x37」（0x37 は END_QUERY）。§4.1 の固定 offset「0x100 MEM」（MEM は 0x1E0）。§13.2・§12.11 の WAIT_IDLE・RESULT。§15.2 の MSI の条件に IRQ_GLOBAL が無い。§15.1 の 64 KiB と BAR0 128 KiB。GCLK_HZ が 0x064 と VEND+0x10。§12.11・§12.12 は draft。 | §7.3 872・905 行、§4.1 493 行、§12.11 2138 行、§13.2 2236 行、§15 2398・2423〜2425 行。回答 N28 |
| N30 | L | 新規 | timestampPeriod は §8.6 で 10.0 ns（100 MHz）の固定、§3.2.3 で 1e9/GCLK_HZ、§3.1 の WDT の注は 200 MHz。§8.6 の値は例か。 | §8.6 1364 行、§3.2.3 309 行、§3.1 224 行 |

## 解決した N

N05（session の fault の後の扱い、§3.3・§10）・N20（shader の fault の kind 1/9/10 と stage、§3.5・§12.6）・N22（graphics と compute の bind point、§7.3）・N29（UMA の boot の framebuffer、§13.3）。

DONE
