<!-- awesome-plan project=zedbsd record=ws229-p007 -->

# ws229-p007: WS229 Gen9 対応の規約の全文の見直し

Status: cleared（2026-10-14、P1。範囲は下。実機の byte は T1 が SV7 と 5330 で試験中）
Parent: [ws229](../ws.md)

## 範囲

`git diff d1e743394..main -- src/drivers/gpu/i915 src/kern/boot.c include/kern/boot.h` の C・header と `plan/ws229/tests` の C（約 94 file、追加 約 10900 行）を `plan/coding-style.md` の全文と照らした。

## 方法

1. `plan/tools/style-check.py` を範囲の全 file に走らせ、WS が変えた行（`git diff -U0`）に当たる指摘だけを抽出（39 件）。
2. 機械で見えない規則を、追加行への grep で確かめた: `return f(...)` の形、関数の最後の `return error;`、条件演算子、initializer の関数呼び出し、for の宣言、`_local`/`function_result`、入れ子の呼び出し。
3. clang-format はこの host に無く、未実施。

## 直したこと（書き方だけ。動作・byte は不変）

| 規則 | 数 | 例 |
| --- | --- | --- |
| 段落の comment が無い（5 節） | 約 24 | workarounds.c、device.c、defaults.c、eu.c、tlb.c、blit.c、irq.c、tests の各 file |
| 閉じ括弧の後の空行（5 節） | 3 | eu.c の 2 か所、state-test.c |
| 条件演算子（6 節） | 2 | golden.c（名前）、takeover.c（`where_running`） |
| 条件の中の関数呼び出し（6 節） | 1 | device-info.c の `i915_hweight16` を `slice_count` に |
| 引数の中の入れ子の呼び出し（8 節） | 1 | submit.c の `~i915_gen9_engine_irqs(ge)` を `engine_irqs` に |
| 未使用の変数の warning | 1 | golden.c の `domain`（`GOLDEN_GEN9` の build だけ） |

## 直さなかったこと（理由）

- `intel/lrc-offsets.h` の 8 件: register の並びの data table（`I915_LRC_NOP`・`I915_LRC_END`）で、段落ではない。
- `reset_contract_test.c:197` の `RESET_CTL(...)`: macro で関数ではない（style-check の誤検出）。
- `compile.c`・`compile-compute.inc`・`irq.c`・`submit.c` の `drv_i915_eu_grf(...)`・`GEN8_GT_IMR(n)` などの引数: register を作る macro・小さい構築子で、元の file が同じ形（d1e743394 の compile.c にも同形 3 件）。fallible な呼び出しの入れ子ではない。
- 欠陥に見える所: 見つからなかった。

## 検証（worktree /home/awe/zedBSD-worktrees/p1、出力は build/p007/）

| コマンド | 結果 |
| --- | --- |
| `sh plan/ws229/tests/host/run.sh` | PASS。Gen12 の 17 file が 03dee2287 と byte 同一、882 命令を gen120/gen90 で decode |
| `sh plan/ws229/tests/eu-gen9-host.sh` | PASS |
| `sh plan/ws229/tests/eu-test-gen9-host.sh` | PASS（Gen12 の batch が SHA-256 0978611… で不変、Gen9 の 64 命令・14 期待） |
| `sh plan/ws229/tests/gen12-golden/run.sh` | PASS。543 行が d1e743394 と同一、Gen9 の smoke は 202 write・24 step |
| `sh src/drivers/gpu/i915/tests/contracts/run.sh` | PASS。11 contract、39 check（reset）0 failure、ASan/UBSan |
| `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/p007/k64 vmunix` | rc 0、warning 0、vmunix check PASS |
| 同、`I915_TESTS=y I915_TEST_SET=execution ZEDBSD_TEST_CPPFLAGS=-DI915_TEST_SCENARIO=draw` | rc 0、warning 0、vmunix check PASS |
| `git diff --check` | 指摘なし |

## 未実施

- clang-format（host に無い）。QEMU・実機（T1 の担当）。他の `I915_TEST_SCENARIO` の build。
- 約 10900 行の全行の人の目の通読はしていない。style-check と grep の範囲の確認である。
