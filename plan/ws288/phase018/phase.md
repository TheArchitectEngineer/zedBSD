<!-- awesome-plan project=zedbsd record=ws288-p018 -->

# ws288-p018 CP の firmware の Vulkan の object model と draw/dispatch

- 親: [WS288](../ws.md)。設計: [design.md](../design.md) §2.4（ring の class V・gpu-op の中継、§2.4.8 object model）、§6.9（firmware）、§8 の p018 の行、§15（MSAA: resolve は BLT の RESOLVE、STATE 0x00B まで毎回書き、前の draw は `WAIT_IDLE` flags 0 で待つ）、§16（p030: format・MRT・query・stencil・depth bias などの STATE と µop 14 QUERY）。
- 状態: in-progress（2026-10-25 09:33 P2: 実装は全部の段を commit 済み。Verilator の firmware の harness と長い試験は T の結果待ち。Q1 が T の結果で cleared を判定する）。先に p030 の段 S15（shared memory の atomic）を同じ担当が行った（q1565、amaterrace-gpu 2cf347b）。
- 依存: p017（CP の CPU と firmware の土台）、p009、p010（binary の interface の表、cleared）、p027（cleared）、p030（RTL は並行で実装中。firmware は GHCI の draft の STATE に合わせる）。
- 承認: 2026-10-25 ユーザー「…そのあと、p010,p018,p019,p006,p011を実装します。…」「ゴールまで推しで進んでよいです。判断に迷ったら、Vulkan 1.0の原則論で進めてください。…」

## 範囲（design §8 の p018 の行）

- object の表、image・image view・sampler、`GHCI_PROGRAM`（pipeline の stage の binary と interface の表の検証、`include/gsi1_isa.h`）、pipeline layout・descriptor set layout/pool/set/update、render pass・framebuffer、graphics/compute pipeline → STATE の word の列、記録の全部の R の command、`QUEUE_SUBMIT` の辿り（bind の状態機械、interface の表で descriptor → `SET_REG`/`LOAD_CONST`、`LOAD_PROGRAM`、barrier、clear → BLT、内蔵の blit/clear の kernel、`DRAW`/`DISPATCH`）、record の VkResult と `RESULT` の IRQ、`DESTROY_DEVICE`/session の close の回収。
- GHCI §7.2・§11.6 への追従（SESSION_OPEN の未知の flag を拒む、frame の中の SESSION_OPEN/CLOSE・GRANT・REVOKE を拒む）。
- p010 の決定: VertexIndex・InstanceIndex は interface kind 13 で firmware が届ける。TEX_FORMAT（slot の word 6）は必ず VkFormat を書く。
- MSAA: resolve は BLT の RESOLVE（STATE 0x00B まで毎回書く、前の draw は WAIT_IDLE flags 0）。
- `atg_cp_seq.v` 等の RTL は変えない（必要なら Q1 に）。

## 受け入れ

Verilator の firmware の試験の harness（host の C から staging と frame の packet を作って流し、µop の列と STATE と record の期待と比べる）、vkdemo の部分集合の command の列が期待の µop になる。firmware の build（`tools/fw-build.sh`、CLANG_BIN）が warning 0。長い試験は T に回す。

## 記録

- 2026-10-25 07:13 Q1: 新設。
- 2026-10-25 09:33 P2（q1565-i01、amaterrace-gpu の worktree `p2-p018`、base main afdd3f7）: 実装を段に分けて commit した。

### 段と commit（amaterrace-gpu、branch p2-p018）

| 段 | commit | 内容 |
| --- | --- | --- |
| 1 | 6f77952 | 土台: heap（`GHCI_HEAP`、GPU の memory の line 単位の first fit）、object の表（DMEM、identity の hash、payload は heap の block）、記録（1 KiB の chunk の鎖、DMEM の cache、data packet、marker）、buffer・image・view・sampler、fill・copy・update |
| 2 | cfee60e | descriptor set layout/pool/set/update/copy、pipeline layout、`GHCI_PROGRAM`（header と interface の表の検証）、graphics/compute pipeline、render pass・framebuffer、subpass、draw・dispatch（direct・indirect）、barrier・event、occlusion query、secondary、image の転送（copy・clear・resolve・blit） |
| 3a | 2e012f9 | HCR `RESIDENT_WARPS`（0x04C、[15:0] = NSU×NWARPS、読むだけ）、`vkCmdBlitImage` の scale・format 変換を内蔵の draw（blit の VS/FS）で |
| 3b | f0116cf | stencil の aspect の buffer⇔image の copy を内蔵の compute kernel（`s8_to_image`・`s8_from_image`）で、host の試験の compute・transfer の case、TEX_ROW（slot の word +7、attachment の layout の view は 1） |
| 4 | 2ce0fe6 | Verilator の firmware の harness（`sim/verilator/fw-run.sh`・`fw_harness.cpp`・`fw_harness.vlt`）、host の試験の trace（`fwtest -t`）、draw の case を vkdemo の 1 frame の command の列に（upload の barrier と copy、render pass、readback）、`fw/cp/README.md` の書き直し |

