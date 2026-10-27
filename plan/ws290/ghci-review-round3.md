# GHCI の三次回答（H01〜H23）のレビュー（2026-10-27 P1）

- 対象: hardware の担当の三次回答 [ghci-answers-hw-round3.md](ghci-answers-hw-round3.md) と、amaterrace-gpu の commit **7fc707d** の `docs/ghci.md`（2281 行）・`include/ghci_cmd.h`・`ghci_regs.h`・`ghci_frame.h`・`rtl/gpu/cp/atg_cp_regs.v`（と判定に要った `atg_cp_ring.v`・`atg_cp_ctl.v`）。`git show` で読んだだけ（build・試験は無し）。
- 質問: [ghci-questions-round2.md](ghci-questions-round2.md)（H01〜H23）、[ghci-questions.md](ghci-questions.md)（G02〜G49）、[design-review-v3.md](design-review-v3.md) の V07。一次回答 [ghci-answers-hw-20261027.md](ghci-answers-hw-20261027.md)。
- 依頼: Queue q1622-i01（2026-10-27 ユーザー「下記の通りだそうです。疑問が解決するかレビューしてください。P1でやりましょう。」）。source と design.md は変えない。
- 根拠の書き方: 「md N」= 7fc707d の ghci.md の N 行、「regs.h N」= `include/ghci_regs.h` の N 行、「cmd.h N」= `include/ghci_cmd.h` の N 行、「regs.v N」= `rtl/gpu/cp/atg_cp_regs.v` の N 行。Vulkan 1.0 の必須の値・format は repo の外の知識で、**未確認**と書く。
- 判定の語: **解決** = 回答と本文が合い driver が書ける、**一部** = 合うが driver に要る物が残る、**未解決** = 回答にも本文にも無い、**食い違い** = 回答と本文・header・RTL（またはそれら同士）が問いの点で合わない。

## 要約

| 範囲 | 件数 | 解決 | 一部 | 未解決 | 食い違い |
| --- | --- | --- | --- | --- | --- |
| H01〜H23 | 23 | 0 | 7 | 0 | 16 |
| H に入っていない G02〜G49 と V07 | 26 | 5 | 10 | 3 | 8 |
| 計 | 49 | 5 | 17 | 3 | 24 |

- 回答の文はほぼ質問に答えているが、**7fc707d の本文・header・RTL がその回答に追いついていない所が多い**（PSO の新しい配置、CMD_FENCE の 64 bit、DRAW_INDIRECT の配置、0x044 の register、CP_HEAP の register、VEND の位置、EXT_CAP_FEATURE の bit）。
- 1.1 の記述（§3.1.1 の 128 KiB の窓、§7.5 の gpu-op、§11.6 の APERTURE・KICK、§12 の CAPS[n]、`ghci_regs.h` の 1.1 の offset と CAPS）が残り、同じ名前の macro が `ghci_regs.h` の中で違う値に定義されている（C では再定義の誤り）。
- driver がすぐ書けるのは G02（窓の大きさ）・G03（pointer の単位）・G05（VM_MAP の opcode）・G13（CP の状態）・G34（IRQ の保持）だけ。H04（BIND_SESSION に一本化）も核心は決まったが session 0 の扱いが本文に無い。
- 次の疑問は **J01〜J27 の 27 件**（下の表）。

## hardware の担当へ送る次の疑問

