<!-- awesome-plan project=zedbsd record=ws052p006 -->

# ws052-p006: CPU の idle・割り込みの HAL の実装と、S0 idle の入口・出口

Phase ID: `ws052-p006`
Parent: [WS052](../ws.md)
Status: in-progress（2026-10-05 P1 generation17。HAL の実装（910bc04c、merge 済み）、kernel の流れ（ae2c61bf）、UAPI（58554e8b）を実装し、amd64 の vmunix の link と sleepctl の build（warning 0）まで。T1 の QEMU（S0IDLE の拒否・DEVICES の往復・拒否の試験）と 5330 の UAT（SLP_S0 の residency、電源ボタン・蓋での起床、counter と RTC の比較）待ち）
Phase disposition: normal
Queue: q742（P1、ベータ2）

## 範囲

- 承認済み: HAL v2（hal.h に 29f954f5 で適用、ユーザー 2026-10-05「hal.hの追加をレビューしました。OKです。…HAL実装に進むことを承認します。」）、
  `/dev/system` の UAPI（ユーザー 2026-10-05「/dev/system から S0 idle に入るための UAPIの変更を承認します。…」、Q1 が
  `docs/architecture/power-management.md` に記録）。hal.h はこれ以上変えない。
- 流れは [README-v2 §6〜§7](../proposed/README-v2.md) と `docs/architecture/power-management.md`。user の process の停止は kernel の側。
- 範囲外: Keiland の契機と表示（p007）、networkd が sleep の前に radio を切る流れ（p007）、Wi-Fi・HDA の本当の低電力（p005 の「止めて入る」まで）。

## 実装

### HAL（910bc04c、merge 済み）

| 部分 | 内容 | 場所 |
| --- | --- | --- |
| `hal_cpu_idle_suspend_supported` / `hal_cpu_idle_suspend` | LPIT の FFixedHW の MWAIT hint（5330: 0x60）、CPUID.1 ECX[3]（MONITOR）、CPUID.5 ECX[0..1]（拡張・割り込みでの break）、CPUID.5 EDX の該当の sub-state の数、invariant TSC を起動時に確かめる。入る時は LAPIC の LVT のうち CMCI・thermal・perf（max LVT の欄で在るものだけ）・LINT（fixed の配送だけ）を mask して保ち、LAPIC の timer を止め、`monitor` と `mwait eax=hint, ecx=1`、`sti; nop; cli`、timer を再開して LVT を戻す | `src/hal/amd64/bsp-pcat/idle-suspend.c`・`.h`、`lapic.c`、`acpi.c`（`discover_lpit`）、`timecounter.c`（`amd64_timecounter_invariant`）、`cmain.c` |
| `hal_irq_set_wake` / `hal_irq_suspend` / `hal_irq_resume` | IRQ ごとの `wake` と `suspended`。suspend は wake でない IRQ を mask して `suspended` に、resume は `masked` を保ったまま戻す。保留中の IRQ が来たら落とす（mask し直して EOI）。`hal_irq_unmask` は suspended の間 hardware を unmask しない | `src/hal/amd64/bsp-pcat/irq.c`・`.h` |
| 他の arch | `HAL_ERR_UNSUPPORTED` を返す stub | `src/hal/idle-suspend-unsupported.c`、各 `platform/*/vmunix.mk` |

### kernel（ae2c61bf）

| 部分 | 内容 | 場所 |
| --- | --- | --- |
| user の停止 | `kern_freeze_user` が停止の印を立てて全 CPU に通知し、user に戻ろうとする thread は `kernel_user_return_handler` の終了の確かめの後（`kern_freeze_user_return`）で止まる（user code を走る thread は次の割り込みで、kernel の中で眠る thread は system call の終わりで）。kernel thread（ACPI の事象・device の仕事）は走り続ける。印の間に来た signal は再開の後に配る。`kern_thaw_user` が起こす | `include/kern/freeze.h`、`src/kern/freeze.c`、`src/kern/signal.c` |
| 調整役 | ioctl の thread が調整役。`kern_sleep_supported`（HAL の probe、FADT の Low Power S0 Idle（flags bit 21）、LPS0 の device、PCI・ACPI の口が揃う）。`kern_sleep_s0idle`: begin の事象 → user の停止 → PCI の suspend（子から、拒否は戻して device の名前）→ LPS0 の enter → wake の GPE だけ → `hal_irq_suspend` → state を ENTER にして CPU が入るのを待つ → 起床の理由 → 逆の順で戻す → user の再開 → end の事象。途中の失敗は前の段を戻し、failed の事象 | `include/kern/sleep.h`、`src/kern/sleep.c` |
| idle の loop | `sched_idle` が `hal_cpu_idle` の代わりに `kern_sleep_idle(cpu)`: ENTER の間は `hal_cpu_idle_suspend`、最初に戻った CPU が ENTER→WAKE にして調整役を起こす | `src/kern/sched.c`、`src/kern/sleep.c` |
| 時刻 | CPU 0 が入る前後に counter を読み、眠った間の tick を進める（端数は次に繰り越す）。起床後に counter と RTC の眠った秒数を log し、2 秒より離れたら WARNING | `src/kern/clock.c`（`kern_clock_idle_suspend_begin`・`_end`）、`sleep.c` |
| 起床の理由 | driver の note（`kern_sleep_note_wake`: 電源ボタン・蓋・AC、電池は別の印）を最大 300 ms（10 ms 刻み）待つ。note が無ければ GPE で分類: EC の GPE は電池の note が無ければ keyboard（電池だけなら spurious）、他の GPE は usb、GPE も無ければ spurious。spurious は入り直し、10 回で打ち切る（理由 SPURIOUS で戻る） | `sleep.c`（`sleep_wait_wake`・`sleep_reason`）、`acpi-event.c`（`drv_acpi_events_sleep_woken`）、`acpi-ec.c`（`drv_acpi_ec_gpe`）、`acpi-power.c`（weak の note） |
| SCI | 起動時に SCI を `kern_irq_set_wake` で wake に。電源ボタンの固定の事象で note | `acpi-kern.c`、`src/kern/irq.c` |

