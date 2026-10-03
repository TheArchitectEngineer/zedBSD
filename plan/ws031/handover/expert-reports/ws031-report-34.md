# WS031 第34報 — T単位: HAL時間backendを正式修正(TSC自前較正の迂回を撤去)。実機で source=pit 公開・ktest 65/0・P3到達

ご指示の T（「HAL検査を迂回する自前TSC較正を正式backendに固定しない＝HAL/kernel側を改善」）を実施しました。**根本原因を特定して HAL を修正**し、wait.c から rdtsc / scheduler-tick 較正を撤去して **共通backend(`kern_rtc_read_counter`)へ接続**しました。実機（参照条件 4GiB/4vCPU/host-phys-bits-limit=39）で HAL timecounter が **source=pit** で公開され、ktest 65/0・P3到達。**D（DRM/vblank 完成）と P（P3続行）は継続中**です。build 0 error/warning、drm 非blacklist、GPU vfio-pci 維持。台帳 E-64。添付 `wait.c`。

---

## 1. 3.1 診断 — HAL が無効化する理由（確定）

`kern_rtc_read_counter`（amd64 validated timecounter）が本環境で常に不可用でした。HAL 診断ログで確定:

```
A64 TIMECOUNTER CANDIDATE policy=0 max=32 ratio=0/0 crystal=0 invariant=0 adjust=1
A64 TIMECOUNTER AP FAIL cpu=1..3 reason=candidate-unavailable
A64 TIMECOUNTER UNAVAILABLE cpus=4 source=none reason=candidate-unavailable
A64 TIMECOUNTER KVM probe max=40000001 sig=4b4d564b:564b4d56:0000004d   (診断追加)
```

- **CPUID 0x15（TSC比率/crystal）不在**（ratio=0/0, crystal=0）、かつ **invariant-TSC bit=0**（QEMU `-cpu host` は invtsc を既定でマスク）。`amd64_tsc_cpuid_frequency_evaluate` は invariant 必須のため **UNAVAILABLE** を返し、candidate が成立せず timecounter は公開されない。
- **KVM CPUID**: signature は `KVMKVMKVM` 一致だが **max leaf=0x40000001**（0x40000010 = KVM_TSC_KHZ leaf は非公開）→ CPUID からの周波数取得も不可。
- 過去に「動いた」実行は、wait.c の（今回撤去した）silent fallback 経由で、HAL backend は実は常に不可用でした。
- 別途、guard には「cross-CPU raw TSC が last_sample を下回ると**永久無効化**」（runtime regression 用）もあります。

> `sched_ticks()` 生成元の確認（3.1の追加点）: 本環境では PIT/LAPIC 由来の周期割込で tick を進めており、TSC を再較正する対象とは別系統です（TSC 較正に sched_ticks を使う自前版は今回撤去）。

## 2. HAL 修正（迂回でなく本体修正）

1. **KVM 検出** `amd64_kvm_present()`（CPUID 0x40000000 signature）。KVM は仮想TSCの安定を保証。
2. **KVM 時は `NEEDS_PIT` を選択**（invariant 不在でも既存 PIT 較正経路＝lapic.c を起動）→ **PIT が TSC 周波数を実測**（実機 **2,497,019,031 Hz ≈ 2.497 GHz**、自前較正の値と一致＝相互検証）。
3. **metadata 互換判定を PIT source では invariant-TSC bit 非必須に緩和**（`timecounter-policy.c`）。CPUID の invariant advertisement ではなく、**各AP の実 TSC bracket probe**（uncertainty 有界）が経験的に検証する契約に接続。CPUID15 source は従来どおり invariant 必須のまま。

```c
/* timecounter-policy.c amd64_timecounter_metadata_compatible() */
-	    !ap->frequency.invariant_tsc_supported ||
+	    (source != AMD64_TIMECOUNTER_SOURCE_PIT &&
+	     !ap->frequency.invariant_tsc_supported) ||
```

4. **runtime guard を permanent-disable → monotonic clamp** に変更（cross-CPU の小skew は last 値へ clamp して単調維持、Linux TSC clocksource の last-value guard 準拠）。初回 regression は `A64 TIMECOUNTER REGRESS` で一度記録（今回未発火＝bracket 許容内）。

```c
/* timecounter-policy.c amd64_timecounter_read_guarded()  —  disable → clamp */
if (raw < last) {  /* … one-time diag … */  raw = last; }  /* never regress */
else               __atomic_store_n(&state->last_sample, raw, RELAXED);
```

（KVM→PIT の選択は `timecounter.c amd64_timecounter_bsp_prepare()` に `amd64_kvm_present()` 分岐を追加。）

## 3. 3.3 — parity 側から較正・CPU固有命令を外す（添付 wait.c）

- wait.c は **`kern_rtc_read_counter`（HAL共通backend）のみ**を使用。**rdtsc inline / sched-tick 較正 / `parity_wait_init` を撤去**。
- `us_to_ticks` は乗算 overflow 飽和 + **切上げ**（要求より短い待ちにしない）。
- **時間源読取失敗を隠さない**: `-EIO` + **sticky time-base fault**（`parity_wait_time_base_faulted()`）で通常成功/`-ETIMEDOUT` と区別。probe 開始前に `parity_wait_time_base_ok()` 検査、不在なら FAILED `time_base_anomaly`。`parity_udelay` も fault を latch（log して先へ進まない）。
- slow 側は `kern_deadline_after`/`waitq_sleep` の戻り値を処理（異常時の無限loop回避）。

## 4. 実機結果

```
A64 TIMECOUNTER KVM stable-tsc: PIT calibration
A64 TIMECOUNTER PIT READY hz=0:2497019031
A64 TIMECOUNTER READY cpus=4 source=pit hz=0:2497019031      ← HAL timecounter 公開
ktest (…): 65 checks, 0 failures
P1 gt_reset attempt=0 passes=2 rc=0
P2 hw_probe tail: dram_detect rc=0 bw_init rc=0 sagv=2
P3 drm_vblank_init: 4 CRTCs + vblank worker initialised
attach end: reached=P3 outcome=BLOCKED where=intel_bios_init
runner-result: selftest=PASS probe=BLOCKED last_op=intel_bios_init cleanup=1 published=0
```

**使用 time backend = HAL timecounter (source=pit, 2.497 GHz)**。自前TSC較正の迂回は撤去済で、正式 backend での受入です。time-base anomaly 無し、boot CPUs 4、VFIO -22 無し。

## 5. 残作業（D と P を継続）

- **T 細部**: slow 側の高分解能 sleep-range（現状 1 tick=10ms 粒度。共通時間層の依存として高分解能待機を追加予定）、wait 4試験（時間源未準備/CPU移動・時刻不連続模擬/fast→slow/50µs delay）。
- **D**: DRM device 管理 + drm_vblank_init の**完成**（drmm_add_action_or_reset の「登録失敗時その場実行」契約、per-pipe disable timer / seqlock、途中失敗回収、DRM 4試験）。現状は Work A+B の骨子（4 CRTCs + vblank worker）まで。
- **P**: P3 続行（intel_bios_init → VGA → power domain → … DMC firmware provider）。

以上、T の核（HAL 時間 backend の正式化 + wait.c 接続）完了のご報告です。D/P を継続します。ご確認の点（HAL 修正の方向: KVM→PIT較正 + PIT source の invariant 非必須化 + monotonic clamp）があればご指示ください。
