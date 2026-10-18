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

### 2. tlb.c（TLB の invalidation）— 済み

- 前の版は「This is Linux intel_gt_invalidate_tlb_full()」で、Gen12 と Gen9 の register の選び方を Linux の分け方で 2 つの switch に持っていた。
- 書き直し: engine の class ごとの invalidation の register（class・番地・masked か）を世代ごとの表にし、世代の記述（`struct i915_tlb_layout`: 表、instance の上限、forcewake の domain、OA を書くか）を 1 つ選んで、1 つの関数で engine の bit を引く形にした。timeout の log は小さい関数に分けた。log の文言は同じ。
- 公開の関数の名前: `drv_i915_gt_invalidate_tlb_full` → `drv_i915_tlb_invalidate_all`（呼び手は試験の fhd-render.c・eu-test.c だけ）。tlb.h の Linux の説明を外した。
- 動作の確かめ: 前の版と比べて、forcewake の get の順と失敗の止め方、uncore lock の中の engine の順の書き込み（番地と語）、OA の書き込み（Gen12 で 1 つ以上書いた時だけ）、wait の順と limit、counter の更新、domain の逆順の put が同じことを読んで確かめた（engine の型が要るので host の比べ合わせは作っていない）。build: 2 組とも rc=0、warning 0。

### 3. reset.c の GT の reset の部分（reset.h を含む）— 済み

- 前の版は「follows Linux's __intel_gt_reset(ALL_ENGINES) through gen8_reset_engines() …」で、関数の分け方と名前が Linux の gen6_hw_domain_reset・gen8_engine_reset_prepare/cancel・get_sfc_forced_lock_data・find_sfc_paired_vecs・gen11_lock_sfc/unlock に 1 対 1 だった。
- 書き直し: GDRST の pulse（書いて clear を待つ、2 回）、whole-GT の 1 回の attempt、engine の reset、RESET_CTL の handshake の enter/leave、converter の層（class ごとの register の位置の表 `struct i915_reset_converter`、engine から layout を選ぶ、partner、hold/release、保持の記録 `struct i915_reset_hold`）に組み直した。値は block ごとに `Values referenced:`。log の文言は同じ。recovery の部分（node の回復）は zedBSD の物なので変えていない。
- 動作の確かめ: host 試験 `plan/ws244/tests/reset-equiv/run.sh <前の reset.c> <今の reset.c> <新しい出力の dir>`（GT の reset の部分だけを stub で build）。whole-GT の 6 の場面（成功、3 回とも timeout、timeout の後に成功、settle の失敗、forcewake の失敗 2 つ）と engine の 22 の場面（reset domain 無し、handshake の 3 状態と timeout、engine の domain 無し、forcewake の失敗、converter を自分で保持・ack の timeout で空いた/使用中、HEVC の pipe で partner 経由・空いた・奪われた、誰も使わない、access の無い decode、partner 無し、enhancement、Gen9、pulse の timeout、settle の失敗）で、register の読み書き・wait・forcewake・lock・delay・log の列が前の版と一致（`reset-equiv: PASS (491 lines)`）。
- build: 2 組とも rc=0、warning 0。

### 残り

- device-info・ggtt・ppgtt・memory・irq・dma.h ほか（inventory §3 の表）。
- pcode-equiv・reset-equiv の試験は開発の確かめ用（前の版を入力に取る）。p004 が cleared になったら消してよい（path を Q1 へ）。
