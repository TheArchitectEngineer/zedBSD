# GHCI 1.0（cf8be08）の固定の値・構造で Vulkan 1.0 の必須を満たせない所

調べた日: 2026-10-28。読むだけの調査（zedBSD・amaterrace-gpu の file は変えていない）。

## 0. 出典と確認の水準

- GHCI: `git -C /home/awe/amaterrace-gpu show cf8be08:docs/ghci.md`（行番号は全部この版）、`1a1fa85:include/ghci_cmd.h`、`bea1ef9:include/gsi1_isa.h`。
- Vulkan: https://registry.khronos.org/vulkan/specs/1.0/html/vkspec.html を取得した。**この URL は今は 1.4.366 の spec（全拡張入り）を返す**。
  そこで次の表の「Vulkan Core」の列（＝1.0 の値）と、後の feature・拡張に条件付きの印（‡ § † など）の付かない欄だけを使った。
  - Table 76 "Required Limits"（§56.1 Limit Requirements）
  - Table 89〜98 "Mandatory Format Support"（§57.3 Required Format Support）
  - §55.1 Feature Requirements（1.0 は `robustBufferAccess` だけが必須）
  - Table 113 SPIR-V capability（1.0 の必須: Matrix・Shader・InputAttachment・Sampled1D・Image1D・SampledBuffer・ImageBuffer・ImageQuery・DerivativeControl）
  - 依頼文の参考の値は、全部 Table 76 の Core の値と一致した（maxStorageBufferRange 2^27、bufferImageGranularity ≤131072、maxDrawIndexedIndexValue 2^24−1 など）。
- 1.0 の原本の版の表と 1.4 の表の「Core」の欄の差は**未確認**（1.4 の表で Core と明記された値を 1.0 の値として読んだ）。
- zedBSD の設計: `/home/awe/zedBSD-claude1/plan/ws290/design.md` §11.4・§11.5・§15・§20 U8・U10。

## 1. 結論の要約

flag をどう立てても残る、Vulkan 1.0 の必須に対する**固定の**不足は次の 3 つの束に分かれる。

1. **driver では隠せない（または実用上隠せない）物**
   - texture/sampler の slot が **draw ごとに全 stage で 16 個**、しかも「texture k と sampler k の組」で固定（§8.4）。Vulkan は **stage ごとに** sampled image 16・sampler 16 を要る（VS 16 + FS 16 = 32、分かれた image×sampler の組は 16 を超え得る）。
   - **8 byte の非 fp16 の color attachment が無い**（R32G32_UINT/SINT/SFLOAT、R16G16B16A16_UINT/SINT。R32G32_SFLOAT は表で明示的に No）。
   - **A2B10G10R10_UNORM_PACK32** が format の表に無い（必須の COLOR_ATTACHMENT_BLEND を shader では作れない）。
2. **driver の工夫で隠せるが大きい作業が要る物**: 必須の vertex format の欠け、attribute の register の 60 語の上限、triangle fan、component swizzle と unnormalizedCoordinates の field の欠け、B10G11R11・E5B9G9R9・B4G4R4A4 の filter 付き sampling、clear・update・event・subpass などの command の欠け、byte 単位の CopyBuffer。
3. **仕様に書いてないので判定できない物（未確認）**: subPixelPrecisionBits・viewportBoundsRange・maxViewportDimensions、vertex fetch の範囲外の振る舞い（robustBufferAccess）。

## 2. 主の表: flag に依らない固定の不足

列の「隠せるか」: **できる** = driver・compiler の工夫で仕様どおりにできる（費用は書く）／**一部** = 一部の場合だけ、または性能・規模が実用の外／**できない**。

### 2.1 descriptor と texture の slot