| ID | 元 | 疑問 | 根拠 |
| --- | --- | --- | --- |
| J01 | G01・H10 | 次の 1.1 の記述は GHCI 1.0 の一部か、消す物か。RTL の `SLIM_MMIO = 0`（1.1 の窓）の構成は GHCI 1.0 の device として在り得るか。 | md 208〜251（§3.1.1「physical hardware decodes」）、md 750〜773（§7.5 gpu-op・INIT）、md 1297〜1426（§11.6 の APERTURE・gpu-op・KICK・HEAP）、md 1255〜1259（DRAW・LOAD_PROGRAM・INVALIDATE）、md 2051（FENCE・WAIT_IDLE・RESULT・FLIP COMPLETE）、md 2244（aperture）、md 2279（0x074〜0x07C）、md 128〜130（§1.4）、md 514（PSO 48 bytes）、regs.h 179〜534、`include/ghci2_regs.h`、cmd.h 76・`ghci_frame.h` 43〜44（0x10110） |
| J02 | H02・H12・G10・G12 | CTRL・STATUS・IRQ_STATUS の bit はどれが正か。RTL の slim はどの layout を返すか。 | regs.h 104・105 と 313・312（`GHCI_CTRL_HALT`・`GHCI_CTRL_RESET` が 1<<1 と 1<<2 で逆）、regs.h 106〜107（IRQ_GLOBAL・TLB_FLUSH が md 313〜314 の STEP・WDT_EN と同じ bit）、regs.h 114・116 と 322・324（SU と ROP が 4 と 6 で逆、md 322・324 は SU 4・ROP 6）、regs.h 140 と 346（`GHCI_IRQ_WATCHDOG` が 1<<2 と 1<<3）、md 354〜355（[2] WATCHDOG・[3] DISPLAY）と md 228（1.1: [2] RING_EMPTY・[3] WATCHDOG）、regs.v 254〜256・336〜339（slim は 1.1 の index へ送る） |
| J03 | H09・H10・G14 | FAULT_INFO の kind と unit の番号の正はどれか。SYNDROME はどの bit か。kind 2・7 の今の意味。不正 opcode と program の header の不正をどう分けるか。 | kind が 3 通り: md 378〜387（8 が無い、2 は KICK、7 は Aperture）、regs.h 150〜160（8 = MMU_OOM）、regs.h 364〜373 と RTL（atg_cp_ctl.v 206〜212、atg_cp_ring.v 723・788: APERTURE 1・OPCODE 2・RING 3・AXI 5・KICK 6・ISA 8）。md 1591（ISA fault は kind 8）、md 549（ring の誤りは kind 3）。unit: md 369〜376（BLT 1・VF 2・SU 4）と regs.h 166〜172（VF 1・SU 2・BLT 6）。CLIENT_ID [7:0] と SOURCE_BLOCK [23:16] の違い。regs.h 164 の SYNDROME は [31:16] で BLOCK・SESSION と重なり、md に SYNDROME の field が無い |
| J04 | H05・H12・G16 | 1.0 の register の全表（offset・名前・access）はどれが正か。WDT_CYCLES・IRQ_SET・RESIDENT_WARPS・CP_HEAP_BASE/LIMIT・RING_BASE_HI を RTL は受けるか。CP_HEAP_LIMIT は含むか含まないか。 | md 204〜206（0x040 DIAG_CP_PC、0x044 DIAG_CP_STATUS）と H12 の回答（0x040 IRQ_SET、0x044 WDT_CYCLES、0x04C RESIDENT_WARPS/DIAG_CP_STATUS）、md 314・348・395、regs.h 63〜67。CP_HEAP（md 1138〜1139）は §3.1 の表・regs.h に無く、md 175 の範囲（〜0x0AC）の外。regs.v 264〜269・283〜285（slim の書き込みは CTRL・IRQ_STATUS・IRQ_ENABLE・RING_BASE_LO・RING_SIZE・RING_WPTR・SCRATCH だけ）、regs.v 441（RING_BASE_HI は 0）、regs.v 444（0x044 は DIAG_CP_STATUS） |
| J05 | G03 | RING_SIZE は byte の数か log2 か。RING_BASE の揃えは 64 byte か 32 byte か。 | md 198（byte、2 の冪、4 KiB〜16 MiB）、RTL は 5 bit の log2（atg_cp_ring.v 187・197・841）、md 234（1.1: log2 10..20）。md 196（64 byte）と md 233（32 byte） |
| J06 | H02・H03・H06・G41・G42・G47 | 1.0 に CAPS・CAPS2 は在るか（在るなら offset、field の幅）。EXT_CAP_FEATURE（0x084）と EXT_CAP_FMT0（0x080）の bit の正。0x080・0x084 は §3.1 の表の register か、FMT の「近道」か。 | H02「全部 DEVICE_CAPS」、しかし md 295〜304（CAPS2 を offset 無しで、LDS 16・L2_WAYS 16・TU_L1 16 が 4 bit に入らない）、md 2255・2266（CAPS・CAPS2 を読む）、H06 の DEVICE_CAPS2 は無い。FEATURE: md 288〜293（SUBGROUP・SA・TILED・STENCIL・[7:4] MAX_SESSIONS）と regs.h 294〜301・regs.v 171〜174（LOGIC_OP・EARLY_Z・HW_CURSOR・COLOR_LUT・[7:4] MAX_MRT・DUAL_SOURCE・COARSE_SCAN・FAST_CLEAR）。FMT0: regs.h 286〜292（BC1〜BC7 の bitmap、BC6H 有り）、md に記述無し。regs.h 304〜308 の CAPS2 の macro は md 297〜304 と別の配置 |
| J07 | H06・G32 | session の数はどこで読むか、上限と id の幅。CAP_PROT = 1・CAP_HW_WALKER = 0 の時の SESSION_OPEN の root_pt、両方 0 の時の CMD_BIND_SESSION（Mandatory）の扱い。 | md 292（MAX_SESSIONS は 4 bit で「既定 16」が入らない）、md 128（KC705 は 31 sessions）、cmd.h 413〜418（0..15）、md 1163（PTE の tag 6 bit）、md 389（FAULT_INFO 8 bit）、md 598〜600 |
| J08 | H02・H17・V07 | RTL の DEVICE_CAPS で BLT・DISP・MSAA・PROT・TEX_FFILT・ROP_PACK の bit は立つか。CAPS が既定値でない時に CAPS（1.1 の layout）を返す分岐は残るか。 | regs.v 149〜152（`ATG_DEF_DEVICE_CAPS` は [29:24] を 0、NUM_SU を 1 に固定、[30]・[31] は同じ MESH）、regs.v 426〜427（V07 の分岐が 7fc707d でも残る） |
| J09 | H03・H13・G17・G43 | 各 capability の register の offset の正。slim で MEM・FMT・PROT・DISP2・VEND の register は読めるか。driver は EXTCAP の word 1 の offset だけを信じればよいか。Vulkan の vendorID は VEND の FourCC か PCI の ID か。 | md 427（list は 0x100〜0x10F と 0x180〜0x1BF）と md 431（VEND 0x180〜0x19F）が重なる。regs.v 384〜394（VEND 0x0C0、SA 0x140、FMT 0x160）と md 430・H03（FMT 0x140）。regs.v 365〜367・448（slim は 0x100〜0x3FF を list の語だけで答える）、MEM（regs.v 469〜473）・VEND（regs.v 479）は slim でない枝にだけ在る（推測: slim では 0 が読める） |
| J10 | H04・H07・G04・G07・G09 | command の 64 bit の VA の上位 32 bit は 0 必須か。session 0 の address は何か（bus address か、LOCAL_BASE を含む 32 bit の GPU 物理か）、session 0 に VM_MAP できるか。fence_addr・WAIT_FENCE・WRITE_TIMESTAMP の address は bus address か session の VA か。ring は 4 GiB より上に置けるか。 | md 1148（VA 32 bit）、md 639〜640 など（VA 64 bit）、H04「session 0 は flat の物理」は本文に無い（md 1312 は PROT の文脈だけ）、一次 G09「HW_WALKER = 0 は flat」も本文に無い、cmd.h 480〜493（「GPU VA or host memory address」）、md 196〜197（ring 64 bit）と regs.v 441（RING_BASE_HI は 0） |
| J11 | H08・G31 | walker は PTE のどこで LOCAL と host を分けるか。host の bus address が [LOCAL_BASE, LOCAL_BASE + LOCAL_SIZE) の中や HOST_LIMIT 以上に在る時はどうなるか（driver が避ける範囲か）。 | md 1133・cmd.h 270 の `GHCI_VM_FLAG_LOCAL`、md 1155〜1163（PTE に LOCAL の bit が無い）、md 1231（HOST_LIMIT は address の範囲で host を決める） |
| J12 | H05・G08・G35 | root の page directory の memory はどこか（host memory か local か、0 埋めが要るか、SESSION_CLOSE の後に再利用してよい時点）。`root_pt_phys = 0` は許されるか。UMA で heap に使う address の種類。reset の後に session・VM_MAP・page table は残るか。2 MiB で 2 GiB は全 session の合計か。OOM の VM_MAP は一部だけ map されて残るか。 | H05「root は host が確保」、cmd.h 417「0 = CP allocates」、md 1146「CP が heap に 2 段を確保」。md 488〜490（reset で保つ物に CP_HEAP・session が無い）、md 1062（CP_HEAP を restore）、md 1333（reset は全 session を失う）、md 1141〜1143 |
| J13 | H22・G11・G39 | fence の幅、memory に書く byte 数と揃え、IRQ の flag の位置、WAIT_FENCE の幅と比較の式。CMD_FENCE を単調でない値（event・query）に使ってよいか。 | H22（dw4 = seq_hi 32 bit、64 bit が必須）と md 748・cmd.h 365〜372（dw4 は seq_hi 16 bit + flags 16 bit、IRQ は flags の bit 0）、md 1025〜1028（32 bit で回り込み比較）、cmd.h 480〜485（wait_value 32 bit、int32 の比較）、一次 G11（flags[1] で 64 bit） |
| J14 | H19・G25・V12 | CMD_DRAW_INDIRECT の正の配置、count を buffer から取る時の maxDrawCount の場所、indexed の区別。CAP_INDIRECT_DRAW が言う indirect dispatch の opcode は在るか。 | H19（dw3..4 draw_count_va または maxDrawCount、dw5 [15:0] stride・[16] indexed・[17] count_from_buffer）と md 704〜713・cmd.h 310〜317（dw3 draw_count、dw4 stride、dw5 flags[0] = count from buffer）、一次 G25（flags[0] = indexed）、md 269（indirect dispatch）と §7.2 に opcode 無し |
| J15 | H18・G24・G44 | 128 byte の PSO の正の配置。属性ごとの binding・input rate（instance）、PSO の vertex_stride と CMD_BIND_VERTEX_BUFFERS の stride のどちらか。TRIANGLE_FAN は無いか。sample mask・alpha to coverage・varying の数・attachment ごとの blend と write mask・task と mesh の code_va はどこか。 | H18 の配置（attr_formats 16 byte・attr_bindings 16 byte・attr_offsets 32 byte）は md 800〜841・cmd.h 145〜199 に無い（4 bit の format、8 bit の offset は属性 0〜7 だけ）。64 byte の属性は reserved の 32 byte に入らない。md 1010〜1011（16 bit の offset・12 bit の stride と書く）、cmd.h 96（topology は 0〜4）、md 611 |
| J16 | H16・G21 | attachment（address・pitch・format・samples）はどこで指定するか。attachment ごとの clear の値（float・整数の format）、depth/stencil の format、MSAA の resolve の先。 | H16「BEGIN_RENDER_PASS で」、しかし cmd.h 453〜464 に attachment の field が無く clear は color 1 つの 32 bit、md §7.3 に payload 無し |
| J17 | H15・G20 | set の中の descriptor の index の規則（型で大きさが違う: texture 32、sampler 16、buffer 16）。combined image sampler の表し方。§12.10 の interface table（(set, binding) → bank の語）と descriptor table の関係、bank の語を誰が埋めるか。CMD_LOAD_CONST の bank の範囲と uniform の bank の語の関係。table の揃え。 | md 901（binding × descriptor_size_bytes）、H15「16 または 32」、md 1811〜1869（interface table は 1.1 の bank の形）、cmd.h 472〜477（offset 0..511） |
| J18 | H01・H17・G22・G23・G27・G49 | 次の command の payload: CMD_BIND_SESSION、BEGIN/END_QUERY（結果の address・幅・availability・reset）、WRITE_TIMESTAMP の pipeline_stage の値と counter の有効 bit、COPY_IMAGE・BLIT・RESOLVE_IMAGE、COPY_BUFFER の flags、DISP_SET、FLIP（plane・image・record）、SET_DYNAMIC_STATE の depth bias の符号、CMD_BARRIER の stage と flags の意味、DRAW_MESH_TASKS の dword 数。「Mandatory (Raster)」は何の条件か（NUM_SU = 0 の device で受けるか）。 | md §7.3（631〜748）に無い。cmd.h に struct が無い（VERTEX_BUFFERS・SET_DYNAMIC_STATE・COPY_IMAGE・BLIT・RESOLVE・QUERY・DISP_SET・FLIP）。md 611（5 dw）と cmd.h 320〜325（4 dw）。md 683（float / 24.8）。一次 G27 の `STAGE_VERTEX`・`ACCESS_*` は cmd.h 335〜345 に無い |
| J19 | H14・G18・G48 | §8.6 の値は device に依らず固定か。maxComputeSharedMemorySize 4096 で Vulkan 1.0 を満たすか。§8.6 に無い limit の値。timestampPeriod は GCLK_HZ から取るか固定か。 | md 1016（4096、Vulkan 1.0 の下限 16384 より小さい: 未確認）、md 1282（LDS は config で 0〜16 KiB）、md 1014（256 = 8 warps × 32 lanes、NWARPS・PHYS_LANES に依らず?）、md 1017（subGroupSize は 1.0 の limit に無い）。無い物: maxPerStageResources・maxDescriptorSet*・maxVertexOutputComponents（一次 G18 の varying 16 語）・maxFragmentInputComponents・maxViewports・maxViewportDimensions・maxFramebuffer*・各 sample counts・min*OffsetAlignment・bufferImageGranularity・nonCoherentAtomSize・maxDrawIndirectCount・maxDrawIndexedIndexValue・point と line の範囲・maxClipDistances |
| J20 | H23・G24 | format の正は §8.5.1 の表か FMT register か。`CAP_BC` はどの bit か。BC6H・BC の sRGB・SNORM は無いか。flag で外れる format（R16G16B16A16_SFLOAT、R32_SFLOAT、整数の color attachment）は Vulkan 1.0 の必須か（未確認）、無い device はどうするか。storage image は FMT で報告されるか。buffer の feature（uniform/storage texel、vertex）、BLIT・TRANSFER の feature。 | md 919〜967（47 行、Guard `CAP_BC` は DEVICE_CAPS に無い）、regs.h 286〜292（FMT0 に BC6H）、md 938〜946、md 441〜442（storage image は報告しない）と表の Storage Image の列、md 452（FILTER_LINEAR を「bit 1」、Vulkan は bit 12: 未確認）、md 444「A driver reports its format properties from these」 |
| J21 | H11・G15 | session だけを落とした後の STATUS.FAULTED と FAULT_INFO（次の fault を latch するか、何で消えるか）。PROT の無い device で SESSION_ID ≠ 0 の fault は装置全体の停止か。 | H11・md 1055〜1060 と md 1408〜1413（version 1 は装置全体の回復）、md 326（FAULTED は unrecoverable）、md 485（FAULTED を出るのは RESET だけ）、md 1114（offending context だけ止める）、md 366（first fault） |
| J22 | H21・G38 | COHERENT の付かない host memory の mapping は GPU の何を変えるか。coherent でない AXI の platform（UMA）で COHERENT を付けると何が起きるか。「host memory は必ず COHERENT」を本文に書くか。 | H21 と md 1238〜1240（coherence は platform の性質で GPU は知らない）、md 1131、一次 G38（L2 を同期点で無効化・bypass） |
| J23 | H20・G28・G49 | IRQ_STATUS[3] は W1C の latch か bit 9〜12 の OR の写しか。bit 9〜12 は IRQ_ENABLE で個別に許すか。FB_NEXT・CURSOR・FLIP の image の address は GPU 物理（32 bit）か session の VA か。UMA で 4 GiB より上の host memory を scanout できるか。 | H20（OR）は md 355 に無い。md 2119〜2121（local memory が有れば local）、md 2090〜2110、md 628〜629（DISP_SET・FLIP の payload 無し） |
| J24 | G18・G19・G45・G46・G48 | §12（GSI1）の各機能（subgroup・packed・3D/cube/compare/gather/.3/texqlod・multisample・CORE）を 1.0 のどの bit で見るか。program の渡し方は PSO の `*_code_va` だけで、PROGRAM gpu-op は消えるか。SFU・registers per lane・NWARPS・TRILINEAR・3D_CUBE・MSAA の sample 数をどこで読むか。CAP_MSAA は 2x/4x か 4x か。 | md 1438・1448・1575・1585〜1587・1684・1702〜1767・1827・1879・1959（1.1 の CAPS[n]: 1.0 の DEVICE_CAPS では 24 = PROT、26 = ROP_PACK、28 = DISP、30 = TASK）、md 1771（PROGRAM gpu-op）、regs.h 197（RESIDENT_WARPS は 1.1 の 0x04C）、md 281（2x/4x）と regs.h 94（4x） |
| J25 | G09・G26・G33・G36・G37・G39 | 一次回答の次の点は本文（§5・§7.3・§9.1・§11.2）に書くか、一次回答の通りでよいか: HW_WALKER = 0 は flat 物理、secondary は 1 階層で VM_*・SESSION_* を禁止、IDLE で受ける書き込み、VM_MAP の 4 KiB 揃え・二重 map の fault（kind は何か）・部分 unmap・size 0、bind の状態は session ごとで BIND_SESSION と reset で消える、FENCE の値は任意。 | md 481（「etc.」のまま）、md 731〜739、md 1116〜1170、md 1025、本文に該当の文が無い |
| J26 | G27 | host が書き直した memory（vertex・texture・program・PSO・descriptor）を読む前に driver が出す CMD_BARRIER の flags は何か。BIND_PIPELINE は instruction cache を無効化するか。 | md 1255〜1259（1.1 の DRAW・LOAD_PROGRAM・INVALIDATE が暗黙に無効化）と一次 G27（暗黙の無効化は無い）、cmd.h 335〜352 |
| J27 | H07・G09 | CAP_IOMMU = 1 の時 driver は IOMMU を使わねばならないか。0 の時に OS の IOMMU の IOVA を渡してよいか。device は OS が IOMMU を有効にしたかを知り得るか。 | md 267（「supported and active」）、H07（IOMMU の有無で 4 GiB の扱いを分ける） |

