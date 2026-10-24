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
