# GHCI レビュー 第 15 版（ghci.md 20bf888 に対する、回答 第 14.0 版の確かめと新しい疑問 N73〜N77、2026-10-28 06:25）

- 対象: `git -C /home/awe/amaterrace-gpu show 20bf888:docs/ghci.md`（2831 行）、同じ commit の `include/ghci_regs.h`・`include/gsi1_isa.h`（`include/ghci_cmd.h` は cb937e6 から変わらず）。差は `git diff cb937e6 20bf888 -- docs/ghci.md include/` で読んだ。行番号は 20bf888 の物。
- 読んだ回答: `docs/ghci-review-reply.md`（20bf888）、1 行目「# GHCI Review Reply - Version 14.0 (2026-10-28) - Commit: 20bf888 (Resolving N62〜N72 and Final Signoff Readiness)」、100 行、md5 c0105a52f3309fa1151cf308ecfa5cfe、末尾 DONE。
- 番号: 新しい疑問は N73 から付ける。第 14 版の一部の続きは新しい番号（N73〜N76）にした。

## 要約

- 第 14 版の 11 件: **解決 7**（N62・N64・N66・N68・N69・N70・N71）、**一部 4**（N63 → N73、N65 → N74、N67 → N75、N72 の (2) → N76。N72 の (1)・(3) は解決）。
- 新しい疑問 **5 件: C 0・H 0・M 2・L 3**（N73〜N77）。driver の実装を止める物は無い。M の 2 件（N74・N75）は Vulkan 1.0 の適合の穴で、答えまで driver は「断って log」で進める。
- N62 の確かめ: texture の R の形で `w1[9:6]` は rs3[6:5]（R の形に rs3 は無い）と n1・a1（float の source modifier、2229 行で texture には使わないと明記）で、texel offset（[25:22]・[29:26]・[13:10]）・`.sat`（[14]）・sub（[21:17]）と重ならない。driver は texture の slot と sampler の slot を別々に compile する。

## 残る疑問

重さ: C＝その部分の driver が書けない、H＝その部分を書く前に答えが要る、M＝仮の扱いで進められるが答えが要る、L＝文書の整理。

