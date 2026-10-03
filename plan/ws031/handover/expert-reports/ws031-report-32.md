# WS031 第32報 — 移植差 5単位(T1〜T5)を修正・実装、実機で P2受入維持 + P3 着手(frontier=drm_vblank_init)

ご指示の 5 作業単位を累積適用し、実機(参照条件 4GiB/4vCPU/host-phys-bits-limit=39、GPUあり通常probe 一回)で検証しました。
**ktest 63 checks 0 failures / boot CPUs 4 / VFIO -22 無し / P2受入維持のまま P3 へ継続**。build 0 error/warning、drm 非blacklist、GPU vfio-pci 維持。

添付: 変更した関数本体 `wait.c`(新規)/`reset.c`/`pcode.c`/`dram_bw.c`。本文に HAL・clock・probe の主要 hunk を掲載します。

---

## 1. 実機ログ(要点)

```
ktest (…): 63 checks, 0 failures
  reset: 成功 passes=2 rc=0 / timeout passes=1 rc=-42 ×3 / retry passes=1 rc=-42 → passes=2 rc=0
  pcode: 成功(応答)/ busy(-EAGAIN)/ status(-ETIMEDOUT)
  dram/bw 保存状態: max[0].num_qgv_points=4, deratedbw[0]/peakbw[0]≠0,
                    max[0].num_planes=0 かつ max[1].num_planes≠0, PSF失敗→max[0].num_psf_gv_points=0
  IRQ oneshot: shared completion signalled from a timer IRQ / 一回・cross-CPU・tag一致
boot: CPUs ready: 4                       (PAT 完全手順を全4CPUで実行、panic/fatal無し)
P2: WC 256MB map / dram rc=0 / bw rc=0 sagv=2 / pci_enable_msi kept for P4 (resource retained)
P3: display_driver_probe_noirq enter (device state carried from P2, msi_kept=1)
    → reached=P3 outcome=BLOCKED where=drm_vblank_init
teardown: MSI resource released / WC aperture unmap rc=0
runner-result: selftest=PASS probe=BLOCKED last_op=drm_vblank_init cleanup=1 published=0
```

---

## 2. T1 待機・errno(新規 `wait.c`、`reset.c`/`pcode.c` 改)

### 共通時間層 `wait.c`
`kern_rtc_read_counter`(単調・**IRQ無効でも進行**・CPU跨ぎ非後退)を時間源に、実時間の atomic poll / udelay / sleep可能 poll を実装。
sched_ticks + spin-count 代用を撤去。時間源不在時のみ bounded fallback で **「時間基盤異常」を診断ログ**(通常HW timeout と区別)。

```c
int parity_wait_reg(struct osdep_mmio *m, uint32_t reg, uint32_t mask, uint32_t value,
	unsigned fast_us, unsigned slow_ms, uint32_t *out)
{
	uint64_t base=0, now=0, freq=0, target; uint32_t v=0;
	if (!kern_rtc_read_counter(&base,&freq) || freq==0u) { /* bounded fallback + 診断ログ */ ... return -ETIMEDOUT; }
	if (fast_us) { target = us_to_ticks(fast_us,freq);          /* atomic: 非sleep, spinlock/IF-off 安全 */
		for(;;){ v=osdep_mmio_raw_read32(m,reg); if((v&mask)==value){*out=v;return 0;}
			kern_rtc_read_counter(&now,&freq); if(now-base>=target)break; kern_compiler_barrier(); } }
	if (slow_ms) { kern_rtc_read_counter(&base,&freq); target=us_to_ticks((uint64_t)slow_ms*1000u,freq);
		for(;;){ v=osdep_mmio_raw_read32(m,reg); if((v&mask)==value){*out=v;return 0;}
			kern_rtc_read_counter(&now,&freq); if(now-base>=target)break; sched_yield(); } }  /* sleep可能 */
	*out=v; return -ETIMEDOUT;
}
```

### reset.c
- **uncore lock 区間を実成立**: `spin_lock_irqsave(uncore_lock)` … `spin_unlock_irqrestore`(gen8_reset_engines 相当、attempt毎)。
- **retry は -ETIMEDOUT のときだけ**(`ret == -ETIMEDOUT`)。ack待ちは `parity_wait_reg(atomic 2000µs)`、settle は **`parity_udelay(50)`**。
- ログ **passes = 実際の write/poll 回数**(初回失敗の attempt は passes=1)。errno は `<errno.h>` 記号(**-ETIMEDOUT=-42**、手書き62撤去)。

### pcode.c
- **device所有 sb_lock(mutex)** で直列化(file-static spinlock + 遅延初期化フラグを撤去)。
- fast=500µs **atomic** → 未完なら **sleep可能 20ms**(mutex保持ゆえ sleep 可)。status→負errno記号。

**errno 契約**: Linux由来の内部処理は 0 または負errno(`<errno.h>` 記号)。zedBSD native 境界での符号変換は今後 P4/P5 の接続時に一度だけ行います。

## 3. T2 帯域状態(`dram_bw.c`/`.h`)

計算結果を **device所有 `struct parity_bw_state`(max[6]{deratedbw[8]/peakbw[8]/psf_bw[3]/num_qgv_points/num_psf_gv_points/num_planes} + sagv_status + valid)** へ保存(local+log のみを撤去)。sb_lock/mmio/di/bw を引数化、probe が device 寿命で所有。

- **num_planes は max[i+1]** へ(参照どおり。max[0] へ独自代入せず=ゼロ初期化のまま)。
- **ct<=0 は入力検査**として明示(算出不能テーブルを有効化せず deratedbw/peakbw=0、参照差としてログ)。PSF失敗は保存 num_psf_gv_points=0 に反映。
- 試験を **return後の保存状態検証**へ強化(§1参照)。P3 で display 構造をゼロ化して P2 の帯域テーブルを消しません。

