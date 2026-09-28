<!-- awesome-plan project=zedbsd record=ws073p024 -->

# ws073-p024: amd64 の `current_space` を 1 回の `%gs` 相対の access で読み書きする（BUG-088 の予防）

Status: cleared（2026-09-28）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-088](../../bugs/BUG-088.md)（[ws073-p023](../phase023/phase.md) の監査の後の main の決定、2026-09-28）

## 目的と受け入れ

監査（p023）では今の呼び手は全て割り込み禁止の中で、不具合は無い。main の決定（2026-09-28）「予防として 1 回の %gs の load にする。実装だけで
承認は要らない。hal.h に触れない」。build（warning 0）、boot test、`tests/bug082.sh` を約 30 回。

## 変更（HAL の実装。hal.h は不変）

- `src/hal/amd64/percpu.h`: `AMD64_PERCPU_CURRENT_SPACE`（`current_space` の offset の enumerator、`AMD64_PERCPU_RUNNING_TASK` の隣）。
- `src/hal/amd64/space.c`: `AMD64_CURRENT_SPACE` の macro（per-CPU の pointer の load と field の load）を削り、`current_space_load()`
  （`movq %gs:OFF, reg`）と `current_space_store()`（`movq reg, %gs:OFF`）に。どちらも先に `amd64_percpu_current()` で選択の検査を保ち、
  `"memory"` の clobber で compiler の順序を保つ（amd64 の aligned な load・store は acquire・release）。使う所は 5 つ（起動の初期化、
  `hal_space_switch()` の比較と 2 つの store、`shootdown()` の送り手の判定）。
- 生成の code の確認: `hal_space_switch` の比較が `movq %gs:0x100, %rax` の 1 命令（llvm-objdump）。

## 検証（QEMU、amd64 native の full guest（`tests/kernel-image.sh`）、NVMe、KVM。実機は未実施）

- build: `make -j32 ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-guest.mk BUILD=build/ws073-guest vmunix`、warning 0、vmunix check PASS。
  規約: `tests/style-diff.py`（space.c・percpu.h）0。
- boot test: `plan/tools/boot-test.sh` PASS（`build/ws073-p024/boot-test/login.png`）。
- `tests/bug082.sh build/ws073-p024/vmunix 30 build/ws073-p024/bug082`: 30/30 PASS（1 回 2〜8 秒、fatal・hang なし）。
- 未実施: 他の arch（amd64 の HAL だけの変更）、実機。