| 項目（Vulkan の名前） | Vulkan 1.0 の要求 | GHCI の固定の値（cf8be08 の節・行） | 不足の程度 | driver の工夫で隠せるか |
| --- | --- | --- | --- | --- |
| `maxPerStageDescriptorSampledImages`・`maxPerStageDescriptorSamplers` | 各 16（**stage ごと**） | texture 命令の slot は `sub[3:0]` の即値 0〜15（§12.5 L2030、§12.9 L2155〜2157）。TU は set 0（texture）と set 1（sampler）の表だけを引く（§8.4 L1336、L1415、L1419〜1423、§12.10.2 L2274）。graphics と compute は slot 0〜3 を共有し（L1402）、1 つの draw の VS と FS は同じ set 0/1 の表を使う。L1423 は「16 slot で必須を満たす」と書くが、stage ごとでなく **draw 全体で 16** | VS と FS が各 16 を使う pipeline（計 32）を作れない。半分 | **できない**（実用上）。VS 側を texture 命令を使わない software の sampling（`lw` で読み、format の decode・filter・address mode・圧縮の decode まで shader で）にすれば理屈ではできるが、規模と速度が実用の外。pipeline 作成を断ると Vulkan の違反（作成の失敗は memory 不足などに限られる） |
| 分かれた `SAMPLED_IMAGE` と `SAMPLER` の組み合わせ（SPIR-V `OpSampledImage`、Shader capability の core） | image と sampler の任意の静的な組（per-stage 16 image・16 sampler の範囲で） | slot k は texture k と sampler k の固定の組（L1336、L1421〜1422）。組ごとに slot を 1 つ取る（design §11.4 L403） | 4 image × 5 sampler = 20 組のように、上限の中の使い方で 16 を超える | **できない**（上の行と同じ予算。超えた分は software sampling しかない） |
| `maxDescriptorSetSampledImages`・`maxDescriptorSetSamplers`（pipeline layout の総数） | 各 96 | 固定の上限の記述は無い。HW の制約は上の slot だけ | 不足ではない（下の注） | **できる**。descriptor は draw の時に driver が slot の表へ写す（design §11.4 の set 0/1）ので、layout に 96 個在っても、pipeline が静的に使う物だけを slot に置けばよい。design §11.4 L403・§20 U8 の「16 < 96」は過剰（16 は layout でなく pipeline の使用の上限） |
| `maxBoundDescriptorSets` | 4 | HW の set は 4 個（L1320、§8.6 L1528）だが set 0・1 は TU 専用の意味を持つ（L1419） | 数は足りる、意味が固定 | **できる**。app の 4 set を driver が HW の 4 表へ詰め直す（design §11.4 の表のとおり）。CPU の費用は draw ごとの写し |
| `maxPushConstantsSize` | 128 | 「標準は 96 byte、c0〜c23」（L1414、L2280）。c24〜c31 は set の base の鏡で、128 byte を c0 から置くと上書き（L1415、L2280） | 32 byte 分の配置の衝突 | **できる**。design §11.4 のとおり 96〜127 byte を c32〜c39 に置き `lwc` で読む |
| image view の `components`（`VkComponentMapping`、core） | 任意の swizzle（IDENTITY・ZERO・ONE・R/G/B/A） | `GhciTextureDescriptor` に swizzle の field が無い（L1264〜1280、0x1C は reserved で 0 必須） | 機能の欠け | **一部**。view は bind 時に決まり shader の compile 時に分からないので、slot ごとの swizzle を constant bank の driver の表に置き、shader が texture 命令の後に毎回 `sel` で並べ替える。gather（`texg.r/g/b/a` は component が即値、L2030）は実行時の 4 分岐。border color の swizzle との相互作用も shader で。費用は全 texture 命令に数命令 |
| `VkSamplerCreateInfo::unnormalizedCoordinates`（core、feature 無し） | 対応必須 | `GhciSamplerDescriptor` に field が無い（L1282〜1301、0x0F は reserved） | 機能の欠け | **一部**。sampler は bind 時に決まるので、slot ごとの flag を constant bank に置き、shader が `texsz` で大きさを得て正規化する分岐を毎回入れる。immutable sampler の時だけ compile 時に決まる |
| uniform texel buffer（`SampledBuffer`、core）・storage texel buffer（`ImageBuffer`、core）、`maxTexelBufferElements` | 65536 要素以上、format は buffer view で実行時に決まる | texel buffer 用の命令が無い（§12.5 の opcode map L2019〜2033）。buffer descriptor に format（L1311）はあるが、それを解く HW は無い | 機能を shader で作る必要 | **できる**。shader が `lw` で読み format を解く（uniform texel buffer の format は SPIR-V に無いので実行時の分岐）。texture slot を使う方法（gsi1_isa.h の `GSI1_TEXEL_BUFFER_WIDTH` 256 の 2D 化）は上の 16 slot の予算を食うので避けるのがよい |
| storage image（`STORAGE_IMAGE_BIT`、`STORAGE_IMAGE_ATOMIC_BIT`）、`maxPerStageDescriptorStorageImages` 4 | 必須の format（R8G8B8A8_UNORM/SNORM/UINT/SINT、A8B8G8R8 の同じ組、R16G16B16A16_UINT/SINT/SFLOAT、R32*、R32G32*、R32G32B32A32*）で load・store、R32_UINT/SINT は atomic | image の load/store 命令が無い（opcode map L2019〜2033）。§8.5.1 の Storage の列（L1436〜1484）は R8G8B8A8_SNORM・R32G32_SFLOAT などを No とする | 表の No は HW の制約でなく記述の問題 | **できる**。shader が descriptor から番地を計算し（linear か 8×8 Morton の tiled、§8.5.2 L1499〜1505）、`lw`・`sw`・`atom` で読み書き。fp16 の変換は `packh`/`unpackh`（CAP_FP16 に依らない、L2052〜2054）。tiled の計算の費用は大きいので storage 用の image は linear にする選択も可 |

