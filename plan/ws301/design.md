<!-- awesome-plan project=zedbsd record=ws301-design -->

# WS301 設計: CPU の性能と電力の枠組み（DVFS と P-core/E-core の相対の性能）

- 版: 第 2 版（2026-10-29 P5、q1666-i02。第 1 版 → [design-review.md](design-review.md) → 下の「第 2 版の改訂」）。第 1 版はp001（調査と設計）の前半: source と 5330 の firmware の表と公開の資料まで。5330 の Debian での CPUID・MSR・動的な SSDT の読み（p001 の後半、§6.0）は未実施。
- 基点: worktree `p5-design-b`（main 63386d8a4）。code は書いていない。HAL の差分の案は [proposed/hal-cpu-perf.diff](proposed/hal-cpu-perf.diff)（適用しない）。
- ユーザーの決定（[ws.md](ws.md) の原文から）: 2026-10-27 09:39（クリック）「UAPI は /dev/system、設定は domain ごとの min・max・desired・EPP、設定できるのは root だけ、HAL の差分の案を作る（承認は差分ごと）。対象はまず amd64（5330 の HWP）、arm64・riscv64 は枠だけ。fan は別の WS（WS303）。」

## 第 2 版への追記: UAPI の置き場所の変更（2026-10-29、Q1 の中継）

- 原文（ユーザー）: 「CPUのスピードは/dev/systemと言ってしまいましたが、/dev/thermalに変更してください。」
- 解釈（P5）: ws.md の 2026-10-27 09:39 の決定のうち「UAPI は /dev/system」はこの原文で置き換わる（他の部分: domain ごとの min・max・desired・EPP、root だけ、HAL の差分の案、amd64 から、は残る）。
  CPU の性能の ioctl（CPU の表・domain の表と設定）と事象は WS303 の `/dev/thermal`（group 'T'、`include/uapi/thermal.h`、事象の record は同じ形）に置き、番号は WS303 の p003 の案と一緒に決める。§5.2・§5.3・R12 の `/dev/system` の番号と事象の class 0x100 は取り下げ。
- policy（Q301-7）: WS303 の strategy（ユーザーの回答 Q303-11: performance・balanced・powersave・manual、manual は実行速度・騒音・細粒度パワーゲートの 0〜100、対応は driver）の受け手になる。WS301 は独自の policy の名前と起動の parameter を持たない。
  x86 の provider の対応の表（strategy と speed の軸 → EPP・min・max）は p004 で設計する。domain ごとの直の設定と strategy の関係は WS303 の Q303-17 で聞く。熱の上限の口は千分率に改める（WS303 の第 4 版 V6）。

## 第 2 版の改訂（2026-10-29 P5、[design-review.md](design-review.md) の blocker 1・major 17・minor 13 を反映。この節が下の本文に優先する）

- **R1（B1）enable の前に HWP の MSR を読まない、#GP で止まらない**: HAL の契約を直した（[proposed/hal-cpu-perf.diff](proposed/hal-cpu-perf.diff) 第 2 版、`git apply --check` を通る）。
  `hal_cpu_perf_capabilities` は enable の前は flags（CPUID の能力）だけを埋め levels は 0、`get`・`set` は enable の前は HAL_ERR_STATE。順は probe（CPUID）→ enable → capabilities の読み。
  HAL の中の MSR の読み書きは、processor が拒む時（#GP）に HAL_ERR_UNSUPPORTED で返る形を p003 で作る（今の `asm_read_msr`・`asm_write_msr` は `src/hal/amd64/asm.c:299〜322` の素の rdmsr・wrmsr で回復できない）。
  CPUID.01H:ECX[31]（hypervisor）が立つ時は provider を作らない。**`CONFIG_CPU_PERF` の既定は p006 の受け入れまで n**（Q301-4 の推しを B に替えた）。
- **R2（M10・M13）firmware の値と §6.0 の直し**: 動的な SSDT は既に git にある（`plan/ws049/tests/latitude5330/dynamic/ssdt16〜23.dat`、Cpu0Hwp・ApHwp・Cpu0Psd・ApPsd）。§2.1 の「今の dump に無い」は誤り。
  Cpu0Hwp の `GCPC` は Nominal Frequency を `NMFQ`、Nominal・Reference Performance を `SFBC`・`SFSC`・`RFBC`・`RFSC` から core の種類（`CORE` の bit mask）で分けて返し、これらの field は静的な SSDT2 にある（`ssdt2.dsl:306〜312`）。
  zedBSD の AML は `_OSC` を呼ばずにこれらの名前を読める → **単位と MHz の対応は firmware の値（Nominal Frequency / Nominal Performance、core の種類ごと）から取る**（Linux の散文の定数も測りも要らない。値の引用は「Value(s) referenced: the Latitude 5330's SSDT2 (CpuSsdt) and dynamic Cpu0Hwp」）。
  `_PSD` は CFGD の bit 25・22 が立つ時、CPU ごとに 1 つの HW_ALL の群（review の読み。実行時の CFGD の値は未確認）。
  §6.0 の直し: Debian は自分で PM_ENABLE を立てるので「今の値」は firmware の状態を示さない（記録はするが判断に使わない）。idle の E-core の周波数は clock の共有を示さない。module の共有の確かめは、同じ module の 2 つの E-core に違う HWP の要求で負荷を掛け APERF/MPERF を比べる（Debian で MSR を書く = 状態の変化、Q1 の許可が要る）か、firmware の `_PSD` を採る（推し: `_PSD` を採り、測りは p006 の確かめに回す）。
