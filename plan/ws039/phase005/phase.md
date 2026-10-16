<!-- awesome-plan project=zedbsd record=ws039-p005 -->

# ws039-p005: 段 A5（CP・KIQ・compute の queue・SDMA・fence・最初の copy と dispatch）

Status: in-progress（2026-10-16 P3、q1215-i01。実装と host 試験と build まで済み、実機は未実施）
Disposition: normal
Parent: [WS039](../ws.md)
Queue: q1215 / q1215-i01
実行者: P3（worktree `/home/awe/zedBSD-worktrees/p3`、branch `agent/p3-ws039`、base main `12eb9ac7a`）

## 範囲（[design.md](../design.md) §16 の p005 の行）

golden・constants・RLC の autoload 待ち・CSB、KIQ、GFX の queue（MAP_QUEUES）、compute の queue 4 本、SDMA0、fence と割り込み、最初の copy、最初の dispatch（p007 の最小の program）、CP_RB0 の退路、host 試験 (e)。**実機: A5 の ok**。§15 #6 の回答（compute の queue は kernel の試験と compute 専用 family の最小の経路）、design-review-2 の反映（KIQ は MEC2 pipe 1 queue 0、BUF_SWAP 0、GFX の MAP_QUEUES の ME=1、SET_RESOURCES は map する queue の bit だけ、WPTR_POLL の EN=0 と CP_PQ_STATUS の DOORBELL_ENABLE=1）に従う。p004 の残り: VM fault の受けを A5 で実機に通す（わざと fault を起こす小さな試験）。

承認: q1215（2026-10-16 ユーザー「進めてください。コールドブートは言ってくれればやります」と design.md §15 の回答）。

## 実装（2026-10-16 P3、実機前）

host でも動く純粋な file（`src/drivers/gpu/amdrdna/`）:
- `command-format.c`: PM4 の type 3 の header、1 語の filler、register の書き込み（context・shader・uconfig の 3 空間）、WRITE_DATA（memory・register、WR_CONFIRM）、RELEASE_MEM の fence（0x06603514・DATA_SEL 1・INT_SEL 2）、ACQUIRE_MEM（GCR 0xC3B1、compute の queue では shader type の bit。Mesa の radv_cs.c と同じ）、DMA_DATA（CP_SYNC、memory → memory）、DISPATCH_DIRECT、SET_RESOURCES・MAP_QUEUES・UNMAP_QUEUES、PREAMBLE_CNTL・CONTEXT_CONTROL・CLEAR_STATE・SET_BASE。SDMA の WRITE・COPY_LINEAR・FENCE（MTYPE 3）＋TRAP。
- `queue-format.c`: compute の MQD（`v10_compute_mqd` の 31 欄）と GFX の MQD（`v10_gfx_mqd` の 18 欄）を、queue の register の今の値と buffer の address から作る。compute の MQD の word 128 以降が HQD の register の並びと同じであることを `_Static_assert` で確かめる。GRBM の queue select の語、RLC_CP_SCHEDULERS の語、SET_RESOURCES の queue の mask、CP_RB0 の退路の RB_CNTL。
- `settings.c`: golden の表（GC 40 項目、SDMA 24＋2 項目。window・offset・mask・値）と適用の規則（mask が全 bit なら置き換え、それ以外は RMW）、clear state（8 つの run と 0 でない 85 個の register、949 語の CSB と preamble の列）、RB・WGP・TCC の fuse の読み、tile steering、tcp harvest の 2 語、SH_MEM_CONFIG（0xC00C）と SH_MEM_BASES（0x20001000）。
- `check-program.c`: 最初の dispatch の program（P5 の compiler の `plan/ws039/tests/shaders/ids.comp` の出力、136 語。RSRC1 0x002f0001・RSRC2 0x00001384、末尾を s_code_end で 192 語まで）、raw buffer の記述子（P5 の `amdrdna_gfx10_raw_buffer_descriptor` と同じ語）、出力の検査（2×2×2 の workgroup、1536 語、全語の期待値）。
- 識別子は全部 amdrdna の語（13 節）。block ごとに Value(s)/Procedure referenced。