### 2.2 vertex の入力

| 項目 | Vulkan 1.0 の要求 | GHCI の固定の値 | 不足の程度 | 隠せるか |
| --- | --- | --- | --- | --- |
| `maxVertexInputAttributes`（vec4 の 16 個） | 16 attribute、各 4 成分まで = 64 語 | VF が attribute を r0〜r59 に詰める。「Σ C_i ≤ 60、超えたら pipeline の作成を断る」（§12.10.3 L2294〜2298）。r60・r61 は VertexIndex・InstanceIndex（L2304〜2309） | 64 語に対し 60 語。vec4 × 16 で 4 語不足 | **できる**。超えた attribute は shader が `r60`（gl_VertexIndex）・`r61` から番地を計算して `lw` で読む（vertex pulling）。buffer の VA と stride は constant bank へ |
| 必須の `VERTEX_BUFFER_BIT` の format | R8・R8G8・R8G8B8A8 の UNORM/SNORM/UINT/SINT、B8G8R8A8_UNORM、A8B8G8R8_{UNORM,SNORM,UINT,SINT}_PACK32、A2B10G10R10_UNORM_PACK32、R16・R16G16・R16G16B16A16 の UNORM/SNORM/UINT/SINT/SFLOAT、R32・R32G32・R32G32B32・R32G32B32A32 の UINT/SINT/SFLOAT（Table 90〜94） | §8.5.1（L1438〜1484）の Vertex Buffer の列に在るのは R8*・R8G8*・RGBA8*・BGRA8_UNORM・R5G6B5・A1R5G5B5・R16 の SFLOAT 3 つ（CAP_FP16）・R32_{UINT,SINT,SFLOAT}・R32G32_SFLOAT・R32G32B32A32_{UINT,SFLOAT}。**無い**: R16/R16G16/R16G16B16A16 の UNORM・SNORM・UINT・SINT（12 個）、R32G32_{UINT,SINT}、**R32G32B32_{UINT,SINT,SFLOAT}**、R32G32B32A32_SINT、A8B8G8R8 の 4 個、A2B10G10R10_UNORM | 必須 28 個前後の欠け（R32G32B32_SFLOAT は実用上の頻度が高い） | **できる**。A8B8G8R8_* は R8G8B8A8_* と memory の並びが同じなので format を差し替え、R32G32B32A32_SINT は UINT で読んで bit をそのまま使う。他は vertex pulling（format は pipeline 作成時に分かるので shader に decode を焼ける）。R32G32_UINT/SINT を R32G32_SFLOAT で読む代用は、fetch が NaN・denormal の bit を保つか**未確認** |
| `maxVertexInputBindingStride` | 2048 | 動的な stride 0〜4095（L778）は足りる。PSO の静的な stride は binding 0〜3 だけ、8 bit（L1221） | 静的な field は不足だが動的で足りる | **できる**。Vulkan 1.0 の stride は pipeline の状態なので、driver が draw の直前に `CMD_BIND_VERTEX_BUFFERS` を pipeline の stride 付きで出す |
| `robustBufferAccess`（1.0 で唯一の必須 feature） | 範囲外の buffer の読み書き・vertex の fetch が範囲内の値か 0 を返し、fault しない | shader の `lw`/`sw` に範囲検査は無く、未 map の番地は page fault で session が止まる（§12.6 L2062）。VF の範囲外の振る舞いは書かれていない（`size_bytes` は L777 に在る） | VF は**未確認** | shader 側は **できる**（compiler が descriptor の size で検査を入れる。design §15.2 L525 の方針）。VF は **一部**（範囲外を 0 にする規定が無ければ、vertex pulling に検査を入れて代える） |