- **R3（M5）電圧の群**: ADL の datasheet 655258（`adl-ds-vol1.txt:10881`、Fable の写し）は全部の IA core を 1 つの VCCCORE の rail に置く。ユーザーの要求「電圧や周波数を共有する群」に合わせ、UAPI の CPU の表に **周波数の群（domain）と電圧の群（voltage_group）の両方**を出す。5330 は電圧の群が 1 つ（全 core）、周波数の群は core ごと。
- **R4（M6）core の種類から速さを決めない**: SDM Table 21-63 は core の種類の値から属性を推すなと書く。HAL は種類の符号（CPUID 1AH の値）だけを返し（`core_type`）、kernel は「速い・効率の良い」を性能の値（R2 の Nominal、HFI）から決める。
- **R5（M1・M2・M15）HFI**: HAL が「表が書かれていたら写してから hardware に再び書かせる」順の `hal_cpu_perf_feedback_copy` を持つ（bit 26 を先に消さない、IA32_PACKAGE_THERM_STATUS の他の bit を保つ mask は HAL の実装）。表の class の数と能力の数を capabilities で返す（kernel は Intel の表の行の形を HAL の答えから組む）。
  相対の性能の基準: **静的な値**（R2 の Nominal Performance × core の種類ごとの MHz/単位 → 最大の turbo の MHz 相当、または HWP の Highest × MHz/単位）で「最速 = 100」を決め、HFI は「今の使える性能」として別の欄で出す（熱で基準が入れ替わらないように）。HFI の全部が 0 の表・SMT の片方だけ 0 は「今は使えない」の flag。WS302 に効率の値と変化の事象も渡す。
- **R6（M3・M4）HWP の他の制御**: `hal_cpu_perf_enable` は論理 CPU ごとの要求が効く形（IA32_HWP_CTL がある時はその極性を含めて HAL が設定する）。PECI の上書き（Dell の EC の可能性）は HWP_STATUS の bit を `HAL_CPU_PERF_PLATFORM_LIMIT` で報告し、UAPI で「platform が制限中」を出す。
- **R7（M7・M8）HAL の差分と Phase**: module の comment を直した。topology と性能の制御は 1 つの差分にし（独立に承認できないため）、p002（枠組み）が topology、p004 が両方を要る。全 12 の HAL の directory に既定の答え（topology は CPU ごとに 1 core、性能の制御は UNSUPPORTED）を p003 で書く。
- **R8（M9）CPU ごとの thread をやめる**: `sched_set_cpu` は呼び手が無く、thread は飢えうる。要求は CPU ごとの「最新の要求」の箱（latest wins）に置き、`hal_cpu_notify()` で相手の CPU を起こし、`kernel_cpu_notify_handler()` の中で HAL を呼ぶ。ioctl は全部の相手の確かめを 1 秒待ち、来なければ ETIMEDOUT（要求は箱に残り後で効く、と UAPI の comment に書く）。
- **R9（M11）基準の周波数**: APERF/MPERF の基準は TSC の rate（SDM）。`reference_khz` は HAL が TSC の rate を返す（MSR_PLATFORM_INFO は使わない）。
- **R10（M12）電力の測り**: RAPL は HAL の口が要るので、この WS では使わない。p006 の電力の比べは電池の放電の速さ（ACPI の `_BST`、`acpi-power.c` が読む）で行う（電池で、AC を抜いて）。
- **R11（M14）回帰**: 既定 n（R1）。p006 に WS052 の S0 idle の回帰（`sleepctl devices` と `s0idle`）を足す。S0 idle の後の書き直しは「S3 の resume」ではなく、S0 idle の後に HWP の要求が保たれるかを p006 で確かめ、保たれなければ書き直す（FreeBSD の S3 の判断をそのまま当てない）。
- **R12（M16・M17）UAPI**: `system_cpu_perf_domain` は 240 byte（review が -m64・-m32 で測った）。policy の ioctl と boot の parameter はユーザーの決定（domain ごとの min・max・desired・EPP）を超えるので、入れるかを **Q301-7** で聞く。SET は全部 root だけ。WS303 は第 3 版で `/dev/system` を使わなくなったので、WS301 の ioctl は 22〜、事象の class は 0x100（mask 0x1ff）。
- **minor**: diff の hunk の数の誤りは直した（作り直し、`git apply --check` OK）。ws.md の LW と `_PSS` の fallback の扱いを design に揃える（`_PSS`＋PERF_CTL の fallback はこの WS では作らない: 5330 は HWP を持ち、`_PSS` は `_OSC` が要る）。design.md の 7 行目の「」はユーザーの click の答えの要約で原文ではない（ws.md の原文を見る）。
  Fable の使用は P5 の lane の承認（2026-10-29 ユーザー「…その管理下のサブエージェント(Fable 5.1 High)に依頼したいです。」、`plan/agents/P5/queue.md`）に依る。

### 第 2 版の Phase の表（§7 を置き換える。LW は ±50%）

| Phase | 内容 | 依存 | LW | 受け入れ |
| --- | --- | --- | --- | --- |
| p001 | 調査と設計、design-reviewer、ユーザーの判断（Q301-1〜7） | — | 残り 1 | 答え |
| p002 | 枠組み（domain・電圧の群・policy は Q301-7 次第・相対の性能の基準・熱の上限・最新の要求の箱と notify の道）と host 試験 | p001、HAL の topology の承認 | 5 | host 試験、build warning 0 |
| p003 | HAL: topology と性能の制御（#GP の回復、12 の directory の既定の答え） | 承認 | 5 | amd64 の build、QEMU で UNSUPPORTED |
| p004 | x86 の provider（HWP、firmware の Nominal の読み、`_PSD`、HFI の写し、PECI の flag） | p002、p003 | 5 | 5330 で表の読み |
| p005 | UAPI と `cpuperf` の道具 | p002、Q301-7 | 3 | ioctl の host 試験、QEMU（ssh） |
| p006 | 5330 の受け入れ（表・設定と APERF/MPERF・電池の放電・S0 idle の回帰） | p004、p005 | 4 | 1 本の script の PASS |
| p007・p008 | arm64・riscv64 の provider（後で、Q301-5） | p002 | 3・5 | 後で |
| p009 | 規約の全文と文書 | p006 | 2 | 規約、文書 |

