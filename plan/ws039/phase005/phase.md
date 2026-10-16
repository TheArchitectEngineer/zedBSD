<!-- awesome-plan project=zedbsd record=ws039-p005 -->

# ws039-p005: 段 A5（CP・KIQ・compute の queue・SDMA・fence・最初の copy と dispatch）

Status: cleared（2026-10-16 Q1: 実機 10.0.10.3 で A5 ok（KIQ の道 2 回＋前の kernel 2 回、kiq=0 の退路 1 回）、KIQ・GFX・compute 4 本・SDMA0 の ring test と fence、EOP・TRAP の割り込み、copy の照合、ids.comp の dispatch 1536 語一致（compute と GFX の IB）、VM fault の割り込み到着、Linux と CU・KIQ の場所が一致。main の merge adef3571d、ci の vmunix warning 0。未確認: cold の直後の A5、fault の後の queue の戻し（p013））
Disposition: normal
Parent: [WS039](../ws.md)
Queue: q1215 / q1215-i01
実行者: P3（worktree `/home/awe/zedBSD-worktrees/p3`、branch `agent/p3-ws039`、base main `12eb9ac7a`）

## 範囲（[design.md](../design.md) §16 の p005 の行）

golden・constants・RLC の autoload 待ち・CSB、KIQ、GFX の queue（MAP_QUEUES）、compute の queue 4 本、SDMA0、fence と割り込み、最初の copy、最初の dispatch（p007 の最小の program）、CP_RB0 の退路、host 試験 (e)。**実機: A5 の ok**。§15 #6 の回答（compute の queue は kernel の試験と compute 専用 family の最小の経路）、design-review-2 の反映（KIQ は MEC2 pipe 1 queue 0、BUF_SWAP 0、GFX の MAP_QUEUES の ME=1、SET_RESOURCES は map する queue の bit だけ、WPTR_POLL の EN=0 と CP_PQ_STATUS の DOORBELL_ENABLE=1）に従う。p004 の残り: VM fault の受けを A5 で実機に通す（わざと fault を起こす小さな試験）。

承認: q1215（2026-10-16 ユーザー「進めてください。コールドブートは言ってくれればやります」と design.md §15 の回答）。

## 実装（2026-10-16 P3、実機前）

host でも動く純粋な file（`src/drivers/gpu/amdrdna/`）:
- `command-format.c`: PM4 の type 3 の header、INDIRECT_BUFFER（compute の queue は VALID の bit）、1 語の filler、register の書き込み（context・shader・uconfig の 3 空間）、WRITE_DATA（memory・register、WR_CONFIRM）、RELEASE_MEM の fence（0x06603514・DATA_SEL 1・INT_SEL 2）、ACQUIRE_MEM（GCR 0xC3B1、compute の queue では shader type の bit。Mesa の radv_cs.c と同じ）、DMA_DATA（CP_SYNC、memory → memory）、DISPATCH_DIRECT、SET_RESOURCES・MAP_QUEUES・UNMAP_QUEUES、PREAMBLE_CNTL・CONTEXT_CONTROL・CLEAR_STATE・SET_BASE。SDMA の WRITE・COPY_LINEAR・FENCE（MTYPE 3）＋TRAP。
- `queue-format.c`: compute の MQD（`v10_compute_mqd` の 31 欄）と GFX の MQD（`v10_gfx_mqd` の 18 欄）を、queue の register の今の値と buffer の address から作る。compute の MQD の word 128 以降が HQD の register の並びと同じであることを `_Static_assert` で確かめる。GRBM の queue select の語、RLC_CP_SCHEDULERS の語、SET_RESOURCES の queue の mask、CP_RB0 の退路の RB_CNTL。
- `settings.c`: golden の表（GC 40 項目、SDMA 24＋2 項目。window・offset・mask・値）と適用の規則（mask が全 bit なら置き換え、それ以外は RMW）、clear state（8 つの run と 0 でない 85 個の register、949 語の CSB と preamble の列）、RB・WGP・TCC の fuse の読み、tile steering、tcp harvest の 2 語、SH_MEM_CONFIG（0xC00C）と SH_MEM_BASES（0x20001000）。
- `check-program.c`: 最初の dispatch の program（P5 の compiler の `plan/ws039/tests/shaders/ids.comp` の出力、136 語。RSRC1 0x002f0001・RSRC2 0x00001384、末尾を s_code_end で 192 語まで）、raw buffer の記述子（P5 の `amdrdna_gfx10_raw_buffer_descriptor` と同じ語）、出力の検査（2×2×2 の workgroup、1536 語、全語の期待値）。
- 識別子は全部 amdrdna の語（13 節）。block ごとに Value(s)/Procedure referenced。

