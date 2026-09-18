# Gen12 PS/compute ハング 第29報 — runner 寿命/結果・同期 backend 共有化・Work2 B(cross-CPU 含む)を実機検証

ご指定の直近着手順(runner の結果・寿命管理を閉じる → 同期 backend を共有化 → B の残りを通す)を進め、
**runner・共有 backend・Work2 の主要 5 試験を実機で検証**しました。残るは B の IRQ-context complete と、C/D です。

## 1. runner の結果・寿命管理(差分)
- **result 構造体**: `selftest_status`(PASS/FAIL/NOT_RUN) / `selftest_scope` / `probe_status`
  (STOPPED_AT_P2/BLOCKED/FAILED/NOT_RUN) / `last_completed_op` / `blocked_or_failed_op` / `cleanup` / `published`。
  overall rc を ktest 戻り値だけから生成せず、上記で分離。実機出力:
  `runner-result: selftest=PASS selftest_scope=K0-K5 probe=BLOCKED last_op=i915_ggtt_init_hw cleanup=1 published=0`。
- **thread と handle の分離**: thread object は detached で自動回収。runner handle(result/device 参照/thread_id[log 用])
  は別寿命。thread ポインタは終了後に参照しない(照会は thread_id を保存)。
- **公開順序**: result を fill 後、lock 下で `done=1` と共に公開(単なる done=1 を同期代用にしない)。
  `done` は「probe 結果確定」の意味で、thread 消滅とは別扱い。device 参照は runner の最終アクセスまで保持。
- **共通 launch 判定**: register()/start() が同じ判定を呼び、`ready && !launched` の遷移を lock 下で一度だけ。
  register→ready / ready→register / 重複 start のいずれでも P0 入口は一度。kthread 生成失敗時は launched を戻し
  所有権を残さない。
- **モード**: SYNC_ONLY(GPU 未登録=sync 試験のみ、P0 呼ばない)/ PROBE(GPU 登録=sync+P0-P2)。同一 readiness hook・
  同一 backend。device node/ioctl 追加なし。early attach は register+return 0(非公開)を維持。

## 2. 同期 backend の共有化(差分)
`parity/backend_sync.{h,c}` に実装を集約:
- **completion**(waitq+spinlock, counting 契約): init/complete/complete_all/reinit/kwait(deadline=sched_ticks, 1 成功/0 timeout)。
- **workqueue**(kthread worker): create/queue_work/cancel_work_sync(deadline)/destroy。PENDING/RUNNING 分離、
  self-requeue、per-work done_waitq で cancel_work_sync が RUNNING→終了を待機。
`parity/ktest.c` は test case のみ(専用 completion/worker を廃止)。M4 の request 完了待ちも同 backend を使う予定。

## 3. Work2 B の実機結果(runner ready 文脈, GPU 登録あり PROBE でまとめて実行)
```
K0 native-waitq: start=501 deadline=522 end=522 rc=42   (native timer/waitq が deadline を越えて起床、completion 非依存)
K1 complete-before-wait / K2 counting / K3 blocking-wait-woken-by-thread / K4 timeout / K5 complete_all  = pass
WQ-requeue: 実行中 self-requeue で ran_count==2                    (二重同時実行なし、最後 idle)
cancel_work_sync: 三者(runner/worker/canceller)。order_ok==1       (callback 復帰まで sync-cancel が復帰しない)
cross-CPU: gens=64 cross_cpu=64 mismatch=0 queue_fail=0            (全 64 世代が queuer と別 CPU で実行、公開 payload 一致)
→ 計 19 checks, 0 failures
```
- **cross-CPU が実測で 64/64**(worker thread が別 CPU で実行)。payload を queue の**前**に書き、worker が callback 冒頭で
  local へ読む。書込〜読出の間にテスト用 lock/handshake を挟まない。queue_work 成功時の公開関係を検証(全世代一致)。
- **cancel_work_sync**: canceller は worker 開始を待って cancel を呼ぶ→worker は release まで callback 内で待機→
  runner が release→worker callback 復帰→cancel 復帰。worker が最終行で立てる `worker_returned` が cancel 復帰時 1
  (backend が callback 復帰後に state 遷移+done_waitq wake、cancel はそれで復帰、の構造で保証)。
- 各試験終了時: workqueue destroy で worker 回収、canceller/completer thread は detached 自動回収、waiter は復帰確認。