5330 だけ（p001〜p006・p009）で約 25 LW。

### 第 2 版のユーザーに聞く点（§8 を置き換える・足す）

- Q301-1 HAL の差分（第 2 版、topology と性能の制御は 1 つ）: (A, 推し) 承認。(B) topology だけ先に（性能の制御は案 (b) の MSR の汎用の口で出し直す）。(C) 今は承認しない。
- Q301-2 相対の性能: (A, 推し) firmware の Nominal と MHz/単位による静的な基準で「最速 = 100」、HFI は「今の使える性能」として別に出す。(B) HFI だけ（動的、基準が入れ替わりうる）。(C) HWP の Highest の比だけ（hybrid で単位が違い不正確）。
- Q301-3 `_OSC`: (A, 推し) 呼ばない（R2 で firmware の値を `_OSC` なしで読める）。(B) processor の `_OSC` を呼ぶ。
- Q301-4 HWP の起動の時の enable: (A) 既定 y。(B, 推し) p006 の受け入れまで既定 n、受け入れ後に既定を決め直す。
- Q301-5 arm64・riscv64: (A, 推し) 枠だけ（p007・p008 は後）。(B) この WS に入れる。
- Q301-6 WS303 との連動: WS303 の第 3 版（ユーザーの回答）で strategy が WS301 の policy を受け手にする形になったので、問いは WS303 の Q303-11 に移した。
- **Q301-7（新）policy の名前（performance・balanced・powersave）と起動の parameter**: (A, 推し) 入れる（WS303 の strategy が受け手として使う、Q303-11）。(B) 入れない（domain ごとの min・max・desired・EPP だけ、ユーザーの決定の範囲）。

## 0. 要約

1. x86 は HWP（Intel）と CPPC の MSR（AMD）を、HAL の「今の CPU の性能の制御」の口（機構だけ、案 (a)）で使う。kernel は CPU ごとに束ねた thread からその口を呼ぶ（HAL に CPU 間の呼び出しを足さない）。
2. **zedBSD は `_OSC` を 1 つも呼ばない**ので、5330 の CPU の SSDT は `_PSS`・`_PSD`・`_CPC` の表を読み込まない（§2.1）。この WS の推しは、ACPI に頼らず CPUID と MSR だけで動く形（`_OSC` を呼ぶと PCIe・USB4 などの OS の制御の受け渡しも変わる。Q301-3）。
3. 相対の性能（最速 = 100）は、Intel の Hardware Feedback Interface（HFI）の性能の欄を推す（SDM が「論理 CPU の相対の性能」と定める、IPC の差も含む）。HFI の無い機械は HWP の Highest（同じ種類の core の中だけ比べられる）か測りで代える（Q301-2）。
4. 群（電圧・周波数を共有する core）: x86 は物理 core ごと（SMT の兄弟は 1 つの周波数、SDM §17.4.7）。E-core の 4 つの module が clock を共有するかは公開の資料に無く、§6.0 で測る。
5. UAPI は `/dev/system` の ioctl と事象の class（ユーザーの決定）。番号は WS303 の後（§5.3）。

## 1. 今の振る舞い（code の事実）

- CPU の周波数・P-state・HWP・CPPC の code は無い（ws.md の「今の事実」の grep、この版でも `grep -rn '0x770\|0x771\|0x774\|HWP' src/hal src/kern` が 0）。
- HAL の CPU の口は数・今の CPU・mask・notify・idle・park だけ（`include/hal/hal.h:113〜241`）。MSR の読み書きは HAL の中だけ（`src/hal/amd64/asm.c` の `asm_read_msr` ほか）。
- topology: MADT から LAPIC・x2APIC の ID だけを `struct amd64_acpi_cpu { uint32_t apic_id; }`（`src/hal/amd64/bsp-pcat/acpi.h:23〜25`）に取り、ACPI の processor UID は捨てる。
  `prekern_amd64_smp_init`（`src/hal/amd64/smp.c:43〜95`）は BSP を 0、他を MADT の順に密な番号にする。CPUID 0x1F/0x0B は console の lock の x2APIC の ID だけ（`src/hal/amd64/bsp-pcat/cons.c:580〜616`）。
- sched: thread は CPU に固定で、移すのは `sched_set_cpu`（走っていない thread だけ、`src/kern/sched.c:13`・`1658〜1668`）。CPU ごとに束ねた kernel の thread は作れる（kthread を作り start の前に `sched_set_cpu`）。
- ACPI: `_OSC` を呼ぶ code が無い（`grep -rn '_OSC' src/drivers src/hal` は rpi4-audio の別の語だけ）。`_PDC` も無い。
- `/dev/system` と事象: WS303 の design §1 と同じ（`system-device.c`、`include/uapi/system.h`、ioctl 1〜21）。熱の上限の口は無い。

## 2. 調べた事実

### 2.1 5330 の CPU の ACPI（BIOS 1.31.1、SSDT2 "CpuSsdt" の逆アセンブル `scratchpad/ws301-acpi/ssdt2.dsl`、git に入れない）