| ID | 重さ | 疑問 | 根拠（20bf888 の行） | 推し | driver の仮の扱い |
| --- | --- | --- | --- | --- | --- |
| N73 | L | N63 の続き。kind は 1 対 1 になったが、原因の書き分けが 3 か所で食い違う: (a) secondary の中の特権の command は 418 行・407 行・ghci_regs.h の `GHCI_FAULT_KIND_PRIVILEGED_OP 3u` では kind 3、421 行・606 行では kind 6。(b) secondary の入れ子は 418 行で kind 3、604・607 行で kind 1（syndrome 0x0001）。(c) 「permission violation」が kind 6（421）と kind 10（424・1907・2110）の両方にあり、`GHCI_VM_FLAG_WRITE` の無い page への write がどちらか読めない。(d) 「`SYNDROME[20]`」（424・ghci_regs.h の注）は FAULT_INFO の bit 20（= SYNDROME の bit 0）の意味と思うが、SYNDROME は [31:20] の 12 bit なので `SYNDROME[20]` という bit は無い | 407、418、421、424、440、604〜607、1907、2110、ghci_regs.h 152〜172 | (a) kind 6 に揃え、`GHCI_FAULT_KIND_PRIVILEGED_OP` を 6 の別名に移す（407・418 から外す）。(b) kind 1 に揃える（418 から外す）。(c) kind 6 は session・特権の違反だけ、VM の flag（READ・WRITE・EXEC）の違反は kind 10、と書く。(d) 「FAULT_INFO[20]（SYNDROME[0]）: 0 = read、1 = write」と書き、kind 10 の VM の flag の違反にも同じ bit を使うかを書く | driver は secondary を使わない（記録は primary に展開）。kind 3 で UNIT = CP は driver の誤り、kind 6・10 はどちらも client の fault で、扱いは同じ（どの fault も reset、犯人の session を sticky の error）。read・write は FAULT_INFO[20] で log に出すだけ |
| N74 | M | N65 の続き。1342 行で「texture と sampler は VS と FS の和で 16 まで」になったが、(a) Vulkan 1.0 の `maxPerStageDescriptorSamplers`・`maxPerStageDescriptorSampledImages` の最小は stage ごとに 16 で、`maxDescriptorSetSamplers`・`maxDescriptorSetSampledImages` の最小は 96。VS が 16・FS が 16 の別の texture を使う pipeline は Vulkan 1.0 で合法なので、和で 16 の hardware はこの値を正しく答えられない（16 未満を答えると適合しない、16 を答えると作成で断る pipeline が出る）。(b) 本文が食い違う: 1436 行は今も「16 static slots fully satisfy Vulkan 1.0」、§8.6 の 1565・1568 行は「16 ... slots per pipeline stage」、§8.6 に `maxDescriptorSet*` の行が無い | 1340〜1342、1433〜1436、1565〜1568 | stage ごとに表を分ける: 例えば FS の texture slot k は set 0 の k + 16、sampler slot k は set 1 の k + 16 を読む（表は 32 個、VS は 0〜15）、または PSO に stage ごとの slot の base を置く。hardware を変えないなら、1436 行の主張を外し、§8.6 に「pipeline の和で 16、Vulkan 1.0 の stage ごとの 16 と set の 96 を満たさない」と書く | 16 を答え、VS と FS の和が 16 を超える pipeline は作成で断って log（Vulkan 1.0 で隠せない不足の一覧に載せる）。`maxDescriptorSet*` は 96 を答え、和の検査は pipeline の作成で行う |
| N75 | M | N67 の続き。1519 行は `B4G4R4A4_UNORM_PACK16`・`A8B8G8R8_*_PACK32`・`A2B10G10R10_UINT_PACK32`・`E5B9G9R9_UFLOAT_PACK32` を「Vulkan 1.0 で optional」と書くが、Vulkan 1.0 の Required Format Support の表（sub-byte と 32 bit の packed の表）ではどれも必須の feature を持つ: B4G4R4A4_UNORM_PACK16 は SAMPLED_IMAGE・BLIT_SRC・SAMPLED_IMAGE_FILTER_LINEAR、A8B8G8R8_{UNORM,SNORM,UINT,SINT,SRGB}_PACK32 は R8G8B8A8_* と同じ組、A2B10G10R10_UINT_PACK32 は SAMPLED_IMAGE・COLOR_ATTACHMENT・BLIT、E5B9G9R9_UFLOAT_PACK32 は SAMPLED_IMAGE・FILTER_LINEAR（この表は repo の外。こちらの読み） | 1443（「fully compliant」）、1519 | (a) A8B8G8R8_*_PACK32 は little endian で R8G8B8A8_* と同じ byte の並びなので、texture descriptor の `format`（0x0E）と attachment の format に 51〜57 を受け、37〜43 と同じに扱う、と書く（hardware の変更は要らない）。(b) B4G4R4A4・A2B10G10R10_UINT・E5B9G9R9 は表に足す（hardware の decode）か、1443 の「fully compliant」を外して不足を書く。1519 の「optional」の文は直す | (a) driver が A8B8G8R8_*_PACK32 を 37〜43 の code に置き換えて descriptor・PSO に書く。(b) の 3 つは答えから落とし、使われた時に断って log |
| N76 | L | N72 (2) の続き。2168・2110 行の「strict alignment checking is enabled via `CTRL`」の bit が §3.3 の CTRL（326〜335、[31:7] は reserved）と ghci_regs.h に無い。また「bits 1:0 are ignored」なら 4 byte の境界を跨ぐ word の access は起きない | 326〜335、2110、2168 | `lw`・`sw` は常に bits 1:0 を無視し fault しない、と書き、strict の文を消す（kind 10 の misaligned は `ld.64`・`sd.64` の 8 byte の境界などに限るならそう書く）。CTRL に bit を置くなら §3.3 と ghci_regs.h に番号と reset の既定を書く | compiler は揃った番地だけを出し、CTRL の未定義の bit は書かない |
| N77 | L | 文書の整理: (a) §12.4 の word1 の表（2046〜2053）と §12.5 の custom-1 の行（2077「slot = sub[3:0]」）に texture の R の形の sampler slot `w1[9:6]` が無い。§8.4 の 1433〜1435 行は今も「combined slot index」「set1_base_va + slot × 16」。(b) operand 120〜122 は `qstat`・`sastat`・`qtag` から 120 `vtx_idx`・121 `inst_idx`・122 reserved に変わったが、1964・2101 行は今も `qstat`・`sastat`・`qtag` を書く（命令か operand か）。operand 120・121 を VS 以外（FS・CS・MS）で読んだ値も無い。(c) ETC2・EAC の行の Guard（1508〜1517）は今も「Mandatory」で、1520 行の `CAP_ETC2` と合わない | 1433〜1435、1508〜1520、1964、2024〜2026、2046〜2053、2077、2101 | (a) §12.4 に「texture の R の形: [9:6] sampler slot」を、2077 行と 1433〜1435 行に sampler slot を足す。(b) systolic の operand は無い（122〜127 reserved）と書き、1964・2101 行をそれに合わせる。VS 以外の 120・121 は 0 と書く。(c) Guard を `CAP_ETC2` に | (a) gsi1_isa.h の `GSI1_W1_SAMP_SLOT_SHIFT 6u` を正、(b) 120・121 は VS だけで読む、(c) `CAP_ETC2` を正 |

## 解決した N

- N01〜N61（第 13 版まで）。
- 今回: N62（sampler slot は `w1[9:6]`、gsi1_isa.h 240 行、1340・2204・2321 行。「1:1 mode」は無くなった。§12.4 などの残りは N77）、N64（CP_HEAP の式、1930〜1933 行）、N66（operand 120・121、§12.3 2024〜2025、§12.10.3 2349〜2358。属性は r0〜r59 の 60 語）、N68（2 sample の位置、2457 行）、N69（IRQ_HALTED、401 行と ghci_regs.h）、N70（`CMD_DISP_SET` は 1 + 2N、751・1146 行）、N71（§8.6 の 4 行）、N72 の (1)（`CAP_ETC2` を正。Guard の列は N77）と (3)（push constant 128 byte、2327 行）。
- 一部（続きは上）: N63（kind の 1 対 1 は解決、書き分けは N73）、N65（和で 16 の明記は解決、適合と本文は N74）、N67（R32G32B32_UINT・SINT・R32G32B32A32_SINT と R16G16B16A16_SFLOAT の Guard は解決、残りは N75）、N72 の (2)（N76）。

DONE
