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

### 4. device-info.c・device-info.h（GT の情報）— 済み

- 前の版は「This is Linux's intel_gt_init_mmio()」で、Linux の intel_gt_init_clock_frequency・gen11/gen9_read_clock_frequency・gen12/gen9_sseu_info_init・engine_mask_apply_media_fuses・get_reset_domain・intel_engine_setup_capabilities・intel_gt_check_and_clear_faults に関数ごとに沿っていた。
- 書き直し: 世代の記述（`struct i915_gt_generation`: engine の一覧、engine の GDRST の bit の表、fault の register）を 1 つ選び、timestamp の rate は 1 つの関数（divide logic、Gen9 の 24 MHz、Gen12 の crystal の表）、SSEU は世代ごとの decode、media の fuse、context の大きさ、reset の bit、capability、fault の点検に分けた。register は `I915_INFO_*` の自前の名前にして、block ごとに `Values referenced:`。log の文言は同じ。device-info.h の Linux の struct・enum の説明を外し、engine の id と class の値に referenced を付けた。
- 動作の確かめ: host 試験 `plan/ws244/tests/device-info-equiv/run.sh <前の device-info.c> <今の device-info.c> <新しい出力の dir>`。Gen12 の 18 と Gen9 の 7 の場面（crystal の 0〜7 と shift、divide logic、slice・DSS・EU の fuse、media の fuse で vcs0・vcs2・vecs0 が消える、fault の GGTT/PPGTT、EIR の stuck、版 11・13、Gen9 の slice の組と subslice の fuse、platform の mask の余り）で、register の読み書き・log と、できた GT の情報（clock・SSEU・engine の一覧・reset の bit・context の大きさ・capability）が前の版と一致（`device-info-equiv: PASS (522 lines)`）。
- build: 2 組とも rc=0、warning 0。

### 5. irq.c・irq.h（device の割り込み）— 済み（2026-10-19 q1260）

- 前の版は「follow the reference's gen11_irq_reset(), gen11_irq_postinstall() and gen11_irq_handler() in order」で、関数の分け方と名前が Linux の gen11_gt_irq_reset/postinstall/handler・gen8_gt_irq_*・gen3_irq_reset/init・gen3_assert_iir_is_zero に 1 対 1 だった。
- 書き直し: GT の割り込みの layout を表にした（`struct i915_irq_layout`: master の register と on の値、GT の close・open・serve、GU_MISC の有無）。Gen11 は engine の register の表（class の enable・pair の mask・上半分だけの mask の 3 種）と engine 以外の source（PM・GuC・crypto）の enable/mask の表から書き込みの列を作る。Gen8（Gen9）は bank の表（master の bit、engine の class・instance・bit の位置、PM の bank か）と serve の順（0・1・3・2）から作る。使われていなかった GuC の enable の書き込み（guc_mask が常に 0）は消した。値は block ごとに `Values referenced: Linux v6.19 …`。log の文言は同じ。
- 公開の関数の Linux の名前を外した: `drv_i915_gen3_irq_reset` → `drv_i915_irq_regs_close`、`drv_i915_gen3_irq_init` → `drv_i915_irq_regs_open`、`drv_i915_gen3_assert_iir_is_zero` → `drv_i915_irq_regs_check_clear`、`drv_i915_gen11_gt_irq_handler` → `drv_i915_irq_gt_banks_serve`、`drv_i915_gen11_gt_irq_reset`・`_postinstall` は static に。呼び手 display/interrupts.c・tests/display/host-kernel.c・tests/execution/ktest-gt.c を追従（名前だけ）。irq.h の「the reference's intel_irq_install()」などの説明を外した。device.c の 1 行の「(gen3_irq_reset())」も外した。
- 動作の確かめ: host 試験 `plan/ws244/tests/irq-equiv/run.sh <前の irq.c> <前の irq.h> <今の irq.c> <今の irq.h> <新しい出力の dir>`。Gen12・Gen9・GT 情報なしの 3 つで、install（display 表あり/なし、execlists/GuC、attach の失敗、stale な IIR）、uninstall（detach の失敗、2 回目）、handler（無効、spurious、Gen11 の bank 0/1 の identity・invalid・未知の class・未知の instance・bit なし・GTPM・display・GU_MISC の GSE あり/なし、Gen9 の master の各 bit と全部・PM の handler なし）、bank の serve、PM の IIR の reset（2 回で止む・16 回）、engine の wait・sequence、synchronize（均衡・timeout・time base の失敗）、register の helper で、register の読み書き・posting read・log・lock・wake・EOI・counter の全部が前の版と一致（`irq-equiv: PASS (33782 lines)`）。
- build: amd64 の既定・I915_TESTS=y とも rc=0、warning 0、`amd64 vmunix check: PASS`。xd の host 試験 `sh plan/ws245/tests/run-xd-host-tests.sh build/ws244-xdhost` rc=0（host-kernel.c の改名の確かめ）。

### 残り（再開の手順）

1. ggtt.c（「This is intel_gt_init_scratch()」ほか）・ppgtt.c・ppgtt.h・memory.c・memory.h・dma.h・defaults.h ほか（inventory §3 の表の最後の行）: 多くは 1〜3 行の言及。手で読み、手順の写しがあれば書き直し、無ければ言及を外す。
2. 前の版との比べ合わせ: 書き直す前に file を scratchpad に写して取っておき、`plan/ws244/tests/*-equiv/run.sh <前> <今> <新しい出力の dir>` の形で比べる。Linux 6.8.12 は build/ws229-p001/linux/linux-6.8.12.tar.xz から build/tmp の新しい dir に要る分だけ展開する。

- pcode-equiv・reset-equiv・device-info-equiv・irq-equiv の試験は開発の確かめ用（前の版を入力に取る）。p004 が cleared になったら消してよい（path を Q1 へ）。
