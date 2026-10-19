# ws269-p010d: render の FW の object（前半）

- ID: ws269-p010d（親 [ws269](../ws.md)）
- Status: in-progress（実装と host 試験は済み。実機・T1 の試験は p010 の最後にまとめる。Q1 の判定待ち）
- Disposition: normal
- Queue: q1373 / q1373-i01（2026-10-20 P1）
- 設計: [design-p010.md](../design-p010.md) 版 2.1 の §4.1・§10 の p010d
- 依存: p010b（cleared）、O-P006、O-P009

## 受け入れ条件（design-p010.md §10 の p010d の行）

- heap: PDSCODEDATA・USCCODE の中の割り当て、TRANSFER_FRAG を足す。region header は GENERAL（RGNHDR の heap は作らない）。
- static data area（§4.1、各 heap の割り当ては次の page から、BUG-342 と揃える）。
- free list: global（固定 16 MiB）と local（min 25 page 以上）。HWRT（rt_data 2 組、`cleanup_state`）。
- GROW_UPDATE（delta 0、ready 0、new = current＋ready）、RECONSTRUCTION の応答、UPDATE_STATS を印に。
- CLEANUP（HWRTDATA・FREELIST、busy の上限で isolate）、時間切れの後は core に残す。最初の render で作る（compute だけの session には作らない）。
- FW の heap の見積もりの表、`interface.h` の出典を v7.3 に、p010b の SHA の一覧に `_client_check.h`・`_check.h`・`pvr_ccb.c`・`pvr_fw.c`・heap_config・`pvr_vm.c`・`pvr_queue.c`・`pvr_cccb.c` を足す。
- host 試験（FW の struct の offset を check の header の値と static assert、HWRT の値の計算、GROW・RECONSTRUCTION の応答の frame）、build（VF2・riscv64-virt・amd64、warning 0）。

## 実装（2026-10-20 P1、commit は下の「commit」）

kernel（`src/drivers/gpu/powervr/`）:
- `render-target-layout.h`・`render-target-layout.c`（新規、lock も memory も持たず host で build できる）:
  - FW の record を自分の型で定義し、offset と size を Linux v7.3 `pvr_rogue_fwif_check.h` の値と `_Static_assert` で照らす: free list（64 byte、13 field）、hwrtdata_common（88 byte、14 field）、rta_ctl（32 byte）、hwrtdata（384 byte、19 field、`cleanup_state` 320）。
  - `powervr_target_lay_out`: frame の大きさから MLIST・region header・TPC・VHEAP の byte 数と、TE・PPP・ISP の register の値（Mesa `pvr_arch_job_render.c` の手順、BXE: tile 16×16、format 2、macrotile 4×4、region header 6 byte/2×2、macrotile array 0）。1 sample・1 layer だけ（他は EINVAL。MSAA と layer は p010 の範囲外）。端は 8192 まで。
  - record の組み立て（common・data の半分・free list）、grow・rebuild・statistics の notice の読み、GROW_UPDATE・RECONSTRUCTION_UPDATE・CLEANUP の command の組み立て。
- `render-target.c`（新規）: session の render target と free list、program の heap。
  - `powervr_session_target`: 最初の呼び出しで global free list（4096 page = 16 MiB、grow 0、ready 0）を作る。shape（幅・高さ・sample・layer）を key に最大 4 個を cache、5 個目は LRU を CLEANUP して作り直す（busy なら EBUSY）。HWRT ごとに local free list（32 page = 25 を 16 page の段に切り上げ）、MLIST（2 組を 1 buffer）、region header（2 組を 1 buffer、GENERAL）、VHEAP、TPC（どれも GENERAL、core に uncached）、FW の block 1 つ（data 0 @0、data 1 @384、common @768、856 byte）。呼ぶのは p010n の render（p010d では呼び出し元が無い。compute だけの session は作らない）。
  - free list の page の番号の表は session の GENERAL に写像、page 本体は写像しない（PM が MLIST で引く）。FW の identity は render の通し番号、render の `free_lists` に並べ、FW の要求を identity で引く。
  - notice（FWCCB、core の lock の中、`render-queue.c` の `handle_notice` から）: 103 GROW → delta 0・new = page 数・ready 0 の GROW_UPDATE（110）を待たずに送る、`grow_requests` を数える。104 RECONSTRUCTION → 名指しの list の page の表を書き直し、stack top = current − 1、allocated 0、local なら HWRT の 2 組を state 9（HWR）・HAS_LAST_GEOM を落とす、RECONSTRUCTION_UPDATE（112）を返す。107 UPDATE_STATS → `partial_renders`・`out_of_memory` を数える。`powervr_render_target_events` で読める（p010n が render の前後で比べて `ta: fail(free list)` を出す）。
  - CLEANUP: 各 HWRT の data（後ろの半分から）→ local list → global list。busy の上限は **50 回・10 ms の間・合計 2000 ms**（`CLEANUP_TRIES`・`CLEANUP_PAUSE_MS`・`CLEANUP_LIMIT_MS`）。超えたら EBUSY で、`render_close` が session ごと core に残す（kept_sessions、reset で返す）。
  - 時間切れの後: vk-queue の submit が時間切れで executor を捨てる時に `session->abandoned = 1`。`render_close` は abandoned なら CLEANUP を送らずに session を残す（止まった context に CLEANUP を送り続けない）。reset・abandon の時の `powervr_session_firmware_discard` が `powervr_session_targets_discard` で FW の record の run を落とし、buffer を返す（写像は session の table と一緒に消える）。
  - program の heap: session に PDSCODEDATA・USCCODE・TRANSFER_FRAG の space。PDS・USC は base＋0x10000 から（先頭の page は FW の static data area、mainline の reserved 1 page。0x1000〜0x10000 は空き）で、最初の区画を executor の固定の場所（PDS 0x2000・USC 0x10000）として作成の時に取る（`vk-queue.c` は今の固定の写像のまま、`_Static_assert` で一致を確かめる）。`powervr_session_program_map`・`_unmap` が以後の割り当て。TRANSFER_FRAG は base（0xE4_0000_0000、16 GiB）から。
