# GHCI レビュー 第 5 版（ghci.md 7153329 に対する疑問 N01〜N33、回答 第 4 版を反映、2026-10-28 02:13）

- 対象: `git -C /home/awe/amaterrace-gpu show 7153329:docs/ghci.md`（2635 行）と同じ commit の `include/ghci_cmd.h`（692 行）。行の番号はこの版の物。
- 読んだ回答: `docs/ghci-review-reply.md`（commit 85b142e）、1 行目「# GHCI Review Reply - Version 4.0 (2026-10-27) - Commit: 7153329 (Target: N01〜N32)」、255 行、md5 41210ffe34169e56cd562d37bb87ae3f、末尾 DONE。読み始め（02:09）と読み終わり（02:13）で同じ。
- 回答の番号は、こちらの N の番号と合っている。

## 要約

- 判定（N01〜N32 の残り 22）: **今回の解決 12**（N01・N04・N07・N08・N09・N10・N11・N17・N18・N24・N28・N31）、一部 10。
  回答と本文の食い違い 7（回答が「直した」と言う箇所が本文に残る: N12 §11.2 の flag の表、N14・N23 の `state_id`、N16 の MEM_CACHE_CFG、N21 §9.2、N25 §13.2 の record、N32 の BEGIN_RENDER_PASS の flags）。
  改訂で新しく生じた疑問 1（N33）。
- 残る疑問 **11 件: C 0・H 3・M 6・L 2**。**C は残らない**。N02 は C から H に下げた（set の基の番地・binding の offset・dynamic offset が本文で決まり、buffer の読み書きは仮の扱いで書ける。残りは constant bank の重なりと texture の slot の引き方）。
- **driver の実装**: KMD の基盤（ring・fence・fault・session・VM_MAP・CP_HEAP）は、今回で疑問が無くなった。PSO・render pass も仮の扱いで書ける。
  答え待ちで仮の扱いになるのは、descriptor を使う shader（N02）、blit・copy の image の command（N13）、dynamic state（N14）。
- 前回と同じく、回答が挙げた本文の行に無い事は解決に数えていない。

## 残る疑問

重さ: C＝その部分の driver が書けない、H＝その部分を書く前に答えが要る、M＝仮の扱いで進められるが答えが要る、L＝文書の整理。

