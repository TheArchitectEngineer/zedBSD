# GHCI レビュー 第 13 版（ghci.md cf8be08・gsi1_isa.h bea1ef9 に対する新しい疑問 N47〜N60、driver の設計レビューで見つけた物、2026-10-28 05:08）

- 対象: `git -C /home/awe/amaterrace-gpu show cf8be08:docs/ghci.md`（2782 行）、同じ commit の `include/ghci_regs.h`、`include/ghci_cmd.h`（1a1fa85）、`include/gsi1_isa.h`（bea1ef9）。main の先頭 70a4f1e までに docs/ghci.md・include の差は無い。
- 読んだ回答: `docs/ghci-review-reply.md`（0018f88）、1 行目「# GHCI Review Reply - Version 12.0 (2026-10-28) - Commit: bea1ef9 (Final Signoff: N01〜N46 Fully Resolved)」、末尾 DONE。N01〜N46 は解決のまま。
- 経緯: 第 12 版で疑問 0 と合意した後、zedBSD の driver の設計（版 4.1）の敵対的レビューで、文書の未定義・矛盾が新しく見つかった。行番号は cf8be08 の物。

## 要約

- 新しい疑問 **14 件: C 0・H 1・M 6・L 7**（N47〜N60）。
- **driver の実装**: N47 は render pass の attachment の番地が決められないので、答えまで driver は「attachment は linear、image_va は subresource の先頭、mip_level = base_array_layer = 0、layers = 1」に限る（推しのとおりなら直さずに済む）。他は下の「driver の仮の扱い」で進める。答えで変われば driver を直す。

## 残る疑問

重さ: C＝その部分の driver が書けない、H＝その部分を書く前に答えが要る、M＝仮の扱いで進められるが答えが要る、L＝文書の整理。