## 回答と本文の新しい食い違い（依頼の例の照合）

| 点 | 結果 | J |
| --- | --- | --- |
| H02 の DEVICE_CAPS と H06 の「DEVICE_CAPS2」、H03 の EXT_CAP_FEATURE | DEVICE_CAPS2 は本文・header・RTL のどこにも無い。EXT_CAP_FEATURE の bit は本文と header・RTL で全く違い、[7:4] が MAX_SESSIONS（md 292）と MAX_MRT（regs.h 298）で重なる | J06・J07 |
| H11 の STATUS[9] FAULTED と H12・G13 の STATUS | bit の位置は md 326・regs.h 118 で合う。ただし FAULTED は「unrecoverable」（md 326）で RUN のまま立つ時の消し方が無い。STATUS の SU・ROP の bit は header の中で 2 通り | J02・J21 |
| H05 の CP_HEAP の 32 bit 物理と UMA・H07 の IOMMU | heap・PTE・root は 32 bit。IOMMU の無い PC では host memory を 4 GiB 未満から取るしかない（H07）が本文に無い。ring・fence は 64 bit と書くが RTL の RING_BASE_HI は 0 | J04・J10・J12 |
| H08 の LOCAL（bit 5）と既存の VM flag | VM flag の bit 5 は空き（READ 0・WRITE 1・EXEC 2・COHERENT 3・WC 4）で重ならない。ただし PTE の bit 5 は WC（md 1162）で、PTE に LOCAL の bit が無い | J11 |
| H15 の index「16 または 32」 | 型で大きさが違うので、型の混ざる set の index が決まらない | J17 |
| H01 の dword の数と header の struct | 合う: BIND_PIPELINE 3・INDEX 4・DESCRIPTOR_TABLE 4・VIEWPORT 7・SCISSOR 5・DRAW 5・DRAW_INDEXED 6・DRAW_INDIRECT 6・DISPATCH 4・BARRIER 4・EXECUTE 4・FENCE 5・VM_MAP 10・VM_UNMAP 8・SESSION_OPEN 4・SESSION_CLOSE 2・BIND_SESSION 2・BEGIN_RENDER_PASS 10・END 1・LOAD_CONST 3+N・WAIT_FENCE 4・WRITE_TIMESTAMP 4・COPY_BUFFER 7・FILL_BUFFER 5。合わない: DRAW_MESH_TASKS（md 611 は 5、cmd.h 320〜325 は 4）。struct が無い: 0x21・0x26・0x36・0x37・0x41・0x42・0x44・0x50・0x51 | J18 |
| H22 の CMD_FENCE 5 dw と旧 layout | 5 dw は同じだが、dw4 の中身が回答（seq_hi 32 bit）と本文・header（seq_hi 16 bit + flags 16 bit）で違う。§9.1 は 32 bit の fence のまま | J13 |
| H19 の dword 3..4「または直接 maxDrawCount」 | 1 つの field が address と数の両方になり、count を buffer から取る時の上限の置き場所が無い。本文・header は回答と別の配置 | J14 |
| opcode の表と ghci_cmd.h | md 593〜629 と cmd.h の `GHCI_CMD_*`（35 個）の値は全部一致。回答の表とも一致。cmd.h 76 の `GHCI_GPU_OP_VM_MAP` 0x10110 が残る | J01・J18 |
| blend factor | cmd.h 125〜143 の 0〜18 は VkBlendFactor と一致。compare op・blend op・cull・front face・polygon mode・address mode も Vulkan の番号と一致。topology は 0〜4 で TRIANGLE_FAN（5）が無い | J15 |
| Vulkan の必須の limit・format（未確認） | maxComputeSharedMemorySize 4096 は下限 16384 に届かない。R16G16B16A16_SFLOAT・R32 の float・整数の color attachment を flag の下に置く。A2B10G10R10・B10G11R11・E5B9G9R9・R16 の整数・R32G32 の整数・R32G32B32_SFLOAT（vertex）などが表に無い。BC は一部だけで textureCompressionBC を出せない | J19・J20 |