| ID | 重さ | 判定 | 疑問 | 根拠（ghci.md 7153329・回答 第 4 版） |
| --- | --- | --- | --- | --- |
| N02 | H | 一部（本文の中で矛盾） | 式・`binding_offset_bytes`（UMD が決め命令の即値に入れる）・set の基（c24〜c31）・dynamic offset（shader が足す）は決まった。残り: push constant は 128 byte で c0〜c31 に置き（offset 0 から）、dynamic offset も push constant で渡す。一方 CP は set 0〜3 の基を c24〜c31 に写す。両者は c24〜c31 で重なる: push constant が 96 byte を超える時・LOAD_CONST が c24〜c31 を書いた時、どちらが残るか。`texl` の slot k の descriptor の番地は `set0_va + k × 32`（sampler は `set1_va + k × 16`）で `binding_offset_bytes` を使わないのか。combined の index の組は「register に読んで `texl` を出す」とあるが、§12.9 の `texl rd, rs1, rs2` に組を受ける operand は無く、§12.10.2 は `texl rd, rCoord, slot` と書く: 組はどの operand・どの bit で渡すか。 | §8.4 1279〜1294 行・1376 行・1383 行、§12.9 2114〜2122 行、§12.10.2 2225〜2230 行、§12.3 の表（c0〜c31）。回答 N02 |
| N13 | H | 一部 | tile の並び（行の順）・tile の行の pitch・圧縮 format の tile・tile での mip の置き方・COPY_BUFFER の flags と整列・各 packet の dword の配置は本文に入った。残り: COPY_IMAGE・BLIT・RESOLVE_IMAGE・COPY_BUFFER_TO_IMAGE・COPY_IMAGE_TO_BUFFER は番地・xy・extent だけで、format（texel の byte 数）・image の pitch・linear か tiled か・mip・layer・3D の z・sample の数が無い: blitter は image の layout をどこから知るか。BLIT の「format の変換」の src・dst の format はどこか。descriptor の `row_pitch_bytes` は level 0 だけの値か（§8.5.2 は mip を「64 byte の行の整列で詰める」と言い、hardware が mip ごとの pitch を計算するように読める）。tiled の時 `row_pitch_bytes` は tile の行の pitch か。 | §7.3 976〜1040 行、§8.3 1230〜1235 行、§8.5.2 1460〜1468 行。回答 N13 |
| N14 | H | 一部・食い違い | stencil の front/back の op・logic op・alpha to coverage・sample mask・line width・stride の優先は PSO に入った。残り: 本文は stencil の compare/write mask・reference、attachment ごとの blend（`state_id` 7〜14）、border の表（`state_id` 0x10）を CMD_SET_DYNAMIC_STATE で渡すと言うが、§7.3 の CMD_SET_DYNAMIC_STATE は 6 dw で `state_mask` の [0] depth bias・[1] blend constants・[2] stencil ref だけ、`state_id` の field も 64 bit の番地を入れる場所も無い（header に struct も無い）。stencil の mask は dw10 から消えたので、今はどこにも置けない。これらの packet の形は。stride: Vulkan では stride 0 は「全部の vertex が同じ要素を読む」の意味で正しい値だが、本文は stride 0 を「PSO の既定値を使う」とし、既定値は binding 0〜3 だけ: binding 4〜15 の stride 0、Vulkan の stride 0 はどう出すか。 | §7.3 816〜824 行、§8.2 1179 行・1195 行・1203 行、§8.3 1258 行、§8.6 1497 行。回答 N14・N23 |
| N12 | M | 一部・食い違い | §11.3 の HOST_LIMIT（byte の番地の閾値）と VM_FLAG_LOCAL の phys（絶対の PA）は直った。残り: §11.2 の flag の表は今も「`LOCAL_BASE + offset`」（回答は「完全に排除」と言う）。回答は「GPU の物理の bus は 32 bit、4 GiB より上は IOMMU の IOVA で」と言うが、本文の VM_MAP の phys は 64 bit、PTE の PPN は 48 bit の host PA、EXT_CAP_FEATURE の PA_BITS は 40〜48 bit: host の VM_MAP の phys は HOST_LIMIT（32 bit）より下に限られるか、PA_BITS まで使えるか。CMD_FLIP の base（64 bit）と FB_BASE・FB_NEXT（32 bit）: base_hi は 0 でなければならないか。 | §11.2 1639 行・1681 行、§11.3 1750 行、§3.2.2 281 行、§6.0 622 行、§7.3 1116〜1117 行。回答 N12 |
| N16 | M | 一部・食い違い | 0x048 は WDT_CYCLES、NATIVE_ISA_ID は 0x068、GCLK_HZ は 0x064、CAPS2 は削除、§12 の CAPS と MMIO の対応は §12.1 に入った。残り: §11.5.1 の L2 の容量の register `MEM + 0x14 MEM_CACHE_CFG` は、§11.3 の MEM の表（+0x0〜+0x10）と §4.1 の MEM の行に無い（回答は「MEM 拡張に配置」と言う）: bit の配置は。§12.1 は .3 の形（2D array・3D・cube）・depth compare・gather を `DEVICE_CAPS[25]` CAP_TEX_FFILT（float の filter）で決めるとする: float の filter の無い GPU（CONFIG_TU_ENABLE_FFILT = 0）は 2D array・cube・3D を sample できない、で正しいか。 | §11.5.1 1798 行、§11.3 1745〜1751 行、§4.1 484 行、§12.1 1875 行、§12.9 2196〜2199 行。回答 N16 |
| N23 | M | 一部・食い違い | texture の format の VkFormat・compare_op の値は直った。残り: border の 16 個の表は「CMD_SET_DYNAMIC_STATE の `state_id = 0x10`」で渡すとあるが、その packet に `state_id` も番地の field も無い（N14）。表はどの packet・register で渡すか、表は session の GPU VA か。 | §8.3 1258 行、§7.3 816〜824 行。回答 N23 |
| N25 | M | 一部・食い違い | FLIP は 8 dw（plane の選択・value・64 bit の record の番地）、DDC は DISP2 の I2C（0x50）と決まった。残り: §13.2 は今も record を「{value, FRAME_COUNT}、2 word」「record の番地が 0 なら書かない」と言い、§7.3 の 16 byte の {value, frame_count, timestamp_lo, timestamp_hi} と `RECORD_ENABLE` と合わない。record_pa は「GPU physical address」: host の DMA の番地（HOST_LIMIT の下）か。IMMEDIATE は §13.2 の「plane 0 は次の vsync で取る（ENABLE が 0 の時だけ直ちに）」と合わない: VSYNC_WAIT と IMMEDIATE がともに 0・ともに 1 の時は。`display_id` 1 の controller はどこにあるか（DISP2 の window は 0x200 の一つ）。 | §7.3 1112〜1128 行、§13.2 2488〜2501 行、§4.1 485 行。回答 N25 |
| N32 | M | 一部・食い違い | ghci.md が正本、BARRIER・FENCE の flags、GhciCmdQuery の 4 dw は header と合った。残り: BEGIN_RENDER_PASS の dw8 は本文で [7:0] CLEAR_COLOR・[8] CLEAR_DEPTH・[9] CLEAR_STENCIL・[16] INLINE_SECONDARY、header（573 行）は「[0] Inline secondary buffers」。PSO の dw8 [11] sample shading が header（182 行）に無い。こちらは本文を正として読むが、header を直すか。 | §7.3 884〜888 行、§8.2 1175 行、ghci_cmd.h 182 行・573 行。回答 N32 |
| N33 | M | 新規（改訂で生じた矛盾） | PSO の dw8 に alpha to coverage・sample shading・sample mask が入ったが、§12.11 は今も「fragment program は pixel に 1 回（sample shading 無し）」と言い、MS_CTRL（0x05E、alpha to coverage・centroid）・SAMPLE_KILL（0x05F）を SET_REG の state word として書く。§12.9・§12.11 は今も TEX_IMAGE[3:0]・[15:12]・format 4・TEX_SAMPLER[9]・[12:10] を使い、§8.3 の descriptor のどの field かが書かれていない。PSO と state word のどちらが効くか。sample shading は使えるか。TEX_IMAGE・TEX_SAMPLER の各 field は descriptor のどの field か（例: TEX_SAMPLER[9] は `compare_enable`、[12:10] は `compare_op` か）。 | §8.2 1175 行、§12.11 2242〜2255 行・2302〜2308 行、§12.9 2133〜2155 行 |
| N03 | L | 一部 | baseArrayLayer、resolve（attachment 0 は自動、1〜7 は RESOLVE_IMAGE）、表が 0 の時の値は本文に入った。残り: 表が 0 の時、attachment 0 は「全部 0」と「`color_clear_rgba` を使える」の二通りが書かれる: どちらになるかを何で決めるか。`color_clear_rgba` の「RGBA8 または float」は format で決まるか。 | §8.4 1351 行、§7.3 881 行。回答 N03 |
| N21 | L | 一部・食い違い | DISP2 の DISP_IRQ_STATUS（+0x18）・DISP_IRQ_ENABLE（+0x1C）が入り、bit 9〜12 は消えた。残り: §9.2 は今も「詳しくは DISP2 の STATUS（0x214）」。§13.2 は今も「IRQ_STATUS.FLIP」と、事象を VBLANK・DISP_ERROR・HOTPLUG・FLIP と呼び、DISP_IRQ_STATUS の bit（VSYNC・FLIP_DONE・CURSOR_FLIP_DONE・HOTPLUG・UNDERFLOW）と名前が違う。DISP_ERROR（read error・record error・BAD_WRITE）に当たる bit が DISP_IRQ_STATUS に無い。 | §9.2 1528 行、§13.2 2501 行・2509 行、§3.4 405 行。回答 N21 |

## 解決した N

N01（used_regs は 1〜63、mesh は used_regs_cs、local_size の上限、§8.2・§12.10.1）・N04（kind 6 と kind 1 の別、§6.0）・N05・N06・N07（session 0 の root は reset 後の最初の RUN だけ、reset 後に KMD が session 0 を map し直す、§11.2・§5.2）・N08（fence は常に target_seq を書き、0xFFFFFFFF は廃止、error は FAULT_INFO から、§10・§5.2・App. A）・N09（CP_HEAP は連続の拡張だけ、TLB_FLUSH の手順は削除、session 0 の OOM は FAULTED、§11.2）・N10（client の session への管理の packet の誤りは S だけが fault、§11.6）・N11（FENCE の flags が §7.3・§9.1・header で一致、MEM_WRITE=0 は IRQ だけ）・N15・N17（availability の配置、§7.3）・N18（CAP_HW_WALKER=0・CAP_PROT=0 の動作、§11.6）・N19・N20・N22・N24（KC705 は scratch への spill か compute を出さない、§8.6）・N26・N27・N28（§12.11 の resolve と同期）・N29・N30・N31（EXT_CAP_FEATURE の [19:16] MAX_MRT・[27:20] LDS、§3.2.2）。

DONE
