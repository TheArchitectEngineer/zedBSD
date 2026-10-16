<!-- awesome-plan project=zedbsd record=ws039-p011 -->

# ws039-p011: 段 A5s（session・pasid・動的な VMID・page table・BO・GPU device の登録）

Status: in-progress（2026-10-16 P3、q1216-i01。実機 10.0.10.3 で A5s ok・`/dev/gpu0`・`amdrdnactl gpu` ok を最終の kernel で 3 回。Q1 の merge と clearance の判定待ち）
Disposition: normal
Parent: [WS039](../ws.md)
Queue: q1216 / q1216-i01
実行者: P3（worktree `/home/awe/zedBSD-worktrees/p3`、branch `agent/p3-ws039`、base main `00aaf20f9`）

## 範囲（[design.md](../design.md) §16 の p011 の行）

session: pasid・動的な VMID（§15 #11）・4 段の page table・BO・VM の flush、`drv_gpu_ops` の open〜SHARE・recovery（fault・stop）、停止の hook、GPU device の登録（renderer だけ、§15 #12）、capset の骨、host 試験 (d) の後半。**実機: A5s の ok、`/dev/gpu0` が出る**。取り消しと復帰（p013）は範囲外、recovery の ops は p013 で `isolate` と queue の reset を足せる形。

承認: q1216（2026-10-16 ユーザー「p011に進みます。」と design.md §15 の回答）。

## 実装（2026-10-16 P3、実機前）

host でも動く純粋な file（`src/drivers/gpu/amdrdna/`）:
- `context-pool.c`（新）: GFXHUB の context 1〜15 の pool（design 5.4、Procedure referenced: Linux `amdgpu_ids.c`）。space は自分の context を持ち続け、table の世代が変わった時か、最後の flush が別の ring に積まれてその ring がまだ越えていない時だけ flush。無ければ「他の ring の仕事が終わった」context のうち LRU を取り上げて flush（同じ ring の仕事が残るだけなら sync）。全部忙しければ LRU の最古の fence を待つ。held（隔離）は渡さない。pasid（1〜65535 の bitmap、すぐには再利用しない）。
- `command-format.c`: PFP（fetcher）の register の書き込み、write-then-wait（WAIT_REG_MEM op 1）、memory の待ち（pipeline sync）、PFP_SYNC_ME。
- `amdrdna-hw.h`: 上の型と宣言、VRAM の PTE の MTYPE NC（0）、`AMDRDNA_POOL_SPANS` を 128 → 512（session ごとの VA の pool と VRAM の解放の断片化のため）。

