# Gen12 PS/compute ハング 第28報 — 作業A(実行文脈)完了・実機実証、Work2 の核を検証。次は B 残/C/D

ご指示の作業順(A 実行文脈 → B Work2 → C reset/MSI/GGTT → D P2 実機)を進めています。**A を完了**し、
その ready 文脈で **Work2 の核(blocking/timeout/別 thread 起床/counting/complete_all)を実機で検証**しました。
今回は「Linux 由来の処理がそのまま sleep できる通常 thread の入口」を作り、そこへ test と parity probe の両方を
接続した、という成果です。

## 1. 作業A: register → readiness hook → managed runner(Q1①)を実装
ご指定の構造どおりに実装しました。
- **early attach は register のみ**: parity mode の `i915_start` は `drv_i915_parity_runner_register(device)` を
  呼び **return 0**(device は attached だが**非公開・P0 未実行**)。parity 固有の PCI enable/reset/GGTT は runner が P0 から一度だけ。
- **readiness hook**: `boot_start` の **VFS init 直前**(scheduler/timer up・platform device 発見済の最初の適切な位置)で
  `drv_i915_parity_runner_start()` を呼ぶ。**hook は待たない**。※本最小 image は VFS mount 対象 storage が無く VFS init が
  error 6 で idle に入るため、kern_init_start(userspace 起動)の手前ではなく **VFS の手前**を採用しました(到達可能で、
  かつ scheduler/timer/device が揃う位置)。この選択でよいかご確認ください。
- **managed runner thread**: `parity/runner.c`。runner 状態(device / result / done / thread)が handle。
  registration lock は probe/test 実行中は保持しない。probe 引数を early-attach stack に残さない。detached=1 で
  終了時 auto-reclaim しつつ、result/done を handle として保持(この「detached + handle で追跡」で可か確認願います)。
- **一度だけ実行**: early で P0-P2 を実行して後段で再実行、はしない。runner が Work2 → P0-P2 を順に一度実行。

## 2. 実機結果(runner thread = ready 文脈)
```
i915: parity runner: device registered, probe deferred to readiness   [early attach: register のみ]
i915: parity runner started                                            [readiness hook]
i915: parity runner thread begin (execution base ready)
i915: parity ktest (in-kernel sync, real threads): 9 checks, 0 failures
    K1 complete-before-wait / K4 timeout / K2 counting / K3 blocking-wait-woken-by-thread / K5 complete_all  = all pass
i915: parity attach begin (stop_after=P2)
    P0 pci_enable ok / P1 BAR 8MB, fuses 5DSS, sanitize reset(polls=40) /
    P2 dma 39bit(i915_bits=39, bus=32) / ggtt_probe 4GB / scratch dma=0x3fc77000 pte=0x3fc77001
    BLOCKED at i915_ggtt_init_hw
i915: parity runner thread end (ktest_rc=0)
```
- **attach 文脈で hang していた K4(timeout)が ready 文脈で成功**。K3(実際に sleep へ入った waiter を別 thread の
  complete が起床)も成功。→ **実 waitq+spinlock completion + kthread の blocking/timeout/cross-thread wakeup が動作**。
  timer 問題は「通常 schedule される runner thread」で解消(attach 中の boot device-probe 文脈が原因だった)。
- P0-P2 は runner から同一挙動で走破(DMA/scratch は第27報どおり保持)。

## 3. まだ満たしていない点(B 残、次に実施)
ご指定の Work2 完了条件のうち、**現状は K1〜K5 相当まで**です。以下が未了で、次に同 runner 基盤上で実装します。
- **K0(native 実行基盤)を独立試験化**: completion に依存せず native timer/waitq/scheduler の動作を別途記録
  (現状は K4 が timer 経路を実質検証済だが、専門家指示どおり分離する)。
- **IRQ 文脈からの complete**(通常 thread からの ISR 直呼びは不可、実 IRQ 文脈で)。
- **cross-CPU queue とデータ公開**(実 CPU ID が異なる記録)。
- **実行中 work への外部からの cancel_work_sync**(callback 終了まで復帰しない、三者 runner/worker/canceller 分離)+ 正当な再 queue。
- ログ拡充: test_id / thread_id / cpu_id / thread-or-idle-or-IRQ 文脈 / IRQ 有効状態 / wait 開始 tick / deadline /
  timeout 処理 / 起床処理 / wait 復帰 tick / 戻り値。host runner にも独立全体 timeout(成功扱いにしない区切り)。
- sync backend の統一: 現 ktest の実 kernel completion を、後段(M4 request 完了待ち)が使う共有 sync backend として
  接続予定(現 P0-P2 probe は同期で completion 未使用のため、統一先の利用者は M4)。この段取りで合っていますか。

## 4. 残り(C / D)
- **C**: P1 reset を `__intel_gt_reset(ALL_ENGINES)` 関数列へ置換(Gen11+ は GEN11_GRDOM_FULL=GEN6_GRDOM_FULL 同値、
  gen6_hw_domain_reset は正常時 2 回 write/poll + 50µs settle、外側 retry 最大 3、forcewake/lock。単発 write に「もう一回」
  継ぎ足さない。fast poll は Work2 の blocking wait と別)。MSI 資源/handler 分離(P2 資源+message+enable、P4 handler)。
  ggtt_init_hw(address-space init, mappable aperture の WC mapping[gmadr/mappable_end 区別], fence init。GGTT 全 entry を
  独自 scratch-fill しない)+ memory region → GGTT enable → bus master → MSI → GVT/OpRegion/PCODE/DRAM を正本 P2 順で接続。
- **D**: 同一 parity probe 経路で P2 末尾まで、**参照条件 4GB/4vCPU** に戻して実機確認(1GB の今回結果は別記録として保持、
  返った DMA アドレスを記録。高位ページを無理に割り当てさせない)。bounce 有無を mapping 結果に記録し、予期せぬ bounce は
  GPU 使用前に未対応として停止。

## 5. 現状サマリ
適合層 osdep 6 層 + 実 backend(pci/mmio/dma) + pte + probe + runner + ktest、kernel build 統合、0 error/warning。
mock 契約 191 checks 全 pass(mock-VERIFIED)。**実 kernel 並行試験 9 checks 全 pass(runner ready 文脈)**。
実機 P0→P2(scratch まで)走破・honest BLOCKED・安全 teardown・device 非公開。単一 parity/legacy 選択(fallback なし)。
累積修正[PAT/AUX/WA/golden]は legacy 側に保持。

（成果物: `src/drivers/gpu/i915/parity/`{osdep, backend_{pci,mmio,dma}.c, pte.{h,c}, probe.c, runner.{h,c}, ktest.{h,c},
 parity.h, backend.h}, `include/drivers/i915-parity.h`, `plan/ws031/linux-parity/`{ledger.md, M1-test-results.txt}。
 git 操作なし。開示: Makefile/vmunix.mk/i915.c/main.c。GPU vfio-pci 維持。ご確認いただければ B 残→C→D を続行します。）
