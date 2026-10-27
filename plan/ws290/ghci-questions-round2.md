# GHCI の疑問の 2 回目（一次回答で解決しない点、2026-10-27 Q1）

- 元: [ghci-questions.md](ghci-questions.md) の G01〜G39 と、hardware の担当の一次回答 [ghci-answers-hw-20261027.md](ghci-answers-hw-20261027.md)。P3 が amaterrace main ce9725a（docs・include の最後 f718918）で見つけた食い違いを含む。
- 2026-10-27 ユーザー「一次回答で解決されない疑問があれば教えてください。担当に伝えます。」への Q1 の答え。資料の修正の後に、解決したかを読み直す。

| ID | 元 | 解決しない点 |
| --- | --- | --- |
| H01 | 全体 | 回答に出た command（`CMD_BIND_SESSION`・`CMD_SESSION_OPEN/CLOSE`・`CMD_BEGIN_RENDER_PASS`・`CMD_COPY_*`・`CMD_BLIT`・`CMD_FILL_BUFFER`・`CMD_RESOLVE_IMAGE`・`CMD_WRITE_TIMESTAMP`・`CMD_BEGIN/END_QUERY`・`CMD_WAIT_FENCE/EVENTS`・`CMD_DISP_SET`・`CMD_FLIP`・`CMD_LOAD_CONST`、include だけの `CMD_DRAW_MESH_TASKS` 0x2A）の opcode・dword 数・payload の配置の一覧が要る。どれが必須で、どれが capability の flag 付きか。 |
| H02 | G02・G18・G19 | capability の register の全体の bit の表が要る。CAPS（0x020）・CAPS2（0x024）が 19 register の表の RING_BASE_LO/HI と同じ offset。DEVICE_CAPS の [15:8]（PHYS_LANES）・[23:16]（NSU）が、既存の bit（HW_WALKER・IOMMU・FP16・GSI1[7] など）や新しい [24] PROT・[25] TEX_FFILT・[26] ROP_PACK・[30] TASK・[31] MESH とどう並ぶか。G19 の `CAPS[2]`（CAP_SU）はどの register か。 |
| H03 | G02・G29 | include・RTL は EXT_CAP_FMT0・FEATURE を 0x080・0x084 に置くが、回答の FMT は 0x140。どちらが正か。RTL の slim の構成は EXTCAP_PTR = 0（MEM・FMT・DISP2 が見えない）だが、それで正しいか。 |
| H04 | G04 | session の切り替えは `CMD_BIND_SESSION` か「キュー/PSO バインド」か、どちらか一つに決めてほしい。session 0（driver 自身）の扱い。 |
| H05 | G06・G08・G35 | page table の memory を誰が用意するか: `CMD_SESSION_OPEN` の `root_pt_phys` は host が root を渡す形だが、下の段の page table は CP が `CP_HEAP` から取るのか、host が渡すのか。`CP_HEAP_BASE/LIMIT`（0x0B0/0x0B4）は 32 bit か、host の bus address か local memory か、誰がどの状態で書くか、要る大きさと揃え。初期化の順（G30）のどこで設定するか。 |
| H06 | G06・G18 | session の数（今 16）を driver はどこで読むか（capability の field か）。session の command は `CAP_HW_WALKER` と `CAP_PROT` のどちらに依るか。 |
| H07 | G07・G31 | host の bus address は 64 bit だが PTE の物理は [31:12]（4 GiB まで）。4 GiB より上の host memory（IOMMU 無しの PC では普通）を VM_MAP の iova に渡せるか。渡せないなら driver は 4 GiB 未満から取る、で正しいか。 |
| H08 | G31 | VM_MAP の物理の引数で、local memory（`LOCAL_BASE + offset`、KC705 0x8000_0000）と host の bus address（同じ値もあり得る）をどう区別するか（flag か、address の範囲か）。`HOST_LIMIT` の意味が回答に無い。 |
| H09 | G08・G14 | `FAULT_MMU_OOM` の kind の番号が G14 の表に無い。G14 の表は 8 が抜けている（以前の ISA の fault）。program の header の不正（G19）が kind 1 で、不正 opcode と区別できるか。 |
| H10 | G01・G14 | G14 の kind 2「不在ユニットへの KICK」・7「アパーチャ／未オープン書き込み違反」の KICK・aperture は、単一の GHCI 1.0 にまだ有るのか（以前の版の仕組みに見える）。 |
| H11 | G15 | `CAP_PROT` で session だけを破棄した時、driver はどの session が落ちたか・CP が RUNNING のままかをどう知るか（IRQ・STATUS・FAULT_INFO のどれか、W1C の後に続くか）。 |
| H12 | G16 | `WDT_CYCLES` を 0x044 に置くと、19 register の表（0x000〜0x048）の 0x044 の register と重ならないか。STATUS.mem_ready（G30）の bit の位置。 |
| H13 | G17・G29 | EXTCAP `VEND`（ID 4）の中身の配置、`GCLK_HZ` の register の位置。PERF・DBG の中身。 |
| H14 | G18 | Vulkan の limit に要る値の出所: constant bank・texture slot・sampler・descriptor の数・vertex の属性と binding の数・`maxVertexInputAttributeOffset` など。「標準 profile の値」は ghci.md に表として書かれるか。 |
| H15 | G20 | buffer の descriptor（uniform・storage・texel）の byte の配置、Vulkan の (set, binding) と table の index の対応。push constant は `CMD_SET_DYNAMIC_STATE` と `CMD_LOAD_CONST` のどちらか、その配置と大きさの上限。 |
| H16 | G21 | attachment は `CMD_BEGIN_RENDER_PASS` と PSO の配列のどちらか。color は 0〜7 と書くが FMT_LIMITS は 4 か 8。render pass を終える command は有るか。 |
| H17 | G22 | blitter が有るかどうかの capability の flag（G19 の「Blit 専用」の構成と逆に、blitter の無い構成は有るか）。転送の command の session の VA か bus address か。 |
| H18 | G24 | PSO の 128 byte の新しい配置（属性の binding、16 bit の offset、sample mask、alpha to coverage、varying の数）。属性の format は今 4 bit で VkFormat の番号が入らない。`ghci_cmd.h` の blend factor の表（0〜11）は VkBlendFactor と番号が違うので直るか。 |
| H19 | G25 | `draw_count_va` の他に、indirect の stride・最大の数の欄は有るか。 |
| H20 | G28 | `FB_BASE` などが local VRAM の物理なら、local memory の無い（MEM が無い・UMA）device では表示の image をどこに置くか。host memory から scanout できるか。IRQ の bit 9〜12 と §3.4 の IRQ_DISPLAY（bit 3）の関係。 |
| H21 | G38 | host memory を VM_MAP する時、driver は COHERENT を必ず付けるべきか。GPU は host memory を PCIe で snoop するか（No Snoop を付けないか）。 |
| H22 | G11 | Timeline の 64 bit の fence（`flags[1]`）は capability の flag 付きか。CMD_FENCE の payload の配置（以前の lo 32 + hi 16 bit の形は残るか）。 |
| H23 | 新 | texture の format の一覧（ETC2・EAC の他に、§8.3 に名前の有る BC1〜BC7・RGB565・D24S8 など）は、それぞれ対応するか、capability の flag 付きか。§8.4 に、format ごとの sampled・filter・color attachment・depth・storage・vertex の対応の表が欲しい（Vulkan の format の feature に写すため）。 |