## 判定の詳細: H01〜H23

| ID | 判定 | 根拠と残り |
| --- | --- | --- |
| H01 | 一部 | 表（md 593〜629）は回答と同じで cmd.h の値とも合う。残り: payload が §7.3 に無い command（BIND_SESSION は §11.2 だけ、BEGIN/END_RENDER_PASS・LOAD_CONST・WAIT_FENCE・WRITE_TIMESTAMP・QUERY・COPY_*・BLIT・FILL・RESOLVE・DISP_SET・FLIP・MESH）、DRAW_MESH_TASKS の dw の不一致、indirect dispatch の欠け（V12）、「Mandatory (Raster)」の意味（J18・J14） |
| H02 | 食い違い | md 264〜283・regs.h 79〜96 は回答の bit と合う。しかし RTL は [29:24] を 0 に固定し V07 の分岐が残る（regs.v 149〜152・426〜427）。§12 は今も CAPS[n] を使い（G19 の CAPS[2] は 1.0 に無い）、§3.2.2・§16・Appendix A が CAPS・CAPS2 を書く。regs.h に `GHCI_CAPS_*`（241〜265）と bit 24〜29 の無い `GHCI_DEV_CAP_*`（268〜280）が残る（J06・J08・J24） |
| H03 | 食い違い | md 430 は FMT 0x140、RTL は FMT 0x160・SA 0x140（regs.v 389・392）。0x080 は header で BC の bitmap（regs.h 283〜292）で FMT の近道ではない（FMT は FMT_INDEX で選ぶ窓、md 436〜445）。EXTCAP_PTR は XCAP_MASK ≠ 0 で 0x100（regs.v 440）になったが、slim では capability の register の読みが list の語だけ（regs.v 448、推測: MEM・VEND は 0）（J06・J09） |
| H04 | 一部 | CMD_BIND_SESSION（md 600・1125、cmd.h 427〜430）に一本化は合う。残り: 「session 0 は flat の物理を直に」は本文に無い。session 0 の address の空間、session 0 への VM_MAP（J10） |
| H05 | 食い違い | md 1135〜1143 は回答と合う。しかし CP_HEAP_BASE/LIMIT は §3.1・regs.h に無く RTL slim は書き込みを受けない（regs.v 264〜269）。Appendix A（md 2271〜2275）に回答の「Step 4.5」が無い。root を誰が確保するかが回答（host）と cmd.h 417（0 = CP）と md 1146（CP）で違う。reset の後の扱いが md 488〜490・1062・1333 で割れる（J04・J12） |
| H06 | 食い違い | DEVICE_CAPS2 は無い。EXT_CAP_FEATURE[7:4] は md 292 で MAX_SESSIONS（4 bit に既定の 16 が入らない）、regs.h 298・RTL で MAX_MRT。md 128 は 31 sessions。依存（HW_WALKER か PROT）は md 596〜600 と合う（J06・J07） |
| H07 | 一部 | driver の規則（IOMMU を使わない時は 4 GiB 未満から取る）は回答で決まった。本文に 4 GiB の制限の文が無い（md 1156 の PTE から読めるだけ）。ring・fence が 64 bit と書く（md 196〜197）のに RTL の RING_BASE_HI は 0。CAP_IOMMU の意味（J10・J27） |
| H08 | 食い違い | LOCAL flag は md 1133・cmd.h 270・`ghci_frame.h` で入った。PTE（md 1155〜1163）に LOCAL の bit が無く、HOST_LIMIT（md 1231）は address の範囲で区別する別の model（J11） |
| H09 | 食い違い | kind 8 は regs.h 158・md 1143 で MMU_OOM、しかし §3.5 の表（md 378〜387）に 8 が無く、md 1591 は ISA fault を kind 8、RTL は ISA 8（regs.h 371）。SOURCE_BLOCK 4 = SU は md 374 と合うが regs.h 168 は SU = 2。SYNDROME の field は本文に無い（J03） |
| H10 | 食い違い | KICK の廃止は md 180 と合うが、md 380 の kind 2 は「Absent block KICK」、md 385 の kind 7 は「Aperture or locked register」のまま。§3.1.1・§7.5・§11.6 に KICK・aperture が残る（J01・J03） |
| H11 | 食い違い | md 1055〜1060 は回答と合う。しかし md 1408〜1413 は「version 1 の回復は装置全体」。FAULTED は unrecoverable（md 326）で RESET でしか出ない（md 485）。WS290 は PROT を実装しない（WS292）ので driver への影響は小さい（J21） |
| H12 | 食い違い | MEM_READY = STATUS[12] は md 329・regs.h 121 と合う（解決）。0x040〜0x04C は回答と md 204〜206・regs.h 63〜67・RTL（regs.v 443〜444）が合わない。RESIDENT_WARPS は 1.0 の窓に無い（J04） |
| H13 | 食い違い | md 431 は回答と同じ VEND 0x180・GCLK_HZ +0x10。しかし md 427 の list の範囲と重なり、RTL は VEND を 0x0C0 に置く（regs.v 387・479）。PERF の中身（md 467〜474）は 1.1 の PERF0〜3（0x080、FMT0 と重なる）と「0 CP .. 6 SU」の busy の番号（どちらの STATUS とも合わない）を指す。DBG の中身は 1.1 の register（md 247〜249）（J09・J01） |
| H14 | 食い違い | §8.6（md 984〜1018）は回答と同じ値。しかし maxVertexInputAttributeOffset 65535・BindingStride 4095 の根拠の「PSO の 16 bit・12 bit の field」が PSO（md 811〜812、cmd.h 160）に無い。maxComputeSharedMemorySize 4096 は Vulkan 1.0 の下限未満（未確認）、LDS の config と合わない。足りない limit が多い（J15・J19） |
| H15 | 一部 | buffer descriptor（md 881〜890、cmd.h 437〜444）は回答と合う。push constant は CMD_LOAD_CONST（md 906〜910、cmd.h 472〜477）で決まった。残り: index の規則、combined image sampler、§12.10 の interface table との関係（J17） |
| H16 | 食い違い | END_RENDER_PASS（md 613、cmd.h 467〜469）と FMT_LIMITS の color 数は決まった。attachment を指定する field が BEGIN_RENDER_PASS（cmd.h 453〜464）に無く、回答と合わない。clear の値が 1 つ（J16） |
| H17 | 一部 | CAP_BLT（md 279、regs.h 92）と session の VA で転送は決まった。RTL は bit 27 を 0 に固定（J08）。COPY_IMAGE・BLIT・RESOLVE の payload が無い。CAP_BLT の無い discrete の upload の手段（J18） |
| H18 | 食い違い | blend factor は cmd.h 125〜143 で直った（解決）。属性の新しい配置は md 800〜841・cmd.h 145〜199 に無い（J15） |
| H19 | 食い違い | 回答の配置と md 704〜713・cmd.h 310〜317・一次 G25 が 3 通り（J14） |
| H20 | 一部 | UMA で共有 DRAM から scanout は md 1192〜1193・2119〜2121 と合う。残り: bit 3 が OR であることは md 355 に無い、W1C の扱い、address の空間、DISP_SET・FLIP の payload（J23・J18） |
| H21 | 一部 | No Snoop を付けないは md 1248〜1249・2244 と合う。「必ず COHERENT」は本文に無く、md 1238〜1240 と COHERENT の flag の意味が合わない（J22） |
| H22 | 食い違い | 回答（64 bit 必須、dw4 = seq_hi 32 bit）と md 748・cmd.h 365〜372（seq_hi 16 bit + flags）・md 1025（32 bit）・cmd.h 480〜485（WAIT_FENCE 32 bit）（J13） |
| H23 | 食い違い | 表（md 919〜967、47 行）は入った。Guard の `CAP_BC` が無い bit、BC6H と sRGB・SNORM の欠け、Storage Image の列と md 441〜442、Vulkan の必須の format を flag の下に置く（未確認）（J20） |