kernel（`src/drivers/gpu/amdrdna/`）:
- `cp.c`: golden・READ_TIMEOUT・GB_ADDR_CONFIG・SE/SH ごとの RB と CU・TCC・tile steering・VMID 0〜15 の SH_MEM・GDS の 0（`amdrdna_cp_settings`）、RLC の autoload の完了待ち（CP_STAT 0 と BOOTLOAD_COMPLETE）・CSB を VRAM に置いて RLC_CSIB・SRM・tcp harvest（`amdrdna_cp_run_list`）、割り込み（CP_INT_CNTL_RING0 と ME1 の pipe 0〜3 の EOP・priv・bad opcode、idle の event は off）→ KIQ（RLC_CP_SCHEDULERS → MQD を register から作る → WPTR_POLL の EN=0 → HQD の register → MEC の doorbell の範囲 0〜0x450 → HQD_ACTIVE → CP_PQ_STATUS の DOORBELL_ENABLE）→ CP_MEC_CNTL=0 → KIQ の ring test（最初の PM4）→ SET_RESOURCES（0x01010101）と compute の MAP_QUEUES 4 本 → GFX の MQD と doorbell の範囲と MAP_QUEUES → CP_MAX_CONTEXT 7・CP_DEVICE_ID 1・ME/PFP/CE の unhalt → preamble（CSB の列と SET_BASE）→ GFX と compute の ring test（`amdrdna_cp_start`）。`amdrdna.kiq=0` は CP_RB0 の register の道で GFX だけ（compute の queue は作らない。MEC は Linux の道と同じく unhalt。wptr は doorbell ではなく CP_RB0_WPTR の register に書く（Linux の doorbell 無しの道）。dispatch は GFX の ring の IB）。ring の書き込み（room の検査、fence、256 語（SDMA は 16 語）の詰め、64 bit の wptr の写しと doorbell）、fence の待ち（2 ms の spin の後 1 ms の sleep）、ring test（SCRATCH_REG0 を SET_UCONFIG_REG で）。停止: GFX は UNMAP PREEMPT、compute は UNMAP RESET（KIQ の fence と ring test）、ME/PFP/CE と MEC の halt、割り込み off。
- `sdma.c`: SDMA の golden（両 engine）→ SDMA0 の F32 の unhalt と AUTO_CTXSW → gfx queue（RB_SIZE、ptr 0、WPTR_POLL の address と F32_POLL、RPTR の writeback、RB_BASE、MINOR_PTR_UPDATE、doorbell 0x200 と NBIO の SDMA0 の範囲（size 20）、UTC_L1・MIDCMD_PREEMPT、UTCL1 の RESP_MODE 3・REDO_DELAY 9、page policy、RB_ENABLE・IB_ENABLE）→ TRAP_ENABLE → ring test（WRITE）。停止: trap と ctxsw off、RB・IB off、F32 halt。SDMA1 は halt のまま。
- `engines.c`: 段 A5。最初に全 buffer を取って GART に map する（writeback の page、各 ring・MQD・EOP、試験の buffer。Navi1x は SDMA が動いた後に CPU から GART の TLB を invalidate しない、design 5.3）→ settings → run list → queue → SDMA → 試験: 各 ring に fence＋割り込み（EOP・TRAP の数を mark）、copy（SDMA で system → VRAM、BAR0 で読み戻し、GFX の DMA_DATA で VRAM → system、4 KiB の模様を比較）、dispatch（compute の queue 0 の ring の上で ACQUIRE_MEM・PGM・RSRC1/2/3・RESOURCE_LIMITS・STATIC_THREAD_MGMT・TMPRING・START・NUM_THREAD・USER_DATA・DISPATCH_DIRECT、出力 1536 語を全部照合。同じ列を GFX の ring から INDIRECT_BUFFER（VMID 0）でも流して照合: executor の universal の family の道）、VM fault（最後の compute の queue から GART の guard（何も map しない先頭 1 MiB）に WRITE_DATA、fault の割り込みの数を mark。fault が来なくても A5 は fail にしない）。失敗は停止を 1 回流す。IH の client 0x14（EOP 181・bad opcode 183・priv 184/185）と 0x08（TRAP 224）を数える。
- `amdrdna.c`: 最後の段を A5 に、shutdown は A5 → A4 → A3 → A2 の逆順、engines の lock の初期化。`ih.c`: CP と SDMA0 の entry を `engines.c` へ。`block-table.c`: gc_info の scan converter と packer の数（tile steering 用）。`stage.c`: `amdrdna.kiq=0` の説明。
- UAPI（`include/uapi/amdrdna.h`）: status の予約 14 語のうち 11 語を A5 の値に（動いている物の flag、compute の queue の数、GB_ADDR_CONFIG、RB の mask、CU の数、TCC の off、fence の数、EOP の割り込み、SDMA の trap、copy の byte、dispatch の照合語）。大きさは 352 byte のまま。`amdrdnactl status` に engines の行。
- build の行（`platform/amd64/vmunix.mk`、P5 の変数の行は触らない）に 7 file。