- `\_SB.PR00`〜`PR63` は ACPI0007（64 個、実在は 12、`dsdt.dsl:30494〜`）。
- SSDT2 の `SSDT` の package（`ssdt2.dsl:92〜120`）が動的な表の物理の番地と長さを持つ: CPU0IST・APIST（`_PSS`）、CPU0CST・APCST（`_CST`）、CPU0HWP・APHWP（`_CPC`）、CPU0PSD・APPSD（`_PSD`）、HWPLVT（無し、0x80000000）。
- これらは `\_SB.PRxx._OSC`（UUID 4077A616-290C-47BE-9EBD-D87058713953）か `_PDC` の中の `GCAP`（`ssdt2.dsl:390〜470`）が `Load` する。HWP の表（`_CPC`）は OSYS ≥ 2015 で、CFGD の bit 22 が立ち、
  かつ `\_SB.OSCP & 0x40`（platform の `\_SB._OSC`、UUID 0811B06E-…、`dsdt.dsl:28368〜` が CAP0 を OSCP に写す）の時だけ。→ **今の zedBSD では `_CPC`・`_PSD`・`_PSS` は namespace に無い**。
  動的な表の中身（`_PSD` の群の形、`_CPC` の Nominal Frequency）は今の dump に無く、Debian の `/sys/firmware/acpi/tables/dynamic/` で読む（§6.0）。
- `\_SB._OSC` の OSCP は他の bit（0x04 など、USB4 の native の bit の処理 `U4FN`）にも効く（`dsdt.dsl:28370〜28400`）。

### 2.2 公開の資料（Fable、2026-10-29、GPL の source は読まない条件。頁は `scratchpad/ws301-fable/`）

| 事実 | 出典 | 程度 |
| --- | --- | --- |
| CPUID.06H:EAX の bit: 7 HWP、8 通知、9 activity window、10 EPP、11 package の要求、15 Highest の変化、17 field ごとの valid、19 HFI、20 idle の兄弟の要求を無視、22 HWP_CTL、23 Thread Director | Intel SDM Vol 3B rev 093 §17.4.1、FreeBSD `specialreg.h`（BSD-2） | 確か |
| IA32_PM_ENABLE 0x770 は package、bit 0 は一度だけ書け reset まで消えない。IA32_HWP_CAPABILITIES 0x771（Highest・Guaranteed・Most Efficient・Lowest の 4 byte）、IA32_HWP_REQUEST 0x774（論理 CPU ごと: Min・Max・Desired・EPP・Activity Window・Package Control の bit 42・valid の bit 59〜63）、REQUEST_PKG 0x772、INTERRUPT 0x773、STATUS 0x777、ENERGY_PERF_BIAS 0x1B0 | SDM Vol 3B §17.4.2〜17.4.6、Vol 4 Table 2-2 | 確か |
| SMT の兄弟は 1 つの周波数で、hardware が両方の REQUEST を見て決める。周波数の範囲は package 全体か物理 core ごとのどちらか | SDM Vol 3B §17.4.7 | 確か |
| OS の推奨: Desired 0、Window 0、Min = Lowest、Max = Highest。Min = Max は HWP を迂回（推さない） | SDM Vol 3B §17.4.10 | 確か |
| HWP の単位と MHz の対応は「processor family specific」。hybrid の P-core と E-core で単位が違う（P-core 約 78.741 kHz/単位、E-core 100 MHz/単位）という値は Linux の commit の散文にしか無い | SDM §17.4.3（確か）、Linux の散文（GPL の project の散文、code は読んでいない） | 単位の値は推定 |
| CPUID.1AH: EAX[31:24] が core の種類（0x20 Atom、0x40 Core）、値の大小に意味は無い。CPUID.1FH の level（1 thread、2 core、3 module、4 tile、5 die） | SDM Vol 1 §21（Table 21-63・21-70） | 確か |
| HFI: CPUID.06H:EDX[7:0] 能力、[11:8] 表の page 数 − 1、[31:16] この CPU の行。MSR 0x17D0 PTR（package）、0x17D1 CONFIG。表は 16 byte の頭 + CPU ごとに 8 byte（byte 0 性能 0〜255、byte 1 効率）、0 は「ここで走らせない」、更新は IA32_PACKAGE_THERM_STATUS の bit 26 | SDM Vol 3B §17.6、Table 17-4〜17-9 | 確か |
| HFI の性能は「論理 CPU の相対の性能」で、熱・電力で動的に変わる | SDM Vol 3B §17.6.3 | 確か |
| ADL-P の E-core 4 つの module が clock を共有するか | Intel の資料に記述なし（datasheet 655258 は食い違う定型文） | 不明（§6.0 で測る） |
| AMD: CPUID Fn8000_0008_EBX[27]、MSR C001_02B0 CAP1（Highest・Nominal・LowNonLin・Lowest）、02B1 ENABLE（package）、02B3 REQUEST（EPP・Des・Min・Max、Intel と並びが違う） | AMD APM Vol 2（24593 r3.45）§17.6、FreeBSD `hwpstate_amd.c`（BSD） | 確か |
| FreeBSD の hwpstate_intel は hybrid では package の制御を切る（最後に書いた core の周波数に揃う恐れ）、相対の性能も MHz の換算もしない、resume で HWP を入れ直す | FreeBSD `hwpstate_intel.c`（BSD-2） | 確か（考え方だけを参考） |
| Pi 4・Pi 5: firmware の mailbox の clock id 3（ARM）、0x00038002 set、0x00030004/7 max/min、0x00030047 実測。4 core で 1 つの clock | Raspberry Pi の firmware の wiki、FreeBSD `bcm2835_mbox_prop.h`、QNX の BSP の文書 | 確か（1 つの clock は強い推定） |
| JH7110: 4 core で 1 つの clock（PLL0 → cpu_root → cpu_core）と 1 つの電源（AXP15060 の dcdc2）、OPP 375/500/750 MHz は 0.8 V、1500 MHz は 1.04 V | `jh7110.dtsi`・`jh7110-common.dtsi`（SPDX「GPL-2.0 OR MIT」を MIT で読んだ） | 確か |
| i5-1245U: 2 P + 8 E、12 thread、P-core 最大 4.4 GHz、E-core 最大 3.3 GHz、Thread Director あり | Intel の製品頁 226260 | 確か |
| ACPI 6.5 の `_CPC`（rev 3、21 の欄 + 2）・`_PSD`（0xFC SW_ALL・0xFD SW_ANY・0xFE HW_ALL）・`_PSS`・`_PPC` の節（8.4.5・8.4.6 の見込み） | uefi.org が bot の確認の頁で取れず、**記憶による推定、未確認**（ユーザーに PDF を置いてもらえるかを Q1 が聞く） | 未確認 |