kernel（`src/drivers/gpu/amdrdna/`）:
- `cp.c`: golden・READ_TIMEOUT・GB_ADDR_CONFIG・SE/SH ごとの RB と CU・TCC・tile steering・VMID 0〜15 の SH_MEM・GDS の 0（`amdrdna_cp_settings`）、RLC の autoload の完了待ち（CP_STAT 0 と BOOTLOAD_COMPLETE）・CSB を VRAM に置いて RLC_CSIB・SRM・tcp harvest（`amdrdna_cp_run_list`）、割り込み（CP_INT_CNTL_RING0 と ME1 の pipe 0〜3 の EOP・priv・bad opcode、idle の event は off）→ KIQ（RLC_CP_SCHEDULERS → MQD を register から作る → WPTR_POLL の EN=0 → HQD の register → MEC の doorbell の範囲 0〜0x450 → HQD_ACTIVE → CP_PQ_STATUS の DOORBELL_ENABLE）→ CP_MEC_CNTL=0 → KIQ の ring test（最初の PM4）→ SET_RESOURCES（0x01010101）と compute の MAP_QUEUES 4 本 → GFX の MQD と doorbell の範囲と MAP_QUEUES → CP_MAX_CONTEXT 7・CP_DEVICE_ID 1・ME/PFP/CE の unhalt → preamble（CSB の列と SET_BASE）→ GFX と compute の ring test（`amdrdna_cp_start`）。`amdrdna.kiq=0` は CP_RB0 の register の道で GFX だけ（compute の queue は作らない、dispatch は GFX の ring）。ring の書き込み（room の検査、fence、256 語（SDMA は 16 語）の詰め、64 bit の wptr の写しと doorbell）、fence の待ち（2 ms の spin の後 1 ms の sleep）、ring test（SCRATCH_REG0 を SET_UCONFIG_REG で）。停止: GFX は UNMAP PREEMPT、compute は UNMAP RESET（KIQ の fence と ring test）、ME/PFP/CE と MEC の halt、割り込み off。
- `sdma.c`: SDMA の golden（両 engine）→ SDMA0 の F32 の unhalt と AUTO_CTXSW → gfx queue（RB_SIZE、ptr 0、WPTR_POLL の address と F32_POLL、RPTR の writeback、RB_BASE、MINOR_PTR_UPDATE、doorbell 0x200 と NBIO の SDMA0 の範囲（size 20）、UTC_L1・MIDCMD_PREEMPT、UTCL1 の RESP_MODE 3・REDO_DELAY 9、page policy、RB_ENABLE・IB_ENABLE）→ TRAP_ENABLE → ring test（WRITE）。停止: trap と ctxsw off、RB・IB off、F32 halt。SDMA1 は halt のまま。
- `engines.c`: 段 A5。最初に全 buffer を取って GART に map する（writeback の page、各 ring・MQD・EOP、試験の buffer。Navi1x は SDMA が動いた後に CPU から GART の TLB を invalidate しない、design 5.3）→ settings → run list → queue → SDMA → 試験: 各 ring に fence＋割り込み（EOP・TRAP の数を mark）、copy（SDMA で system → VRAM、BAR0 で読み戻し、GFX の DMA_DATA で VRAM → system、4 KiB の模様を比較）、dispatch（compute の queue 0 で ACQUIRE_MEM・PGM・RSRC1/2/3・RESOURCE_LIMITS・STATIC_THREAD_MGMT・TMPRING・START・NUM_THREAD・USER_DATA・DISPATCH_DIRECT、出力 1536 語を全部照合）、VM fault（最後の compute の queue から GART の guard（何も map しない先頭 1 MiB）に WRITE_DATA、fault の割り込みの数を mark。fault が来なくても A5 は fail にしない）。失敗は停止を 1 回流す。IH の client 0x14（EOP 181・bad opcode 183・priv 184/185）と 0x08（TRAP 224）を数える。
- `amdrdna.c`: 最後の段を A5 に、shutdown は A5 → A4 → A3 → A2 の逆順、engines の lock の初期化。`ih.c`: CP と SDMA0 の entry を `engines.c` へ。`block-table.c`: gc_info の scan converter と packer の数（tile steering 用）。`stage.c`: `amdrdna.kiq=0` の説明。
- UAPI（`include/uapi/amdrdna.h`）: status の予約 14 語のうち 11 語を A5 の値に（動いている物の flag、compute の queue の数、GB_ADDR_CONFIG、RB の mask、CU の数、TCC の off、fence の数、EOP の割り込み、SDMA の trap、copy の byte、dispatch の照合語）。大きさは 352 byte のまま。`amdrdnactl status` に engines の行。
- build の行（`platform/amd64/vmunix.mk`、P5 の変数の行は触らない）に 7 file。

