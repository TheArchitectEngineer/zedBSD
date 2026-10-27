# GHCI の疑問の 4 回目（J01〜J27 の回答を読んで、2026-10-27 Q1）

- 元: 2026-10-27 ユーザーが伝えた GHCI の担当の J01〜J27 の回答の要約（amaterrace-gpu d819f4f、`docs/driver_qa_j01_j27.md`）。ユーザー「GHCIの担当からです。質問がありますか？」。
- 読んだのは貼られた要約だけ（driver_qa_j01_j27.md の本文は使用量の都合で未読）。d819f4f は docs/ghci.md と RTL を変えていない（`git show --stat`）。

| ID | 元 | 質問 |
| --- | --- | --- |
| K01 | 全体 | 正の文書はどれか（ghci.md・driver_qa_j01_j27.md・addressing_architecture.md・header）。d819f4f で ghci.md と RTL は変わっていない。ghci.md と RTL はいつこの回答に合わせるか。 |
| K02 | J04・J12・H05・H12 | J04 の register の全表に CP_HEAP_BASE/LIMIT・WDT_CYCLES・RESIDENT_WARPS が無い（0x044・0x048 は SCRATCH0/1 になった）。CP_HEAP（J12 が使う）・watchdog の周期・同時の warp の数はどこで設定・読むか。J01 の「19 本・0x000〜0x048」と J04 の 0x080・0x084 の関係。 |
| K03 | J02・H11・G13 | CP_STATE の値が前の回答（IDLE・RUN・HALTING・HALTED・FAULTED・RESET）から（IDLE・FETCH・DECODE・…・FAULT・HALT）に変わった。STATUS の FAULTED も bit 9（H11）から bit 2 に。H11 の「session だけ落ちても CP_STATE は RUN のまま」は新しい表でどう見えるか。CTRL の WDT_EN は bit 4（G16）から 5 に、RTL に無いとされた IRQ_GLOBAL（G12）が bit 6 に入った。どれが最終か。 |
| K04 | J03・H09・H10 | FAULT_INFO の配置が二次回答（[7:0] client・[15:12] kind・[23:16] source・[31:24] session）と全く違う。J03 の SESSION_ID は 8 bit の field で 0〜15。kind の番号も H09・H10 と違う（OOM 8 は同じ、他は別）。J03 が最終で header・RTL もそうなるか。 |
| K05 | J08・H01・H02 | DEVICE_CAPS の [27]〜[31] が H02（27 BLT・28 DISP・29 MSAA・30 TASK・31 MESH）から（27 TASK・28 MESH・29 BLT・30 DISP・31 MSAA）に変わった。[15:8] は PHYS_LANES から SIMD_LANES に。J08 が最終か。 |
| K06 | J09・H13 | VEND（H13 では 0x180、GCLK_HZ を含む）が J09 の表に無く、0x180 は MEM になった。GCLK_HZ（timestampPeriod）と subsystem の ID はどこで読むか。vendorID・deviceID は「0x10EE または 0x5441」「0x4748 または 0x7024」と二通り: driver はどこから読めばよいか（register か PCI の ID か）。 |
| K07 | J10・J11 | VA 30〜40 bit・PA 40〜48 bit が可変なら、driver はその device の VA と PA の幅をどの register で読むか。PTE が 64 bit（8 byte）なら 4 KiB の root は 512 entry: 段の数と各段の index の bit（VA 32 bit と 40 bit の時）を示してほしい。VM_MAP の「gpu_va_hi は 0 だけ」（G07）は無くなるか。 |
| K08 | J12・J21・H11 | MMU_OOM で session が HALT し「STEP/RUN で復帰」とあるが、STEP は debug 用（G15）。session ごとの停止と装置全体の CTRL の関係、OOM で途中まで map された VM_MAP の状態。J21 の全体 reset の後に session・page table・CP_HEAP の中身は残るか（driver は再生が要るか）。 |
| K09 | J13・H22・H01 | CMD_FENCE が 6 dw（H01・H22 は 5 dw）。WAIT_FENCE（4 dw）の値は 32 bit か 64 bit か、比較の式は。 |
| K10 | J14・H19・H01 | CMD_DRAW_INDIRECT が 7 dw（H01・H19 は 6 dw）。count を buffer から取る時（flags bit 1）、count の buffer の address と最大の数はどの dword か（dw4 の draw_count だけでは足りない）。 |
| K11 | J15・H14・H18 | PSO の属性が 8 個（attributes[8]）だけで、H14 の maxVertexInputAttributes 16・H18 の 16 属性と食い違う。binding_offset・format_location の bit の配置、input rate（instance）、task shader の code_va、属性の offset の幅を示してほしい。 |
| K12 | J16・H16 | CMD_BEGIN_RENDER_PASS（10 dw）に 8 個の color と depth の address・pitch・format・samples・clear の値は入らない。attachment の記述は memory の表を指すのか。その表の配置。 |
| K13 | J17 | combined image sampler は「連続 48 byte」と「{tex_id, sampler_id} の index の対」のどちらか一つに決めてほしい。descriptor の index の規則（大きさの違う型が混ざる set）。 |
| K14 | J18・H01 | payload が未定義の command: BEGIN/END_QUERY、COPY_IMAGE、BLIT、RESOLVE_IMAGE、DISP_SET、FLIP、SET_DYNAMIC_STATE、SET_VIEWPORT、SET_SCISSOR、CMD_BARRIER（J22 の bit の他の dword）、LOAD_CONST、DISPATCH、DRAW_MESH_TASKS、EXECUTE_COMMANDS、BIND_* の配置。 |
| K15 | J19・J20・H14・H23 | Vulkan 1.0 の必須の値・format を満たすか（未確認の推測: maxComputeSharedMemorySize は 16384 以上が必須、color attachment の R16G16B16A16_SFLOAT など）。J20 の format の表は H23 の 47 個より少ない: H23 の表と J20 のどちらが正か。 |
| K16 | J25・G26 | secondary の入れ子が「最大 2 階層」（一次回答 G26 は 1 階層）。どちらか。 |
| K17 | J22・G27 | CMD_BARRIER は J22 の 4 bit（L2 flush・L1 invalidate・TLB・drain）だけか。前の回答の stage・access の bit（一次 G27）は無くなったか。 |
| K18 | J24・G18・G48 | GSI1 の任意の機能（subgroup・packed・3D/cube・SFU など）と、lane あたりの register の数・NWARPS・MSAA の sample の数を、どの register の bit で読むか。 |
| K19 | J05・H03 | RING_SIZE を byte の数にするなら、RTL（今は 5 bit の log2）も直るか。折り返しを禁止して末尾を NOP で埋めた時、CP は WPTR が先頭に戻るのをどう扱うか（ptr は size/4 で 0 に戻るか）。 |