### 2.3 primitive・rasterization

| 項目 | Vulkan 1.0 の要求 | GHCI の固定の値 | 不足の程度 | 隠せるか |
| --- | --- | --- | --- | --- |
| `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN`（core、feature 無し） | 対応必須 | topology は POINT・LINE・LINE_STRIP・TRIANGLE・TRIANGLE_STRIP だけ。FAN は INVALID_OP（§8.2 L1231、「driver が index の展開で模す」と明記） | 機能の欠け | **できる**。非 indexed は CPU か compute で index を作る。indexed・indirect は compute で index と indirect の引数を作り直す（primitive restart も展開時に処理）。design §11.5 L415 の「indexed の fan は断る」は隠せる不足を断っている |
| `subPixelPrecisionBits`・`viewportSubPixelBits` | ≥4・≥0 | 記述無し | — | **未確認**（仕様担当に値を聞く） |
| `maxViewportDimensions`・`viewportBoundsRange` | (4096, 4096)・[−8192, 8191] | `CMD_SET_VIEWPORT` は float（L801〜806）。上限・guard band の記述無し。render area は 16 bit（L902〜903） | — | **未確認** |
| `pointSizeRange`・`lineWidthRange` | `largePoints`・`wideLines` が false なら [1, 1] | POINT_SIZE は無視・1.0 固定（L2322）、線幅 8 bit 固定小数（L1218） | 不足無し | — |

### 2.4 format（flag に依らない欠け）

Table 89〜95 の必須の欄（‡ など条件付きの欄を除く）と §8.5.1（L1436〜1484）を照らした。BLIT_SRC/DST は CAP_BLT（§3 の表）、sRGB・FP16・FFILT・ROP_PACK・BC/ETC2 の行は §3 の表へ回した。