### UAPI（58554e8b）

- `include/uapi/system.h`: `KERN_SYSTEM_SLEEP_S0IDLE 2U`、`reserved` を `uint32_t wake` に、`KERN_SYSTEM_WAKE_NONE 0`〜`OTHER 8`。
- `src/drivers/generic/system-device.c`（`system_sleep`）: 権限が無ければ EPERM、知らない mode か入力の欄が 0 でなければ EINVAL、支えが無ければ
  EOPNOTSUPP（何も触らない）、sleep が進行中なら EBUSY。それ以外は成功で `result`・`resume_result`・`wake`（DEVICES と眠らなかった時は NONE）・`device` を返す。
- 事象: class `KERN_SYSTEM_EVENT_POWER`、action `CHANGE`、subject `sleep.begin`・`sleep.end`（detail `reason=<名前>`）・`sleep.failed`（detail `device=<名前>`）。
  docs の `power.sleep.*` はこの class と subject の組（文書と違う形が要るなら Q1 が文書を直す）。
- `userland/tests/sleepctl/main.c`: `s0idle` の command（`sleep result=R resume=S wake=W device=NAME`）、`-x` の拒否の試験に DEVICES の `wake` の欄と S0IDLE の欄の非 0 を追加。

## 確認（2026-10-05、P1 の worktree）

| 確認 | 結果 |
| --- | --- |
| amd64 の vmunix の build・link | rc=0、warning 0 |
| 他の platform の object の一覧 | pcat・pc98 に `sleep.c`・`freeze.c`、arm64・sparcv9・x68k に stub（HAL の stub の link は 910bc04c で確かめた） |
| `make ZEDBSD_CONFIG=plan/ws052/tests/config-amd64-sleep.mk BUILD=build/ws052-sleep build/ws052-sleep/bin/sleepctl` | rc=0、warning 0 |
| host: `plan/ws052/tests/run-host-sleep.sh` | pass |
| host: WS132 の `host-acpi-power`（acpi-power.c の weak の note） | 137 checks passed |
| `plan/tools/style-check.py`（base の file との比較） | 新しい指摘なし |
| QEMU・実機 | **未実施**（下の T1・UAT） |

## 未実施の確認

- T1（QEMU、`config-amd64-sleep.mk` の image、`p004-guest.sh` の roundtrip）: boot-test、`sleepctl s0idle` → `sleep refused errno=EOPNOTSUPP`（QEMU は FADT に S0 idle が無い）、
  `sleepctl devices` の往復（`wake=0`）、`sleepctl -x` → `refusals ok`。
- 5330 の UAT: `sleepctl s0idle` で SLP_S0 の residency（LPIT の counter）が増える、電源ボタン・蓋で起きる（`wake=1`・`2`）、counter と RTC の秒数の log が一致する（p006 の受け入れ）。

## 制限・残件

- 起床の理由は推定: EC の GPE を keyboard とみなす（電池の note だけなら spurious）、他の GPE は usb。TIMER は検出しない（RTC の alarm を wake に設定していない）。
- SCI の配送先を CPU 0 に固定していない（今の IOAPIC の設定に依る）。
- Wi-Fi・audio を止めるのは userland（networkd・audiod、p007）。radio が on なら p005 の driver が EBUSY で中止する。
- spurious の入り直しは 10 回、理由の待ちは 300 ms（実機の結果で調整）。
- kernel の調整役（`sleep.c`・`freeze.c`）に host 試験は無い。QEMU では S0IDLE が拒否で終わるので、入口・出口の流れは 5330 でしか通らない。

## 結果

（T1・UAT 待ち）

## T1-178 の結果（2026-10-05 Q1）

QEMU（main caf2f817）: boot PASS、`sleepctl -x` → `refusals ok`、devices の往復 PASS（`sleep result=0 resume=0 device=-`）、`sleepctl s0idle` は `EOPNOTSUPP`（zedBSD の値 21、`include/uapi/errno.h`）で拒否、device に触らない。QEMU の受け入れは満たす。残り: 5330 の UAT（SLP_S0 の residency の増加、電源ボタン wake=1・蓋 wake=2、counter と RTC の秒の一致）。cleared にするのは UAT の後。
