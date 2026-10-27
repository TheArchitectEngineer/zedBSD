# GHCI レビュー 第 14 版（ghci.md cb937e6 に対する、回答 第 13.0 版の確かめと新しい疑問 N62〜N72、2026-10-28 06:05）

- 対象: `git -C /home/awe/amaterrace-gpu show cb937e6:docs/ghci.md`（2816 行）、同じ commit の `include/ghci_cmd.h`・`include/ghci_regs.h`・`include/gsi1_isa.h`。差は `git diff cf8be08 cb937e6 -- docs/ghci.md include/` で読んだ。main の先頭 989126a は回答の file だけの差。行番号は cb937e6 の物。
- 読んだ回答: `docs/ghci-review-reply.md`（989126a）、1 行目「# GHCI Review Reply - Version 13.0 (2026-10-28) - Commit: cb937e6 (Resolving N47〜N60 and Vulkan 1.0 Extensions)」、124 行、md5 6afb37a069d39d48e8a235ac942e15bb、末尾 DONE。
- 番号: 回答の Part B は「N61」として足された（texture と sampler の独立 index、swizzle、unnormalized、VertexIndex・InstanceIndex の operand）。こちらの新しい疑問は N62 から付ける。

## 要約

- 第 13 版の 14 件: **解決 10**（N47・N49・N50 の bit の配置・N51・N52・N54・N55・N56・N59・N60）、**一部 4**（N48 の式、N53 の format、N57 の表、N58 の本文、下の N64・N67・N68・N69 に続ける）。
- Part B（N61）: swizzle（1279 行の 0x1C）と unnormalized（0x0F）の field は driver で使える。sampler の独立 index と VertexIndex の operand は本文と encoding に穴がある（N62・N66）。
- 新しい疑問 **11 件: C 0・H 1・M 6・L 4**（N62〜N72）。
- **driver の実装**: N62 の答えまで、sampler の独立 index は使わず texture k と sampler k の組で compile する（cf8be08 と同じ）。他は「driver の仮の扱い」で進める。design は cb937e6 に追従させる（N47 の linear・N48 の CLOSE が table を返す・N49・N50・N51・N59 の VF の 0 埋め・swizzle と unnormalized を使う）。

## 残る疑問

重さ: C＝その部分の driver が書けない、H＝その部分を書く前に答えが要る、M＝仮の扱いで進められるが答えが要る、L＝文書の整理。