## 3. 責務と境界

| 層 | 責務 | 持たない物 |
| --- | --- | --- |
| HAL（承認された差分だけ） | (A) CPU の topology（package・die・module・core・thread・core の種類・firmware の ID）を起動の時に集める。(B) 今の CPU の性能の制御の機構（能力の読み、enable、要求の書き、要求と計数の読み、HFI の表の番地） | 方針（どの値を書くか）、CPU 間の呼び出し |
| kernel の枠組み（`src/kern/cpuperf.c`、`include/kern/cpuperf.h`、`CONFIG_CPU_PERF`） | domain（群）の組み立て、CPU ごとの thread、policy の解決、相対の性能の計算、熱の上限の口、UAPI の中身、事象 | hardware の register |
| provider | x86: HAL の (B) を使う provider（kernel の中の小さな表）。arm64・riscv64: driver の provider（Pi の firmware、JH7110 の clock と PMIC）は後の Phase | — |
| UAPI（`include/uapi/system.h`） | CPU ごとの表、domain の表と設定、policy、事象の class | — |
| 道具（`userland/base/cpuperf/`） | 表示と設定 | — |

provider の口（kernel の中）: `struct kern_cpuperf_provider_ops { probe; set_request(domain, req); get_status(cpu, &st); }`。x86 の provider は CPU ごとの thread の上で HAL を呼ぶ。
arm64・riscv64 は「枠だけ」（ユーザーの決定）なので、この WS では provider の口と「provider の無い機械」の振る舞い（domain 1 つ、ENOTSUP）までを作る。

## 4. 設計

### 4.1 topology（HAL の差分 A）

- HAL は AP の起動の中で各 CPU の CPUID.1FH（無ければ 0BH）と 1AH を読み、package・die・module・core・thread と core の種類を記録する。MADT の ACPI processor UID も残す（`struct amd64_acpi_cpu` に足す。HAL の実装の中なので承認は要らない）。
- kernel は `hal_cpu_topology` で表を作る。DT の arm64・riscv64 は cluster を module に、reg を firmware の ID に（実装は後、今は「1 package・CPU ごとに core」の既定の答え）。
- 使い手: WS302（core の種類・相対の性能）、この WS の domain、`/dev/system` の CPU の表。

### 4.2 性能の制御の機構（HAL の差分 B）

- 案 (a)（推し、2026-10-27 の ws.md の推しのまま）: HAL は「今の CPU の」HWP・AMD CPPC を、能力の読み・enable・要求の書き・要求と APERF/MPERF の読み・HFI の表の番地の口で出す（diff の (B)）。方針は持たない（`hal_cpu_idle_suspend` と同じ分け方）。
- 案 (b): HAL に MSR の汎用の読み書きと「指定の CPU で関数を走らせる」口を足し、kernel が MSR の番号を持つ。kernel に x86 の register が入り、arch の境界が崩れるので推さない。
- 呼び方: kernel は CPU ごとに 1 本の kernel の thread（`cpuperf/N`、`sched_set_cpu` で束ねて start）を持ち、要求をその thread の queue に入れて起こす。HAL に CPU 間の呼び出しを足さない。
- HWP の enable は一度だけで reset まで消えない（§2.2）。`CONFIG_CPU_PERF=y` の kernel は起動の時に enable する。`n` の kernel は触らない（firmware の今の状態のまま）。enable の後に別の kernel（kexec は無い）や Debian で困る事は無い（reset で消える）。

### 4.3 domain（群）と要求の解き方

- x86: 物理 core（topology の core が同じ CPU）を 1 domain。SMT の兄弟は hardware が 2 つの要求を見て 1 つの周波数にする（§2.2）ので、kernel は domain の要求を兄弟の全部に同じに書く。
  E-core の module の共有が §6.0 の測りで確かめられたら、module を domain にする（決めるのは §6.0 の後、p004 の受け入れの前）。package の制御（bit 42・REQUEST_PKG）は使わない（hybrid で最後に書いた core に揃う恐れ、FreeBSD と同じ判断）。
- 設定（ユーザーの決定: domain ごとの min・max・desired・EPP）: 単位は provider の抽象の単位（HWP の 0〜255）。user が MHz で書く口は作らない（MHz は推定の換算で、表示だけ）。
- 範囲の検査: lowest ≤ min ≤ max ≤ highest、desired は 0 か min〜max。熱の上限（§4.6）があれば max を `min(max, 熱の上限)` に切る（user の値は残し、切った事を flag で示す）。
- 「step（設定できる値）」: HWP は lowest〜highest の連続の整数（step 1）。表で並べる provider（JH7110 の OPP、Pi の 100 MHz 刻みの想定）は最大 16 の step の表。UAPI はどちらも表せる（§5.2）。