kernel:
- `space.c`（新）: address space（pasid、4 段の page table を CPU の VRAM pool に 4 KiB ずつ、RAM に影の木）、VA は 4 GiB〜64 TiB を 64 KiB 整列、backing（CPU の pool の VRAM、64 KiB 単位、0 で埋める、refcount、kept）、map/unmap（PTE = 物理 | PRESENT・READ・WRITE・EXE・MTYPE NC、PDE = 物理 | PRESENT、HDP flush、世代を進める）、run（HDP flush → context の grant → [sync] → [flush: PFP で context の PTB の base LO/HI、invalidate engine（GFX 0・compute 4+pipe）の write-then-wait、PFP で IH_VMID_LUT[ctx] = pasid、PFP_SYNC_ME] → INDIRECT_BUFFER（VMID）→ ring の fence → 待ち（期限で space を lost、session に報告）→ HDP drop）。VM fault（gmc.c の `amdrdna_vm_fault` から）を IV の pasid（無ければ context の持ち主）で space に当て lost にし session に報告。destroy は仕事が終わっていれば context と pasid を返し、終わっていなければ context を held にして table を残す（quarantine）。
- `node.c`（新）: `drv_gpu_ops`（open・close・get_info・resource_create（RAM）・blob_create（VRAM backing）・destroy（仕事が残る space の blob は VRAM を kept）・read/write（HDP drop/flush）・map（BAR0 の DEVICE）・share（export/release/import））。登録（`drv_gpu_register`、capability は RESOURCE|BLOB|TRANSFER|MAPPING|SHARE）、停止（renderer と space の停止 → unregister）、session の lost の報告。
- `runtime.c`（新、nvrtx の runtime.c と同じ形）: command の frame の queue・supervised job の marker・worker、capset（id 4 の protocol、libvulkan が読む値）、recovery（`stop_begin`・`stop_poll`（queue と space の仕事が終わったら 0）・`fault`（queue と space を止める）、`isolate`・`reset` は NULL）。executor の bind（`amdrdna_runtime_bind(node, execute, timelines)`）で CAPSET|COMMAND|NOTIFICATION|JOB|JOB_CAPACITY を足す。p011 では bind しない（libvulkan は renderer と見ない）。
- `vk-node.c`（新）: P5 の `vk/amdrdna-vk.h` が宣言だけで待つ node の側の関数を同じ名前・同じ形で: `amdrdna_vk_blob`・`device_id`・`heap_bytes`・`memory_detach`・`memory_import`・`gpu_alloc`（backing ＋ session の space に map）・`gpu_free`・`gpu_run`（GFX ring、session の VMID、2000 ms、失敗は EIO）・`gpu_copied`・`gpu_drawn`・`blob_read`。宣言は `amdrdna-node.h` にも同じ形で置いた（p008 で P5 の header と重なるが同じ宣言）。
- `sessions.c`（新）: 段 A5s。space を動かせる状態に → node の record → 検査（どれも executor と同じ `amdrdna_vk_gpu_alloc`・`gpu_run` の口を通す）: (1) 1 session の IB で WRITE_DATA 2 語・DMA_DATA で移す・check program の dispatch（code・table・output 全部 session の VA）を全語照合、(2) 16 session を順に（15 の context の LRU、16 番目が最古を取り上げ、最初の session が戻ると次に古い物を取り上げ、もう一度は flush 無し: flush 17・取り上げ 2 を数える）、(3) session が未 map の VA（4 KiB）に compute queue 2 から書き、hub の fault の pasid で session が lost になるかを印（fail にしない）→ `drv_gpu_register` → `A5s ok …`。`amdrdna.kiq=0` は A5 の fault の検査で GFX ring が止まっているので A5s は ENOTSUP。検査が失敗した時は `amdrdna_space_describe`（space.c）が context の hub の register と 4 段の entry を印に出す。
- `command-format.c`: L2 を通す DMA_DATA（`amdrdna_pm4_move_cached`、SRC_SEL・DST_SEL 3）。実機で、session の VMID の IB の中の DMA_DATA（SEL 0 = DAS/SAS の address）は何も写さなかった（fault も無い）。L2 の select にして通った（RADV が GFX9 以降で使う形）。A5 の VMID 0 の copy は SEL 0 のまま（実機で ok の物を変えない）。
- `engines.c`: check program の load と dispatch の command を `amdrdna_check_load`・`amdrdna_check_commands` に切り出した（A5 の動作は同じ、A5s が再利用）。`gmc.c`: `amdrdna_vram_give`・`amdrdna_vram_bus`・`amdrdna_hub_register`、fault を space へ。`cp.c`: `amdrdna_ring_fence`・`ring_sequence`・`ring_fence_address`。`amdrdna.c`: 最後の段 A5s、attach で `amdrdna_spaces_init`、shutdown は node の停止 → A5 → … の逆順。`control.c`・UAPI: status の予約 3 語を `spaces`（開いている数・held の数）・`context_flushes`・`device_flags`（REGISTERED・RENDERER・FAULTED）に（大きさ 352 のまま）。`amdrdnactl status` に sessions の行。build の行に 6 file。

P5 の executor（`agent/p5-ws039` の 175405996、main に未 merge）との口: P5 の `vk/amdrdna-vk.h` の node の側の関数を全部 `vk-node.c` で実装した（名前と引数は同じ）。p008 に残るのは (a) node の open で `amdrdna_vk_session_open`、close で `amdrdna_vk_session_close`、(b) blob_create の blob_id で `amdrdna_vk_memory_attach`（session の space に map して backing を渡す、nvrtx の `nvrtx_vk_node_blob_attach` と同じ形）、(c) `amdrdna_runtime_bind(node, execute, timelines)` の 2 つの関数（`amdrdna_vk_stream_execute(session->vk, …, amdrdna_vk_dispatch)` と `session->vk->timelines`）、(d) `vk/` の build の行。P5 の file は変えていない。食い違い: 無し（`amdrdna_vk_gpu_run` は P5 の注記どおり「EIO when the work did not finish」）。