| ID | 重さ | 疑問 | 根拠 | 推し | driver の仮の扱い |
| --- | --- | --- | --- | --- | --- |
| N47 | H | render pass の attachment（color・depth/stencil・resolve）の image の配置が無い。linear か tiled かの flag、layer の間の pitch（slice pitch）が無く、`mip_level`・`base_array_layer` から `image_va` を起点に番地をどう求めるかも書かれていない。§8.5.2 の tiled と subresource の配置は texture（`GhciTextureDescriptor.flags[0]`）と転送の packet（`[8] tiled`）にしか結び付いていない。`layers`（1..2048）の layered rendering も pitch が無いと番地が決まらない | 1343〜1368（attachment の struct）、1371〜1376（width・height・layers）、1499〜1505（tiled・subresource の配置）、2448 付近（多 sample は linear） | 次のどちらかを本文に明記: (a) attachment は常に linear の行（64 byte 揃えの `row_pitch_bytes`）、`*_va` は描く subresource（mip・layer）の先頭の番地で、`mip_level`・`base_array_layer` は 0 を書く（予約）、`layers` は 1 だけ。(b) attachment の struct に tiled の flag と `slice_pitch_bytes` を足し、CP が §8.5.2 の式で mip・layer の番地を求める（式を書く）。推しは (a)（driver の記録が単純で、tiled の描画は将来の CAP で足せる） | (a) の形に限る |
| N48 | M | session の root の page table の大きさと CP_HEAP の再利用。628 行は session 0 の root を「4 KiB root L1」、1701 行は VA 32 bit の root を 2048 entry = 16 KiB と書く。`CMD_SESSION_CLOSE` は「intermediate page tables を返す」（1887）とあり root を返すか書かれていない。566 行の「allocation pointer が CP_HEAP_BASE に戻る」は bump の確保に見え、CLOSE で返った table が再利用されるか、16 KiB の連続の root が断片化で取れなくなり得るかが分からない。driver は MMU_OOM を起こさないよう CP_HEAP を上限で数えたいが、その上限が決まらない | 566、628、1680〜1695、1699〜1708、1887 | 本文に、(1) root の大きさ（VA 32 bit 16 KiB、40 bit 8 KiB と中間の table）、(2) SESSION_CLOSE・VM_UNMAP が root と中間の table を返し再利用すること、(3) driver が CP_HEAP の大きさを決める式（session 数 × root + 4 GiB を覆う L0 の数 × 4 KiB などの上限）を書く | root は reset まで返らないと見なし、上限に root 16 個分の余裕を足して数える。勘定が尽きたら新しい session を断る |
| N49 | M | CAP_PROT = 0 の `CMD_SESSION_CLOSE` の作用と、閉じた session への `CMD_VM_UNMAP`・`CMD_SESSION_CLOSE` の結果。PROT = 0 では VM_MAP の session_id は無視され共有の table に入る（1852）が、CLOSE がその session の PTE を消すのか（session_id を無視しているので CP は区別できないはず）が書かれていない。閉じた session への VM_MAP は kind 1 の 0x21（1861）だが、VM_UNMAP・CLOSE は書かれていない | 1852、1858〜1861、1887 | (1) PROT = 0 の SESSION_CLOSE は共有の table の PTE を消さない（driver が CLOSE の前に VM_UNMAP する）と明記。(2) 閉じた session への VM_UNMAP は fault 無しの no-op（PROT = 0 は共有の table から消す）、閉じた session への CLOSE も no-op と明記 | 全 mapping の VM_UNMAP を SESSION_CLOSE より前に同じ順で置く。閉じた session への操作は置かない |
| N50 | M | `FAULT_INFO` の bit の配置が ghci.md と ghci_regs.h で食い違う。ghci.md は [15:8] SESSION_ID・[27:24] SHADER_STAGE・[31:28] SYNDROME（4 bit）、ghci_regs.h は `GHCI_FAULT_SYNDROME(info)` = [31:16]。ghci.md 自身の syndrome の値 0x0001（603・606・942）・0x0010（1858）・0x0020（1859）・0x0021（1861）は 4 bit に入らない。kind の名前も、ghci.md の kind 4 WATCHDOG・5 AXI_ERROR・10 MEMORY_FAULT（424）と、1858 の「Kind 10 PERM_FAULT」・ghci_regs.h の 4 TIMEOUT・5 FENCE_ERROR・10 PERM_FAULT が違う | 408〜431、603〜606、942、1858〜1861、ghci_regs.h 147〜163 | 配置を一つに決めて両方に書く。推し: [3:0] KIND・[7:4] UNIT・[15:8] SESSION_ID・[19:16] SHADER_STAGE・[31:20] SYNDROME（12 bit、0x001・0x010・0x020・0x021 が入る）。kind の名前を ghci.md の物（4 WATCHDOG・5 AXI_ERROR・10 MEMORY_FAULT か PERM_FAULT のどちらか一つ）に揃える | KIND・UNIT・SESSION_ID だけを読み、stage と syndrome は log に生の値で出す |
| N51 | M | `CMD_DRAW_INDIRECT` の INDEXED の位置が 2 つの形で違う。7 dword の形は dword 6 の bit 0、8 dword（count buffer）の形は header の `flags[0]`。7 dword の形で header の flags は何か、8 dword の形に dword 6 の flags は無いのか（dword 6〜7 は count buffer の番地）が分からない | 866〜885 | どちらの形も header の `flags[0]` = INDEXED に揃え、7 dword の dword 6 は予約（0）にする。または両方の位置を明記 | 7 dword は dword 6 の bit 0、8 dword は header の flags[0] に書き、他方は 0 |
| N52 | L | `DEVICE_CAPS[3]` CAP_INDIRECT_DRAW の説明は「indirect dispatch も」と書くが、§7.2 の表の `CMD_DISPATCH_INDIRECT`（0x2E）の guard は `NUM_SU > 0` | 253、734 | どちらかに揃える | 0x2E は CAP_INDIRECT_DRAW と NUM_SU > 0 の両方がある時だけ使う |
| N53 | M | §8.5 は「Vulkan 1.0 core の必須に完全に適合」と書くが、§8.5.1 の表に Vulkan 1.0 の必須の format が無い。例: `R32G32B32_SFLOAT`（vertex buffer）、`A2B10G10R10_UNORM_PACK32`、`B10G11R11_UFLOAT_PACK32`、`R16G16B16A16_SFLOAT` が `CAP_FP16` に依存している（必須の sampled・color attachment）、`R32G32_UINT`・`R32G32_SINT`、16 bit の UNORM・UINT・SINT | 1431〜1432、1436〜1485 | 表に足すか、1432 の「完全に適合」を外して不足を書く。少なくとも vertex の `R32G32B32_SFLOAT`（3 成分の float の頂点は普通に使われる）は足してほしい | U8（ユーザーの決定）により、欠ける format は答えから落とし、使われた時に断って log |
| N54 | M | `RESIDENT_WARPS[15:0]`「warps in flight」が、今動いている warp の数（変わる値）か、同時に動ける warp の数（容量）か。`warp` の operand は「0 to the GPU's resident warps − 1」（1967）で、scratch の大きさ（2281〜2282 の `w × bytes_per_warp`）は容量で決まる | 225、1967、2281〜2282 | 「[15:0] 同時に resident になれる warp の最大数（全 shader unit の和、定数）」と明記 | 容量と読み、scratch を `RESIDENT_WARPS[15:0] × bytes_per_warp` で取る |
| N55 | L | 1789 行は PA_BITS を `EXT_CAP_FEATURE[11:8]` と書くが、§3.2.2 は [7:4]（[11] は CAP_LOGIC_OP） | 278〜283、1789 | 1789 行を [7:4] に | [7:4] |
| N56 | L | §2 の「Reads have no side effects」と、`TIMESTAMP_LO` の読みが上位を latch する事が食い違う | 165〜171 | 「TIMESTAMP_LO の latch を除き」と書く | LO を先に読む |
| N57 | L | 本文に出る `IRQ_STATUS.IRQ_HALTED`（§5.2 の表）と `GHCI_FAULT_KIND_INVALID_ARG`（`CMD_FLIP` の display_id）が §3.4 の bit の表・§3.5 の kind の表に無い | 559、1152、387〜400、412〜424 | 表に足すか、本文を在る物（HALTED は STATUS を poll、INVALID_ARG は kind 1）に直す | HALTED は STATUS を poll、display_id は 0 だけ |
| N58 | L | 標準の sample 位置が 1 と 4 sample だけで、2 sample が無い（`CAP_MSAA_4X` は 1・2・4 sample を描く） | 2408〜2411 | 2 sample の位置（Vulkan の標準は (0.75, 0.75)・(0.25, 0.25)）を足す | `standardSampleLocations` は 2 sample の位置が決まるまで false |
| N59 | L | 決まっていない小さな点: (1) `CMD_BIND_DESCRIPTOR_TABLE` の `descriptor_count` の単位（descriptor の数か byte か、CP が何に使うか）、(2) 転送の packet（0x41・0x42・0x45・0x46）の座標・大きさが圧縮 format で texel か block か、(3) vertex fetch が buffer の範囲外を読んだ時（robustBufferAccess）の値 | 788〜795、1001〜1078、769〜779 | 各点を本文に | (1) その set の descriptor の数、(2) texel（4 の倍数）、(3) driver は範囲を検査しない（robustBufferAccess の vertex は shader の側で扱えない物を断る） |
| N60 | L | §1.5 は「OS の driver は C の header（ghci_regs.h）を写す」と書くが、ghci_regs.h には §3.1 と食い違う旧い定義が残る（例: 211〜213 行の `GHCI_RING_BASE 0x050u`・`GHCI_RING_SIZE 0x054u`・`GHCI_RING_WPTR 0x058u`。§3.1 では 0x050 は CP_HEAP_BASE） | 136〜140、ghci_regs.h 211〜213 | 旧い定義を消すか「pre-1.0、使わない」と明記（gsi1_isa.h の N45 と同じ扱い） | driver は header を写さず ghci.md の本文から値を起こす |

## 解決した N

- N01〜N46（第 12 版まで、回答 第 12.0 版）。

DONE