### 4.4 policy

- `PERFORMANCE`（EPP 0、min = guaranteed）、`BALANCED`（EPP 0x80、min = lowest、既定）、`POWERSAVE`（EPP 0xC0、max = guaranteed）、`CUSTOM`（domain ごとの設定が policy と違う時）。値は SDM §17.4.10 の推奨と EPP の範囲から決めた初期値で、p006 の測りで直す。
- policy を変えると全部の domain の要求を作り直す。domain の個別の設定は policy の上に残り、`CUSTOM` を示す。policy の既定は `BALANCED`、起動の引数 `cpuperf=performance` などで変えられる（boot の parameter の口の形は p002 で既存の `kern_boot_parameter` に合わせる）。

### 4.5 相対の性能（最速 = 100）

- 推し: HFI がある時は、HFI の表の性能の byte（class 0）を全 CPU で比べ、`100 × perf(i) / max(perf)`（整数、四捨五入）。HFI は熱・電力で値を変えるので、表の更新（`hal_cpu_perf_feedback_ready`）を cpuperf の周期（1 秒）で見て読み直し、変わったら事象。
  HFI の表は kernel が物理に連続の page を取り（`kern_pmem_alloc`）、package ごとに 1 つ、HAL の `hal_cpu_perf_feedback` で渡す。割り込み（熱の LVT）は使わない（LVT は HAL の物で、口を足すと差分が増える。周期の読みで足りる）。
- HFI が無く core の種類が 1 つ（uniform）: HWP の Highest の比（同じ種類の中では単位が同じ）。
- HFI が無い hybrid: Highest の比は単位が違い使えない。測り（起動の後に各 domain で Min = Max = Highest の短い spin を走らせ、命令の数／時間）か、既定の比（P-core 100、E-core は測りが要る）。推しは「HFI 無しの hybrid は相対の性能を『不明』として返し、WS302 は core の種類だけを使う」（Q301-2）。
- 5330 は CPUID.06H:EAX[19]・[23] が立つ見込み（i5-1245U は Thread Director あり、§2.2）。§6.0 で確かめる。

### 4.6 熱の上限の口（WS303 との関係）

- kernel の中の口 `kern_cpuperf_limit(source, domain_or_all, max)` を作る（source ごとに 1 つ、最小を採る）。今の使い手は無い（WS303 の推しは OS の passive cooling を入れない、WS303 Q303-6）。
  口だけを作り host 試験で確かめる。WS303 の profile の事象を policy に連動させるかは Q301-6。

### 4.7 MHz の表示

- 推定: x86 は APERF/MPERF の比 × 基準の周波数（MSR_PLATFORM_INFO 0xCE の最大非 turbo の比 × 100 MHz、SDM Vol 4）で「今の実際の MHz」（1 秒の平均）。単位の MHz の換算（Highest → MHz）は、§6.0 で各 core の最大の実測 MHz を記録し、`MHz/単位` を domain ごとに測って持つ（Linux の散文の定数は使わない）。表示は「推定」の flag つき。

### 4.8 状態遷移（x86 の CPU ごと）

```
 ABSENT（HWP 無し）
 PROBED --enable--> ENABLED --policy の要求--> RUNNING
 RUNNING --Highest/Guaranteed の変化の事象（HWP_STATUS）--> RUNNING（能力を読み直し、domain の範囲を検め直す、事象）
 RUNNING --sleep（WS052 の S0 idle）--> SUSPENDED --resume--> 要求を書き直す（FreeBSD が resume の後の REQUEST の乱れを見た、§2.2）--> RUNNING
```

### 4.9 資源・並行性・割り込み

- CPU ごとの thread と queue、domain の表、HFI の page（package ごと）は kernel の寿命（CPU の hot-plug は無い）。
- lock: 設定（ioctl・policy・熱の上限）は `cpuperf_mutex` で直列。CPU ごとの thread への要求は spinlock の queue と waitq。読みの写し（CPU の状態）は spinlock（irqsave）。thread は mutex を持たない。
- 割り込みと DMA: 割り込みは使わない（HWP の通知と HFI は周期の読み）。HFI の表は hardware が書く記憶（DMA に近い）: page を 0 で埋め、止める時は ready を待ってから解放（SDM の手順、§2.2）。kernel の寿命の間は解放しない。

### 4.10 失敗と回復

| 失敗 | 振る舞い |
| --- | --- |
| HWP が無い（QEMU の多くの CPU model、古い CPU） | provider 無し: CPU ごとに 1 domain、相対の性能は uniform なら 100、設定は EOPNOTSUPP |
| enable が効かない・要求の書きが #GP | HAL が HAL_ERR を返し、その CPU を ABSENT、log 1 行 |
| HFI の表が 0（「走らせない」）の CPU | 相対の性能 0 を返し flag、WS302 に任せる |
| CPU ごとの thread が詰まる | 設定の ioctl は 1 秒で ETIMEDOUT、要求は queue に残る |
| resume の後の要求の乱れ | resume の hook で全部の要求を書き直す |

### 4.11 選ばなかった案