## 4. T3 PAT(`amd64/asm.c` 新 `amd64_pat_configure`)

wbinvd→wrmsr→wbinvd→flush_tlb を Intel SDM のメモリ型MSR更新手順へ置換。**現CPU初期化で補完**(将来送り撤回)。

```c
/* IRQ保存→cli → PGE無効 → CR0.CD=1,NW=0 + WBINVD + CR3再読(TLB flush)
 * → MTRR無効(DEF_TYPE.E=0, 内容保持; CPUID.01H:EDX.MTRR で gate)
 * → IA32_PAT=0x0007040100070406(index4=WC) → MTRR復元
 * → WBINVD + CR3再読 → CR0/CR4復元 → IRQ復元
 * 設定後 IA32_PAT を read-back、不一致は HAL_FATAL(記録) */
```
BSP+3AP の全4CPUで実行し boot 健全(read-back 一致=HAL_FATAL不発)。WC slot配置/PTE変換は不変、MTRR内容は推測せず保存・無効化・復元のみ。probe/mapping 毎の WRMSR は無し。

## 5. T4 実IRQ文脈 completion 試験(`kern/clock.c` one-shot hook)

診断ビルド限定の one-shot hook を timer ISR(`kernel_timer_handler` 末尾)へ。

```c
static void diag_oneshot_fire(unsigned cpu) {
	if (atomic_load_acquire(&diag_oneshot_armed) == 0U) return;
	if (cpu == diag_oneshot_avoid_cpu) return;                 /* waiter の CPU では発火しない */
	{ unsigned exp=1U; if(!atomic_compare_exchange(&diag_oneshot_armed,&exp,0U)) return; }  /* 一回だけ */
	if (diag_oneshot_fn) diag_oneshot_fn(cpu, diag_oneshot_arg);
}
```

**重要な設計判断(IRQ-safety)**: 共有 backend `parity_kcompletion` は **plain `spin_lock` + `waitq_sleep`= thread文脈専用**です(`spin_lock` は IRQ を無効化せず、`waitq_sleep` は plain unlock を要求)。
同一CPUで waiter が `c->lock` 保持中に timer IRQ が `complete()` を呼ぶと deadlock 窓が生じます。backend を改変しない前提で、hook を **waiter の CPU(avoid_cpu)以外でのみ発火**させ、**cross-CPU 完了**で窓を解消しました(参照条件 4CPU、単一CPUは skip)。試験は「timer IRQ から共有 completion 通知」「一回・cross-CPU・tag一致」を検証(pass)。one-shot object は file-scope=診断kernel寿命保持、IRQ内 free/unregister は行いません。

## 6. T5 P3 着手(`probe.c` continue-mode + `parity.h`/`runner.c`)

- `PARITY_STAGE_P3` 追加、runner は stop_after=P3。
- **continue-mode**: `stop_after==P2` は従来の診断stop+cleanup。P3継続時は **P2資源(WC aperture/MSI/MMIO/DRAM・bw状態)を P2末尾で破棄せず**、最終 teardown で逆順解放。**MSI は継続時 P4 用に保持**(msi_kept)、teardown で解放。診断stop cleanup と通常段階通過を分離。
- **P3 = intel_display_driver_probe_noirq**: i915_inject_probe_failure=no-op、HAS_DISPLAY=true(OpRegion不在と独立)を再現。子処理列(vblank/bios/vga/power domains(+hw)/pmdemand/DMC/workqueue/mode config/CDCLK/color/DBUF/**intel_bw_init=display SW状態(P2 intel_bw_init_hw とは別)**/pmdemand/quirks/FBC)を移植対象として記録。
- **到達点 = 最初の正確な未実装依存 `drm_vblank_init`**(drm_device + INTEL_NUM_PIPES = **display/drm 層**。compute中心 parity は未 bring-up)。

## 7. ご相談(P3 の深度)

P3 子処理はいずれも **drm_device / display power-domain / DMC / CDCLK / DBUF 等の display 基盤**に依存します(現 parity は GT/compute 中心で display/drm 層を未実装)。最初の依存は `drm_vblank_init` です。ここから先の移植は「display 基盤の bring-up」という大きな下位工程になります。方針として:

- **(A)** drm_device 相当と power domains から順に基盤を立ち上げ、子処理を参照どおり移植していく(大規模・多段)。
- **(B)** P3 のうち **GT/submit に必要な最小限**(例: power domains の HW 初期化、DMC firmware)だけを先に接続し、full display(vblank/CDCLK/color/DBUF 等の表示専用)は後段に回す。

DMC firmware は、ご指示どおり VFS 非依存の read-only provider(起動image同梱、名/サイズ/hash/所有明示)で `request_firmware`/`release_firmware` 契約へ接続する用意があります。**P3 の移植範囲・順序(特に display 基盤をどこまで立ち上げるか)**についてご指示ください。

## 8. 台帳・区別

E-56〜E-59 に記録。**「実行成功(P2受入)」と「既知の移植差の解消(T1-T5)」を別記録**。56→63 checks の追加分(reset/PCODE/dram-bw保存/IRQ oneshot)は **実kernel上の fake backend 試験**であり、実GPUで各失敗条件を再現した訳ではない旨も明記しています。旧MSI API 撤去は本件の完成条件外(新経路の非依存は確認済)。

以上、T1〜T5 完了のご報告と、§7(P3 の深度)のご指示をお願いします。