| 項目 | Vulkan 1.0 の要求 | GHCI の固定の値 | 不足の程度 | 隠せるか |
| --- | --- | --- | --- | --- |
| **8 byte の整数・float の color attachment**: R32G32_UINT/SINT/SFLOAT、R16G16B16A16_UINT/SINT | COLOR_ATTACHMENT（＋BLIT_DST）必須（Table 93・94） | 表に在る 8 byte の color attachment は R16G16B16A16_SFLOAT（CAP_FP16）だけ。R32G32_SFLOAT は Color Attach **No**（L1461）、他の 4 つは表に無い（L1436〜1484）。CAP_ROP_PACK（Path 2）は「32 bit float と整数」の packer で、8 byte 整数の扱いは書かれていない（L2489） | 5 format の欠け | **できない**。8 byte の texel を書ける別の format が無い（fp16 は bit を保てない、R32_UINT 2 枚に分けても texel の間隔が 4 byte になる）。shader から `sw` で直に書くと重なる fragment の順序（API の順）が保てない |
| **A2B10G10R10_UNORM_PACK32** | SAMPLED・BLIT_SRC・FILTER_LINEAR・COLOR_ATTACHMENT・BLIT_DST・**BLEND**・VERTEX_BUFFER・UNIFORM_TEXEL_BUFFER（Table 92） | 表に無い（L1436〜1484） | format 全部の欠け | 全体は **できない**。blend は固定機能が要り shader で作れない。sampling は R32_UINT として `texf` で 4 点を読み shader で解いて filter（**一部**、重い）。vertex は pulling で **できる** |
| A2B10G10R10_UINT_PACK32 | SAMPLED・BLIT_SRC・COLOR_ATTACHMENT・BLIT_DST・UNIFORM_TEXEL_BUFFER | 表に無い | format の欠け | **一部**。sampling は R32_UINT として読み shader で分ける（nearest だけなので可）。color attachment は R32_UINT（CAP_ROP_PACK）として shader で詰めて書けるが、成分ごとの `colorWriteMask`（R だけ等）は表せない |
| R16_UINT/SINT、R16G16_UINT/SINT | SAMPLED・BLIT_SRC・COLOR_ATTACHMENT・BLIT_DST・VERTEX・UNIFORM_TEXEL_BUFFER | 表に無い | format の欠け | **できる**。同じ大きさの R8G8_UINT・R8G8B8A8_UINT として読み書きし（sampling は nearest のみ要る）、shader で 2 byte を繋ぐ・分ける。write mask は byte の組へ写せる。color attachment は CAP_ROP_PACK が要る |
| R16G16B16A16_UINT/SINT、R32G32_UINT/SINT の sampling | SAMPLED（nearest） | 8 byte の整数の texture format が無い | format の欠け | **できる**。R32_UINT の幅 2 倍の view として `texf` 2 回（address mode は shader が整数座標で計算）。R32G32_SFLOAT として読む代用は bit が保たれるか**未確認** |
| R32G32B32A32_SINT | SAMPLED・COLOR_ATTACHMENT・BLIT・VERTEX・texel buffer | 表に無い（UINT は在る、L1462） | 名前の欠け | **できる**（UINT と bit が同じ。color は CAP_ROP_PACK） |
| A8B8G8R8_{UNORM,SNORM,UINT,SINT,SRGB}_PACK32 | R8G8B8A8_* と同じ機能 | 表に無い | 名前の欠け | **できる**（little endian で memory の並びが R8G8B8A8_* と同じ。format の番号を差し替え） |
| B10G11R11_UFLOAT_PACK32、E5B9G9R9_UFLOAT_PACK32 | SAMPLED・BLIT_SRC・**FILTER_LINEAR**（B10G11R11 は UNIFORM_TEXEL_BUFFER も）（Table 95） | 表に無い | format の欠け | **一部**。R32_UINT として `texf` で読み shader で解く。linear filter・mip・address mode・border を shader で全部作る（1 sample に `texf` 4〜8 回）。正しくはできるが重い |
| B4G4R4A4_UNORM_PACK16 | SAMPLED・BLIT_SRC・FILTER_LINEAR（Table 89） | 表に無い | format の欠け | **一部**（同上。R8G8_UINT として読み 4 bit ずつ解いて shader で filter） |
| depth/stencil の片方の aspect の copy（D24_UNORM_S8_UINT・D32_SFLOAT_S8_UINT の DEPTH か STENCIL だけ） | `vkCmdCopyBufferToImage`・`ImageToBuffer`・`CopyImage` で aspect 別、buffer 側は詰めた形 | copy の command に aspect の選択が無い（L1053〜1077）。D24S8 は 32 bit に混ぜ、D32S8 は 64 bit に混ぜた配置（L1507） | 機能の欠け | **できる**（compute で詰め替え。tiled の Morton の番地も shader で計算） |

