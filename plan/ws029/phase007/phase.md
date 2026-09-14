<!-- awesome-plan project=zedbsd record=ws029-p007 -->

# WS029 p007: 実機での描画確認、静的レビュー、規約全文確認

<!-- awesome-plan-current:start -->
Status: cleared
Phase disposition: normal
Parent: [WS029](https://github.com/awemorris/zedBSD/issues/386)
Queue: q314 active / q314-i07 cleared (whole Phase)
Dependencies: ws029-p006
Next: WS029 display/scanout planning (not created)
<!-- awesome-plan-current:end -->

Combined ID: `ws029-p007`
Primary Milestone: MG006
Related Milestones: MG003
Objectives: O2, O4
Estimate: 180 active minutes（q314 の合計 1440 の一部）

共通の設計・事実・関数構成は [i915-design.md](../i915-design.md) を正本とする。本文は差分と受入条件だけを書く。実装前に [Guardrail](https://github.com/awemorris/zedBSD/issues/363) と `plan/coding-style.md` 全文を読む。HAL（`include/hal/hal.h`）と UAPI layout は変更しない。git add/commit/push はユーザーが行う。

## 目標

実機で copy/fill/store/job を通し、host が guest RAM を独立に照合し、hang 注入から engine reset と別 session の継続を確認する。全新規 source の静的レビューと規約全文確認を行い、WS029 の記録を更新する。

## 手順

1. `run-i915-remote.py --attempt q314-i915-test-001 --test` を実行。`GPUI915 PASS` と `memory_check` PASS まで修正と再実行を繰り返す（同条件 3 回まで、修正は必ず記録）。
2. **hang 注入**（stretch）: `gpu-i915-test --hang` で BCS0 に `MI_SEMAPHORE_WAIT`（決して満たされない条件）を投入した producer と、別 process の consumer/peer を走らせ、core の実行期限（`config-i915-selftest-amd64.mk` に `CONFIG_GPU_JOB_EXECUTION_MS := 8000`）→ stop 期限 → `isolate`（engine reset）→ peer の copy 継続を `GPUI915 ISOLATION_PEER_OK` で確認。成立しなければ結果と原因を制限として記録する（受入必須ではない）。
3. **静的レビュー**: `gcc -fanalyzer`（p009 の `analyzer-initial.log` と同じ呼出し）と `clang --analyze` を新規 source に適用し、警告を実 callee/入力前提と照合して記録。`plan/coding-style.md` §14 の checklist を全新規ファイルに適用し、指摘と修正を `plan/ws029/phase007/style-review.md` に残す。`git diff --check`。
4. **回帰**: host fixture 全種、`plan/ws014/tests` の GPU core fixture（`gpu-framework`、`gpu-supervision`、`gpu-job`、`venus-backend`）、build selection（I915/VENUS の 4 組合せ）。
5. **記録**: `plan/ws029/phase007/results.md`（受入表、host status 前後、hash、失敗履歴、制限）、`plan/ws029/results-q314.md`、`ws029/ws.md` の registry を cleared/未達で更新、後続（display/scanout、GuC 不使用の再確認、非 LLC 機、userland Vulkan の native 経路）を registry の planning 行として列挙、GitHub 同期。

## 完了条件

`test-001` 系の PASS（guest 検証 + host `memory_check`）、静的レビューと規約 checklist の完了、回帰 PASS、host 復旧、記録同期。hang 注入は結果を記録すれば未達でも p007 を cleared とする（本文に明記）。

## 実行境界

p006 と同じ host 境界。source/doc の git add/commit/push はユーザー担当。

## p007 実機描画確認・静的レビュー結果（2026-09-14）

実機（Latitude 5330、IGD `8086:46a8` rev 0c、VFIO passthrough）で copy/fill/store/job を通し、host が guest RAM を QMP `pmemsave` で独立に照合した。全新規 source の静的解析と規約全文確認を行った。

### 受入表

| 項目 | 結果 | 証拠 |
| --- | --- | --- |
| 実機 attach → selftest → publish | PASS | `q314-i915-boot-007`（boot-pass）、`selftest bcs0 store=ok irq=1` |
| copy（`XY_SRC_COPY_BLT`） | PASS | guest `copy=1`、host src pattern `0x5a000000+i` 0 不一致 |
| fill（`XY_COLOR_BLT`） | PASS | guest `fill=1`、host dst fill `0x3197a5e2` 0 不一致 |
| store（`MI_STORE_DWORD_IMM`） | PASS | guest `store=1`、host dst[0]=`0xdeadbeef` |
| job（RESERVE/COMMIT/WAIT） | PASS | guest `job=1` |
| host memory_check（QMP 独立照合） | PASS | `q314-i915-test-002` result.json `memory_check.status=pass` |
| 静的解析 gcc -fanalyzer / clang --analyze | PASS（0 件） | `phase007/analyzer-gcc.log`、`analyzer-clang.log` |
| 規約 §14 全新規ファイル | 完了 | `phase007/style-review.md` |
| 回帰（host fixture / GPU core / build selection） | PASS | 下記 |
| host 復旧（attach 前後で driver=i915, GDM active） | PASS | 各 attempt `host_restored=true` |

### 実機 memory_check（`q314-i915-test-002`）

- src（handle 2、phys 0x3ef1c000、va 0x100000000）: 256×64 の pattern を copy、`pattern_mismatches=0`。
- dst（handle 3、phys 0x3ef30000、va 0x100200000）: fill 後に store、先頭 `0xdeadbeef`、残り `0x3197a5e2`、`fill_mismatches=0`、`store_word_ok=true`。
- GPU が実際に guest RAM へ書いた内容を host が独立に確認した。

### hang 注入（stretch、未達 → 制限として記録）

`gpu-i915-test --hang`（`q314-i915-hang-002`、`CONFIG_GPU_JOB_EXECUTION_MS=10000`）: producer が BCS0 に `MI_SEMAPHORE_WAIT`（決して満たされない）を投入し、別 process の peer が同 engine に copy を投入した。

- **確認できたこと**: core の実行期限が hung session を検出し、`isolate` が hung session の object を quarantine（`i915: session N quarantined; objects retained for reset`）した。isolation 機構自体は実機でも動く。
- **未達**: peer session の copy が継続せず、peer も quarantine された（`GPUI915 ISOLATION_PEER_OK` は出ず）。
- **原因（推定）**: v1 は BCS0 単一 engine で 1 request 直列。peer の copy は hang の後ろに queue され、engine が hang で占有される。core の監視は両 context に期限を課すため、engine reset で hung を切り離しても peer も期限に達して isolate された可能性が高い。engine reset 後の queued peer request の再投入とタイミングは実機で未確立。
- **isolation→engine reset→peer 継続の機構は host backend fixture（`i915-backend-test.c` の `test_isolate`）で検証済み**。実機での peer 継続は後続の課題（下記 registry）。phase 規定により、結果を記録すれば hang 注入未達でも p007 は cleared。

### 静的レビュー

`phase007/style-review.md`。gcc-14 `-fanalyzer` 0 件、clang `--analyze` 初回 1 件（`i915.c` の false-positive null-deref、不変条件の明示 guard で解消）→ 0 件。規約 §14 checklist を全新規ファイルに適用し完了。

### 回帰

| test | 結果 |
| --- | --- |
| `run-i915-host-tests.sh`（uncore/gtt/irq/lrc/stream/backend、通常＋ASan/UBSan） | PASS |
| `run-gpu-framework-test` / `run-gpu-supervision-test` / `run-gpu-job-test` / `run-venus-backend-test` | PASS |
| `run-i915-build-selection-test.py`（Venus/i915 × y/n × 6 platform） | PASS |
| kernel build 3 構成（i915 / i915+selftest / GPU なし） | PASS（warning 0、GPU なしは `drv_i915_` symbol 0） |
| `git diff --check` | PASS |

### 実機 bring-up で判明し修正した点

1. UEFI loader（`bootloader/uefi/bootx64.c`）は GOP framebuffer を必須とするため、QEMU を `-vga none` から `-vga std`（表示 backend なし）に変更。標準 VGA は class-match の PC/AT graphics、IGD は exact-ID-match の i915 が取る。
2. PCI core は claim 済み BAR しか map しないため、`i915_start` で `drv_pci_device_claim_bar(BAR0)` を追加。
3. `i915_get_info` の `max_resources` を 0 → `UINT32_MAX`（core が `resource_count >= max_resources` で ENOSPC を返す）。
いずれも host fixture に回帰を追加済み（BAR claim/release stub、backend test の resource 作成）。

### cold VFIO attach の bring-up 間欠ハング（既知の制限）

実機で attempt を繰り返すと、attach の engine bring-up〜selftest（guest.log の `MSI vector` の直後）で間欠的にハングする。観測: `boot-007`・`test-002`・`hang-001` は通過（`test-002` は full pass + host memory_check）、`boot-005`・`boot-006`・`test-003` はハング。`test-003` は `test-002` と論理同一（削除したのは debug log のみ）で、run 間の device 状態差による間欠事象であり、log 削除の回帰ではない。

- **原因（推定）**: cold VFIO bind 直後は GT が RC6 で、forcewake ACK は返るが domain の wake が安定する前に最初の engine register 書込み／ELSQ 投入が走ると停止する可能性。
- **扱い**: full pipeline（copy/fill/store/job + host RAM 照合）は `test-002` で実証済み。bring-up の安定化（forcewake 後の GT wake/RC6 settle 待ち、初回投入前の engine idle 確認）を最優先の後続とする。未検証の変更で回帰を招かないよう、本 Phase では現象を記録して cleared とする。

### 制限と後続（WS029 registry の planning 行）

- cold VFIO attach の bring-up 間欠ハング安定化（forcewake/RC6 settle）。**最優先**。
- 実機 peer 継続（hang 注入）は未確立。engine reset 後の queued request 再投入を実機で詰める。
- display/scanout（IGD UPT で今回対象外）、GuC/HuC 不使用の再確認、非 LLC 機、userland Vulkan の native 経路。