## 4. まだ満たしていない点
- **B: IRQ-context complete**(唯一の残 B)。実 IRQ 経路(timer IRQ の追加 handler、または IPI)から共有 completion へ
  complete し、待機 thread が復帰することを実 IRQ 文脈で確認します。通常 thread からの ISR 直呼びは代用にしません。
  → 実装方針として、**timer IRQ(KERN_CLOCK_HZ=100)に追加 handler を kern_irq_register で載せ、一度だけ complete して
  unregister** を検討しています。この方式(scheduler tick IRQ に一時 handler を相乗り)で問題ないか、あるいは推奨の
  triggerable IRQ 経路があればご教示ください。
- **launch-once の mock 試験**(register→ready/ready→register/race/重複 start で P0 一度)は、実装は済ですが host mock
  での網羅試験は未追加です。次で追加します。
- **init-memory 解放との関係**: zedBSD に起動用領域の後解放機構があるか未調査です。runner/probe/引数/callback 参照領域が
  実行終了前に解放されない保証を、機構の有無とともに次報で記録します(機構が無ければその事実を記録)。

## 5. 残り(C / D、B と並行してコード実装可)
- **C1**: P1 reset を `__intel_gt_reset(ALL_ENGINES)` 関数列へ(forcewake 取得→reset callback→engine prepare/cancel→
  domain reset[正常時 2 回 write/poll + 50µs settle]→retry 最大 3→forcewake 解放)。単発 write に「もう一回」継ぎ足さない。
  fast MMIO poll は blocking wait に変えない(Work2 の sleep 可能待機と区別)。GEN11_GRDOM_FULL=GEN6_GRDOM_FULL 同値。
- **C2**: `ggtt_init_hw`(i915_address_space_init(GGTT)→caps/callback→mappable_end≠0 で aperture WC mapping→arch WC→
  fences)。scratch は現位置保持。GGTT 総範囲 / PTE window / CPU aperture を別資源(gmadr.start, mappable_end)。全 entry
  独自 scratch-fill なし、scratch 二重確保なし。資源ごとに cleanup、aperture-fail と post-fence-fail を「全 unmap」に潰さない。
- **C3**: MSI 資源/handler 分離(P2 資源+message+enable / P4 handler)。旧 API は互換 wrapper。handler 未接続でも IRQ 資源を
  安全に所有/解放(GPU-free IRQ-complete 成功を MSI 配線確認の代わりにしない)。
- **C4**: P2 後段を正本 i915_driver_hw_probe 順(aperture 競合→ggtt_init_hw→tile→memory region→GGTT enable→bus master→
  MSI→GVT/OpRegion/PCODE/DRAM/bw)。PAT/AUX/WA/golden は legacy 保持のまま、台帳で「legacy 保存 vs parity 移植済」を分け、
  対応後段位置で統合(P2 前倒し一括はしない)。
- **D**: SYNC_ONLY を GPU-free 一回で B 全試験(IRQ/cross-CPU/cancel 含む、4 vCPU で online/実行 CPU 記録)→ その後
  GPU あり(**参照条件 4GB/4vCPU**)で P0→新 P1 reset→P2 後段を末尾まで、driver 非公開・cleanup 確認。1GB の今回結果は
  別記録として保持、返った DMA アドレス記録、予期せぬ bounce は GPU 使用前に停止。最終判定 STOPPED_AT_P2/BLOCKED/FAILED。

## 6. サマリ
適合層 osdep 6 層 + 実 backend(pci/mmio/dma/sync) + pte + probe + runner + ktest、kernel build 統合、0 error/warning。
mock 契約 191 checks 全 pass(mock-VERIFIED)。**実 kernel 並行試験 19 checks 全 pass(cross-CPU 64/64 含む)**。
実機 P0→P2(scratch まで)走破・BLOCKED at ggtt_init_hw・安全 teardown・device 非公開。単一 parity/legacy 選択。
累積修正[PAT/AUX/WA/golden]は legacy 側保持。

（成果物: `src/drivers/gpu/i915/parity/`{osdep, backend_{pci,mmio,dma,sync}.c, pte.{h,c}, probe.c, runner.{h,c},
 ktest.{h,c}, parity.h, backend.h}, `include/drivers/i915-parity.h`。git 操作なし。開示: Makefile/vmunix.mk/i915.c/main.c。
 GPU vfio-pci 維持。ご確認あれば IRQ-context 完了→C→D を続行します。）