設計からの変更・決めたこと:
- p011 の登録は renderer の capability を出さない（executor が無いので）。design 10.1 の COMMAND・NOTIFICATION・JOB・CAPSET は runtime.c に実装して p008 の bind で出す。libvulkan は CAPSET|BLOB|TRANSFER|COMMAND|MAPPING が揃わないと renderer と見ないので、p011 の `/dev/gpu0` は desktop に使われない。
- VMID の pool の判定は design 5.4 のとおり。context の取り上げは「他の ring の fence が終わった」物だけ、同じ ring の残りは pipeline sync（Linux の grab_idle と同じ）。
- VRAM の PTE の MTYPE は NC（Linux の既定）、GART は従来どおり UC。
- page table は design のとおり VRAM（CPU の pool）。空になった leaf は session の close まで残す。
- **run ごとに IB の前で cache の sync（ACQUIRE_MEM、GCR 0xC3B1 = GL2 の invalidate と writeback を含む）**（Linux の `amdgpu_ib_schedule` の mem sync と同じ）。実機で、CPU が BAR0 で書き直した同じ VA の IB を、flush の無い次の run が古い中身で実行した（L2 が古い行を持つ。MTYPE NC の VRAM は CPU の窓の書き込みを L2 が見ない）。sync を入れて通った。p008 の executor も CPU が書く IB・記述子・shader を同じ形で読むので、この sync に頼れる。
- 仕事の終わらない session（lost のまま fence が来ない）の destroy は context を held、table と backing を残す（design 11.3 の quarantine の先取り。reset は p013）。

host 試験（実機の証拠ではない）:
- `plan/ws039/tests/amdrdna-sessions-test.c`（新、host 試験 (d) の後半）: context の保持・世代・別の ring の flush、16 space の LRU（flush 17・取り上げ 2）、別の ring の仕事で待ち・同じ ring で sync、held、release、fence の wrap、pasid（1 から・すぐ再利用しない・65535 で満杯）、PM4（PFP の書き込み 0xc0033700/0x40100000、write-then-wait 0xc0053c00/0x143（compute 0x43）・poll 0x20、pipeline sync 0x113・poll 4、PFP_SYNC_ME 0xc0004200、IB の VMID）を Linux の builder から手で出した語と照合。34 項目 PASS（後で 35）。
- `plan/ws039/tests/amdrdna-host-test.sh` に足した。`audit-symbols.py` は `compiler/` と `vk/` を歩かない形に（Q1 の判断）。一般語 29 個を allow list に。2026-10-16 13:51 PASS（PASS の行 339、audit-symbols を含む。L2 の DMA_DATA の語 0xe0300000 を足して 35 項目）。
- `amdrdnactl gpu [GPU-DEVICE]`（新、`userland/tests/amdrdnactl/gpu-check.c`）: `/dev/gpu0` を開き GET_INFO、VRAM の blob（MAPPABLE|SHAREABLE、64 KiB）の write/read、mmap して照合と書き込み、storage の write/read、export して 2 つ目の open で import して読む。
- build: `make -j16 ZEDBSD_CONFIG=plan/ws039/tests/config.mk BUILD=build/p3/usb AMDRDNA_RUN=A1v build/p3/usb/vmunix build/p3/usb/bin/amdrdnactl` rc 0、warning 0、kernel include check PASS、amd64 vmunix check PASS。`plan/tools/style-check.py` の違反は新規・変えた file で 0。

## 実機（2026-10-16、q1216-i01、10.0.10.3 の RX 5600 XT、USB の zedBSD は DHCP で 10.0.30.5）