### 2.5 command（必須の command に対応する packet が無い・制約が固定）

1.0 の必須の command と §7.2 の opcode 表（L712〜751）を照らした。Draw*Indirect・ExecuteCommands・copy 系の flag は §3 の表。

| 項目 | Vulkan 1.0 の要求 | GHCI の固定の値 | 不足の程度 | 隠せるか |
| --- | --- | --- | --- | --- |
| `vkCmdClearAttachments`（部分の rect・複数の layer） | 必須 | packet 無し。clear は `CMD_BEGIN_RENDER_PASS` の flag と LoadOp だけ（L908〜911、L1390〜1397） | packet の欠け | **できる**（render pass の中で rect の quad を描く internal pipeline。stencil は REPLACE、layer は base 番地をずらして 1 枚ずつ）。design §11.5 L427 の「部分の rect は断る」は隠せる |
| `vkCmdClearColorImage`・`vkCmdClearDepthStencilImage` | 必須（attachment にできない format も対象） | packet 無し | packet の欠け | **できる**（attachment にできる物は clear だけの render pass、他は FILL_BUFFER か compute で packed の値を書く） |
| `vkCmdUpdateBuffer` | 必須、65536 byte まで | packet は 255 dw まで（L701） | packet の欠け | **できる**（staging から copy、または compute） |
| `vkCmdSetEvent`・`ResetEvent`・`WaitEvents` | 必須 | packet 無し。`CMD_FENCE`/`CMD_WAIT_FENCE` は host の物理番地・local の PA（L952、L966） | packet の欠け | **できる**（design §11.5 の FENCE と WAIT_FENCE。物理番地なので ring を kernel が書く U7 が前提） |
| `vkCmdCopyQueryPoolResults`・`vkCmdResetQueryPool` | 必須（32/64 bit、WAIT・PARTIAL・AVAILABILITY） | packet 無し。query の結果は 64 bit 固定（L1139〜1140） | packet の欠け | **できる**（compute） |
| `vkCmdNextSubpass`、input attachment（`InputAttachment` capability、`maxPerStageDescriptorInputAttachments` 4） | 必須 | render pass の記述は 1 subpass・8 color・1 DS・1 resolve の固定（L1338〜1386） | 構造の欠け | **できる**（subpass ごとに render pass を区切り store→load、input attachment は shader が番地を計算して読む。design §11.5 L414） |
| `pResolveAttachments`（color attachment ごと） | 必須（MSAA が在る時） | 自動の resolve は color attachment 0 だけ（L1398、L2462）。他は `CMD_RESOLVE_IMAGE`（CAP_BLT） | 構造の制約 | **できる**（CAP_BLT が無ければ shader の resolve） |
| `vkCmdCopyBuffer` の byte 単位 | `VkBufferCopy` の srcOffset・dstOffset・size に揃えの要求無し | `size_bytes` は 4 byte 揃え（L992）。番地の揃えは記述無し。shader の memory も語単位（`lw`/`sw` は bit 1:0 を無視、§12.8 L2121） | 端の 1〜3 byte | **できる**（端を compute の `atom.and`/`atom.or` で byte を書く。番地の揃えの要否は**未確認**） |
| `vkCmdBlitImage`（3D の z の拡大縮小、鏡像） | 必須（format が BLIT を持つ時） | `CMD_BLIT` は 2D の slice だけ、座標は 16 bit の min/max（L1016〜1029）。x1 < x0 の鏡像の可否は**未確認** | 構造の制約 | **できる**（graphics の draw で） |
| 範囲の注: render pass の attachment の `base_array_layer` | `maxImageArrayLayers` を 2048 と答えるなら layer 2047 の view を attachment に | `base_array_layer` は uint8（0〜255、L1349・L1358・L1367）、§8.6 は 2048（L1521） | 内部の不一致（Vulkan の最小 256 は満たす） | **できる**（`image_va` を layer の先頭へずらして 0 を渡す） |