設計からの変更・決めたこと:
- compute の queue は MEC1 の pipe 0〜3 の **queue 0**（design 7.4 の「queue 1」から変えた）。理由: design 7.4 が queue 0 を避けたのは KIQ が MEC1 の pipe 0 queue 0 だった第 1 版の名残で、review M2 で KIQ は MEC2 pipe 1 queue 0 に移った。Linux は同じ機械で comp_1.0.0〜comp_1.3.0（queue 0）を map している（`temp/dump/dmesg-load.txt`）。doorbell は 0x003〜0x006（<< 1）のまま。
- KIQ の ring test は Linux と同じ SCRATCH_REG0 の書き込み（design 7.2 の WRITE_DATA の writeback ではなく）。KIQ の投入の完了は WRITE_DATA の fence の後に ring test。
- MQD・EOP・ring・writeback は全部 GART の system memory。CSB・dispatch の code と記述子表は VRAM（CPU の pool、HDP flush）。
- RELEASE_MEM には shader type の bit を付けない（Linux・Mesa とも付けない）。ACQUIRE_MEM と DISPATCH_DIRECT は compute の queue で付ける（Mesa）。
- 割り込みは数えて mark するだけで、fence は writeback を poll する（A5 の試験。待ちを割り込みで起こす形は p011 以降）。
- GFX の ring での dispatch は IB で流す: 実機の `amdrdna.kiq=0` の最初の試行で、ring の上（IB の外）の DISPATCH_DIRECT の wave が VMID 10 で走り（IV と GFXHUB の fault の ctx=10、SQ の割り込み client 0x0a src 0xef）program の fetch が fault した。VMID 0 の IB にすると通る（executor も IB で流すので同じ形）。compute の queue の ring の上の dispatch は HQD の VMID 0 で通る。
- `amdrdna.kiq=0` の最初の試行では doorbell の wptr で GFX の preamble が進まなかった（CP_STAT 0x80008000 = PFP busy、fence 来ず）。CP_RB0_WPTR の register と MEC の unhalt にして通った（どちらが効いたかは切り分けていない）。
- VM fault の試験は、fault した queue（KIQ の道では compute の queue 3、退路では GFX の ring）を止めたままにする: write は default page に回らず queue が先へ進まない（fence が来ない）。fault の割り込みは届く。queue の戻しは p013（A5c）の queue の reset。停止の UNMAP RESET の後の reboot で Debian に毎回戻った。
- 最初の dispatch の program は compiler の出力の語を埋め込み（design 9.2 の「語をそのまま使う」）。host 試験が P5 の GFX10 interpreter で走らせて全語を照合し、今の compiler の出力とも比べる（違えば NOTE、fail にしない）。