手順: p005 と同じ（Debian で USB の sda2 の `vmunix` を差し替え（p005 の物は Debian の `~/ws039/vmunix.before-p11`）→ `hw-run.sh boot 0001` → `AMDRDNA_ZED=root@10.0.30.5 hw-run.sh collect` → 新しい `amdrdnactl` を `/tmp` へ → `amdrdnactl advance A5s`（A2〜A5s が続けて走る）→ 45 秒後に `status`・`ls /dev/gpu*`・`dmesg` → `amdrdnactl gpu` → `reboot` で Debian へ）。電源は全部 warm reboot（Debian 経由）で、毎回 A0 は `sos=0 smu=0x0`。証拠 `build/tmp/amdrdna-hw-p11-{1..6}/`（worktree の中、commit しない）。

| 回 | 時刻 | kernel（vmunix の SHA-256 の先頭） | 結果 |
| --- | --- | --- | --- |
| 1 | 13:34 | 3f710ce1…（b0940834d） | A5s fail: 移した語が 0 |
| 2 | 13:40 | 15a70e5f… | A5s fail: WRITE_DATA の 2 語は session の VA に着いた（0x3e9/0x3ea）、DMA_DATA は写さない、page table と hub の context 1 の base は期待どおり → DMA_DATA を L2 の select に |
| 3 | 13:44 | 183c2923… | dispatch と move は ok、context の検査の pass 17（flush 無しの run）が前の run の印を読んだ → run ごとの cache の sync |
| 4 | 13:47 | 01097983…（最終） | **A5s ok**、`/dev/gpu0`、`amdrdnactl gpu` ok（道具の storage の usage と export の fd の誤りを直した後） |
| 5 | 13:52 | 同上 | **A5s ok**、`/dev/gpu0`、`amdrdnactl gpu` ok |
| 6 | 13:55 | 同上 | **A5s ok**、`/dev/gpu0`、`amdrdnactl gpu` ok |

最終の kernel（4〜6 回）の印:
- `A5s session run ok at 0x100000000: written, moved, dispatch 1536 words`（session の VA の IB で WRITE_DATA・L2 の DMA_DATA・check program の dispatch（code・記述子・output 全部 session の VA）、1536 語が全部一致）
- `A5s contexts ok: 16 sessions, 18 runs, drops 17 (17 expected), changes of hands 2 (2 expected)`
- `space 18 lost: page fault at 0x1000 in context 4`、`vm fault gfx hub ctx=4 space=18 at 0x1000 access=0x20 status=0x00440b56 unit=0x5 walker=3 denied=0x5`（IV の pasid が IH_VMID_LUT に書いた space の id と一致）、`space 18 kept: its work in context 4 did not end`（compute queue 2 は書き込みの後で止まったまま: run は EIO、context 4 は held、table と backing は残る）、`A5s vm fault check: … fault named the space`
- **`A5s ok sessions spaces=18 contexts=14 flushes=17 takeovers=2 dispatch=1536 fault=named space=18 gpu device registered (no display)`**
- status: `stage=A5s failed=none`、`sessions spaces=0 held=1 flushes=19 device=0x1`（REGISTERED）、`crw-rw-rw- … /dev/gpu0`
- `amdrdnactl gpu`: `driver=amdrdna capabilities=0x14d resources=1024 bytes=67108864`、blob 64 KiB の transfer 4096 byte・mapping 64 KiB・storage の transfer・export/import（2 つ目の open で同じ byte）が ok、`gpu ok`。終わった後 `spaces=0`（open/close で space が返った）。

確かめていない物: cold power cycle の直後の A5s（warm だけで 6 回）、停止の hook の行（reboot の log は file system の後で disk に残らない。Debian には毎回戻り、次の A0 は sos=0）、`stop_begin`・`stop_poll`・`fault` の op（GPU に仕事を出す core の session が無いので core が呼ばない。fault は unregister の時だけ）、executor を bind した renderer（p008）、長い時間の動作、多数の session の同時の run、VRAM の断片化。compute queue 2 と context 4 は fault の検査の後 reboot まで止まったまま（取り消しと復帰は p013）。

## 状態と次

- [x] 実装、host 試験、build
- [x] 実機（10.0.10.3）: `advance A5s` で A5s の ok、`/dev/gpu0`（最終の kernel で 3 回）
- [ ] Q1: merge と clearance の判定