### 2.6 limit の数値（§8.6 L1515〜1544 と本文）: 足りている物

固定の値で Vulkan の最小を下回る limit は無い（§2.1〜2.3 の構造上の物を除く）。確かめた主な物:
maxImageDimension1D/2D/Cube 16384・3D 2048・ArrayLayers 2048（≥4096/256/256）、maxTexelBufferElements 2^26、maxUniformBufferRange 65536、maxStorageBufferRange 2^32−1、maxPushConstantsSize 128、maxMemoryAllocationCount 4096、maxSamplerAllocationCount 4000、per-stage UBO 16・SSBO 16・storage image 8（shader の実装なので任意）、maxVertexInputBindings 16・AttributeOffset 524287・BindingStride 4095、maxVertexOutputComponents・maxFragmentInputComponents 64（POS は別、L2330）、maxComputeWorkGroupCount 65535、Invocations 256、Size {256,256,64}、min/maxTexelOffset −8/7（L2183）、maxSamplerLodBias（8.8 で ±127、L1295）、mip 15 段（16384 に足りる）、maxFramebufferWidth/Height 16384・Layers 2048、maxViewports 1、maxSampleMaskWords 1、maxDrawIndexedIndexValue（32 bit の index、L786）。
minUniform/Storage/TexelBufferOffsetAlignment・bufferImageGranularity・nonCoherentAtomSize は driver が選ぶ値（GHCI は 4 KiB の page、L1085）で満たせる。

## 3. 別表: flag が 0 の時だけ欠ける物（短く）

| flag（節・行） | 0 の時に欠ける Vulkan 1.0 の必須 | 隠せるか |
| --- | --- | --- |
| CAP_INDIRECT_DRAW（L253） | `vkCmdDrawIndirect`・`DrawIndexedIndirect`（core の command）。`DispatchIndirect` は §7.2 L734 で「NUM_SU > 0」、L253 では CAP_INDIRECT_DRAW と書かれ食い違う | 一部（compute が draw の packet を書き CAP_SECONDARY_RING で実行する案。両方 0 なら submit を分けて CPU が読む以外に無い） |
| CAP_SECONDARY_RING（L254） | `vkCmdExecuteCommands` の HW 実行 | できる（記録の展開、design D6） |
| CAP_BLT（L265） | copy・blit・resolve・fill の packet、必須の BLIT_SRC/BLIT_DST の bit | できる（compute・graphics の internal pipeline。design U10 は今は断る） |
| CAP_MSAA（L267） | framebuffer/sampledImage の Color・Depth・Stencil・NoAttachments の SampleCounts に 4（1\|4 が必須） | できない |
| MAX_MRT < 4（L292） | `maxColorAttachments`・`maxFragmentOutputAttachments` 4 | できない |
| CAP_LDS_SIZE_KB < 16（L294、KC705 は 4） | `maxComputeSharedMemorySize` 16384 | 一部（Workgroup の変数を global memory に置き `bar` と `atom` で模す。重い） |
| CAP_SRGB（FMT0[9]、L304） | R8G8B8A8_SRGB・B8G8R8A8_SRGB・A8B8G8R8_SRGB の SAMPLED・FILTER・COLOR・BLEND | できない（線形空間の blend）。sampling だけは shader の変換で一部 |
| CAP_TEX_FFILT（L261） | R16_SFLOAT・R16G16_SFLOAT・R16G16B16A16_SFLOAT の FILTER_LINEAR | 一部（shader の手の filter） |
| CAP_FP16（L256） | 上の 3 つの fp16 の format 全部（SAMPLED・COLOR・BLEND・STORAGE・VERTEX）。§8.5.1 は Guard を CAP_FP16 とする | 一部（sampling・vertex は shader で解ける。COLOR＋BLEND はできない） |
| CAP_ROP_PACK（L262） | 整数（R8・R8G8・RGBA8 の UINT/SINT、R32_UINT/SINT、R32G32B32A32_UINT）と R32_SFLOAT・R32G32B32A32_SFLOAT の COLOR_ATTACHMENT | できない |
| CAP_BC と CAP_ETC2 の両方が 0（L301〜302） | BC・ETC2・ASTC の少なくとも 1 系統の SAMPLED・FILTER・BLIT_SRC | 一部（ETC2 を shader で decode して filter。重い）。§8.5.1 は ETC2 を「Mandatory」（L1475〜1484）と書き FMT0[7] と食い違う |