- `interface.h`: notice 103・104・107、command 110・112、cleanup の種類 1・2、PDS・USC の大きさ、TRANSFER_FRAG、static area 1 page を足した。StarFive 6.12 を出典にしていた block（KICK・MMUCACHE・CLEANUP、notice、memory context、compute context、client CCB、compute の command、GENERAL の heap）を v7.3 の check header・fwif.h と照らし、値が同じなので出典を `Linux v7.3` に直した。
- `render-private.h`（session と render の field、宣言）、`render-queue.c`（heap の init、notice の振り分け、discard）、`render.c`（close の CLEANUP と destroy）、`vk-queue.c`（abandoned、static assert）、`platform/riscv64/vmunix.mk`（2 file）。

### FW の heap と memory の見積もり

| 物 | FW の heap | GPU の memory |
| --- | --- | --- |
| global free list（session に 1 つ） | record 64 byte → 1 page | page 16 MiB（連続）＋ 番号の表 16 KiB |
| HWRT 1 個（1920×1080） | block 856 byte → 1 page、local list の record 1 page | local の page 128 KiB＋表 4 KiB、MLIST 2×4 KiB、region header 2×12992 → 28 KiB、VHEAP 4 KiB、TPC 32 KiB（約 200 KiB） |
| session の最大（HWRT 4 個） | 9 page = 36 KiB（FW の heap の一般の部分 15 MiB） | 約 17 MiB |

### static data area と BUG-342

- main に BUG-342 の修正はまだ無い（2026-10-20 08:50 の main fc4d81dd5、`render.c` の GENERAL の space は base＋0 から）。p010d は GENERAL の space の初期化（`render.c` の `render_open`）に触らず、BUG-342 の修正と同じ行を変えない。HWRT の buffer は session の GENERAL の space から取るので、BUG-342 が先頭の page を空けると自動で次の page からになる。
- PDS・USC の heap は p010d が先頭の 1 page を空けた（中身の VDM sync・EOT の program は BUG-342 か p010e）。

## 確かめ（2026-10-20）

| 確かめ | コマンド | 結果 |
| --- | --- | --- |
| target の host 試験と p010a・p010c の試験 | `sh plan/ws269/tests/p010/run.sh build/p010d/p010` | `stage-host-test: 24 passed, 0 failed`、`image-host-test: 55 passed, 0 failed`、`target-host-test: 77 passed, 0 failed`、`selftest-host: 0 failed` |
| executor（p009） | `sh plan/ws269/tests/p009/run.sh build/p010d/p009` | `powervr executor host test: 160 passed, 0 failed` |
| MMU（p006） | `sh plan/ws269/tests/p006/run.sh build/p010d/p006` | `PASS 68 FAIL 0` |
| VF2 の kernel | `timeout 3600 make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/p010d/visionfive2 vmunix -j16` | rc=0、`warning:`・`error:` 0 件、`render-target.o`・`render-target-layout.o` あり |
| riscv64-virt | 同 `config-riscv64-virt.mk BUILD=build/p010d/riscv64-virt` | rc=0、0 件 |
| amd64 | 同 `config-amd64.mk BUILD=build/p010d/amd64` | rc=0、0 件 |
| `git diff --check` | | 空 |

`target-host-test` の中身: 1920×1080（tile 120×68、macrotile 30×18、region header 2160 個・12992 byte、TPC 32768、VHEAP 1536、MLIST 4096、TE・PPP・ISP の値）、800×600、272×16（奇数の tile）、8192、拒否する shape 4 つ。record を byte で読み check header の offset で確かめる（common 9 項、data 11 項、free list 5 項）。GROW の request（103）の読みと answer（110: list・delta 0・new 4096・ready 0・残り 0）、RECONSTRUCTION（104 → 112、id の写し、30 個は 18 に）、UPDATE_STATS（107）、CLEANUP（106、種類 1・2）。期待値は Mesa の手順から手で計算した。

static assert は kernel と host の build で効いている（offset が違えば build が止まる）。

## 未実施・残り

- 実機（VF2）: p010d に呼び出し元が無いので、HWRT・free list が実際に作られるのは p010n の render から。FW が record を受けるか（GROW・RECONSTRUCTION の応答を含む）は p010e の実機の確かめで。QEMU は起動していない。
- MSAA・複数 layer の HWRT（RTC・SRTC・RAA）は作らない（EINVAL）。p010 の範囲外。
- BUG-342: GENERAL の static data area と、PDS・USC の先頭の page の中身は BUG-342 の修正の側。
- kick の `cleanup_ctl_fw_addr`（`powervr_target_cleanup_address`）を付けるのは p010n。
- `interface.h` の `POWERVR_NOTICE_PAGE_FAULT` の名前の直しは p010n（design §4.1）。

## commit

- 241dded0b（source）、他は返却の SHA。