host 試験（実機の証拠ではない）:
- `plan/ws039/tests/amdrdna-engines-test.c`（新、host 試験 (e)）: PM4 と SDMA の packet の語（Linux の builder から手で出した値: fence 0x06603514/0x22000000、MAP_QUEUES の dw1 は compute pipe 2 で 0x20020000・GFX で 0x30040000、UNMAP は compute RESET 0x20000001・GFX PREEMPT 0x30000000、SET_RESOURCES 0x01010101 …）、MQD の欄（RB 16 KiB の PQ_CONTROL 0xd000090b、GFX 64 KiB の HQD_CNTL 0x0b0d、doorbell の source・hit の消去 …）、queue select・RLC_CP_SCHEDULERS（0xc8）・resource mask、golden の表の数と適用、CSB 949 語と枠、fuse の読み（実機の dump の CC_GC_SHADER_ARRAY_CONFIG 0xffe00001 → WGP 5 本・CU 10）、tile steering（0x00122000）、harvest、check program を interpreter で 8 workgroup 走らせ全 1536 語が一致し、1 語壊すと検査が見つける。
- `plan/ws039/tests/amdrdna-engines-compare.py`（新）: MQD の語の位置を `v10_structs.h` から、register の window・offset を `gc_10_1_0_offset.h`・`nbio_2_3_offset.h` から、golden の表を `gfx_v10_0.c`・`sdma_v5_0.c` から読んで照合（170 項目）。
- `plan/ws039/tests/amdrdna-dispatch-gen.c`（新）: compiler で module を compile して埋め込みの語を印字する道具。
- `plan/ws039/tests/amdrdna-host-test.sh` に足した。2026-10-16 12:4x PASS（PASS の行 303、audit-symbols を含む。一般語 38 個を allow list に足した）。
- build: `make -j16 ZEDBSD_CONFIG=plan/ws039/tests/config.mk BUILD=build/p3/usb AMDRDNA_RUN=A1v build/p3/usb/vmunix build/p3/usb/bin/amdrdnactl` rc 0、warning 0、kernel include check PASS、amd64 vmunix check PASS。`plan/tools/style-check.py` の違反は新規・変えた file で 0。`git diff --check` OK。

## 実機（2026-10-16、q1215-i01、10.0.10.3 の RX 5600 XT、USB の zedBSD は DHCP で 10.0.30.5）

手順: p004 と同じ（Debian で USB の sda2 の `vmunix` を差し替え（p004 の物は Debian の `~/ws039/vmunix.p4`）→ `hw-run.sh boot 0001` → `AMDRDNA_ZED=root@10.0.30.5 hw-run.sh collect` → 新しい `amdrdnactl` を `/tmp` へ → `amdrdnactl advance A5`（A2〜A5 が続けて走る）→ `dmesg` と `status` → `reboot` で Debian へ）。firmware は USB の `/lib/firmware/amdgpu/` に p004 で置いた 11 file。電源は全部 warm reboot（Debian 経由）で、毎回 A0 は `sos=0 smu=0x0`（cold power cycle は要らなかった）。Debian は今 amdgpu を起動で load するが firmware が無く PSP の前で止まる（GPU に触らない）。証拠 `build/tmp/amdrdna-hw-p5-{1..7}/`（worktree の中、commit しない）。

