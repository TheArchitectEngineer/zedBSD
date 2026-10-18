<!-- awesome-plan project=zedbsd record=ws244-p004 -->

# ws244-p004 GT の core の小さい物の書き直し

Status: in-progress（2026-10-18 q1251-i01、P1。cleared は Q1）

- 親: [WS244](../ws.md)。範囲: power.c（PCODE）・reset・tlb・device-info・ggtt・ppgtt・memory・irq・dma.h ほかの言及。動作は変えない。

## 実行の記録（2026-10-18）

### 1. power.c（GT driver mailbox、PCODE）— 済み

- 前の版は「a direct port of Linux's intel_pcode.c」で、関数の分け方（snb_pcode_read・snb_pcode_write_timeout・__snb_pcode_rw・gen7_check_mailbox_status・skl_pcode_try_request・skl_pcode_request）が Linux と 1 対 1 だった。
- 書き直し: mailbox の hardware の約束（run/busy の所有、data 2 語、低い byte の status）から、1 つの exchange の記述（`struct i915_pcode_exchange`: command・data・busy の poll・sleep の poll・読む返事の語数）と、それを走らせる 1 関数、1 回の ask、時間で区切る repeat の 3 つに組み直した。status の errno への対応は switch から表にし、block に `Values referenced:` を付けた。register の名前は `I915_PCODE_COMMAND`・`DATA0`・`DATA1`・`RUN_BUSY`・`STATUS_MASK`。
- 公開の関数の Linux の名前を外した: `drv_i915_snb_pcode_write` → `drv_i915_pcode_write`、`drv_i915_skl_pcode_request` → `drv_i915_pcode_request`（呼び手の gt-power.c・gen9-node.c・ktest-sync.c・rps_contract_test.c・host-kernel.c を追従。ktest の log の文言の「skl_pcode_request」は変えていない）。power.h の「Linux's intel_pcode.c transaction core」を外した。
- 動作の確かめ: host 試験 `plan/ws244/tests/pcode-equiv/run.sh <前の power.c> <今の power.c> <新しい出力の dir>`。前の版（3da 前の power.c の API の名前だけを新しくした物）と今の版を同じ stub で build し、read・write・request の 25 の場面（busy、wait の timeout、status の 0x00〜0x12 の全部、最初の ask で一致、3 回目で一致、一致せず最後の 50 ms まで、status の続く request、rtc の失敗）で、register の読み書き・wait・lock・nap・preempt・log の列を比べて一致（`pcode-equiv: PASS (627 lines)`）。
- build: amd64 の既定・I915_TESTS=y とも rc=0、warning 0。

### 残り

- reset・tlb・device-info・ggtt・ppgtt・memory・irq・dma.h ほか（inventory §3 の表）。
- pcode-equiv の試験は開発の確かめ用（前の版を入力に取る）。p004 が cleared になったら消してよい（path を Q1 へ）。