- `_OSC` を呼んで `_CPC`・`_PSD` を読む（ACPI を主にする）: platform の `_OSC` は PCIe・USB4 の制御にも効き（§2.1）、範囲が広がる。CPUID と MSR で足りる。後で `_CPC` が要る機械（AMD の一部、arm64 の ACPI）で別に（Q301-3）。
- 案 (b) の HAL（MSR の汎用の口）: §4.2。
- package の制御: §4.3。
- MHz を user が書く口: 換算が推定で、HWP は抽象の単位で動く。
- HWP の割り込みと HFI の割り込み: HAL の LVT の口が要る。周期の読みで足りる。

## 5. インタフェース（案）

### 5.1 kernel の中

```c
/* include/kern/cpuperf.h */
struct kern_cpuperf_cpu {
	struct hal_cpu_topology topology;
	uint32_t domain;
	uint32_t relative;		/* 0..100, KERN_CPUPERF_RELATIVE_UNKNOWN */
	uint8_t lowest, efficient, guaranteed, highest;
	uint32_t delivered_mhz;		/* last second, estimate */
	uint32_t flags;
};
int kern_cpuperf_cpu(hal_cpu_id_t cpu, struct kern_cpuperf_cpu *info);	/* WS302 が使う */
int kern_cpuperf_domain_set(uint32_t domain, const struct kern_cpuperf_setting *setting);
int kern_cpuperf_policy_set(uint32_t policy);
int kern_cpuperf_limit(uint32_t source, uint32_t domain, uint8_t maximum);	/* domain KERN_CPUPERF_ALL */
```

### 5.2 UAPI（`/dev/system`、固定の幅）

```c
#define KERN_SYSTEM_EVENT_CPU_PERF	0x00000200U	/* policy, a domain's setting, the capabilities or the relative performance changed */

struct system_cpu_perf_info {		/* KERN_SYSTEM_GET_CPU_PERF, _IOWR, in: cpu */
	uint32_t cpu;
	uint32_t count;
	uint32_t package, die, module, core, thread, core_kind;
	uint32_t domain;
	uint32_t relative;		/* 0..100, 0xffffffff unknown */
	uint32_t lowest, efficient, guaranteed, highest;
	uint32_t delivered_mhz;
	uint32_t flags;			/* HAS_CONTROL, ESTIMATED_MHZ, LIMITED */
	uint32_t reserved[4];
};	/* 80 bytes */

struct system_cpu_perf_domain {		/* KERN_SYSTEM_GET_CPU_PERF_DOMAIN, _IOWR, in: domain */
	uint32_t domain;
	uint32_t count;
	uint64_t cpus[8];		/* 512 CPUs as bits */
	uint32_t kind;			/* RANGE or STEPS */
	uint32_t step_count;
	uint32_t steps[16];		/* abstract units (RANGE: steps[0] lowest, steps[1] highest) */
	uint32_t step_mhz[16];		/* estimate, 0 unknown */
	uint32_t minimum, maximum, desired, preference;	/* the setting */
	uint32_t limit;			/* the thermal ceiling, 0xffffffff none */
	uint32_t flags;
	uint32_t reserved[2];
};	/* 232 bytes */

struct system_cpu_perf_setting {	/* KERN_SYSTEM_SET_CPU_PERF_DOMAIN, _IOW, root */
	uint32_t domain;		/* or 0xffffffff for every domain */
	uint32_t minimum, maximum, desired, preference;
	uint32_t reserved[3];
};	/* 32 bytes */

struct system_cpu_perf_policy {		/* KERN_SYSTEM_GET/SET_CPU_PERF_POLICY */
	uint32_t policy;		/* PERFORMANCE, BALANCED, POWERSAVE, CUSTOM (read only) */
	uint32_t reserved[3];
};
```

### 5.3 番号

WS303 が 22〜24 を使う案。WS301 は 25〜29（GET_CPU_PERF、GET_CPU_PERF_DOMAIN、SET_CPU_PERF_DOMAIN、GET・SET_CPU_PERF_POLICY）。実装の順で Q1 が割る。

### 5.4 HAL の差分

[proposed/hal-cpu-perf.diff](proposed/hal-cpu-perf.diff): (A) `hal_cpu_topology`、(B) `hal_cpu_perf_capabilities`・`enable`・`set`・`get`・`feedback`・`feedback_ready`。A と B は別々に承認できる。
全 arch に入る宣言（arm64・riscv64・他は A を既定の答え、B を HAL_ERR_UNSUPPORTED で実装、承認の後）。

## 6. 試験の計画

### 6.0 p001 の後半: 5330 の Debian の読み（Q1 の割当、読みだけ）

`cpuid -r`（全 CPU の 0x6・0x1A・0x1F）、`sudo rdmsr -a 0x770 0x771 0x774 0x772 0x1b0 0xce`（**msr の module を読み込む = Debian の状態の変化**。許可を Q1 に聞く）、
`/sys/firmware/acpi/tables/dynamic/` の写し（`_CPC`・`_PSD`）、`/sys/devices/system/cpu/cpu*/cpufreq/`・`acpi_cppc/`・`intel_pstate/status`、`lscpu -e`。
E-core の module の共有: 1 つの E-core で spin、同じ module の他の E-core と別の module の `scaling_cur_freq`（または turbostat）を比べる（負荷を掛けるが設定は変えない）。

### 6.1 host 試験

domain の組み立て（5330 の topology の表、SMT・module の有無）、範囲の検査、policy の解決、熱の上限の最小、相対の性能（HFI の表の模擬、uniform、hybrid で HFI 無し）、UAPI の layout（`_Static_assert`）。

### 6.2 QEMU

q35 の既定の CPU（HWP 無し）: 表が ENOTSUP・1 domain/CPU、`boot-test.sh` の回帰。`-cpu host` の KVM は HWP を出さない見込み（確かめる）。

