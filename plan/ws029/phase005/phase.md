<!-- awesome-plan project=zedbsd record=ws029-p005 -->

# WS029 p005: drv_gpu統合、native stream、生UAPI試験クライアント

<!-- awesome-plan-current:start -->
Status: cleared
Phase disposition: normal
Parent: [WS029](https://github.com/awemorris/zedBSD/issues/386)
Queue: q314 active / q314-i05 cleared (whole Phase)
Dependencies: ws029-p004
Next: ws029-p006
<!-- awesome-plan-current:end -->

Combined ID: `ws029-p005`
Primary Milestone: MG006
Related Milestones: MG003
Objectives: O2, O4
Estimate: 240 active minutes（q314 の合計 1440 の一部）

共通の設計・事実・関数構成は [i915-design.md](../i915-design.md) を正本とする。本文は差分と受入条件だけを書く。実装前に [Guardrail](https://github.com/awemorris/zedBSD/issues/363) と `plan/coding-style.md` 全文を読む。HAL（`include/hal/hal.h`）と UAPI layout は変更しない。git add/commit/push はユーザーが行う。

## 目標

drv_gpu ops v9 を全て接続し（command/submit/drain、jobs、recovery + isolate）、native stream（設計資料 §7）と root 用の guest 試験プログラム `/bin/gpu-i915-test` を作る。

## 手順

1. **stream 検証**（`i915.c` `i915_stream_parse`）: magic/version/engine(0|1)/relocation_count(≤64)/batch_dwords(1..16384)/flags=0、`bytes == 32 + 16*count + 4*dwords`、末尾 dword が `MI_BATCH_BUFFER_END`、各 reloc の `dword_offset+1 < batch_dwords`、handle が同 session の resource。違反は EINVAL。
2. **`i915_command_submit`**: batch object を session VM に作り（16 KiB 単位で cache して再利用可）、dwords copy + reloc patch、`i915_request_alloc`（EAGAIN はそのまま返す）、emit、submit。`i915_command`（同期 op）は同じ処理で completion なし。`i915_command_drain` は session の request 全退役を待つ（`waitq` を irq から起こす）。
3. **jobs**: `reserve`（slot 確保、callback 保持、`timeline` は 1 だけ受理）、`commit`（marker request 投入）、`cancel(0)`（slot 解放）、`cancel(fault)`（保持し 0）、`capacity`（空き slot 数）。
4. **recovery**: `stop_begin`（context `stopping`）、`stop_poll`（context の未完 request 0 なら 0、else EAGAIN）、`isolate`（未完 request を EIO で `drv_gpu_complete`、context を quarantined、その engine が idle でなければ `i915_engine_reset` して他 context を継続、失敗なら error）、`fault`（全 engine 停止・全 request fail・`failed=1`）、`reset_device`（`i915_gt_reset`、engine 再 init、quarantined object 解放、`failed=0`）。`capabilities |= GPU_CAP_COMMAND | GPU_CAP_NOTIFICATION | GPU_CAP_JOB | GPU_CAP_JOB_CAPACITY`。
5. **guest 試験** `userland/base/tests/gpu-i915/main.c`（`userland/base/tests/gpu-fence/Makefile` と同じ `ZEDBSD_USERLAND_PACKAGE` 登録、program 名 `gpu-i915-test`、`libvulkan` 依存なし、`/dev/gpu0` を直接 ioctl）: `GPUI915 START` → `GPU_GET_INFO`（driver_name=="i915"、capabilities）→ resource src/dst 各 64 KiB → src に pattern（word i = `0x5a000000 + i`）→ **copy**（BCS0 `XY_SRC_COPY_BLT_CMD`、256×64 の 4 byte pixel として 64 KiB）→ `GPU_COMMAND_WAIT` → dst 読出し照合 → **fill**（`XY_COLOR_BLT_CMD` 0x3197a5e2）→ 照合 → **store**（`MI_STORE_DWORD_IMM` で dst+0 に 0xdeadbeef）→ 照合 → **job**（`GPU_JOB_RESERVE` + `COMMIT` + `GPU_COMMAND_WAIT`）→ `GPUI915 PASS copy=1 fill=1 store=1 job=1 src_slot=<n> dst_slot=<n>`。失敗は `GPUI915 FAIL stage=<s> errno=<e>`。handle→slot は `GPU_RESOURCE_CREATE` の返却 handle 下位から取れなければ、K のログ順で対応付ける方針を README に書く。
6. **fixture**: `i915-stream-test.c`（検証全項目）、`i915-backend-test.c` に jobs/recovery/isolate（engine reset が呼ばれる/呼ばれない条件）、`gpu-supervision` 相当は core 側で既に済んでいるので backend の callback だけ。
7. **build**: image まで（`disk-image`）で `/bin/gpu-i915-test` の配置を確認。

## 完了条件

fixture PASS、image build PASS、`i915-stream` の UAPI 文書 `plan/ws029/i915-native-stream.md`（§7 を確定版に）。記録同期。

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