## 判定の詳細: H に入っていない G と V07

| ID | 判定 | 根拠と残り |
| --- | --- | --- |
| G02 | 解決 | BAR0 128 KiB、register は先頭 1 KiB（md 149〜158・1202・2241）。§3.1.1 の 1.1 の窓の残りは J01 |
| G03 | 解決 | RING_RPTR・WPTR は dword index（md 199〜200・537〜538）。RING_SIZE の符号の食い違いは新しい J05 |
| G05 | 解決 | VM_MAP 0x10（10 dw）・VM_UNMAP 0x11（8 dw）（md 596〜597・1121〜1122、cmd.h 387〜410）。cmd.h 76 の 0x10110 の残りは J01 |
| G09 | 一部 | 一次回答「HW_WALKER = 0 は flat 物理」が本文に無い。CAP_IOMMU の意味（J25・J27） |
| G10 | 食い違い | 一次回答の CTRL.TLB_FLUSH は regs.h 107 の [4] で、md 314 の [4] WDT_EN と RTL の CTRL（1.1 の [4] WDT_EN）と重なる（J02） |
| G12 | 食い違い | mask は IRQ_ENABLE で決まり IRQ_GLOBAL は無い（一次）が regs.h 106 に残る。IRQ_SET（md 348 の 0x040）は §3.1 では DIAG_CP_PC（RO）、RTL slim は書き込みを受けない（J02・J04） |
| G13 | 解決 | STATUS[19:16]（md 332〜340・479、regs.h 124〜130）。細部: §3.3 は 0〜7、§5 は 0〜5（5 RESETTING）、regs.h 130 は 5 = RESET_WAIT。driver は 5〜7 を reset 中と読めば足りる |
| G23 | 一部 | WRITE_TIMESTAMP・BEGIN/END_QUERY・WAIT_FENCE の opcode は入った（md 619〜622）。query の結果の置き場・幅・availability・reset、timestamp の stage の値と有効 bit が無い。一次の CMD_WAIT_EVENTS は無い（J18） |
| G26 | 一部 | 一次回答（1 階層、VM_*・SESSION_* を禁止）が md 731〜739 に無い。v1 の driver は使わない（J25） |
| G27 | 食い違い | md 1255〜1259（暗黙の無効化）と一次回答（無し）。一次の stage・access の名前は cmd.h に無い（J26） |
| G32 | 一部 | SESSION_OPEN が session ごとの root を作る（md 1123）ので session ごとと読める。root の memory の扱いが残る（J12） |
| G33 | 一部 | 一次回答（IDLE で WPTR を受ける、W1C は全状態）が md 481「etc.」に無い。一次の「RUN == 1 なら直ちに RUNNING」は IDLE の定義（RUN = 0）と合わない（J25） |
| G34 | 解決 | IRQ_ENABLE は reset で保つ（md 488）、IRQ_STATUS は IRQ_ENABLE に依らず立つ（md 345〜347）。IRQ_GLOBAL は無い（一次 G12、regs.h 106 の残りは J02） |
| G36 | 一部 | 一次回答（4 KiB、二重 map は fault、部分 unmap 可、size 0 は no-op）が本文に無い。二重 map の fault の kind が無い（J25） |
| G37 | 一部 | 一次回答（session ごと、BIND_SESSION と reset で消える）が本文に無い。driver は BIND_SESSION の後に bind を全部出し直すことになる（J25） |
| G39 | 一部 | 一次回答（任意の値）と md 1025「ring ごとに単調」（J13・J25） |
| G41 | 食い違い | H02「全部 DEVICE_CAPS」と md 295〜304・2255・2266（CAPS・CAPS2）、§12 の CAPS[n]（J06・J24） |
| G42 | 食い違い | EXT_CAP_FEATURE は md 285〜293 に入ったが header・RTL と bit が違う。EXT_CAP_FMT0 は md に無い。どちらも §3.1 の表に無い（J06） |
| G43 | 食い違い | EXTCAP_PTR は 0x100 になった（regs.v 440）。capability の offset が md・回答・RTL で違い、slim で register が読めるか不明（J09） |
| G44 | 食い違い | 0x2A は md 611 に入った（5 dw）が cmd.h 320〜325 は 4 dw。PSO に task・mesh の code_va が無い（J15・J18）。driver は WS226 まで使わない |
| G45 | 食い違い | CAPS が 1.0 に無いなら重なりは消えるが、§12 は CAPS[24]・[26]・[28]・[29]・[30] を 1.1 の意味で使い、1.0 の DEVICE_CAPS の同じ番号は PROT・ROP_PACK・DISP・MSAA・TASK。TEX_EXT（3D・cube・compare・gather）に当たる bit が 1.0 に無い。FEATURE[8] DUAL_SOURCE（regs.h 299）と DEVICE_CAPS[5] の重なりが残る（J24・J06） |
| G46 | 一部 | CAPS が無くなれば消えるが、md 753（CAPS[17]）と regs.h 241〜265 に残る（J01） |
| G47 | 未解決 | CAPS2 の 4 bit の field（md 300〜304）は直らず、CAPS2 自体の場所が無い（J06） |
| G48 | 未解決 | MRT の数だけ FMT_LIMITS[3:0] で決まった（H16）。SFU・registers per lane（RESIDENT_WARPS は 1.0 の窓に無い）・NWARPS・TRILINEAR・3D_CUBE・MSAA の sample 数（md 281 と regs.h 94 で違う）は無い（J24・J19） |
| G49 | 一部 | DISP_SET 0x50（2+2N）・FLIP 0x51（4 dw）が md 628〜629 に入った。payload が無く、§13.2（md 2047）は「§7.3 の micro-op」のまま。IRQ bit 3 と 9〜12 の関係は回答だけ（J18・J23） |
| V07 | 未解決 | regs.v 426〜427 は 7fc707d でも、DEVICE_CAPS の parameter が 0 で CAPS が既定でなければ CAPS（1.1 の layout）を返す（J08） |

## 未実施

- amaterrace-gpu の RTL の他の module（atg_cp_ctl.v の STATUS の組み立て、FMT・PROT・DISP2 の register を答える module）は読んでいない。「slim で MEM・VEND が 0」は atg_cp_regs.v だけからの推測。
- Vulkan 1.0 の Required Limits・Required Format Support は repo の外の知識で、照合していない（未確認）。
- design.md・design-review-v3 への反映はしていない（依頼の範囲の外。仕様の調整の後）。