| ID | 重さ | 疑問 | 根拠（cb937e6 の行） | 推し | driver の仮の扱い |
| --- | --- | --- | --- | --- | --- |
| N62 | H | sampler の slot の field（`w1[14:11]`、`GSI1_W1_SAMP_SLOT_SHIFT 11u`）が、texture の R の形の texel offset dw（`w1[13:10]`、2214〜2216 行、`GSI1_W1_TEX_DW_SHIFT 10u`）と `.sat`（`GSI1_W1_SAT`、bit 14）に重なる。また本文の他の所は「combined slot = sub[3:0]」のまま（§8.4 1432・1434、§12.5 の opcode 表 2064、§12.9 2191、§12.10.2 2308）で、1340 行の「legacy 1:1 binding mode」は定義が無い（どの bit で選ぶか、既定はどちらか） | 1338〜1340、2064、2191、2214〜2216、2308、gsi1_isa.h 236〜245 | texture の全部の形（l・b・f・g・qlod・sz、R と I の形）で使わない bit に sampler の slot を置き、§12.4・§12.5・§12.9・§12.10.2 と gsi1_isa.h を揃える。「1:1 mode」は無くし、sampler の slot を常に書く（同じ値を書けば今の組と同じ）と書く | 独立の index は使わず、texture k と sampler k の組（cf8be08 の形）で compile する |
| N63 | M | `FAULT_INFO.KIND` の別名で、1 つの値が別の原因を兼ねるようになった: 2 = KICK_ABSENT / RING_OOB、3 = BAD_REG / PRIVILEGED_OP / **PAGE_READ**、6 = **PAGE_WRITE**（cf8be08 は PROT_FAULT）、7 = LOCKED_REG / TLB_ERROR、8 = MMU_OOM / **ISA_FAULT**、9 = PAGE_FAULT / PROT_FAULT。driver は KIND で「driver の誤り（framing・MMU_OOM）」と「client の shader の fault」を分けて回復を変えるので、同じ値が両方を指すと分けられない。§6.0 の 605 行は今も「kind 6 = PROT_FAULT（secondary の特権の違反）」、§12.6 は ISA の fault を kind 1 と書く | 412〜425、605、2090〜2100 | 1 つの値に 1 つの意味（cf8be08 の 1〜10）に戻し、名前の別名は同じ意味の物だけにする。read・write は SYNDROME の bit 20、ISA の fault は kind 1 と UNIT = SU で示す | KIND と UNIT の組で分ける: UNIT = CP の 2・3・7・8 は driver の誤り（8 は MMU_OOM）、UNIT ≠ CP の 1・3・6・8・9・10 は client の fault |
| N64 | M | N48 の CP_HEAP の式「MaxSessions × RootBytes + MaxMappedGiB × 4 KiB + 64 KiB」は足りない。L0 の table 1 枚（4 KiB、512 entry）は 2 MiB を覆うので、1 GiB には L0 が 512 枚（2 MiB）要る。VA 40 bit は L1 の table（1 GiB ごとに 4 KiB）も要る。PROT = 1 は session ごとに別の table なので、同じ物理を 2 つの session に map すれば table も 2 組 | 1725〜1745（table の構造）、1918〜1921 | CP_HEAP_BYTES ≥ Σ_session（RootBytes + 40 bit なら L1 の 4 KiB × ⌈map した範囲 / 1 GiB⌉ + L0 の 4 KiB × ⌈map した範囲 / 2 MiB⌉）+ 64 KiB（PROT = 0 は共有の table 1 組で数える） | 上限で数える（session ごとの root と L1、2 MiB の chunk ごとに L0 の 4 KiB）。CLOSE と UNMAP が table を返すので（1913・N48）、勘定は CLOSE・UNMAP の fence の後に返す |
| N65 | M | N61 で sampler は独立にできたが、texture の slot（set 0 の 16 個）は 1 つの draw の VS と FS で共有のまま（1402・1432）。1434 行の「16 static slots fully satisfy Vulkan 1.0 (`maxPerStageDescriptorSamplers = 16`)」と 1340 行の「per shader stage」は、VS が 16・FS が 16 の別の texture を使う pipeline（Vulkan 1.0 で合法）を満たさない | 1338〜1340、1402、1432〜1434 | stage ごとに texture・sampler の表を分ける（例: VS と FS で別の set 0/1 の base）、slot を 32 にする、または 1434 行の主張を「VS と FS の和が 16 まで」に直す | VS と FS の texture の和が 16 を超える pipeline は作成で断って log（U8） |
| N66 | M | N61 の「Vertex Attribute 64 語化」と gl_VertexIndex・gl_InstanceIndex の system operand（`GSI1_VTX_INDEX 120`・`GSI1_INST_INDEX 121`）が ghci.md に無い: §12.3 の operand の表は 119（`mask`）で終わり、§12.10.3 は今も「Σ C_i ≤ 60（r0〜r59）、超えれば driver が断る」「VertexIndex は r60・InstanceIndex は r61、used_regs_vs ≥ 62 の時だけ」（2332〜2343） | 2010〜2011、2332〜2343、gsi1_isa.h 172〜173 | §12.3 に 120・121 を足し、§12.10.3 を「属性は r0〜r63 の 64 語まで（used_regs の上限 63 との関係も）、VertexIndex・InstanceIndex は operand 120・121 で読む、r60・r61 への配達は属性が使わない時だけ」などに書き直す | 60 語と r60・r61 のまま（cf8be08 の形）。60 語を超える pipeline は作成で断る |
| N67 | M | §8.5 の「fully compliant with Vulkan 1.0 core mandatory requirements」（1443）に対し、表（1447〜1520）に今も無い必須の format: `R32G32B32_UINT`・`R32G32B32_SINT`（VERTEX_BUFFER）、`R32G32B32A32_SINT`、`A8B8G8R8_{UNORM,SNORM,UINT,SINT,SRGB}_PACK32`、`A2B10G10R10_UINT_PACK32`、`E5B9G9R9_UFLOAT_PACK32` と `B4G4R4A4_UNORM_PACK16`（SAMPLED・FILTER_LINEAR）。また `R16G16B16A16_SFLOAT` の Guard「Mandatory (`CAP_FP16` / `FFILT`, N53)」（1484）は、必須なのか CAP_FP16 で無くなるのかが読めない | 1443、1447〜1520 | 表に足すか、1443 の主張を外して不足を書く。R16G16B16A16_SFLOAT は「必須（CAP に依らない）」か「CAP_FP16 の時だけ」のどちらかに | U8: 欠ける format は答えから落とし、使われた時に断って log。R16G16B16A16_SFLOAT は CAP_FP16 の時だけ出す |
| N68 | L | N58 の回答（2 sample の標準の位置を §12.11 に追記）が本文に無い。2442 行は今も 1 と 4 sample だけ | 2442〜2445 | (0.75, 0.75)・(0.25, 0.25) を足す | 2 sample を出さない |
| N69 | L | N57 の `IRQ_STATUS.IRQ_HALTED`（bit 6）が §5.2（559）にだけあり、§3.4 の bit の表（396〜401）に無い | 396〜401、559 | §3.4 の表に bit 6 を足す | `STATUS.HALTED` を poll する |
| N70 | M | `CMD_DISP_SET` の大きさが食い違う。§7.2 の表と本文の header は「2 + 2N dword」（750・1145〜1146）だが、本文の配置（dword 1 + 2k に offset、2 + 2k に value、k = 0..N−1）は header と N 組で 1 + 2N dword になる（count の語は無い） | 750、1144〜1150 | どちらかに揃える。推し: 1 + 2N（count の語を置かない）。2 + 2N なら dword 1 に N を置くと書く | 1 + 2N |
| N71 | L | Vulkan の limit の値が §8.6 に無い: `subPixelPrecisionBits`、`viewportSubPixelBits`、`maxViewportDimensions`、`viewportBoundsRange` | 1538〜1575、`CMD_SET_VIEWPORT` | §8.6 に値を足す | 4・0・{16384, 16384}・[−32768, 32767] と仮に答える |
| N72 | L | 文書の中の食い違い: (1) §8.5.1 は ETC2・EAC を「Mandatory」（1503〜1512）、§3.2 は `EXT_CAP_FMT0[7]` CAP_ETC2 で有無を示す（302）、(2) `lw`/`sw` は「bits 1:0 of the address are ignored」（2155）、§12.6 は misaligned で kind 10（2097）、(3) push constant は「up to 96 bytes」（1425・2314）、§8.6 は 128（1553） | 同左 | 各点をどちらかに揃える | (1) FMT0[7] を正、(2) 揃わない番地を作らない、(3) 96〜127 byte は c32〜c39 |

## 解決した N

- N01〜N46（第 12 版まで）。
- 今回: N47（attachment は linear、`*_va` は subresource の先頭、mip・layer は 0、layers は 1、1393〜1398）、N49（PROT = 0 の CLOSE は共有の PTE を消さない、閉じた session への CLOSE・UNMAP は no-op、1914〜1915）、N50（FAULT_INFO の [19:16] stage・[31:20] syndrome、412〜439。kind の別名は N63）、N51（INDEXED は両方の形で header の flags[0]、872〜880）、N52（252）、N54（225）、N55（1817）、N56（171）、N59（descriptor_count、圧縮の単位は texel、VF の範囲外は 0、779・795・1016）、N60（ghci_regs.h の旧い定義に注記）。
- 一部（続きは上）: N48（root の大きさ 628 行と CLOSE・UNMAP が table を返す事は解決、式は N64）、N53（format の追加は解決、残りは N67）、N57（INVALID_ARG は解決、表は N69）、N58（N68）。
- Part B（N61）: swizzle（1279）と unnormalized（1300）は解決。sampler の独立 index は N62・N65、VertexIndex の operand は N66。

DONE
