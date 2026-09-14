# Queue q314 結果: WS029 i915 ネイティブ GPU driver（2026-09-14）

Alder Lake-P（Iris Xe、`8086:46a8`）向けの i915 ネイティブ GPU driver を drv_gpu framework 上に実装し、実機 VFIO passthrough で copy/fill/store/job を動作させ、host が guest RAM を独立に照合するところまで到達した。Linux i915（MIT）の定義・テーブルは出典付き `.inc` へ転記し、driver 論理は zedBSD 規約で新規に書いた。HAL・UAPI は不変。git add/commit/push はユーザー担当。

## Phase 結果

| phase | 内容 | 結果 |
| --- | --- | --- |
| p001 | 対象確定・ライセンス境界・移植方針 | cleared。参照 29 ファイル全 MIT（v6.19）、symbol 257、VFIO 前提確定 |
| p002 | driver 骨格（PCI attach、MMIO/forcewake、GGTT、割込み） | cleared。3 構成 build、host fixture、`.inc` 生成器 |
| p003 | メモリ（GEM object、48-bit PPGTT、CPU view） | cleared。resource_create/read/write 接続 |
| p004 | 実行（engine/LRC/execlists、request/seqno、engine reset、selftest） | cleared。ELSQ 投入、CSB 消費、breadcrumb |
| p005 | drv_gpu 統合、native stream、生 UAPI 試験クライアント | cleared。command/job/recovery ops v9、`/bin/gpu-i915-test` |
| p006 | VFIO passthrough テストループ | cleared。復旧 rehearsal、`boot-007` boot-pass（実機 selftest store=ok） |
| p007 | 実機描画確認、静的レビュー、規約全文確認 | cleared。copy/fill/store/job + host memory_check PASS、解析 0 件、回帰 PASS |

## 実機での到達点

- attach → forcewake → GT reset → GGTT（1M entry）→ MSI → engine×2 → **selftest（BCS0 が store を実行し user interrupt を上げた）** → `/dev/gpu0` 公開。
- userland `/bin/gpu-i915-test`: copy（`XY_SRC_COPY_BLT`）、fill（`XY_COLOR_BLT`）、store（`MI_STORE_DWORD_IMM`）、supervised job を実行し `GPUI915 PASS copy=1 fill=1 store=1 job=1`。
- host が QMP `pmemsave` で dst の物理範囲を dump し、copy pattern・fill 値・store 語を独立に照合（`memory_check.status=pass`）。GPU が実際に guest RAM を書いた証拠。
- 各 attempt 後に host の i915 と GDM を復旧（`host_restored=true`）。

## 実装（すべて local、driver は Zlib、`.inc` は MIT 出典付き）

- `src/drivers/gpu/i915/`: `internal.h`、`i915.c`（PCI + drv_gpu 全 ops）、`uncore.c`、`ggtt.c`、`ppgtt.c`、`gem.c`、`engine.c`、`lrc.c`、`request.c`、`irq.c`、`selftest.c`、`linux/{i915-regs,i915-ids,i915-commands,i915-lrc-offsets,i915-mocs}.inc`。
- `include/drivers/i915.h`。build 配線（`Makefile`、`platform/amd64/vmunix.mk`、`src/kern/platform/pcat.c`、`config/drivers/pci.drivers`、`config/kernel-options.list`）。
- `userland/base/tests/gpu-i915/{main.c,Makefile}`。
- `plan/ws029/tests/`: `gen-inc.py`、`gen-symbols.py`、`fetch-linux-refs.sh`、`host-i915-facts.sh`、host fixture（`i915-fixture.inc`、`i915-{uncore,gtt,irq,lrc,stream,backend}-test.c`、`run-i915-host-tests.sh`）、build selection、`config-i915-amd64.mk`/`config-i915-selftest-amd64.mk`、`run-i915-analyzer.sh`、remote loop（`host-igd.sh`、`i915-qemu.py`、`run-i915-remote.py`、`README-i915-remote.md`）。
- doc: `plan/ws029/i915-design.md`、`i915-symbols.md`、`i915-vfio-plan.md`、`i915-license-audit.md`、`i915-native-stream.md`、`phase00N/` 記録。

## 検証（agent-1 + 実機）

- host fixture（uncore/gtt/irq/lrc/stream/backend、通常＋ASan/UBSan）: PASS。
- kernel build 3 構成: PASS（warning 0）。GPU なし build に `drv_i915_` symbol 0。build selection 6 platform × 4 組合せ: PASS。
- GPU core 回帰（framework/supervision/job/venus-backend）: PASS。
- 静的解析 gcc `-fanalyzer` / clang `--analyze`: 0 件。`git diff --check`: PASS。
- 実機: `boot-007` boot-pass、`test-002`/`test-003` pass（guest + host memory_check）。

## 制限・後続（WS029 registry の planning 行）

- hang 注入の実機 peer 継続は未達（isolation の quarantine は動作。単一 BCS0 直列と engine reset 後の再投入タイミングが要検討）。
- display/scanout（IGD UPT で今回無し）、GuC/HuC 不使用の再確認、非 LLC 機対応、userland Vulkan の native 経路。
- 物理連続 GEM backing（≤16 MiB/object）。scatter/gather は後続。