dualSrcBlend・logicOp・independentBlend（MRT_BLEND）・subgroup・task/mesh は 1.0 で任意なので不足ではない。

## 4. design.md への補足（判断の材料）

- §11.4 L403・§20 U8 の「`maxDescriptorSetSampledImages`・`Samplers` も 16。Vulkan 1.0 の最小 96 に足りない」は、descriptor を draw ごとに slot へ写す設計なら 96 と答えてよい。本当に固定で破れるのは「1 pipeline の全 stage の合計が 16」の所（per-stage 16 を VS と FS が同時に使う時、組の数が 16 を超える時）。
- §11.5 の「indexed の fan は断る」「ClearAttachments の部分の rect は断る」「CAP_BLT = 0 の copy は断る」は、GHCI の固定の不足ではなく driver で隠せる物（v1 の範囲の判断）。
- U8 の一覧に無い固定の不足: vertex の attribute の 60 語の上限、必須の vertex format の欠け、8 byte の color attachment、A2B10G10R10、B10G11R11・E5B9G9R9・B4G4R4A4、swizzle・unnormalizedCoordinates の欠け。

## 5. 仕様担当への提案に使える要点

1. texture slot の 16 は draw 全体で共有されるので、Vulkan の「stage ごと 16」を満たさない。slot の index を stage ごとに分ける（例: `sub[4]` か stage ごとの set 0/1 の base）か、slot を 32 以上にするか、descriptor の番地を register で渡す bindless の形を足してほしい。sampler を texture と独立の index にできれば組の数の問題も消える。
2. 8 byte の color attachment（R32G32_UINT/SINT/SFLOAT、R16G16B16A16_UINT/SINT）と A2B10G10R10_UNORM/UINT（blend 付き）の ROP の format が無い。Path 2 の packer の対象と §8.5.1 の表に足してほしい。
3. `GhciTextureDescriptor` の reserved（0x1C）に component swizzle（4×3 bit）、`GhciSamplerDescriptor` の reserved（0x0F）に unnormalizedCoordinates の bit を置ければ、shader の毎回の分岐が要らない。
4. vertex の attribute の register の上限 60 語（L2298）を 64 にする（VertexIndex・InstanceIndex を special operand に移す等）か、必須の vertex format（R16 の UNORM/SNORM/UINT/SINT、R32G32_UINT/SINT、R32G32B32_*、A2B10G10R10）を VF に足すと vertex pulling が要らなくなる。VF の範囲外の fetch を 0 にする規定（robustBufferAccess）も明記してほしい。
5. 記述の食い違いと欠け: subPixelPrecisionBits・viewportBoundsRange・maxViewportDimensions の値、`DISPATCH_INDIRECT` の guard（L253 と L734）、ETC2 の Mandatory と FMT0[7]（L1475 と L302）、`lw`/`sw` の「bit 1:0 を無視」（L2121）と misaligned の MEMORY_FAULT（L2063）、`base_array_layer` uint8 と maxImageArrayLayers 2048（L1349 と L1521）、push constant の 96 と 128（L1414 と L1525）。
