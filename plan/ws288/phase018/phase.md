<!-- awesome-plan project=zedbsd record=ws288-p018 -->

# ws288-p018 CP の firmware の Vulkan の object model と draw/dispatch

- 親: [WS288](../ws.md)。設計: [design.md](../design.md) §2.4（ring の class V・gpu-op の中継、§2.4.8 object model）、§6.9（firmware）、§8 の p018 の行、§15（MSAA: resolve は BLT の RESOLVE、STATE 0x00B まで毎回書き、前の draw は `WAIT_IDLE` flags 0 で待つ）、§16（p030: format・MRT・query・stencil・depth bias などの STATE と µop 14 QUERY）。
- 状態: planned（2026-10-25 07:13 Q1 新設）。先に p030 の段 S15（shared memory の atomic）を同じ担当が行う。
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
- 2026-10-25 08:10 P2 → Q1（途中）: firmware が atg_cpu の 32 KiB の IMEM・16 KiB の DMEM に入らない（DMEM が溢れる: object の表 6.4 KiB など）。P2 の判断: GHCI_HEAP 0x10108（driver が heap を貸す、session の無い時の ring だけ）、record の時に µop へ翻訳、compiler の bank_first（vertex と fragment が constant bank を共有、zedBSD agent/p2-p010 e981c797e、gsi1-host-test 32/0）、共有の `include/ghci_layout.h`（image は linear、1x の attachment は幅を偶数に）。
  - Q1 の判断: KC705 の atg_cpu を IMEM 64 KiB・DMEM 32 KiB に（design §2.4.7 が許す範囲、atg_top に parameter、既定は今の値）、P2 が p2-p018 で行う。(1)〜(4) は受け入れ。TU の奇数の幅の attachment は p030 の段 S16（row pitch）に。
- 2026-10-25 08:23 P2 → Q1: 段 1（firmware の芯）p2-p018 6f77952（親 2cf347b）。GHCI_HEAP 0x10108（ghci.md §7.2）、session 32 個と heap の上限、object の表 160、recording は heap の 1 KiB の chunk の鎖、buffer・image（ghci_layout.h）・image view・sampler・memory の bind・command pool/buffer・fill/copy/update buffer・QUEUE_SUBMIT・semaphore・GHCI_PROGRAM の decoder、format の表（tools/fw-formats.py → fw/cp/formats.inc）、ring だけの規則（frame の中の SESSION_OPEN/CLOSE/HEAP は frame を終える、flag 付きの SESSION_OPEN と GRANT/REVOKE は FEATURE_NOT_PRESENT）。RTL: atg_top の CPU_IMEM_WORDS・CPU_DMEM_WORDS（KC705 で 16384・8192）、fw-build.sh は -Os で IMEM 26 KiB。P2 の申告: 新しい host の bench fw/cp/tests/run.sh PASS（3/3）、fw-build warning 0、lint 118、atg_cpu・atg_cpu_fw・atg_cpu_mem・atg_top_fw PASS。T1-777（T3）に。
- 2026-10-25 08:50 Q1: T1-777（p2-p018 6f77952、段 1）PASS: sim-quick 90、verilator 6、lint 118。
- 2026-10-25 08:59 P2 → Q1: 段 2（p2-p018 cfee60e）。descriptor set の layout・pool・set・update/copy、pipeline layout、render pass と framebuffer、graphics・compute の pipeline → STATE の word、bind と dynamic state の全部、draw・draw indexed・dispatch・3 つの indirect、render pass の begin/next/end（BLT の clear、WAIT_IDLE の後の RESOLVE）、vkCmdClearAttachments、barrier と event、occlusion の query、secondary の command buffer、image の copy・buffer と image の間・clear・resolve。足りない物: vkCmdBlitImage の拡大縮小・反転・format の変換（同じ大きさは可）、D24S8 の stencil と 1 byte の buffer の間（内蔵の compute の kernel が要る）、timestamp（出さない）。scratch の kind 14: firmware は CAPS を読めないので driver が GHCI_HEAP の 3 語目に resident の warp の数（NSU×NWARPS）を渡す（ghci.md §7.2・ghci_frame.h）。fw-build は -Oz と linker の relaxation、IMEM 58.5 KiB / 64。P2 の申告: fw/cp/tests/run.sh 4/4、atg_cpu_fw・atg_top_fw PASS。T1-778 に。
- 2026-10-25 09:09 P2 → Q1: p2-p018 2e012f9（親 cfee60e）。HCR の RESIDENT_WARPS（0x04C、読み出し専用、[15:0] = NSU×SU_NWARPS）を GHCI §3.1・§7.2・ghci_regs・atg_cp_regs・atg_cp・atg_top に（P3 に転送）。vkCmdBlitImage の内蔵の blit の draw（拡大縮小・反転・format の変換、firmware の VS/FS、正規化しない座標、fw/cp/programs/*.s → tools/fw-programs.py → programs.inc）。P2 の申告: lint 118、atg_cp_regs・atg_top_fw・atg_cpu_fw・fwtest PASS。IMEM 60.1 KiB / 64（残り約 4 KiB）。T1-780（T3）に。