### 6.3 5330（素の起動、1 本の script）

表の読みが §6.0 の Debian の値と合う（12 CPU、core の種類、Highest 等）。policy と domain の min・max で APERF/MPERF の MHz が従う（負荷の spin、P-core と E-core ごと）。powersave で idle の package の電力（RAPL の energy の読み、zedBSD に無ければ p006 で足すか Debian との比）が下がる。

## 7. Phase と LW（±50%）

| Phase | 内容 | 依存 | LW | 受け入れ |
| --- | --- | --- | --- | --- |
| p001 | 調査と設計（この文書）＋ §6.0 の Debian の読み、design-reviewer、ユーザーの判断 | — | 残り 2 | §6.0 の記録、Q301-1〜6 の答え |
| p002 | 枠組み（domain・policy・相対の性能・熱の上限・provider の口・CPU ごとの thread）と host 試験 | p001 | 5 | §6.1 PASS、全 platform の build warning 0 |
| p003 | HAL: A（topology）と B（HWP・AMD CPPC の機構）、承認された差分だけ | 承認 | 4 | amd64 の build、QEMU で HWP 無しの答え |
| p004 | x86 の provider（HWP、HFI の表、domain の確定、resume の書き直し） | p002、p003 | 5 | 5330 で §6.3 の表 |
| p005 | UAPI と `cpuperf` の道具 | p002、UAPI の決定 | 3 | ioctl の host 試験、QEMU |
| p006 | 5330 の受け入れの script | p004、p005 | 3 | §6.3 |
| p007 | arm64（Pi 4・Pi 5 の mailbox の ARM clock） | p002、Q301-5 | 3 | 後で |
| p008 | riscv64（JH7110 の OPP、PMIC の dcdc2） | p002、Q301-5 | 5 | 後で |
| p009 | 規約の全文と docs（power-management.md の範囲の変更、ユーザーの review） | p006 | 2 | 規約、文書 |

5330 だけ（p001〜p006・p009）で約 24 LW。p007・p008 を入れて約 32 LW。

## 8. ユーザーに決めてほしい点

- **Q301-1 HAL の差分**（[proposed/hal-cpu-perf.diff](proposed/hal-cpu-perf.diff)）: (A, 推し) A（topology）と B（今の CPU の機構）の両方を承認。(B) A だけ（B は案 (b) の MSR の汎用の口に変えて出し直す）。(C) 今は承認しない（p002 の host の部分だけ進める）。
- **Q301-2 相対の性能の源**: (A, 推し) HFI。HFI の無い hybrid は「不明」を返す。(B) HFI が無ければ起動の後の測り（数秒の spin を各 domain で）。(C) HWP の Highest の比（hybrid では単位が違うので P/E を正しく比べられない）。
- **Q301-3 ACPI の `_OSC`**: (A, 推し) この WS では呼ばない（CPUID・MSR で足りる）。(B) processor の `_OSC` だけ呼んで `_CPC`・`_PSD` を読む（platform の `_OSC` は呼ばない。`_CPC` の表は platform の `_OSC` の bit も要るので読めない見込み、§2.1）。(C) platform と processor の `_OSC` を呼ぶ（PCIe・USB4 の制御の受け渡しも変わるので別の WS で扱う）。
- **Q301-4 HWP を起動の時に enable するか**: (A, 推し) `CONFIG_CPU_PERF=y`（amd64 の既定 y）の kernel は起動の時に enable、policy は BALANCED。(B) 既定 n、試す時だけ y。
- **Q301-5 arm64・riscv64**: (A, 推し、ユーザーの「枠だけ」の解釈) この WS は provider の口と既定の答えまで、p007・p008 は後（別の Queue か別の WS）。(B) p007・p008 までこの WS に入れる。
- **Q301-6 WS303 の profile との連動**: (A, 推し) しない（それぞれ独立、WS303 は事象を出すまで）。(B) WS303 の profile（quiet・cool）で policy を POWERSAVE に寄せる。
- §6.0 の Debian の読みで msr の module を読み込んでよいか（Q1 経由）。
- ACPI 6.5 の PDF を手で置いてもらえるか（Q1 がユーザーに聞く。置かれたら §2.2 の未確認の節を確かめる）。

## 9. 出典と引用

- 値（MSR の番号・bit・CPUID の bit・HFI の表の形）は Intel SDM rev 093・AMD APM 24593 r3.45 から、block ごとに「Value(s) referenced: Intel SDM Vol 3B rev 093 §17.4.2 (IA32_HWP_CAPABILITIES)」の形で書く。手順（enable の順、HFI の止め方）は「Procedure referenced:」。
- FreeBSD（BSD-2）は考え方の参考だけ（hybrid の package の制御を切る、resume の書き直し）。code・名前・構造は写さない。
- DTS（GPL-2.0 OR MIT）は MIT として値（OPP）だけを引用。Linux の散文の値（P-core の 78.741 kHz）は使わず測る。GPL の source は読んでいない。
- ACPI 6.5 の節番号は記憶による推定、未確認。

## 10. 未確認

1. 5330 の CPUID.06H（HWP・HFI・TD の bit）、各 CPU の 0x771 の値、PM_ENABLE の今の値（firmware か Linux が enable 済みか）（§6.0）。
2. E-core の module の clock の共有（§6.0）。
3. 動的な SSDT の `_PSD`・`_CPC` の中身（§6.0、この設計は使わないが表示の比べに使う）。
4. QEMU の KVM で HWP の CPUID が見えるか。
5. HFI の表の更新の頻度と、周期の読み（1 秒）で足りるか。
6. ACPI 6.5 の節番号（§2.2）。
