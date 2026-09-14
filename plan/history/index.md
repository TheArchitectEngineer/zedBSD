<!-- awesome-plan-current:start -->
Active Queue: Active Queue: none
Last finished Queue: q313 / ws014-p010 cleared
WS014: incomplete; p004 planning, not queued
WS029: planning; p001-p007 planned
WS030: completed
WS003: retired, reuse prohibited
<!-- awesome-plan-current:end -->

# Past Log

## q314完了: WS029 i915ネイティブGPU driver（2026-09-14）

q314 finished、ws029-p001..p007 全 cleared。実機（Latitude 5330、IGD 8086:46a8、VFIO passthrough）で attach → selftest（BCS0 が store を実行し user interrupt）→ /dev/gpu0 公開まで到達し、userland /bin/gpu-i915-test が copy/fill/store/job を実行、host が QMP pmemsave で guest RAM を独立照合して PASS（test-002）。Linux i915（MIT）の定義/テーブルは出典付き .inc へ転記、driver 論理は zedBSD 規約で新規実装。HAL/UAPI 不変。静的解析 gcc -fanalyzer / clang --analyze 0 件、規約 §14 確認、host fixture・GPU core 回帰・build 3 構成 PASS。制限: cold VFIO attach の bring-up 間欠ハング（最優先の後続）、hang 注入の実機 peer 継続未達、display/scanout は対象外。後続は WS029 registry の planning 行に列挙。source/doc の git add/commit/push はユーザー担当。
## p006 実機結果（VFIO passthrough テストループ、2026-09-14）

Latitude 5330（`awe@10.0.10.25`）の IGD（`0000:00:02.0`、`8086:46a8` rev 0c）を `host-igd.sh` で vfio-pci に切替え、i915 selftest 付き image を QEMU で起動して attach → selftest → `/dev/gpu0` 公開まで到達した。各 attempt の後に i915 と GDM を復旧した。

### 復旧 rehearsal（QEMU なし）

`plan/ws029/phase006/host-rehearsal.json`。`attach` で driver=vfio-pci、`/dev/vfio/0` 出現、GDM inactive。`restore` 後 driver=i915、GDM active、`driver`/`gdm`/`dev_vfio`/`drm_nodes` が開始前と一致（`restored=true`）。復旧手順が成立することを確認。

### attempt 履歴（同条件の修正と再実行）

| attempt | mode | 結果 | 原因・修正 |
| --- | --- | --- | --- |
| boot-001 | boot-only | fail | harness の import 依存 `venus_rfb.py` を転送していなかった → 転送一覧に追加 |
| boot-002 | boot-only | fail | UEFI loader が GOP framebuffer 無しで `Locate GOP` 停止 → QEMU 引数を `-vga none` から `-vga std`（表示 backend なし）に変更 |
| boot-003 | boot-only | fail(attach) | i915 が `map-registers` で EINVAL → BAR0 を `drv_pci_device_claim_bar` してから map するよう修正（PCI は claim した BAR しか map させない） |
| boot-005 | boot-only | 進捗 | BAR0 は正しく map（regs va=0xffffffffe0000000, bus=0x380010000000）、GGTT/MSI まで到達、engine bring-up で停止 → 段階 log 追加 |
| boot-006 | boot-only | 進捗 | 両 engine init と `engines started` まで到達、selftest で停止 → selftest に log 追加 |
| **boot-007** | boot-only | **pass** | `i915: selftest bcs0 store=ok irq=1 seqno=1/1`、`registered native GPU node`。実機の BCS0 が store を実行し user interrupt を上げた |

### 受入

- attach 全段階（PCI enable、BAR0 claim/map、forcewake、GT reset、GGTT、MSI、engine×2、selftest、publish）を通過。
- `boot-007` boot-pass: guest.log に `attach stopped` なし、`selftest bcs0 store=ok`、`registered`。
- host 復旧: attempt 後 driver=i915、GDM active（`host_restored=true`）。attach 中は driver=vfio-pci、`/dev/vfio/0`、GDM inactive。
- 証拠: `plan/ws029/temp/remote/q314-i915-boot-007/`（result.json、guest.log、qemu.log、qmp.jsonl、console）、`plan/ws029/phase006/host-rehearsal.json`。

実機で GPU が実際に命令を実行した最初の到達点。copy/fill/store/job と host 側 RAM 照合は p007。

## q314 p005 完了: drv_gpu 統合、native stream、生 UAPI 試験クライアント（2026-09-14）