| 回 | 時刻 | kernel（vmunix の SHA-256 の先頭） | 道 | 結果 |
| --- | --- | --- | --- | --- |
| 1 | 12:29 | bbdeb50c…（fb98810ff） | KIQ | **A5 ok**（compute の ring の dispatch 1536 語、vmfault=1） |
| 2 | 12:32 | 同上 | KIQ | **A5 ok**（同じ行） |
| 3 | 12:38 | 同上 | kiq=0 | A5 fail: preamble の fence 来ず（CP_STAT 0x80008000）→ 直した |
| 4 | 12:45 | 2c6a505a… | kiq=0 | ring・fence・copy は ok、GFX の ring の上の dispatch が VMID 10 で fault → IB にした |
| 5 | 12:49 | 9edf0d4d…（最終） | kiq=0 | **A5 ok direct**（GFX の IB の dispatch 1536 語、vmfault=1） |
| 6 | 12:52 | 同上 | KIQ | **A5 ok**（compute の ring と GFX の IB の dispatch で 3072 語） |
| 7 | 12:55 | 同上 | KIQ | **A5 ok**（6 と同じ行） |

最終の kernel の KIQ の道（6・7 回）の印:
- `A5 settings 40 gb=0x00100044 rb=0xffff cu=36 tcc_off=0x3c steering=0x00122000`
- `A5 run list ready, clear state 949 words at 0x80007f0000`
- `A5 kiq ring test ok`（3 回: unhalt の直後、compute の MAP の後、GFX の MAP の後）、`A5 gfx ring test ok`、`A5 compute ring test ok` × 4、`A5 queues up: scheduler, graphics, 4 compute`、`A5 sdma0 ring test ok`
- `A5 fences 6, interrupts gfx=1 compute=1/1/1/1 sdma0=1 errors=0`（EOP の割り込みが GFX と compute の 4 本で、SDMA0 の TRAP が届いた）
- `A5 copy ok: sdma0 system->vram 4096B, gfx vram->system 4096B`
- `A5 dispatch ok on compute from the ring: 2x2x2 groups of 4x3x2, 1536 words`、`A5 dispatch ok on gfx from an indirect buffer: 2x2x2 groups of 4x3x2, 1536 words`
- `vm fault gfx hub ctx=0 space=0 at 0x2000 access=0x20 status=0x00040a50 unit=0x5 walker=0 denied=0x5`、`compute fence 2 not reached (at 1)`、`A5 vm fault check: write to 0x2000 from compute, faults reported 1`
- **`A5 ok kiq gfx compute=4 sdma0 fences=6 irq=gfx:3 compute:5 copy:2 copied=8192B dispatch=3072 vmfault=1 cu=36 rb=0xffff tcc_off=0x3c`**
- status の engines の行: `flags=0x7 compute=4 gb=0x00100044 rb=0xffff cu=36 tcc_off=0x3c fences=6 eop=8 traps=2 copied=8192B dispatch=3072`
- A3・A4 は p004 と同じ（`A3 ok … tmr=9216KiB fw=12 … warnings=0`、`A4 ok if=0x37 smu=42.65.0 mode=S … temp=43C`）。

照合: CU 36 は Linux の dmesg「SE 2, SH per SE 2, CU per SH 10, active_cu_number 36」と一致。KIQ の場所は Linux の「kiq ring mec 2 pipe 1 q 0」と同じ。GB_ADDR_CONFIG 0x00100044 は cold の dump と一致。RB 16 個（mask 0xffff）、TCC は 16 のうち 2〜5 が off で 12（192 bit の bus）→ CB の後の GL2 の writeback が要る側（design 9.3、p008 で使う）。VM fault は GFXHUB（client 0x1B）から ctx 0・address 0x2000（GART の guard）・write で届き、`amdrdna_vm_fault` が decode した（p004 の残り）。

確かめていない物: cold power cycle の直後の A5（warm reboot だけで 7 回）、停止の行の disk への記録（reboot の log は file system の後）、fault した queue の戻し（p013）、SDMA の TRAP 以外の SDMA の割り込み、長い時間の動作。

## 状態と次

- [x] 実装、host 試験、build
- [x] 実機（10.0.10.3）: `advance A5` で A5 の ok（KIQ・GFX・compute 4・SDMA0、fence と割り込み、copy、dispatch（compute の ring と GFX の IB）、VM fault）を最終の kernel で KIQ の道 2 回・退路 1 回、前の kernel で KIQ の道 2 回
- [ ] Q1: merge と clearance の判定