設計からの変更・決めたこと:
- compute の queue は MEC1 の pipe 0〜3 の **queue 0**（design 7.4 の「queue 1」から変えた）。理由: design 7.4 が queue 0 を避けたのは KIQ が MEC1 の pipe 0 queue 0 だった第 1 版の名残で、review M2 で KIQ は MEC2 pipe 1 queue 0 に移った。Linux は同じ機械で comp_1.0.0〜comp_1.3.0（queue 0）を map している（`temp/dump/dmesg-load.txt`）。doorbell は 0x003〜0x006（<< 1）のまま。
- KIQ の ring test は Linux と同じ SCRATCH_REG0 の書き込み（design 7.2 の WRITE_DATA の writeback ではなく）。KIQ の投入の完了は WRITE_DATA の fence の後に ring test。
- MQD・EOP・ring・writeback は全部 GART の system memory。CSB・dispatch の code と記述子表は VRAM（CPU の pool、HDP flush）。
- RELEASE_MEM には shader type の bit を付けない（Linux・Mesa とも付けない）。ACQUIRE_MEM と DISPATCH_DIRECT は compute の queue で付ける（Mesa）。
- 割り込みは数えて mark するだけで、fence は writeback を poll する（A5 の試験。待ちを割り込みで起こす形は p011 以降）。
- 最初の dispatch の program は compiler の出力の語を埋め込み（design 9.2 の「語をそのまま使う」）。host 試験が P5 の GFX10 interpreter で走らせて全語を照合し、今の compiler の出力とも比べる（違えば NOTE、fail にしない）。

host 試験（実機の証拠ではない）:
- `plan/ws039/tests/amdrdna-engines-test.c`（新、host 試験 (e)）: PM4 と SDMA の packet の語（Linux の builder から手で出した値: fence 0x06603514/0x22000000、MAP_QUEUES の dw1 は compute pipe 2 で 0x20020000・GFX で 0x30040000、UNMAP は compute RESET 0x20000001・GFX PREEMPT 0x30000000、SET_RESOURCES 0x01010101 …）、MQD の欄（RB 16 KiB の PQ_CONTROL 0xd000090b、GFX 64 KiB の HQD_CNTL 0x0b0d、doorbell の source・hit の消去 …）、queue select・RLC_CP_SCHEDULERS（0xc8）・resource mask、golden の表の数と適用、CSB 949 語と枠、fuse の読み（実機の dump の CC_GC_SHADER_ARRAY_CONFIG 0xffe00001 → WGP 5 本・CU 10）、tile steering（0x00122000）、harvest、check program を interpreter で 8 workgroup 走らせ全 1536 語が一致し、1 語壊すと検査が見つける。
- `plan/ws039/tests/amdrdna-engines-compare.py`（新）: MQD の語の位置を `v10_structs.h` から、register の window・offset を `gc_10_1_0_offset.h`・`nbio_2_3_offset.h` から、golden の表を `gfx_v10_0.c`・`sdma_v5_0.c` から読んで照合（170 項目）。
- `plan/ws039/tests/amdrdna-dispatch-gen.c`（新）: compiler で module を compile して埋め込みの語を印字する道具。
- `plan/ws039/tests/amdrdna-host-test.sh` に足した。2026-10-16 12:2x PASS（PASS の行 301、audit-symbols を含む。一般語 37 個を allow list に足した）。
- build: `make -j16 ZEDBSD_CONFIG=plan/ws039/tests/config.mk BUILD=build/p3/usb AMDRDNA_RUN=A1v build/p3/usb/vmunix build/p3/usb/bin/amdrdnactl` rc 0、warning 0、kernel include check PASS、amd64 vmunix check PASS。`plan/tools/style-check.py` の違反は新規・変えた file で 0。`git diff --check` OK。

## 実機

未実施（次の区切り）。

## 状態と次

- [x] 実装、host 試験、build
- [ ] 実機（10.0.10.3）: `advance A5` で A5 の ok（KIQ・GFX・compute 4・SDMA0、fence、copy、dispatch、VM fault）
- [ ] Q1: merge と clearance の判定