- 承認済みの RTL・memory の変更（Q1 経由）: KC705 の CPU の IMEM 64 KiB・DMEM 32 KiB（atg_top の CPU_IMEM/DMEM_WORDS、KC705 の top・lint・TB、fw-build・link.ld）、`RESIDENT_WARPS`（docs/ghci.md の HCR 表・§7.2、ghci_regs.vh/.h、atg_cp_regs・atg_cp・atg_top、atg_cp_regs_tb）。それ以外の RTL は変えていない。
- zedBSD 側（agent/p2-p010）: e981c797e で compiler の option `bank_first`（VS と FS が 1 つの constant bank を共有するため、push の bank を先頭に）と harness の `bank=N`。

### 判断（Vulkan 1.0 の原則で決め、Q1 に報告済み）

- `GHCI_HEAP` の 3 つ目の word = resident warps（firmware の window は HCR を読めないため）。scratch memory は bytes/warp × resident warps を program ごとに確保（interface kind 14 `GSI1_KIND_SCRATCH`）。
- 記録の時点で µop に翻訳する（replay は chunk を流すだけ、例外は marker: indirect の count、query の結果の copy）。uniform block は実行時に記録した blit で bank へ copy。
- image の layout は `include/ghci_layout.h`（常に linear、attachment は幅を偶数に、MS の行は roundup(w,2)·S·ps、plane は 64 B 単位）を driver と共有。
- event・query pool の word の address は result の aux で driver へ。event の word は 1 = set、0 = reset。query の slot は {u64 result, u32 available, u32 0}。
- timestamp は出さない（`vkCmdWriteTimestamp` は記録の誤り FEATURE_NOT_PRESENT）。cube の array は出さない。
- 内蔵の blit は unnormalized の座標の sampler。stencil の copy は compute kernel。
- firmware の大きさ: -Oz と linker relaxation（IMEM 60576 B / 64 KiB）。

### 確認（自分で数えた）

- `tools/fw-build.sh`（CLANG_BIN=/home/awe/zedBSD-claude1/build/llvm/bin、-Werror）: warning 0、IMEM 60576 B、DMEM の image 3616 B。2ce0fe6 で再 build して committed の image と差分なし。
- `fw/cp/tests/run.sh`（ASan/UBSan）: fwtest 6/6 PASS（sessions、buffers、images、draw、compute、transfer）、kernels 2/2 PASS。
- `fwtest -t` の scene（2ce0fe6）: 6 case、30 step、µop 864 word、result 296 word、STATE 239 word（全部 0x200 未満）。ASan 版と非 ASan 版で byte 一致（vkdemo の列を入れる前に確認）。
- `fw_harness.cpp`: Verilator の C++ の生成（`verilator --cc`、4 s、warning 0）と g++ の構文の確認（自分の file の warning 0）だけ。Verilator の build と実行はしていない（T へ）。
- 以前の段: atg_top_fw の TB PASS（f0116cf、35 s）、lint・atg_cp_regs_tb は 2e012f9 で実施（T の結果は Q1 が持つ）。

### T への依頼（未実施）

1. `/home/awe/amaterrace-worktrees/p2-p018` @ 2ce0fe6: `sim/verilator/fw-run.sh`（Verilator の build 900 s、実行 1800 s の timeout）。期待: `PASS fw.sessions`・`fw.buffers`・`fw.images`・`fw.draw`・`fw.compute`・`fw.transfer` の 6 行と `PASS verilator fw`。各行の「µop words (0 differ)」「result words (0 differ)」「STATE words (0 differ)」。FAIL なら出力の全文を P2 へ（最初の不一致を 6 つまで印字する）。
2. 同じ commit で `tools/lint.sh`（warning 0）と `make sim-quick`（全部 PASS、atg_top_fw・atg_cp_regs を含む）。

### 残り

- T の 1・2 の結果。fw-run.sh の draw の case は `end` だけの program で実際の unit を動かすので、RTL が vertex の出力の無い warp で止まるなら harness の program を本物の VS/FS に替える。
- 奇数幅の attachment の sampling は TU が TEX_ROW を読むまで（p030 S16、P1）。
- descriptor の array は要素 0 だけ（compiler の制限）。
- ZYBO の 32 KiB の firmware はこの image に入らない（KC705 だけ）。