[WS029 p005](https://github.com/awemorris/zedBSD/issues/402)（q314-i05）を cleared にし、[p006](https://github.com/awemorris/zedBSD/issues/403)（q314-i06、VFIO passthrough テストループ）を in-progress にする。実機は未使用。HAL/UAPI 変更なし。git add/commit/push はユーザー担当。

新規・変更（すべて local）:
- `plan/ws029/i915-native-stream.md`: native stream の確定版（header 32 byte、relocation 16 byte、engine/timeline の対応、job の意味、試験 batch、harness 向け handle 対応付け）。
- `src/drivers/gpu/i915/i915.c`: `drv_i915_stream_parse`（magic/version/engine/count/dwords/flags/reserved/bytes 一致/末尾 `MI_BATCH_BUFFER_END`/relocation 範囲を検査）、`i915_submit_stream`（session の batch pool（最大 32、空き object を再利用）へ copy、relocation を handle→session object の VA で patch、request を投入）、`i915_submit_marker`、`i915_command`（同期受理）、`i915_command_submit`（bytes 0 は marker、timeline 0/1=BCS0、2=RCS0）、`i915_command_drain`（`retire_waitq` で pending 0 まで待つ）、jobs（`reserve`: slot 確保＋callback 保持、`commit`: marker 投入、`cancel(0)`: slot 解放、`cancel(fault)`: RETAINED で保持、`capacity`: 空き slot 数）、recovery（`stop_begin`/`stop_poll`(pending で EAGAIN)/`isolate`(session の request を EIO で回収し、active なら engine reset して他 session を継続)/`fault`(全 request 回収、`failed=1`)/`reset_device`(GT reset、engine 再 program、quarantined object/VM 解放)）。capabilities = RESOURCE|TRANSFER|COMMAND|NOTIFICATION|JOB|JOB_CAPACITY。resource log に `handle=` を追加。
- `request.c`: RESERVED/RETAINED slot も `request_fail` で回収、retire 時に batch を pool へ戻し `retire_waitq` を起こす。`engine.c`: `drv_i915_engine_recover`（irq_lock 外で engine reset、`resetting` 中は handler と kick が待つ）。`internal.h`: stream 定数、session の object/batch list、`retire_waitq`。
- `userland/base/tests/gpu-i915/{main.c,Makefile}`（`/bin/gpu-i915-test`、libvulkan 非依存）: `GPUI915 START` → GET_INFO（driver_name/capabilities）→ 64 KiB resource ×2 → pattern write → copy（`XY_SRC_COPY_BLT`）→ 照合 → fill（`XY_COLOR_BLT`）→ 照合 → store（`MI_STORE_DWORD_IMM`）→ 照合 → job（RESERVE/COMMIT/WAIT）→ `GPUI915 PASS copy=1 fill=1 store=1 job=1 src_handle=<h> dst_handle=<h>`、失敗は `GPUI915 FAIL stage=<s> errno=<e>`。両 config の `ZEDBSD_USER_PROGRAMS` に追加。
- fixture: `i915-stream-test.c`（新規、正常 3 形と不正 13 形）、`i915-fixture.inc` に waitq stub と `XY_SRC_COPY_BLT`/`XY_COLOR_BLT` の emulation、`i915-backend-test.c` に ops 経由の stream（relocation patch、copy/fill の結果照合、不正 handle/長さ拒否）、marker、drain、jobs（capacity 32→31、rollback、commit 完了、fault cancel の保持）、stop_begin/poll、isolate（hang した session を engine reset で切り離し peer の request が継続）、fault→open ENODEV→isolate→close→reset_device→再 open 成功。

検証（agent-1）:
- `sh plan/ws029/tests/run-i915-host-tests.sh`（uncore/gtt/irq/lrc/stream/backend、通常＋ASan/UBSan）: PASS。
- kernel build 3 構成 PASS（warning 0）、`make ... disk-image`（i915 config）PASS、`build/i915-amd64/rootfs/bin/gpu-i915-test` を確認。
- `run-i915-build-selection-test.py` PASS、`git diff --check` PASS。

制限: 実機での blit/interrupt は未証明（p006/p007）。RCS0 の 3D state は対象外（`MI_STORE_DWORD_IMM`/`PIPE_CONTROL` 経路のみ）。

## q314 p004 完了: 実行（engine/LRC/execlists、request/seqno、engine reset、selftest）（2026-09-14）

[WS029 p004](https://github.com/awemorris/zedBSD/issues/401)（q314-i04）を cleared にし、[p005](https://github.com/awemorris/zedBSD/issues/402)（q314-i05、drv_gpu 統合）を in-progress にする。実機は未使用（selftest の実行は p006）。HAL/UAPI 変更なし。git add/commit/push はユーザー担当。

新規・変更 source（すべて local、Zlib。転記 `.inc` は MIT 表示付き）:
- `linux/i915-commands.inc`（63 定義: MI_*/XY_*/PIPE_CONTROL）、`linux/i915-lrc-offsets.inc`（`gen12_xcs_offsets`/`gen12_rcs_offsets` と NOP/LRI/REG/REG16/END の encode macro を `I915_LRC_*` に改名して verbatim 転記）、`linux/i915-mocs.inc`（LE_/L3_/L4_ macro、`MOCS_ENTRY`、`GEN11_MOCS_ENTRIES`、`gen12_mocs_table`）。generator `plan/ws029/tests/gen-inc.py` に verbatim block 転記と `BUILD_BUG_ON_ZERO` 除去を追加。`i915-regs.inc` に `GEN11_GRDOM_RENDER`、`GEN9_LNCFCMOCS`、`BLIT_CCTL_*_MOCS_MASK`、`GEN12_GFX_PREFETCH_DISABLE` を追加（168 定義）。
- `engine.c`: `drv_i915_engines_start`（forcewake GT+RENDER を device 寿命で保持、global MOCS 64 entry と LNCFCMOCS 32 pair を書込み、RCS0/BCS0 の HWSP object と kernel context を作成し `i915_engine_program`: HWSTAM、`GEN11_GFX_DISABLE_LEGACY_MODE`、STOP_RING 解除、HWS_PGA、EMR/EIR/ESR、BCS の `BLIT_CCTL` を uncached MOCS index 3、CSB pointer reset）、`drv_i915_engine_reset`（STOP_RING+PREFETCH_DISABLE→MODE_IDLE 待ち→`RESET_CTL` request/ready→`GDRST` engine domain（2 回書き）→cancel→再 program）、`drv_i915_engine_interrupt`（irq_lock 内で CSB 消費→seqno retire→次 request 投入、lock 外で完了 callback）、`drv_i915_engine_idle`。
- `lrc.c`: `drv_i915_lrc_create`（RCS 14 page / BCS 2 page の image を GGTT に bind、context ごとに 64 KiB ring、offset 列を `MI_LOAD_REGISTER_IMM|LRM_CS_MMIO(|FORCE_POSTED)` に展開、CONTEXT_CONTROL の inhibit、PDP0=PML4、MI_MODE pair の STOP_RING 解除、RING_START/HEAD/TAIL/CTL、末尾 `MI_BATCH_BUFFER_END|1`、descriptor low=64B addressing|VALID|PRIVILEGE|GGTT、high=sw_id<<5|class<<29|instance<<16）、`submit`（image tail 更新→ELSQ port1=0/port0=desc|FORCE_RESTORE→`EL_CTRL_LOAD`）、`reset_csb`、`csb_consume`（HWSP write pointer、entry -1 なら mmio mirror、gen12 parse: away 無効または new queue で promotion、それ以外 completion）、`ring_space`/`ring_emit`（末尾 NOOP 詰めで wrap）。
- `request.c`: slot 32、FIFO queue、`kick`（engine idle 時のみ emit+submit: preparser disable→TLB invalidate flush(BCS: MI_FLUSH_DW、RCS: PIPE_CONTROL)→extra dwords→`MI_BATCH_BUFFER_START_GEN8|NON_SECURE`(48-bit PPGTT)→breadcrumb（BCS: flush + `MI_FLUSH_DW` post-sync store to HWSP seqno via GGTT、RCS: PIPE_CONTROL flush + QW_WRITE）→`MI_USER_INTERRUPT`→ARB enable→ARB_CHECK/NOOP）、`retire`（HWSP seqno 一致）、`fail`（queue/active を error で回収）、`complete_list`（`drv_gpu_complete` を lock 外で呼び slot 解放、`drv_gpu_capacity_changed`）。
- `selftest.c`（`CONFIG_DRIVER_PCI_I915_SELFTEST=y` のみ link）: kernel context で BCS0 に `MI_STORE_DWORD_IMM|USE_GGTT`（HWSP scratch dword に 0xdeadbeef）を含む request を投入し 100 ms 以内に値・seqno・user interrupt 増加を確認、`i915: selftest bcs0 store=%s irq=%u seqno=%u/%u`。失敗は attach 失敗。
- `irq.c` が engine へ転送、`i915.c` は attach で engines start（+selftest）、open で engine ごとの context 作成、close で破棄（quarantine 時は保持）、stop で GT reset→engines stop→object 回収。`uncore.c` に `drv_i915_domain_reset`。`platform/amd64/vmunix.mk` に engine/lrc/request と条件付き selftest、`plan/ws029/tests/config-i915-selftest-amd64.mk`。
- fixture: `i915-lrc-test.c`（新規: image layout が Linux の CTX_* index と一致、descriptor、ELSQ 書込み、CSB 判定と mirror fallback、ring wrap/space）、`i915-fixture.inc` に execlists emulator（ELSQ load を記録し `fixture_run_engines()` が image→ring を parse: MI_STORE_DWORD_IMM(GGTT/PPGTT)、MI_FLUSH_DW store、PIPE_CONTROL QW write、MI_BATCH_BUFFER_START を PPGTT 経由で追跡、MI_SEMAPHORE_WAIT で hang、CSB event 2 件と割込み identity を作り handler を呼ぶ; MI_MODE/RESET_CTL/GDRST の応答）、`i915-backend-test.c` に engine 初期化 register 値、selftest 成功、batch による resource 書込みと完了 callback、hang→`request_fail(EIO)`→engine reset→再実行を追加、`i915-irq-test.c` に engine 転送の確認。

検証（agent-1）:
- 3 構成 build PASS（i915、i915+selftest、GPU なし: `drv_i915_` symbol 0）、warning 0、`amd64 vmunix check: PASS`。
- `sh plan/ws029/tests/run-i915-host-tests.sh`（uncore/gtt/irq/lrc/backend、通常＋ASan/UBSan）: PASS。
- `python3 plan/ws029/tests/run-i915-build-selection-test.py`: PASS。`git diff --check`: PASS。

設計との差分: ring は engine 共有ではなく context ごと（execlists は context save で RING_HEAD を image に書き戻すため、共有 ring では head/tail が食い違う。Linux と同じ構成）。forcewake は attach 後に恒久保持（IRQ 文脈からの ELSQ 書込みで ACK 待ちを避けるため）。`CTX_R_PWR_CLK_STATE` は 0（RCS の 3D 利用は WS029 後続）。

制限: 実機での CSB/割込み挙動は未証明（fixture の model は Linux の parse 規則に基づく）。1 engine 1 request 直列。`GPU_CAP_COMMAND`/JOB/recovery ops は p005。

## q314 p003 完了: メモリ（GEM object、48-bit PPGTT、CPU view）（2026-09-14）

[WS029 p003](https://github.com/awemorris/zedBSD/issues/400)（q314-i03）を cleared にし、[p004](https://github.com/awemorris/zedBSD/issues/401)（q314-i04、実行）を in-progress にする。実機は未使用。HAL/UAPI 変更なし。git add/commit/push はユーザー担当。

新規・変更 source（すべて local、Zlib）:
- `src/drivers/gpu/i915/ppgtt.c`: `drv_i915_ppgtt_create`（scratch page → scratch PT/PD/PDP の連鎖、PML4 は scratch PDP で充填）、`destroy`、`va_alloc`（bump、開始 0x1_0000_0000、2 MiB 揃え、再利用なし）、`insert`（4-level walk、欠けた table を 4 KiB page で確保し下位 scratch entry で充填）、`clear`（leaf を scratch に戻す）、`lookup`（試験用）。encode: page/table = `GEN8_PAGE_PRESENT|GEN8_PAGE_RW`（PAT index 0）、scratch table は Linux の `gen8_pde_encode(I915_CACHE_NONE)` と同じ `PAT0|PAT1`。
- `src/drivers/gpu/i915/gem.c`: `drv_i915_gem_create`（`kern_pmem_alloc_limited` 4 KiB 揃え・39-bit 上限・zero fill、device list に登録）、`destroy`（bound/quarantined なら保持）、`bind/unbind_ggtt`、`bind/unbind_vm`、`read/write`（範囲再検査、barrier）。
- `src/drivers/gpu/i915/internal.h`: `struct i915_ppgtt`/`i915_ppgtt_page`/`i915_gem_object`、session は `vm` を別 allocation で保持（quarantine 時に close で device の `quarantined_vms` に移し、reset/stop で解放）、device の object list と counter。
- `src/drivers/gpu/i915/i915.c`: capabilities = `GPU_CAP_RESOURCE|GPU_CAP_TRANSFER`、`open`（session 番号、PPGTT 作成）、`close`（PPGTT 破棄または quarantine 保持）、`resource_create`（`GPU_RESOURCE_USAGE_STORAGE` のみ、1..16 MiB、VM へ bind、log `i915: resource session=%u slot=%u bytes=%llu phys=0x%llx va=0x%llx`）、`resource_destroy`（quarantine 時は保持）、`resource_read/write`（mutex 下で copy）、`i915_stop` が残存 object と quarantined VM を解放。
- build: `platform/amd64/vmunix.mk` に `ppgtt.c`/`gem.c`。監査に `gt/intel_gtt.c`（MIT）を追加（29→30 ファイル）。
- fixture: `plan/ws029/tests/i915-gtt-test.c` に PPGTT 3 試験（scratch 連鎖と encode 値、index bit ごとの walk と table 確保数、VA allocator）、`i915-backend-test.c`（新規: 登録→attach→publish→open/get_info→create/write/read/destroy→close→unpublish→detach、6000 B が 2 page に丸められ VA 0x1_0000_0000、16 MiB 上限、EINVAL/ENOMEM 巻戻し、quarantine 保持と detach での回収、lease 全解放）、`i915-fixture.inc` に mutex/PCI attach/drv_gpu 登録の stub と 32 MiB の偽 page pool。

検証（agent-1）:
- `make BUILD=build/i915-amd64 ZEDBSD_CONFIG=plan/ws029/tests/config-i915-amd64.mk vmunix`: PASS（warning 0、`amd64 vmunix check: PASS`、`drv_i915_*` 28 symbol）。I915 := n: PASS、symbol 0。
- `sh plan/ws029/tests/run-i915-host-tests.sh`（uncore/gtt/irq/backend、通常＋ASan/UBSan）: PASS。
- `python3 plan/ws029/tests/run-i915-build-selection-test.py`: 6 platform × Venus/i915 各 y/n PASS（i915 object 6 個は amd64 かつ y のときだけ）。
- `git diff --check`: PASS。

判明した事項: quarantined session の close で page table が漏れる設計穴を fixture が検出し、VM を別 allocation にして device 側 list へ移す形に直した（close は失敗できない契約のため close 時に allocation しない）。

制限: 実機未接続。`GPU_CAP_COMMAND`/JOB は p004–p005。resource 上限は 1 object 16 MiB、物理連続。

## q314 p002 完了: driver 骨格（PCI attach、MMIO/forcewake、GGTT、割込み）（2026-09-14）

[WS029 p002](https://github.com/awemorris/zedBSD/issues/399)（q314-i02）を cleared にし、[p003](https://github.com/awemorris/zedBSD/issues/400)（q314-i03、メモリ）を in-progress にする。実機は未使用。HAL/UAPI 変更なし。git add/commit/push はユーザー担当。

新規 source（すべて local、Zlib）:
- `include/drivers/i915.h`: `drv_i915_pci_driver_register()` のみ公開。
- `src/drivers/gpu/i915/internal.h`: `struct i915_device`/`i915_ggtt`/`i915_session`、定数（engine slot、forcewake domain、timeout、予約 GGTT page、IRQ bank）、cross-file prototype。
- `src/drivers/gpu/i915/i915.c`: ID 表（ADL-P/ADL-N/RPL-U/RPL-P、`8086:46a8` を含む 54 ID）、attach → start（stage: dma-provider、save-pci-state、enable-pci-memory、bar0、map-registers、uncore、gt-reset、ggtt-probe/scratch/bitmap/fill、enable-bus-master、irq、gpu-publication）→ publish（`drv_gpu_ops` v9、capabilities=0、open/close/get_info のみ）、stop/detach の逆順解放。失敗時 `i915: attach stopped at <stage>: <errno>`。
- `src/drivers/gpu/i915/uncore.c`: `drv_i915_read32/write32`（BAR0 窓の範囲検査）、`drv_i915_wait32`（`sched_ticks` 上限）、`drv_i915_forcewake_get/put`（GT/RENDER の 2 domain、masked write、ACK poll 50 ms、参照計数）、`drv_i915_uncore_init`（全 domain 解放、GDRST 進行中なら待つ）、`drv_i915_gt_reset`（GRDOM_FULL、1 s）。
- `src/drivers/gpu/i915/ggtt.c`: `drv_i915_ggtt_start`（GMCH 0x50 の GGMS から entry 数、BAR0 上半分を map、scratch page、bitmap、全 PTE を scratch で埋めて flush）、`alloc/free`（first-fit、先頭 1 MiB 予約）、`insert/clear`（PTE=phys|PRESENT、`GFX_FLSH_CNTL`）、`stop`。
- `src/drivers/gpu/i915/irq.c`: `drv_i915_irq_start/stop/reset`（MSI 1 本、RENDER_COPY enable に user/CS error/context switch/semaphore、RCS0/BCS0 mask、他 class は disable/mask、master enable）、`drv_i915_irq_handler`（master disable→bank→selector→identity valid 待ち→class/instance/intr→counter→ack→master enable）。今は counter（user/context switch/error/unknown/identity timeout）だけを更新し、p004 で request 処理へ接続する。
- `src/drivers/gpu/i915/linux/i915-regs.inc`（163 定義）、`linux/i915-ids.inc`（4 表）: `plan/ws029/tests/gen-inc.py v6.19` が Linux v6.19 の MIT ファイルから `#define` だけを機械転記（`_MMIO` 除去、`REG_BIT`→`I915_INC_BIT` 等、U suffix）。header に各出典の copyright 行、MIT permission notice、出典 path と SHA-256、変換規則を記載。generator は出典 SHA-256 が `i915-license-audit.md` の値と一致し判定が MIT であることを検査し、転記本文が未転記 symbol を参照していれば失敗する。
- build: `Makefile`（`CONFIG_DRIVER_PCI_I915`/`_SELFTEST` 既定 n、-D、`KERN_GPU_BACKENDS` に追加）、`platform/amd64/vmunix.mk`（`AMD64_I915_SOURCES`）、`src/kern/platform/pcat.c`（登録）、`config/drivers/pci.drivers`、`config/kernel-options.list`（selftest bool）、`plan/ws029/tests/config-i915-amd64.mk`。

検証（agent-1）:
- `make BUILD=build/i915-amd64 ZEDBSD_CONFIG=plan/ws029/tests/config-i915-amd64.mk vmunix`: PASS（warning 0、`amd64 vmunix check: PASS`、`drv_i915_*` 14 symbol link）。
- 同 config で I915 := n: PASS、`drv_i915_`/`drv_gpu_register` symbol 0。
- `sh plan/ws029/tests/run-i915-host-tests.sh`（uncore/gtt/irq、通常＋ASan/UBSan）: PASS。forcewake 参照計数と timeout 時の巻き戻し、GDRST self-clear/stuck、範囲外 MMIO 拒否、GGTT 1M entry の scratch fill・first-fit・insert/clear/free・不正引数、IRQ enable/mask 値、bank/identity decode（RCS0/BCS0/未知 class）、CS error の EIR 記録、identity timeout。
- `python3 plan/ws029/tests/run-i915-build-selection-test.py`: 6 platform × Venus/i915 各 y/n で PASS（GPU core はどちらかの backend が y のときだけ、i915 object は amd64 かつ y のときだけ）。
- `git diff --check`: PASS。規約 checklist（forward declaration、purpose comment、`Succeeded:` return、条件分割、for 初期化子なし）を自己確認。

制限: 実機未接続のため hardware 動作は未証明。engine/LRC/submission は p004。`GPU_CAP_*` は 0 のため `/dev/gpu0` は open/get_info しかできない。

## q314 p001 完了: 対象確定・ライセンス境界・移植方針の固定（2026-09-14）

[WS029 p001](https://github.com/awemorris/zedBSD/issues/398)（q314-i01）を cleared にし、[p002](https://github.com/awemorris/zedBSD/issues/399)（q314-i02、driver 骨格）を in-progress にする。source 変更なし、host 状態変更なし。

成果物（すべて local）:
- `plan/ws029/phase001/approval.json`: 開始指示「では、実装してください。」を、MIT 定義/テーブルの分離転記＋論理新規実装、GDM 停止・i915 unbind・VFIO passthrough の承認として記録（設計資料 SHA256 付き）。firmware は必要時に `userland/firmware/<機種>/`。
- `plan/ws029/i915-license-audit.md`（`plan/ws029/tests/fetch-linux-refs.sh v6.19`）: 参照 29 ファイル（`drivers/gpu/drm/i915/` 26、`include/drm/intel/pciids.h`、`include/drm/intel/i915_drm.h`、`include/uapi/drm/i915_drm.h`）を tag v6.19 で取得し SHA256 を記録。判定は全ファイル MIT（SPDX MIT または MIT/X11 permission notice）。GPL のファイルは参照していない。script は MIT 以外があれば非 0 で終了する。
- `plan/ws029/phase001/host-facts.json`（`plan/ws029/tests/host-i915-facts.sh`、読取専用）: Latitude 5330、kernel 6.19.13+deb13-amd64、`00:02.0` = `8086:46a8` rev 0c、i915 bound、iommu group 0 単独（全 17 group）、`CONFIG_VFIO_PCI_IGD=y`、vfio 系 module 解決可、`/dev/vfio` は `vfio` のみ、GDM active、QEMU 10.0.11 に `vfio-pci` あり、RMRR `0x6c000000–0x707fffff`。設計資料 §1 と差異なし。
- `plan/ws029/i915-symbols.md`（`plan/ws029/tests/gen-symbols.py v6.19` が生成）: p002–p005 が転記・参照する 257 symbol を 10 群（補助 macro、device ID/PCI、forcewake、reset、engine register、割込み、command、LRC、GTT、MOCS）に分け、出典 file:line と gen12/ADL-P での有効条件を列挙。値は書いていない。未検出 0。論理の参照元関数（ggtt/ppgtt/uncore/irq/engine/lrc/execlists/reset/emission/mocs）を「転記せず挙動を新規実装」として別表に列挙。
- `plan/ws029/i915-vfio-plan.md`: IGD UPT（画面出力なし）、iommu group 0 単独、RMRR relaxable、`vfio-pci` module、QEMU `-device vfio-pci,host=0000:00:02.0`、`sudo -n` 起動の理由（memlock）、`host-igd.sh attach/restore/status` の手順、許可済み操作（GDM 停止、i915 unbind、VFIO passthrough、restore）と許可外操作（reboot、package 導入、cmdline/modprobe.d/udev/limits 変更、BIOS、他プロセス kill）、既知 risk の扱いを記載。
- `plan/ws029/phase001/host-facts-after.json` と `host-state-diff.txt`: p001 の前後で driver/GDM/`/dev/vfio`/drm node/cmdline が同一であることを示す。

判明した事項:
- `SNB_GMCH_CTRL`/`BDW_GMCH_GGMS_*` は v6.19 では `include/drm/intel/i915_drm.h`（MIT）にあり、`intel_pci_config.h` にはない。`I915_MOCS_PTE` は `include/uapi/drm/i915_drm.h`（MIT）の enum。両ファイルを参照一覧と監査に追加した。
- gen12 の CSB 判定は `GEN12_CSB_SW_CTX_ID_MASK`/`GEN12_IDLE_CTX_ID`/`GEN12_CTX_STATUS_SWITCHED_TO_NEW_QUEUE` を使い、`GEN8_CTX_STATUS_*` は使わない。CSB entry が `-1` のままの場合は `GEN8_EXECLISTS_STATUS_BUF`/`GEN11_EXECLISTS_STATUS_BUF2` の mmio mirror から読む（tgl HSDES 22011327657 相当）。
- LRC image の per-context batch pointer は gen12 では offsets 表の index 0x12（`lrc_ring_wa_bb_per_ctx`）で、`CTX_BB_PER_CTX_PTR` という define は存在しない。p004 では 0 を書く。

検証: 監査 script exit 0、generator 未検出 0、host 前後 diff すべて same。次: p002（`src/drivers/gpu/i915/` 骨格、PCI attach、MMIO/forcewake、GGTT、割込み）。

## q314開始: WS029 i915ネイティブGPU driver（2026-09-14）

ユーザーの「では、実装してください。」により q314 を開始し、[WS029 p001](https://github.com/awemorris/zedBSD/issues/398) を q314-i01 として実行する。提示済みの計画（plan/ws029/i915-design.md、p001–p007）に対する開始指示を、p001 の移植方針（MIT 定義/テーブルの分離転記＋論理の新規実装）と p006 の host 操作（GDM 停止、i915 unbind、VFIO passthrough）の承認として journal に記録する。firmware が必要になった場合は userland/firmware/<機種>/ に置く（現計画では GuC/HuC 不使用で firmware なし）。順序は p001 → p002 → … → p007。見積 1440 active minutes、120 分ごとに点検、同条件 retry 3 回。HAL/UAPI 不変、host reboot・package 導入・cmdline 変更は許可外、git add/commit/push はユーザー担当。開始時点で新実装・試験の成功は主張しない。

## q314準備: WS029 i915ネイティブGPU driver（2026-09-14）

ユーザー指示により[WS029](https://github.com/awemorris/zedBSD/issues/386)へp001–p007を作成し、Queue q314を準備状態で作成した。実行は次の指示で開始し、開始時にp001の移植方針承認とp006のhost操作許可をjournalへ記録する。計画の正本は plan/ws029/i915-design.md（事実、決定、ファイル構成、関数一覧、初期化/実行/割込みの手順、native stream、試験設計、受入）。

決定: 対象はDell Latitude 5330のAlder Lake-P（8086:46a8）、execlists/ELSQ（GuC不使用）、表示は対象外（IGD UPTは画面出力なし）、LLC coherent前提、Linux i915のMIT定義/テーブルを出典付き`.inc`へ分離転記し論理はzedBSD規約で新規実装、HAL/UAPI不変、GEM backingは物理連続16 MiBまで。p001–p005はhost fixtureとamd64 buildで固定し、p006でVFIO passthroughのtest loop（GDM停止→i915 unbind→vfio-pci→QEMU→復旧）、p007で実機のcopy/fill/store/jobとhostのpmemsave照合、静的レビュー、規約全文確認。

Linux参照10ファイルのMIT表記を確認済み（p001で固定tagの全ファイルを機械監査）。host事実: IOMMU group 0単独、vfio-pci module、CONFIG_VFIO_PCI_IGD=y、QEMU 10.0.11、sudo -n可。見積1440 active minutes、120分ごとにレビュー、同条件retry 3回、VM 300秒/build・転送1200秒/host attach・restore各120秒。host reboot・package導入・cmdline変更は許可外。git add/commit/pushはユーザー担当。

## q313開始: GPU監督の共通化仕上げと局所隔離（2026-09-14）

ユーザー指示により[WS014 p010](https://github.com/awemorris/zedBSD/issues/397)をq313-i01の単一Phaseとして実行する。前提はp009の自己レビュー（plan/ws014/gpu-stack-review5.md、SHA256 `05435c48bdfd9168184fc026a1a628ba5d392c91522fa0556b9ed662fa2a200c`）と、その後のframework側実装可否・Venusから移せる処理の回答。S1 停止期限の起点をstop_begin実呼出しへ（D1/B3）、S2 close時のcommit済みjob監督継続（D2）、S3 停止shortcut・fault cancel後のsession失敗・RESERVED回収・停止flag・control期限定数のframework移管、S4 monitor起床の限定とrecovery_ready除去（D4/B6）、S5 停止未確認contextのsession隔離とidle時reset回収（D3/B4/B5）の順に、各段階を限定fixtureで固定してから進める。

既存UAPIのlayout/ioctl番号/sizeは変えず、内部opsは版9へ進める。実QEMUは既存7件の回帰に加え、producer-exit-delayed（既定policyで15秒jobを持つproducer終了後にconsumer fenceが成功）とproducer-exit-hang（event待ちjobで実行期限DEVICE_LOST、他sessionの継続、idle時のreset回収）を新設し、producer-exitは実行期限ERRORへ期待値を更新する。

p009/q312のcleared/finishedを保持し、順序はp010 → p004 planning/未queue → 別WS029。720 active minutes見積・120分レビュー、fixture120秒/build・転送1200秒/VM180秒（hang系300秒）で有限化。追加HAL、stock互換、一般DE/native i915、git add/commit/push、system package/GDM/VFIO変更は含めない。private host/転送・隔離依存build・GitHub同期は既存承認を使用する。開始時点では新実装・試験の成功は主張しない。GitHub Issues/Projectへのq312完了とq313開始の公開は、このsessionでは自動承認レビューにより保留され、outbox/draftsに記録した。

## q312開始: GPUレビュー対応とフレームワーク共通化（2026-09-13）

ユーザー指示により[WS014 p009](https://github.com/awemorris/zedBSD/issues/396)をq312-i01の単一Phaseとして実行する。承認範囲は[review4回答](https://github.com/awemorris/zedBSD/issues/395#issuecomment-5652742665)とGPU共通化の協議。job/fenceの状態・容量待機・期限・session故障と参照保持をdrv_gpuへ寄せ、backendは実資源の予約・投稿・完了と停止/DMA退役確認を担う。R1のU排他と容量通知、R3の期限/障害範囲、R4のdirectAcquire通知、R5のprivate fence reset再利用・測定、R6の寿命を改善する。

R2はstrictを当面維持し、stock互換の能力と退役条件を限定検証する。安全性が成立しなければstrictと具体的な不足・制約を記録する。context停止も能力と実確認が前提で、停止不能時はquarantine/全体resetを維持する。通常BLOB表示・GPU内共有・標準APIとzwl/libwaylandのテストドライバ範囲を保持する。

p008/q311のcleared/finishedを保持し、順序はp009 → p004 planning/未queue → 別WS029。720 active minutes見積・120分レビュー、有限fixture/build/VMで実装・受入する。追加HALや一般DE/native i915、git add/commit/push、system package/GDM/VFIO変更は含めない。private host/転送・隔離依存build・GitHub同期は既存承認を使用する。開始時点では新実装・試験の成功は主張しない。

## q311開始: GPU完了責任・fence所属と描画資源の改善（2026-09-13）

ユーザーがレビュー回答を承認し、独立Phaseの作成・実行を指示した。[WS014 p008](https://github.com/awemorris/zedBSD/issues/395)を単一項目q311-i01で実行する。p007/q310のcleared/finishedを保持し、p008 → p004 planning/未queue → 別WS029 native i915の順とする。

A1の局所表示エラー分離、A3のGPUドライバによるfence終端、A4のqueue容量、A5のacquire待機、A6の同時進行slot別pool/cb再利用、A7のexternal worker撤去・console通知、A2/A8の検証補強を含む。fenceはdrv_gpuフレームワークへ移し、kernには不透明handle/fd/refcount/poll/SCM_RIGHTSを残す。共通DRIVER分類＋ops識別とGPU組込時だけのbuildを用いる。

isolated paired rendererのSTRICT_QUEUE能力を合意し、実GPU成功だけを正常retireへ流す。失敗はsticky化して後続まとめretireを抑止し、K watchdogでERROR終端する。native投入前の予約も期限管理し、U停止による未監督仕事を残さない。通常BLOB表示と標準OPAQUE_FDを維持する。

720 active minutes見積・120分レビュー、有限fixture/build/VMで完了まで進める。HAL追加変更・一般Wayland・native i915・git add/commit/pushは含めない。既存private host/転送とGitHub同期の承認を使用する。開始時点で新実装や検証成功は主張しない。詳細はp008本文と承認回答コメント、local plan/ws014/phase008/に残す。

## q310完了: BLOB直接表示・標準fd共有・GPU同期改善（2026-09-13）

WS014 p007 / q310-i01をcleared、q310をfinishedとする。active Queueなし。WS014はincompleteでp001/p004はplanning、p004とWS029 native i915は未queue。p006/q309・WS030の既存clearanceを維持する。

直接VK_KHR_displayもGPU copy/blit→共有linear画像→SET_SCANOUT_BLOBへ移行し、通常vkdemoのCPU画像readback/uploadを除いた。IRQ完了通知、vkCmd batchingとmapped reply、表示待ち中のcontroller排他短縮、所有jobによる非同期present、同一openの待機admission、安全なtransport回復を実装した。

review2 B/C/E/F/Gも反映: KERNEL_HANDLE_FENCE/POLLINとcommand/present wait/signal、標準VK_KHR_external_memory_fdとexternal_fence_fdのOPAQUE_FD、描画node＋表示nodeの組、driverによるscanout import判定とswapchain作成時に一度だけの経路選択。GPU_DISPLAY_EVENTS/POLLPRIのQUERY→列挙→exact ACKと、GPU_BLOB_CREATE_PLACEDによる物理配置要求を追加した。GPU ABI v1の旧要求layoutを保ち、内部drv_gpu_opsはv6。実backingで満たせない配置はENOTSUP、OOMやdevice lossはfallbackで隠さない。

OPAQUE_FDはguestにdma-buf/SYNC_FDを要求しない。標準memory共有は同じdeviceUUID/driverUUIDの互換範囲、別GPUの任意importは未対応。表示専用foreign import・DMA/cache/placement・別node組合せは実コードfixtureによる検証であり、異種実機DMAや新HAL allocatorの受入ではない。Venusは非零physical placementを拒否する。

| 最終実QEMU | 結果 |
| --- | --- |
| q310-wayland-008 | PASS、46.531秒、QEMU exit0。128回同一fd並行操作、標準fence・linear/buffer/optimal共有、初期topology QUERY/ACK、FIFO/MAILBOX各6画面・異常終了/reopen/console |
| q310-direct-004 | PASS、41.83秒、QEMU exit0。直接表示6画面の独立oracle、通常readback0の動く2実画像、QEMU BLOB trace/対象寸法のlegacy経路なし、SIGINT/reopen/lease競合/console |
| q310-fence-exit-003 | PASS、4.424秒、QEMU exit0。pending producer終了後DEVICE_LOST、独立30秒fault期限と実測、最終waitpid |
| q310-recovery-004 | PASS、13.807秒、QEMU exit0。所有rendererだけを停止、10000ms timeoutとpeer error/旧参照gate、再開後checked resetと新contextの4096byte/decoder |

最終buildと限定K/U/driver/WSI fixtureの通常・ASan/UBSan、170公開APIのdispatch/両ABI、Noct生成8file一致を確認した。通常デモは約2秒で13frame程度という実測を残し、速度倍率や物理vblank保証・CTS適合を主張しない。zwlの同期1surface/passとlibwayland限定protocolはユーザー承認のテストドライバ制約として維持する。

optimal共有はstock virglrenderer1.1.0 proxyのOPAQUE attach不足を補ったisolated paired library/serverで受入した。exact168B capsetとINIT handshakeで合意した場合だけ選択し、private WSIのnative DMA経路を保持。system library/packageは変更していない。patch/library/server hashと手順はlocal/uncommitted plan/ws014/phase007/renderer-opaque/に保存した。stockだけでoptimal共有が通るとは扱わない。

失敗履歴wayland001–005、direct001、recovery001、fence-exit002を保存。fence-exit002はconsumer/driver双方10秒の期限競合でVK_TIMEOUTが先行した。fault専用期限を30秒に分け、final closeはpending fenceをerrorへしてからcallback drainを待つ順序へ改善した。修正前FAIL・修正後normal/sanitizer PASSの因果fixtureも保存し、実行中ioctl/file参照によるfinal close入口までの遅延とは区別する。

資料はlocal/uncommitted plan/ws014/phase007/results.md、gpu-uapi-contract.md、display-wsi.md、transport-sync.md、各verification JSONとQueue履歴plan/history/queue-q310.md。新HAL変更・GDM/VFIO操作・git add/commit/pushなし。GitHub Issues/Projectへの計画/受入同期と、ユーザー担当のrepository公開を区別する。


## q309完了: GPU handle共有・Wayland WSI・virtio scanout（2026-09-13）

WS014 p006 / q309-i01をcleared、q309をfinishedとする。active Queueなし。WS014はincomplete、p001/p004 planning、p004未queue。WS030 completedと既存Phaseのclearanceを維持し、native i915は別WS029のまま。

kernel_handle/handle_fd_*と共通fd参照・SCM_RIGHTS、GPU/Venusの独立process/context共有、VK_KHR_wayland_surface、最小libwayland-client.so、全画面zwl、標準Wayland/Vulkanアプリwltestを実装した。共有GPU imageはGPU copyと所有権同期を経て別processへ渡り、SET_SCANOUT_BLOB/RESOURCE_FLUSHで表示する。通常の新WSI経路にCPU readbackや再uploadを必須としない。旧copy経路もGOPではなくvirtio 2D scanoutであり、直接表示の互換経路として保持する。

GET_DISPLAY_INFOとGET_EDIDのbase/CTA progressive DTDで表示・モードを列挙。実QEMUでは1280×800、74,994mHz、320×200mmを取得した。custom framebuffer寸法はEDID寸法と独立に扱い、1〜100Hzのguest nominal pacingを検証する。virtioは物理pixel clockを設定しないため、物理vblank同期の保証とは区別する。

最終q309-wayland-004は43.869秒、QEMU exit0でPASS。FIFO/MAILBOX各6枚の320×240実VNC画像、計921,600画素が独立期待値と全画素一致し、12回の表示が実import資源とBLOB scanoutに対応した。生成元終了後の独立renderer import/GPU copyも検証専用readbackの1024画素が一致。swapchain再作成、client中断・再open、compositor通常終了・SIGKILL・再起動、実SURFACE_LOST/cleanup=0、強制終了直後の640×480 console復帰とechoによる画面更新を確認した。

同じ最終kernelのq309-direct-002も42.705秒、QEMU exit0でPASS。標準vkdemoの回転直方体6枚をGPU readback/VNC/独立oracleで照合し、通常終了とSIGINT後の再open、console復帰、表示競合拒否とowner完走を確認した。K/fd/SCM・GPU/EDID・Wayland/WSIの実コード限定fixtureとsanitizer、157 Vulkan dispatch/export、両ABIの公式header照合、Noct再生成、rootfs配置、対象buildと全適用規約を確認した。正式CTSや全Wayland SDK互換は主張しない。

途中のharness起動待ち不足とEDID/custom mode回帰を修正し、失敗証拠と再実行理由を保存した。最終reviewのMSG_PEEK二重put疑義は、rights付きpeekを既存guardが拒否するため到達不能と確認し、本体変更を戻して拒否後の参照寿命を追加検証した。formatterは規約と設定の不一致によりexit1でありPASSとは扱わず、全文確認とdiffcheckを記録した。HAL追加変更なし。ローカル結果はplan/ws014/phase006/results.md、技術資料3件、conformance.mdとfinal-evidence/verification.json、Queue履歴はplan/history/queue-q309.md。これらsource/doc/imageのgit add/commit/pushはユーザーが行う。本同期はGitHub Issues/Projectの計画・受入結果である。

## q308: 標準Vulkan 1.0・直接表示libraryとp005訂正

2026-09-13のユーザー確定指示に従い、標準Vulkan 1.0全137core＋VK_KHR_surface/display/swapchain/display_swapchainを提供する単一目標の [WS030](https://github.com/awemorris/zedBSD/issues/388) を新設した。公開headerはlibc/include/vulkan/、独立実装はuserland/base/libvulkan/、配置は/lib/libvulkan.so。EGLは今回cancelし将来GLES-on-Vulkan時へ、Waylandは将来backendとする。上流実装は移入せず、固定した公式XMLから宣言・定数を独立生成する。

[WS014 p005](https://github.com/awemorris/zedBSD/issues/387) のq307旧clearは、直接Venus wire/GPU ioctlを使う有限clientであり「純粋な標準Vulkan APIアプリ」を満たさないため失効（uncleared）。q307の6画像・正常回収・同VM再openという実測と当時の試行履歴は保存し、新しいq308-i04で標準API化を訂正する。p005はin-progressとして再開し、Queue itemは必要library出力までpending。p002/p003のclear、WS014 incomplete、p004未実行、別WS029 i915後段を維持する。

[q308](https://github.com/awemorris/zedBSD/issues/362) の順序はWS030 p001→p002→p003→WS014 p005→WS030 p004。ユーザーは全実装・作業継続・GitHub同期を明示承認済み。見積720 active minutes、120分ごと点検、各command/VM/poll有限、無変更retry3回まで。HALの追加変更・aggregate make check・git add/commit/pushは許可しない。既存private hostへの転送許可を維持する。

全API、必須能力/limits、memory可視性、同期、FIFO/image再利用の意味論を未検証のままcompleteとしない。詳細はWS030の実装契約。依存するsource変更は計画・native lifecycle/親子依存・Projectの同期とreadback後に開始する。

## WS014 p006追加: kernel handle・GPU共有・最小Wayland（2026-09-13）

ユーザー指定により[WS014 p006](https://github.com/awemorris/zedBSD/issues/393)を一つのplanned Phaseとして追加した。kernel_handle/handle_fd_*とSCM_RIGHTS、GPU/Venusの別context共有、GPU画像を扱えるWSI、VK_KHR_wayland_surface、最小client library、全画面zwl、標準APIのwltestを本Phaseで実装・検証する計画。コード配置はlibc/include/wayland/、userland/base/libwayland/・zwl/・wltest/、公開libraryは/lib/libwayland-client.so。

中核のK/driver実装を先に進め、Wayland通信/WSI/試験アプリを接続して実測から設計を改善する。新経路はCPU readbackを必須にせず、GPU allocationの実共有と同期・寿命を確認する。linux-dmabuf-v1、ゲストdma-buf/DRM、EGL、一般DEは採用しない。内部の段取りは別Phaseへ分割しない。

順序はp005 cleared → p006 planned → p004 planning。p004はp006の最終ソース/API/検証を受けて規約確認する。WS030 completedとq308 finished、既存Phaseのclearを維持。今回作成したのは計画であり、active Queue・新しい実装/試験結果はない。HALの追加差分は従来どおり個別承認、git add/commit/pushはユーザー担当。

## q307完了: p005 cleared（2026-09-13 JST）

userland/base/vkdemoにテクスチャ付き回転直方体を実装。独自vertex/fragment shader、実texture、depth、Vulkan pipelineを使用する。q307-vkdemo-002で固定3時刻と実時間3枚のGPU readback/VNC hashが一致し、独立したray/texture期待値との照合も不一致0。正常終了後、同じVMで通常2秒・12frameの回転を再openしてDONE/shell復帰、QEMU exit0まで確認した。

新規ioctlは不要。Uの共通Venus clientとgraphics操作を追加し、実測で発見したKのblob unmap待機の早期timeoutを修正した。clock進行中は10秒deadlineを維持し、clock停止中だけ連続poll上限を使う。HAL追加変更なし。有限host tests、shader/CLI/画像検証、専用amd64 build、p003回帰がPASS。

q307 finished、q307-i01/p005 cleared、active Queueなし。p003/q306の完了を維持し、次はp004（planning、未実行）。p001 planning、WS014 incomplete。native i915は別WS029。汎用libvulkan/ICD・全Vulkan適合・汎用WSI/zero-copyは未実装。

実測結果とAPI表は[p005](https://github.com/awemorris/zedBSD/issues/387)。ローカルのplan/ws014/phase005/results.md、api-coverage.md、evidence/とplan/history/queue-q307.mdへ保存。GitHubは計画/結果本文を同期し、source・資料・画像のgit add/commit/pushはユーザーが行う。

## q306完了: p003 cleared（2026-09-13 JST）

Venus専用driver、GPU任意callback/UAPI、独立したVulkan clear/copy/fence/readbackクライアント、build→転送→新規QEMU→実画面照合の有限ループを実装・検証した。2D/Vulkanともframe1とframe2の全49,152RGB pixelが赤緑／青黄の独立期待値に一致。Vulkan fence完了とGPU readback全画素も確認した。最終sourceからのbuildは実測済みkernel/clientとバイト一致する。

QEMU10のGL scanoutはQMP screendumpで取得できないため、QMPは制御とconsole取得、画面はegl-headlessのreadbackをVNC Unix RAWで取得。hostmemは現行amd64 MMIO窓に合わせ8MiB。HALはユーザーが具体差分を許可した8accessorのみ変更した。

q306はfinished、q306-i01/p003はcleared、active Queueはなし。p002 cleared、p001/p004 planning、WS014 incompleteを維持。次はユーザーが追加したp005（テクスチャ付き回転直方体デモ）、その後p004。native i915は別WS029であり今回未実行。一般のlibvulkan.so、全Vulkan適合、汎用WSI/mmap/zero-copyは未実装。

詳細と実測hashは[p003](https://github.com/awemorris/zedBSD/issues/384)。local evidenceはplan/ws014/phase003/results.md、evidence/、queue履歴はplan/history/queue-q306.md。GitHubは計画・証拠本文を同期し、source/資料のgit add/commit/pushはユーザーが行う。未コミットのファイルを公開済みリンクとして扱わない。


## 2026-09-11 fg006: PC-9821V13での起動改善

ユーザーがCurrent Focused Goalsへの追加と、WS003 p022/p023/p024の具体的実行Phase化を明示指示した。fg004（4機種インストーラ）・fg005（ネットワーク）を保持する。

| 順序 | 実行Phase | 成果 |
| --- | --- | --- |
| 1 | [ws003-p022](../ws003/phase022/phase.md) | 現行IPLのstack・BIOS read契約、artifact対応、診断の前提 |
| 2 | [ws003-p023](../ws003/phase023/phase.md) | LBA0実行後の実機停止境界と原因を絞る観測 |
| 3 | [ws003-p024](../ws003/phase024/phase.md) | 根拠に対応した修正と通常imageでのV13起動確認 |

依存: p022 → p023 → p024。p022/p023は旧履歴専用の扱いを解除し、現行の役割・受け入れを上記と各Phaseに更新する。過去の試行・証拠は保持する。3 Phaseはunclearedのまま次の試行を待ち、今回in-progressやclearedにしない。
この順序は選択した3 Phaseの依存順であり、削除済みの全WS Priorityリストを復活させない。今回の依頼は計画更新。新しいactive Queue・実行時間枠は未設定。

新規source変更・build・実機操作・GitHub公開は未実施。

## 2026-09-11 ネットワーク改善1〜3（計画のみ）

ユーザー指定をfg005 / [ws005](../ws005/ws.md) p013〜p017に整理。net lan disableは無効化。network-enableは有線かWi-FiのどちらかのIP取得で待機終了、既定30秒・設定可能、timeoutでも通常起動とdaemon接続処理を継続することをユーザーが確認。状態通知のsocket/fileは設計選択として保持。現行net wifi enableはバックグラウンド接続開始であることを静的確認した。

既存完了Phase、インストーラfg004、旧Priority削除を維持。新規実装・build・ネットワーク変更・公開なし。GitHub更新は未承認のまま保留。

## 2026-09-11 インストーラ実機bring-up計画（実行なし）

ユーザーがPC98 / Latitude 5320 / SV7 / LX6でのインストールを指定。[ws003](../ws003/ws.md)のfg004として、既存Phaseを再利用しp026〜p032を計画、BUG-013を既存IDで詳細化しBUG-023〜025を追加。PC98はV13/64MB/CF-IDE、/sbin空はQEMU上とユーザーが確認した。

WS019/WS025の閉鎖、旧Priority削除、q303停止を維持。新しいQueue・実装・build・実機操作なし。媒体/方式と実行範囲の具体化が次の段階。

## 2026-09-11 計画判断（新規Queueなし）

2026-09-11のユーザー指示により現在のPriorityリストを削除。WS025のp029/p030/p032/p038をcleared、WS025をcompletedとし、既存completedのWS019/WS006/WS022/WS002とともに閉鎖する。未実施の検証をPASSへ変更せず、今回の計画上の受け入れとして記録する。q303はfinished/stoppedのまま。active Queueと新しいPriority/Focusはない。

決定者: current user。指示原文:

> 計画を更新します。現在のPriorityリストを削除します。WS025の残件はclearedにして、WS025を閉じます。WS019も閉じます。WS006, WS022, WS002を閉じます。

WS025の確認未実施・実機未確認事項は各Phaseの履歴に保持。既存のBug台帳、WS009/WS014保留、Milestoneの判定は変更しない。

## 最新Queueの履歴

最新: q309 finished、ws014-p006 cleared。履歴全文はQueue結果コメントとlocal plan/history/queue-q309.md。以下は先行履歴。

最新: q308 finished、WS030 completed、p005訂正cleared。履歴全文はQueue結果コメントとlocal plan/history/queue-q308.md。以下は先行履歴。

最新active: q308 / WS030標準libraryとWS014 p005訂正。直前finished: q307 / p005当時cleared（現在clearは2026-09-13に失効）。履歴全文はQueue/p005のq307履歴コメントとlocal plan/history/queue-q307.md。以下は以前の履歴。

最新: [q305](queue-q305.md) — finished / ws014-p002 cleared。通常の動的GPU登録APIへの修正、共通cdev/devfs更新、限定test、amd64 buildを完了。次の実行Queueはなし。

前回: [q304](queue-q304.md) — 旧contractの実装・試験履歴。

### 以前のq303履歴

最新: [q303](queue-q303.md) — finished / ws025-p038 uncleared。ユーザー停止。
3ビルドは当時PASS、amd64実行試験は中断、PC/AT未実行。現行修正後の合格ではない。
次のQueueは未承認。

[全Queue索引](queues.md)。旧completedは当時の表記を保持し、現在のclearedへ履歴を改書しない。

## 2026-09-12 fg009: PPC Open Firmware / APM+FAT

PowerBook G4 A1010 / 867MHzを移植先とし、まずQEMU mac99上で、Open Firmware → APM+FATの独自ローダ → zedboot.cfg → 同じFATのvmunix → PPCカーネル初期化を成立させる。後続でamd64上のUSB OHCI、PPCユーザーABI、USB root、rootfs.img/data.imgのループバック利用へ進む。今回は計画のみ。

最初の到達点はp033→p034→p035。rootfs.img/data.imgは後続p038。設定名は今回指定のzedboot.cfg（現行UEFIはzedbsd.cfg）、kernel=vmunix。独自ローダはXCOFFを第一候補とし、OFによるELF直接ロードに依存しない。

- [ws003-p033](https://github.com/awemorris/zedBSD/issues/367): OF起動契約・APM/FAT imageとXCOFFローダ入口 (planned)
- [ws003-p034](https://github.com/awemorris/zedBSD/issues/368): zedboot.cfg・FAT読み取り・PPC ELF handoff (planned)
- [ws003-p035](https://github.com/awemorris/zedBSD/issues/369): PPC HAL・mac99基板対応とカーネル初期起動 (planned)
- [ws003-p036](https://github.com/awemorris/zedBSD/issues/370): amd64でUSB OHCI・USBストレージを検証 (planning)
- [ws003-p037](https://github.com/awemorris/zedBSD/issues/371): PPCユーザーABI・libcとinit到達 (planning)
- [ws003-p038](https://github.com/awemorris/zedBSD/issues/372): PPC USB boot・rootfs.img/data.img統合 (planning)
- [ws003-p039](https://github.com/awemorris/zedBSD/issues/373): PPC/OHCI変更の最終規約・統合確認 (planning)

全体の共通契約は各Phase本文に記載。実行Queueは作成せず、既存の実行保留、fg006完了、他WSの判断を保持する。

## 2026-09-12 WS003終了・WS027新設

ユーザーがPPCを新規移植として独立WSへ移すよう指示し、WS003を閉じて再利用しないことを指定した。その他の未完了は「未完了のまま保留事項へ移し、WS003内のPhaseは終了する」と明示。目標達成や試験PASSを追加する判断ではない。

PPC移植は[ws027](https://github.com/awemorris/zedBSD/issues/374)のp001-p007（旧WS003 p033-p039）へ移管。初期到達点はp003まで。その他の未完了は[Future Work F-004](https://github.com/awemorris/zedBSD/issues/364)へ保留移管。fg009はWS027、fg004は保留。WS003は終了・再利用禁止。実行Queueは作らない。

## 2026-09-12 インストーラ実機動作を独立WS化

ユーザー指示により[WS028](https://github.com/awemorris/zedBSD/issues/382)を新設。単一目標はPC98 V13、Latitude 5320、SV7、LX6でインストーラを実行し、インストール先から起動・loginできること。fg004をこのWSで再選択する。PPC移植は[WS027](https://github.com/awemorris/zedBSD/issues/374)に分離済み。WS003は閉鎖済み・再利用禁止。

NVMeが実機で動作していないというユーザー報告と、menuconfigにNVMe項目がないだけかもしれないという仮説をWS028へ記録。現行pci.driversには項目があり、amd64/i386の組込み・登録経路もある。実機使用config/imageとの一致と失敗境界は未確認。

Future Work F-004のうち旧WS003 p018/p019とp026-p032はWS028への引継ぎ対象とする。その他は保留のまま。元Phaseは閉じたまま保持し、新しい実行Phase・Queueはまだ作らない。

## 2026-09-12 GPU計画更新

ユーザー指示により、[WS014](https://github.com/awemorris/zedBSD/issues/15)の初期bring-up対象をi915からQEMU virtio-gpuへ変更する。未完了・未着手の既存目標の具体化であり、終了WSの再利用ではない。[p001](https://github.com/awemorris/zedBSD/issues/213)の設計検討の手動保留を解除しplanningとする。実装Queueは作成・再開しない。WS009など他WSの保留は自動解除しない。

Vulkanのディスプレイ拡張をOSの公式なユーザー向け表示APIとする案を検討する。正式採用やABI凍結は未決定。Linux DRM互換を必須としない従来方針は維持するが、メモリ管理、同期、画面出力、所有権・権限を担うOS/ドライバ機構は必要。Vulkan APIをそのままカーネルABIへコピーしない。

## 2026-09-12 Vulkan API関数別の責務表

ユーザー依頼により、Vulkan 1.0〜1.4の全コア234関数と選択した表示関連拡張41関数、計275関数について、libvulkan.so側（loader/ICD/WSIを含むユーザー空間実装）とzedBSD GPUドライバ側の責務を一関数一行の表にした。

[WS014 p001の関数別責務表](https://github.com/awemorris/zedBSD/issues/213#vulkan-api-responsibility-table)に全文を掲載する。固定したKhronosレジストリとの集合照合で欠落・重複・空欄なし。libvulkan.so単体の構成とloader/ICD分離の違い、Venus転送、記録/submit/表示の違い、対象外拡張、対応宣言ではないことを明記した。

設計資料でありAPI/ABI採用確定やPhaseクリアランスではない。WS014/p001はplanning、Queueは未開始。Markdownはローカル作業ツリーにも保存し、git commit/pushはしていない。

## 2026-09-12 責務分類をU/Kに統一

ユーザー指示により、[Vulkan API責務表](https://github.com/awemorris/zedBSD/issues/213#vulkan-api-responsibility-table)の275関数を、U（ユーザー空間実装）とK（GPUドライバ）の二つの責務欄だけで整理した。旧Q/C/R分類と境界列を削除。キャッシュ・記録・転送は責務欄の説明として保持する。ドライバへの照会はK、結果の整形等はUであり、キャッシュ可能性を別分類にしない。関数集合とplanning状態、Queue未開始は維持。

## 2026-09-12 GPUドライバ関数インタフェース案

ユーザー依頼により、[同じ責務資料](https://github.com/awemorris/zedBSD/issues/213#vulkan-api-responsibility-table)へK側インタフェース44件の表を追記。仮の関数シグネチャ、入力・出力、Kの責務、対応するVulkan APIを記載した。接続/context、resource/mapping、transport/submit/sync、display/event、および任意機能の群に整理し、初期2Dと後続Venusの範囲を区別した。

275関数のU/K表は保持。今回の関数名・型・構造体は設計案で、実装済み/ABI確定ではない。WS014/p001はplanning、実装Queueなし。git commit/pushなし。

## 2026-09-12 GPU interfaceをcallback構造体へ変更

ユーザー判断により、[GPU責務資料](https://github.com/awemorris/zedBSD/issues/213#vulkan-api-responsibility-table)の44操作をstruct drv_gpu_interfaceの関数ポインタメンバーへ変更。個別drv_gpu_*関数の公開案を置換した。PCI側がattach成功後にinterface/private data等をGPUコアへ登録し、GPUコアが/dev/gpuNを公開・dispatchする。detach/rollbackと参照寿命も記録した。

現行PCI attachはint戻り値のみでGPU登録の引渡し機構は未実装。構造体とPCI側class/service連携の詳細は設計事項。275関数のU/K分類を維持し、コード・Queue・Phase状態は変更しない。

## 2026-09-12 GPU実装の段階化

ユーザー指定の順序をPhase化: [ws014-p002](https://github.com/awemorris/zedBSD/issues/383)（GPUフレームワークのみ）→[ws014-p003](https://github.com/awemorris/zedBSD/issues/384)（QEMU＋Venusの画面取得・自動デバッグ、API不足の修正）→[ws014-p004](https://github.com/awemorris/zedBSD/issues/385)（最終API整理・規約全文確認）。既存p001は設計判断を供給し、未決定を完了扱いしない。次段階のi915ネイティブ実装は単一目標の[ws029](https://github.com/awemorris/zedBSD/issues/386)へ分離する。

Linux i915＋ANVホスト、egl-headless＋QMP screendump、frame更新によるキャプチャ検証、serial/画像/renderer証拠の保存をp003へ記録。実ホストでの動作は未確認。275関数のU/K表と44callback案は出発点で、p002/p003の実装結果により不足を補い整理する。実装Queueは未開始。資料のgit add/commitはユーザーが行い、エージェントはadd/commit/pushしない。

## q304実行開始（履歴）

ユーザー承認の唯一の実装対象は[WS014 p002](https://github.com/awemorris/zedBSD/issues/383)。GPUフレームワークのみを実装し、コーディングスタイル全文に従う。p003/Venus/i915は未開始。p001の必要contractをp002に具体化し、p001全体をclearしたとは扱わない。

Queue: q304 / attempt: q304-i01 / active・in-progress。時間枠は120 active minutes、内容はこの単一Phaseに限定する。重大な未解決仕様・外部blockが生じた場合は根拠と再開条件を記録する。git add/commit/pushは行わない。

## q304実行結果（2026-09-12）

q304-i01 / [ws014-p002](https://github.com/awemorris/zedBSD/issues/383)をclearedとし、単一PhaseのQueue q304をfinishedにした。GPUフレームワーク、5 callbackのstruct drv_gpu_interface、PCI所有のservice公開・解除、/dev/gpuN、session/世代handleを実装した。

実gpu.c/cdev.cと最小backendの通常・ASan/UBSanテスト、ILP32/LP64の固定ABI照合、実pci.cのlifecycleテスト、amd64対象kernel buildがPASS。全文規約レビューで目的コメント・参照寿命を確認し、git diff --checkもPASS。clang-format 19.1.7のdry-runは全文規約と衝突する関数定義/forward declaration整形等を指摘したため非zeroで、機械的整形は適用していない。詳細はPhase本文の検証記録。

WS014はincomplete。p001の残る設計判断とp003/p004はplanning、Venus/実GPU描画/i915は未開始。次はp003でmmap、submit/sync、display等の不足を実利用から補う。現在の実行Queueはなし。コードと資料はローカル作業ツリーにあり、git add/commit/pushはユーザーが行う。

<details>
<summary>旧現在状態ブロックの原文（履歴）</summary>

## V13完了時点の旧状態（後続のWS003終了・q304を反映する前）

Active Queue: none
fg006: completed

2026-09-12、ユーザーがWS003 p022・p023・p024の完了を報告し、MarkdownとGitHubの更新を指示した。3 Phaseをcleared（disposition: normal）として受け入れ、fg006を完了とする。WS003はインストーラ等の残件があるためincompleteを維持する。新しいQueueは作成せず、保留中の実行は再開しない。

対象: [p022](https://github.com/awemorris/zedBSD/issues/88)、[p023](https://github.com/awemorris/zedBSD/issues/89)、[p024](https://github.com/awemorris/zedBSD/issues/90)。

根拠は今回のユーザー完了報告。既存Markdownの自動検証・旧試行の結果は履歴として保持する。今回エージェントがbuild・QEMU・実機検証を再実施したものではなく、新しいartifact hashやroot/init/login到達点は報告されていないため追加しない。旧試行のunclearedや未実施項目を過去に遡ってPASSへ変更しない。
</details>

## q305実行開始時の範囲（履歴）

ユーザーがPCI公開serviceと起動時publishをGPUヘッダへ出す設計を見直し、普通の動的ops登録APIへ変更する案に「では、実装を変更してください」と実行を指示した。q305 / q305-i01はWS014 p002の同一目標内の修正のみ、120 active minutes。p002をin-progressへ戻し、旧q304の結果と試験証拠は履歴として保持する。WS014はincomplete、p003/Venus/i915は開始しない。

公開APIはdrv_gpu_register(const struct drv_gpu_ops *, void *, struct drv_gpu_device **)とdrv_gpu_unregister(struct drv_gpu_device *)。ユーザーが変更したops名を保持。GPUのPCI依存、registrationラッパー、public service/publish API、固定8台配列を除く。PCI側の通常service経路から共通APIを呼べることをfixtureで検証する。

共通cdevの固定16個制限とdevfsの固定snapshotを動的化し、VFSの初期化は既存登録を破棄しない。GPU専用の再公開を不要にする。複数deviceのops共有とprivate data分離、16台超の登録/列挙、早期登録のmount後存続、通常/失敗/解除の参照寿命を確認する。既存resource ioctl契約は維持する。

コード変更前に計画・Issue・Projectを同期して読み戻す。全文coding-styleに従い、限定GPU/PCI/cdev/devfs test、ASan/UBSan、32/64bit ABI、amd64対象make -j16 buildを実行。HAL責務/hal.h変更、aggregate make check、git add/commit/pushは行わない。

## q305実行結果（2026-09-12）

q305-i01 / [ws014-p002](https://github.com/awemorris/zedBSD/issues/383)の修正を完了し、Phaseをcleared、Queue q305をfinishedとする。ユーザー指定どおり、GPUコアは通常の `drv_gpu_register(ops, private_data, **device)` / `drv_gpu_unregister(device)` で個々のdeviceを登録・解除する。GPU公開ヘッダのPCI依存、registration wrapper、専用service table、一括publish APIを除いた。同じ `struct drv_gpu_ops` を共有する複数deviceがそれぞれのprivate dataを持つ。

GPUと共通cdev/devfsの固定台数制限を動的registry・snapshotへ変更し、VFS mount時の登録消去を除いた。早期・追加登録を通常のdevfs経路で扱う。使用中のunregisterはEBUSYでhandle/backendを保持し、解除成功後はhandleを消費する。古いinodeは世代の異なるdeviceや解放済みbackendへ接続しない。既存5 callbackとresource ioctlの責務は維持する。

実GPU/cdev/PCI coreを使う40 GPUの通常・ASan/UBSan試験、ILP32/LP64のUAPI照合、共通cdev/devfsの80 device登録・全件列挙・mount・割当失敗・世代と参照寿命の試験がPASS。共通層の限定runnerは既存GCC -fanalyzer gatesを含めPASS。amd64対象kernel buildとvmunix checkerもPASS。変更箇所の適用コーディング規約全文とlifetime/rollbackをレビューした。最終source hashと手順は `plan/history/queue-q305.md` に保持する。

WS014はincomplete、p001/p003/p004はplanning。QEMU＋Venus・i915は未実行で、新しいactive Queueはない。GPUフレームワーク資料とp001の責務表を現行ops/登録契約へ更新した。ソースと資料はローカル作業ツリーにあり、git add/commit/pushはユーザーが行う。GitHubは計画Issue・Projectを同期し、本文・native lifecycle・Projectフィールドを読み戻す。

## q306: Venus実装・リモートQEMUループ（ユーザー実行指示）

2026-09-12、ユーザーがp002をレビューし「これはOK」と受け入れ、p003完了までの環境確認・実装・QEMU実行を指示した。対象はユーザー提供の `awe@10.0.10.25`。実装は `src/drivers/gpu/venus/` に置き、Venus用のdriverとして構成する。汎用virtio-gpu driverへの抽象化を要求しない。q306 / q306-i01はこの単一Phaseだけを選択する。p002はclearedを維持し、ユーザーのcommit `3236b560` に取り込まれている。

環境確認済み: Debian 13.6 / Linux 6.19.13、Intel Iris Xe 8086:46a8 / i915、Mesa ANV 25.2.6 / Vulkan 1.4.318、QEMU 10.0.11、virglrenderer 1.1.0。KVM API 12、renderD128、udmabufの利用権、Venus/blob/hostmem/egl-headlessオプション、外部メモリ等の必要候補featureを確認。実際のVenus描画はこれから検証する。Intel ICDを指定し、ソフトウェアrendererの誤認を防ぐ。ホスト上の専用作業ディレクトリと使い捨てimageを使用する。

受け入れはzedBSDゲスト内の最小2DとVulkanテスト描画、frame更新のQMP取得・期待画像照合、変更→再build→再起動→新frame照合の再現可能なループ。serial/QMP/QEMU・rendererログ、source/image hash、起動引数、選択したGPUとversionを試行単位で保存する。ホスト単独のvkcubeやcommand提出ログだけではclearしない。全Vulkan適合・物理表示timingは受け入れ外。

既存PCI/DMAと通常のGPU ops登録を使用し、Venus内にPCI virtqueue、capset/context/blob、command/reply転送、完了確認、scanoutを実装する。必要なGPU callback/UAPIを実利用から追加する。初期経路はkernel所有のメモリと検証付きcopy ioctlを候補とし、ユーザー空間がVulkan command/応答を扱う。base systemは独立実装とする既存方針を維持し、上流実装を無断で取り込まない。対象subset・不足API・ownership/versionへの影響を資料へ記録する。

見積枠は240 active minutes、120分ごとに成果・境界を点検する。ユーザーは今回p003完了までの継続を指示済み。同じ失敗状態に対する無変更再試行は3回までとし、各起動・pollにtimeoutを設け、証拠に基づいて修正する。HAL責務/hal.hは変更せず、必要な判断が実際に発生した場合にのみ確認する。C規約全文、意味のある限定test、make -j16対象build、QEMU実測、差分レビューを適用する。p004・ネイティブi915は実行対象に追加しない。git add/commit/pushはユーザーが行う。

## q306 HAL変更の許可待ち（2026-09-12）

ユーザーが「HALの改変には許可が必要です」と明示した。既存宣言の実体補完も含め、HALの全変更に適用する。エージェントが責務変更を伴わないMMIO補完を許可不要と解釈したのは誤り。追加したsrc/hal/amd64/asm.cの8 accessorを取り消し、元のソースへ戻した。具体差分を `plan/ws014/phase003/amd64-mmio-proposal.patch` に保存し、適用・検証の許可を質問中。未許可の候補を用いた追加build/QEMU試験は停止し、独立したdriver/client/loopの確認を続ける。p003はin-progressのまま、clearedではない。

候補はhal.h宣言済みのMMIO read/write8/16/32/64のamd64実装のみ。hal.hや責務の変更はないが、許可は必要である。候補適用時のamd64 kernel/image linkは成功したが、実QEMUでGPU登録にまだ失敗しており、描画成功は確認していない。候補のbuild結果を受け入れ済み実装と混同しない。

前準備はKVM/ANV/QEMU環境、既存kernelの起動・QMP画面取得、TTY履歴のread-only取得まで成立した。GPU core拡張・ユーザー空間クライアント・Venus backendの限定compile/host testsが進んでいる。現行U/K契約は下記資料に記録する。イメージ転送は、ユーザーが10.0.10.25を私有サーバーとして機密データも含め明示許可済み。

## q306 HAL変更の承認・再開（2026-09-12）

ユーザーが提示済み差分に「許可します。」と回答した。`plan/ws014/phase003/amd64-mmio-proposal.patch` の8個のamd64 MMIO read/write accessorの適用・検証を許可されたため、同一差分を適用し、build/QEMU検証を再開する。hal.hやHALの責務は変更しない。直前の「HAL変更の許可待ち」は解消済み。今後の別のHAL変更には、その具体差分に対する事前許可を引き続き必要とする。

PCI BARのcapability部分だけをmapして失敗する問題をdriver側で修正し、register BARを一度だけ全体mapして各capabilityに範囲を渡す。driver単体・ASan/UBSan試験は通過済み。実際のVulkan描画は引き続き未検証で、p003/q306はin-progress。

## q306 実行診断と画面取得方式の更新（2026-09-12）

HALの提示差分はユーザー承認済み。修正後のamd64 image buildと実QEMUのGPU登録・capset4照会が成功した。q306-2d-003/004ではzedBSDの2Dクライアントが全画素/FNV検証とpresent成功マーカーまで到達したが、QMP screendumpは継続してno surfaceを返す。

QEMU v10.0.11公式実装を確認した結果、GL scanoutはSCANOUT_TEXTUREとなり、QMPが呼ぶqemu_console_surface()はNULLを返す。egl-headlessは別途pixman surfaceへ実際のGL画像をreadbackしており、VNCはそのsurfaceを参照する。このためQMPは起動制御・console/log取得を維持し、描画画像はQEMU標準のVNC Unix socket経由で取得する方式へ更新する。外部TCPポートは使わない。実画像の全ピクセル・独立期待値・試行/frame/hash照合という受け入れは維持し、表示成功マーカーだけではclearしない。QEMUやゲスト画像を改造して成功画面を作る方式ではない。

ホストにはvirgl-serverが欠落していたため、Debian公式virgl-server_1.1.0-2_amd64.debを専用rootのdependencies配下へ展開した。システムのdpkg状態は不変。RENDER_SERVER_EXEC_PATHで指定しbinary/packageのhashと起動確認を記録する。

q306-venus-001はcapset4/wire1照会後、返信blob確保付近でENOMEMとなる。Vulkanコマンドの実行成功はまだ未確認。p003/q306はin-progressのまま、driverのHOSTVISIBLE mappingと有限VNC captureを修正・検証する。

根拠: https://github.com/qemu/qemu/blob/v10.0.11/ui/console.c 、https://github.com/qemu/qemu/blob/v10.0.11/ui/ui-qmp-cmds.c 、https://github.com/qemu/qemu/blob/v10.0.11/ui/egl-headless.c 、https://github.com/qemu/qemu/blob/v10.0.11/ui/vnc.c 。

## q307開始: p005をp004の前へ追加（2026-09-13）

ユーザーがテクスチャ付きの回転直方体デモをuserland/base/vkdemoとして作り、vertex/fragment shaderとAPI不足を確認するよう依頼。[p005](https://github.com/awemorris/zedBSD/issues/387)を追加し、p003 cleared → p005 → p004の順とする。q307/q307-i01はp005だけを実行。p003/q306のclear/終了は維持し、p004とnative i915は未実行。

独自GLSL→SPIR-V、実texture/depth/graphics pipeline、時間の進む同一process、GPU readbackとVNC実画面、独立した幾何/texture照合で確認する。既存GPU APIを再利用し、必要なU共通化と実測された不足だけを補う。HALの追加変更は未許可。見積240 active minutes、120分ごとの点検、有限build/VM/pollを適用する。GitHub同期はユーザー明示承認済み、git add/commit/pushはユーザーが行う。

## q308完了: 標準Vulkan・直接表示libraryと標準APIデモ（2026-09-13）

WS030 p001/p002/p003/p004とWS014 p005の標準API訂正をclearedとし、WS030 completed、q308 finished、active Queueなしとする。WS014はincomplete、p001/p004 planning、p004未queue、native i915は別WS029のまま。q307の旧scopeの実測と履歴は保持する。

`libc/include/vulkan/` にVulkan1.0の公開header、`userland/base/libvulkan/` に独立した全137 core＋選択direct-display WSI18の実装を提供し、`/lib/libvulkan.so` に配置した。vkdemoは標準Vulkan/WSIだけを使い、GPU ioctl/Venus codecをアプリへ持ち込まない。ABI、Noct再生成、155実exportとproc-address、全familyの限定意味論試験、U/Kの所有権・権限・失敗回収、適用C規約の独立レビューを実施した。正式CTS認証は主張しない。

最終 `q308-lifecycle-003` は実QEMU10.0.11/virglrenderer1.1.0/Intel ANVで6枚の回転直方体を描画し、実VNC/GPU readback/独立ray-texture oracleが一致（評価対象不一致0）。通常終了後6frame再起動、SIGINT後6frame再起動、640×480文字画面への復帰とechoによる画面更新、別processの表示競合拒否とowner35frame/DONEを確認した。42.671秒、QEMU exit0。最終書式変更後のkernel/appは実行済みbinaryと一致する。

承認済みHAL patch SHA256 `e6ec9e6c2deda41b840fa6f10846438d091f3a20ce782b9251b7979ac7591c8d` のみを適用し、既存hal_space_map_device/device usermapを補完した。追加HAL APIはない。PCI cache属性、queue総数63、allocator破棄、console/query/通知の修正と、先行失敗・再実行理由を保存した。公開coherent HOST_VISIBLE、256MiB aperture、native watchdog等の制約は能力監査へ記録した。

結果は `plan/ws030/results-q308.md`、155行の台帳は `plan/ws030/phase004/api-verification.md`、最終証拠は `plan/ws030/phase004/final-evidence/verification.json`、p005訂正は `plan/ws014/phase005/results-q308.md`、履歴は `plan/history/queue-q308.md`（いずれもlocal/uncommitted）。GitHubは計画Issue/Project/結果コメントの同期であり、source/doc/imageのgit add/commit/pushはユーザーが行う。EGLは今回cancel、Waylandは将来VK_KHR_wayland_surface backendとして追加する。

## q309開始: WS014 p006を単一項目で実行（2026-09-13）

ユーザーの「では、実行してください。」により、[p006](https://github.com/awemorris/zedBSD/issues/393)全体をq309-i01として実行する。kernel handle/fd/SCM_RIGHTS、GPU/Venusの別context allocation共有・GPU内表示、VK_KHR_wayland_surface、最小libwayland-client.so・zwl・wltest、限定検証と実QEMU受入を一つのPhase/項目に含める。中核K/driverを先に実装し、通信/WSI/アプリを接続して実測から改善する。p004は含めない。

時間枠は720 active minutes見積、120分ごとに進捗・残件を確認。各command/VMを有限化し、同条件無変更retryは3回まで。達成の保証や無限継続ではなく、未達は証拠と再開条件を残す。全規約を適用し、既存成果/履歴を保持する。HAL追加変更とgit add/commit/pushは許可されたとは解釈しない。private host・image/source転送の承認を維持する。

q309はactive、q309-i01とp006はin-progress、WS014はincomplete。q308 finished・WS030 completed・既存clearanceは維持。新経路でCPU readbackを必須にせず、実GPU allocation共有と同期/寿命/Wayland protocolを確認する。実装成功・Phase受入はまだ記録していない。

## q309 checkpoint001: GPU画像共有とWayland実表示（2026-09-13）

ユーザーの指摘に沿って表示経路を確認した。従来のCPU readback経路もGOPへは書かず、virtio-gpuの2D resource/SET_SCANOUTへ送っていた。p006では共有GPU allocationをSET_SCANOUT_BLOBへ渡し、通常のWayland表示にCPU readback/再uploadを必須としない。表示数・接続・推奨サイズはGET_DISPLAY_INFO、追加モード・nominal refreshは今回追加したGET_EDIDのbase/CTA progressive DTDから取得する。無効/未対応EDID時は既存50Hz、100Hz超・standard/established timing・DisplayID・物理vblank保証は対象外。

K handle/fd/SCM_RIGHTS、GPU共有、最小libwayland-client.so、VK_KHR_wayland_surface、zwl、標準Wayland/Vulkanアプリwltestを実装した。K参照・copyout rollback、GPU export/import/scanout、Wayland byte/fd FIFO・queue・frame/release・swapchainの限定試験は通常とASan/UBSanで通過。公開ABIはi386/amd64 C/C++で固定公式Wayland/Vulkan headerに一致、Vulkan137 core＋20 WSIの157 dispatch/exportを照合した。正式CTSや全Wayland SDK互換は主張しない。

実QEMU10.0.11/virglrenderer1.1.0/Intel ANVのq309-wayland-001は21.257秒でPASS。生成元プロセス終了後、独立receiver contextでimportしたGPU画像をGPU copyし、検証専用readbackの1024画素が一致。wltest→実SCM_RIGHTS→別processのzwl→scanoutではFIFO/MAILBOX各6枚、計12枚の320×240実VNC画像が独立oracleと全画素一致した。各modeのswapchain再作成と通常終了、zwl12frame/cleanup_failed=0も確認した。これは初回実測で、最終ソースの受入ではない。

検証後、WSI破棄によるアプリ所有surfaceの暗黙unmapを除き、zwlの明示unmap/remapを整理。共有extentの照会範囲とexport上限を一致させた。クライアント/コンポジタ異常終了・再起動、可視console復帰、最終限定回帰/build/規約確認を継続中。p006とq309-i01はin-progress、q309 active、WS014 incomplete。p004未queue、WS030 completedと既存clearanceを維持。HAL追加変更なし、git add/commit/pushはユーザー担当。

local/uncommitted証拠: plan/ws014/phase006/checkpoint001.json、plan/ws014/temp/remote/q309-wayland-001/result.json とevidence/。source/doc/imageはGitHub repository未公開であり、本同期はIssue/Projectの計画と結果記録。

## q310開始: GPUレビュー改善p007（2026-09-13）

ユーザーの新Phase作成・実行指示により、[WS014 p007](https://github.com/awemorris/zedBSD/issues/394)全体を単一項目q310-i01として実行する。直接VK_KHR_displayにもGPU copy/blit→共有linear画像→BLOB scanoutを使い、通常vkdemoのCPU readbackを除く。完了通知・command batch/reply mmap・controller排他の短縮・非同期present・同一open並行性・安全なtransport回復・buffer/optimal allocation共有を改善し、実QEMUで描画/寿命と転送数を検証する。

ユーザーはzwlの1パス1surface同期presentとlibwaylandの限定protocolをテストドライバとして承認した。一般Wayland環境、複数window合成・入力・既存Toolkit対応・常駐化は本Phaseへ入れない。reviewの推奨は仕様と照合し、送信受理、Venus decoder応答、VkFence完了、scanoutを区別する。以前のp006でGPU内表示を実証したのはWayland経路であり、直接表示にCPU経路が残った対応不足を訂正する。

p006/q309は実際の受入範囲のcleared/finishedと証拠を保持する。順序はp006 cleared → p007 in-progress → p004 planning/未queue → WS029 native i915。WS030 completed、p001の未決定と既存clearanceは維持。q310は720 active minutes見積/120分レビューの有限項目。HAL追加変更、VFIO/ホスト表示停止、git add/commit/pushは含めず、private image/source転送とGitHub計画同期の既存承認を使用する。開始時点では新実装・試験の成功を主張しない。

## q310追補: 標準external memory/fence fdと描画・表示の組（2026-09-13）

ユーザーの追加review2 B/C/E/F/Gを[WS014 p007](https://github.com/awemorris/zedBSD/issues/394)へ追加し、q310-i01の同じ目標で実行する。KERNEL_HANDLE_FENCE/POLLIN・command/present wait/signal、VK_KHR_external_fence_fdとVK_KHR_external_memory_fdのOPAQUE_FD、必要な標準問い合わせ/拡張依存、libvulkanでの描画nodeと表示nodeの組、driverによるscanout import可否/制約照会、swapchain作成時の一度だけの共有またはCPU fallback判断を含める。

OPAQUE_FDはLinux dma-buf/SYNC_FDを要求しないが、同じdeviceUUID/driverUUIDの互換条件を守る。別描画GPUからの無条件importは前提にしない。表示専用foreign importはdriverが実backingを検証した場合のみで、未対応は拒否する。通常VenusのBLOB直接表示を受入条件として維持する。別node/制約/fallbackは実コードfixture、標準共有・描画・寿命は実Venusで検証する。

zwlの同期1surface/passとlibwayland限定protocolはユーザー承認のテストドライバ範囲として維持。一般Wayland/Toolkit、external_semaphore_fdやdisplay_controlの全API、native i915、新HAL/物理foreign-DMA受入、git add/commit/pushは自動追加しない。720 active minutes見積/120分レビューを維持。新規機能の実装・受入成功はまだ記録していない。p004はこの追加を含む最終APIを後続で確認し、未queueのまま。

## q310 checkpoint 01: 実QEMUで並行操作・標準fd共有を確認（2026-09-13）

[WS014 p007](https://github.com/awemorris/zedBSD/issues/394) / q310-i01 は実装・検証を継続中。p007は未完了であり、p004/native i915には進んでいない。

- 実QEMU `q310-wayland-005` で同一GPU fdの4 pthread×32回（128 allocation lifecycle）の書込/読出全4096 byte照合がPASS。
- 別processの標準Vulkan OPAQUE_FD fenceのexport/import/reset・実GPU signal・参照寿命がPASS。linear共有と標準buffer OPAQUE_FDのproducer終了後import/GPU copy/pixel検証もPASS。
- 同じ実行はoptimal image capability queryでVK_ERROR_FORMAT_NOT_SUPPORTEDとなりFAIL。対応が必要なnative dedicated allocation条件を確認し、必要なVK_KHR_get_memory_requirements2 / VK_KHR_dedicated_allocationを標準APIで補う。対応型の能力を偽って成功扱いにしない。Vulkan coreは1.0を維持する。
- 限定fixtureの通常版/ASan・UBSanではtyped fence/poll/SCM_RIGHTS、signal fd close/reuse競合、K allocation rollback・元所有者終了後の保持、display-only foreign backingのDMA/cache拒否と最後の解放、非同期presentの所有権、作成時に一度だけのfallback選択、command batching、mapped reply、IRQ out-of-order/制御用slot確保を確認した。fixtureと実GPUの証拠は区別する。
- 実試験で見つかったQUERY出力欄をRESET/WAIT入力へ再利用する不具合を修正。次に露呈したAMD64 pthread初期SPのC ABI不一致はlibcで修正し、005で例外が消えた。HAL変更は行っていない。
- 失敗履歴001（旧診断不足）、002（harnessで未対応の`;`を入力）、003（RESET EINVAL）、004（workerのmovapsでstack alignment例外）、005（上記optimal profile拒否）を保存。失敗を過去の成功へ書き換えない。

残件はoptimal allocationの標準API受入、direct BLOB/Waylandの画面・寿命・性能検証、故障/回復の有限試験、最終API/規約/宣言再生成確認。zwlとlibwaylandの承認済みテストドライバ制約は維持する。資料・コードは作業treeにありgit add/commit/pushはユーザー担当。GitHub Issues/Projectの同期とrepository公開を混同しない。

ローカル証拠: `plan/ws014/temp/remote/q310-wayland-001`〜`005`（実行ごとのsource/image hash、build/transfer/guest/renderer log）。UAPI対応表は `plan/ws014/phase007/gpu-uapi-contract.md`。同表は下記Phase本文にも掲載する。720 active minutes見積・120分レビューを維持し、今回のcheckpoint後もq310を継続する。

q310 checkpoint 02: paired OPAQUE rendererでstandard buffer/optimal/fence、Wayland12画面、direct6画面と通常readback0、producer終了error、10000ms timeout後のchecked recoveryを実QEMUでPASS。最後のtopology POLLPRI/ACK通知と統合確認を継続中。詳細は[WS014 p007](https://github.com/awemorris/zedBSD/issues/394)。

## q311完了: GPU完了責任・driver fence・描画資源改善（2026-09-13）

WS014 p008 / q311-i01をcleared、q311をfinishedとする。active Queueなし。p007/q310の受入を保持し、WS014はincomplete、p001/p004はplanning、p004と別WS029 native i915は未queueのまま。

承認回答A1–A8とfence所属を実装した。fenceはdrv_gpuフレームワークへ移し、kernは不透明handle/fd/refcount/poll/SCM_RIGHTSを保持する。6platformでGPU共通層＋fenceをbackend選択時だけbuildし、GPUなしamd64実ELFでGPU symbol/object不在とgeneric handle/fd残存を確認した。

native投稿前のGPU_JOB予約から独立watchdogが監督し、strict paired rendererの実submission VkFence成功でKがexact generationを終端する。U-only/未commitのpendingをK内部で無期限に待たない。slotは最大64descriptor/32chain（28job＋4control）、外部fenceごとのworkerを撤去。acquireはmonotonic condition、present pool/cbは同時slotごとに再利用し、consoleは文字・所有権変更で起床する。局所表示エラーと全device故障も分離した。

最終5VMは同じkernel/base imageでPASS/QEMU exit0: direct-002 41.345秒、wayland-002 46.011秒、producer-stop-004 14.117秒、producer-exit-002 4.214秒、recovery-002 13.823秒。直接/Waylandの通常BLOB表示・独立画像oracle・複数process・再open/consoleを確認。SIGSTOP中fdを開いたproducerは9970msでDEVICE_LOST、renderer停止は10000msで故障通知後にchecked reset・新context往復を確認した。

K/U/transport/host/consoleの限定normal・sanitizer、170API/両ABI/Noct8file、対象build・規約・独立レビューを完了。U回収競合2件は修正前FAIL→修正後PASS。static analyzerの4警告は実callee/有効入力の前提と照合して記録し、全警告0とは扱わない。初期のbuild/harness失敗も保持する。

新libvulkanのVkDevice作成にはSTRICT_QUEUE対応のisolated paired rendererが必要。stock/旧pairは初期化で拒否する。host system packageとHALの追加変更なし。通常2秒sceneはp007再測定13frameからp00815frameだが、QEMU CPU時間は0.36秒から0.48秒の単発観測で、CPU削減や速度倍率は主張しない。一般Wayland/Toolkit、任意GPU間DMA、native i915、CTSは未受入。source/doc/patchのgit add/commit/pushはユーザー担当。


受入記録: [p008結果コメント](https://github.com/awemorris/zedBSD/issues/395#issuecomment-5652374702)。local/uncommittedの資料は plan/ws014/phase008/、Queue履歴は plan/history/queue-q311.md。


## q312完了: GPUレビュー対応とフレームワーク共通化（2026-09-13）

WS014 p009 / q312-i01をcleared、q312をfinishedとする。active Queueなし。p008/q311の受入を保持し、WS014はincomplete、p001/p004はplanning、p004と別WS029 native i915は未queueのまま。

R1: 容量不足をOOMにせず、`GPU_JOB_CAPACITY` QUERY/WAIT（ioctl 37）と`GPU_JOB_POLICY`（38）を追加。libvulkanはnative準備→QUERY→回収→非待機RESERVEとし、EAGAINではqueue/device/context mutexを外して待つ。R3: 予約10秒・実行60秒・停止10秒をmake/menuconfigの設定と実効値照会にし、session単位のsticky errorと`drv_gpu_recovery_ops`（stop_begin/stop_poll/fault/reset）でcontext単位の停止確認を導入。Venusはflags7のquiescence契約（全native VkDeviceWaitIdleを確認したCPU0 ACK）を持つisolated pairで実停止を証明し、確認不能なら従来のquarantine/全体resetへ進む。R4: direct acquireは画像返却・故障・topologyをwaiter固有pipeと`ppoll`で待ち、10 ms周期起床を除いた。R5: terminal private fenceを最大64本ずつ一括resetしREADYを再利用。R2はstrict（flags7）維持、stock 1.1.0の情報欠落をstock-compat/で記録。fenceとjob監督はdrv_gpu内に保持し、汎用kernへの追加なし。

最終8VMは同一最終artifactでPASS/QEMU exit0: direct-003 41.935秒、wayland-002 47.342秒、submit-load-005 11.833秒（2process 576 submit、OOM 0）、completion-delay-003 25.316秒（15秒遅延完了、peer継続）、context-timeout-003 25.138秒（短縮期限でDEVICE_LOST、peer継続）、producer-stop-002 19.438秒（SIGSTOP中に7770 msで終端）、producer-exit-002 4.165秒、recovery-002 14.051秒（10000 ms watchdog後checked reset）。限定fixture（K 12 suite、U 10+5 job、transport/host/console）、170 API/両ABI/Noct、6platform×GPU有無のbuild入力、GPUなしamd64実ELF、規約確認を完了。失敗履歴（submit-load-001の能力bit漏れ、producer-stop-001の旧期待値、recovery-001のerrno期待値）を保持し、初回成功とは扱わない。

新libvulkanのVkDevice作成にはflags7（OPAQUE+STRICT+QUIESCE）のisolated paired rendererが必要で、stock/旧pairは初期化で拒否する。実行期限60秒は正当な長時間computeにも適用される。任意GPU間DMA、native i915、一般Wayland/toolkit、CTSは未受入。HAL・host system package・git add/commit/pushは行っていない。

受入記録: local `plan/ws014/phase009/results.md`、`runtime-verification/summary.json`。受入記録: [p009結果コメント](https://github.com/awemorris/zedBSD/issues/396#issuecomment-5655170913)。

## q313完了: GPU監督の共通化仕上げと局所隔離（2026-09-14）

WS014 p010 / q313-i01をcleared、q313をfinishedとする。active Queueなし。p009/q312の受入を保持し、WS014はincomplete、p001/p004はplanning、p004と別WS029 native i915は未queueのまま。

S1: 停止期限の起点を`stop_begin`実呼出しへ移し、実行期限内のjobが残る間は停止しない。S2: graceful closeはcommit済みjobを終端せず実結果をfenceへ公開する。S3: native仕事の有無判定、fault cancel後のsession失敗、RESERVED回収、admission拒否、control期限定数を共通層へ移した。S4/B6: monitor起床の限定と`drv_gpu_recovery_ready`除去。S5: `recovery->isolate`（ops版9）で停止未確認contextをsession隔離し、device全体は継続、idle時のchecked resetで回収。libvulkanは自contextのPOLLERRだけでdevice lossをlatchする。UAPI/HALは不変。

最終10VMは同一最終sourceの2 build（既定policy・短縮policy）でPASS/QEMU exit0: exit-delayed-003 19.698秒（producer終了後にconsumer fenceが14750 msでSUCCESS）、exit-hang-006 28.178秒（8000 msでDEVICE_LOST、context隔離、peer継続、idle openでreset回収と通常試験PASS）、producer-exit-002 24.447秒（hostの実結果を公開）、direct-002 41.796秒、wayland-002 47.258秒、submit-load-002 10.886秒、completion-delay-002 25.213秒、context-timeout-002 25.296秒、producer-stop-002 19.341秒、recovery-002 13.926秒。限定fixture（GPU core 10種、Venus 5種、libvulkan 5種、build selection）を通常＋sanitizerでPASS。失敗履歴（exit-hang-001のU側latch、exit-hang-004のreset中open拒否、producer-exit-001の旧期待値）を保持し、初回成功とは扱わない。

隔離で失った容量はidle時のresetまで戻らず自動escalationは無い。隔離contextの表示状態はresetまで残る。closeはcommit済みjobの退役まで待つ。git add/commit/pushはユーザー担当。受入記録: [p010結果コメント](https://github.com/awemorris/zedBSD/issues/397#issuecomment-5655171051)。

受入記録: local `plan/ws014/phase010/results.md`、`runtime-verification/summary.json`、`gpu-supervision-contract.md`。
