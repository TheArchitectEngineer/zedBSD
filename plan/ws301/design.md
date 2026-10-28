<!-- awesome-plan project=zedbsd record=ws301-design -->

# WS301 設計: CPU の性能と電力の枠組み（DVFS と P-core/E-core の相対の性能）

- 版: **第 3 版の改訂 1**（2026-10-29 P5、q1666-i03。Q301-2・Q301-8 の答えで、性能の制御を HAL でなく x86 の driver に移した。§0・§3・§4.1・§4.2・§4.7・§4.9・§5・§7・§9 を直した）。第 1 版 → [design-review.md](design-review.md) → 第 2 版（改訂の節の重ね）→ ユーザーの回答（Q301-3・4・5・7）と 5330 の Debian の読み（§2.3）→ この版。
  第 2 版までの「改訂の節を本文の上に重ねる」形をやめ、**この版は一つの本文**にまとめた（前の版は git の履歴）。第 2 版の R1〜R12 は本文に入れ、どこに入ったかを §11 に残す。第 3 版の review は [design-v3-review.md](design-v3-review.md)。
- 基点: worktree `p5-design-c`（main d94ceac00）。code は書いていない。**推しは HAL を変えない**（§4.2）。前の HAL の差分の案 [proposed/hal-cpu-perf.diff](proposed/hal-cpu-perf.diff)（第 3 版、適用しない）は選ばなかった案 C として残す。ユーザー向けの説明は [hal-review.md](hal-review.md)。

## 0. ユーザーの決定（原文）と解釈

| 日付 | 原文 | 解釈（P5） |
| --- | --- | --- |
| 2026-10-27 | ws.md の 5 つの要求（ステップ・群・設定・取得・最速 = 100 の相対の性能） | 到達目標。ws.md の「単一の到達目標」 |
| 2026-10-27 09:39（クリック、ws.md） | 「UAPI は /dev/system、設定は domain ごとの min・max・desired・EPP、設定できるのは root だけ、HAL の差分の案を作る（承認は差分ごと）。対象はまず amd64（5330 の HWP）、arm64・riscv64 は枠だけ。fan は別の WS（WS303）。」（click の要約） | UAPI の置き場所は下の 2026-10-29 で置き換わる。他は残る |
| 2026-10-29（Q1 の中継） | 「CPUのスピードは/dev/systemと言ってしまいましたが、/dev/thermalに変更してください。」 | UAPI は WS303 の `/dev/thermal`（group 'T'、`include/uapi/thermal.h`）。`/dev/system` の ioctl と事象の class は作らない |
| 2026-10-29 Q303-11（WS303 の問い） | 「ストラテジーはかなり抽象化されたものとして、トータルの「performance or balanced or power save or manual」と、マニュアルなら実行速度、騒音、細粒度パワーゲートの3項目を0-100で設定可能に。具体的なプラットフォームへのマッピングはドライバが決める。」 | WS301 は strategy の受け手。strategy から HWP の要求への対応は WS301 の x86 の provider が決める（§4.4） |
| 2026-10-29 Q301-3（Q1 が用語から聞き直した） | 「最終的には呼ぶが、別なWSにして、システム全体の挙動が問題ないかを厳重にチェックする。今は呼ばない。」 | WS301 は `_OSC` を呼ばない。Q1 が WS310（`_OSC` を呼び全体の挙動を確かめる、WS301 の後）を作った |
| 2026-10-29 Q301-4 | 「試験が通るまで無効 (推し)」 | `CONFIG_CPU_PERF` の既定は n。p007 の受け入れが通ったら既定を決め直す（別の判断） |
| 2026-10-29 Q301-5 | 「枠だけ、実装は後 (推し)」 | arm64・riscv64 は provider の口と「provider の無い」答えまで。Pi・VF2 の provider は後の WS |
| 2026-10-29 Q301-7 | 「持たせない (推し)」（Q1 の注: 利用者が選ぶのは WS303 の strategy だけ、CPU ごとの直の値は root の診断用だけ） | WS301 は自分の policy の名前・起動の parameter を持たない。domain ごとの min・max・desired・EPP の直の設定は root の診断の口として残す（2026-10-27 の決定の「設定」を満たす）。strategy との関係は §4.5 |
| 2026-10-29 Q301-2（Q1 が firmware の値の誤りを伝えて聞き直した。前の答え「firmware の Nominal (推し)」を置き換える） | 「とりあえず推しにしますが、あとでマッピングを変えられるインタフェースにします。」 | 既定の基準は最大の周波数の比（5330 は E = 75）。基準（相対の性能への写し方）は後で替えられる口にする: provider が出せる基準の一覧を持ち、root が選ぶ（§4.7） |
| 2026-10-29 Q301-8 | 「P-stateを操作しますが、HALではなくドライバでできるはずです。」 | HWP の無い CPU も P-state を操作する（§4.9）。HAL の差分を増やさず、x86 の driver が CPUID・MSR を自分で扱う。同じ理由で、HWP・HFI・topology も driver で扱い、HAL の追加をやめる案を推しにした（§4.2） |

未決: Q301-1（HAL を変えない推しでよいか、[hal-review.md](hal-review.md)）。§9 に推しつきで置く。

## 1. 今の振る舞い（code の事実）

- CPU の周波数・P-state・HWP・CPPC の code は無い（`grep -rn '0x770\|0x771\|0x774\|HWP' src/hal src/kern` が 0）。
- HAL の CPU の口は数・今の CPU・mask・notify（`hal_cpu_notify`、`include/hal/hal.h:170`、受け手は `kernel_cpu_notify_handler`、1476）・idle・park だけ。MSR の読み書きは HAL の中の素の `rdmsr`・`wrmsr`（`src/hal/amd64/asm.c:299〜322`、#GP を回復しない）。
- topology: MADT から LAPIC・x2APIC の ID だけを `struct amd64_acpi_cpu { uint32_t apic_id; }`（`src/hal/amd64/bsp-pcat/acpi.h:23〜25`）に取り、ACPI の processor UID は捨てる。
  `prekern_amd64_smp_init`（`src/hal/amd64/smp.c:43〜95`）は BSP を 0、他を MADT の順に密な番号にする。CPUID 1FH/0BH は console の lock の x2APIC の ID だけ（`bsp-pcat/cons.c:580〜616`）。
- sched: thread は CPU に固定（`sched_set_cpu` は走っていない thread だけ、`src/kern/sched.c:1658〜1668`、呼び手は 0）。
- ACPI: `_OSC`・`_PDC` を呼ぶ code は無い。そのため 5330 の CPU の SSDT は `_PSS`・`_PSD`・`_CPC` を `Load` しない（§2.1）。
- 熱: `hw.thermal`（`src/drivers/acpi/acpi-thermal.c`）。熱の上限の口は無い。`/dev/thermal` は WS303 p003 が作る（今は無い）。

## 2. 調べた事実

### 2.1 5330 の firmware（BIOS 1.31.1）

- `\_SB.PR00`〜`PR63` は ACPI0007（64 個、実在は 12、`dsdt.dsl:30494〜`）。SSDT2 "CpuSsdt" の `SSDT` の package が動的な表（`_PSS`・`_CST`・`_CPC`・`_PSD`）の番地を持ち、`\_SB.PRxx._OSC` か `_PDC` の中の `GCAP` が `Load` する（`scratchpad/ws301-acpi/ssdt2.dsl:92〜120・390〜470`）。
  `_CPC` は OSYS ≥ 2015・CFGD の bit 22・`\_SB.OSCP & 0x40`（platform の `_OSC`）の時だけ。→ 今の zedBSD の namespace には無い。
- 動的な表は git にある（`plan/ws049/tests/latitude5330/dynamic/ssdt16〜23.dat`、Cpu0Hwp・ApHwp・Cpu0Psd・ApPsd）。Cpu0Hwp の `GCPC` は Nominal Frequency を `NMFQ`、Nominal・Reference Performance を `SFBC`・`SFSC`・`RFBC`・`RFSC` から core の種類で分けて返す（field は SSDT2、`ssdt2.dsl:306〜312`）。
  `_PSD` は CFGD の bit 25・22 が立つ時 CPU ごとに 1 つの HW_ALL の群（第 1 版の review の読み）。
- **§2.3 の読みで、`NMFQ` は P-core と E-core で同じ 1600 MHz を返す**と分かった（Linux の `acpi_cppc/nominal_freq` が両方 1600）。E-core は nominal_perf 12 なので 1600/12 = 133 MHz/単位になり、実測（100 MHz/単位）と合わない。
  → 第 2 版 R2 の「単位と MHz の対応は firmware の Nominal から取る」は**取り下げる**（§4.7）。`_OSC` を呼ばない（Q301-3）ので、どのみち zedBSD はこの値を使わない。

### 2.2 公開の資料（Fable の調査の写し、2026-10-29、`scratchpad/ws301-fable/`、GPL の source は読まない条件）

| 事実 | 出典 | 程度 |
| --- | --- | --- |
| CPUID.06H:EAX の bit: 7 HWP、8 通知、9 activity window、10 EPP、11 package の要求、15 Highest の変化、16 PECI の上書き、19 HFI、20 idle の兄弟の要求を無視、22 HWP_CTL、23 Thread Director | Intel SDM Vol 3B rev 093 §17.4.1 | 確か |
| IA32_PM_ENABLE 0x770 は package、bit 0 は一度だけ書け reset まで消えない。他の HWP の MSR は enable の前に触ると #GP。0x771 CAPABILITIES（Highest・Guaranteed・Most Efficient・Lowest）、0x774 REQUEST（Min・Max・Desired・EPP・Window・bit 42 package・bit 59〜63 valid）、0x772 REQUEST_PKG、0x773 INTERRUPT、0x777 STATUS、0x1B0 EPB | SDM Vol 3B §17.4.2〜17.4.6 | 確か |
| IA32_HWP_CTL 0x776 bit 0 = 0 で REQUEST の bit 42 = 0 なら REQUEST_PKG を無視し論理 CPU の REQUEST だけを使う | SDM Vol 3B §17.4.4.4、Table 17-2（`sdm-3b.txt:748〜788`） | 確か |
| SMT の兄弟は 1 つの周波数で、hardware が両方の REQUEST を見て決める | SDM Vol 3B §17.4.7 | 確か |
| OS の推奨: Desired 0、Window 0、Min = Lowest、Max = Highest | SDM Vol 3B §17.4.10 | 確か |
| HWP の単位と MHz の対応は「processor family specific」 | SDM §17.4.3 | 確か（値は §2.3 で測った） |
| CPUID.1AH EAX[31:24] は core の種類（0x20 Atom、0x40 Core）、値から属性を推すな。CPUID.1FH の level（1 thread、2 core、3 module、4 tile、5 die） | SDM Vol 1 §21（Table 21-63・21-70） | 確か |
| HFI: CPUID.06H:EDX[7:0] 能力、[11:8] page 数 − 1、[31:16] この CPU の行。MSR 0x17D0 PTR、0x17D1 CONFIG。表は頭 + 行ごとに class × 能力の byte、0 は「ここで走らせない」、更新の印は IA32_PACKAGE_THERM_STATUS の bit 26 | SDM Vol 3B §17.6 | 確か |
| ADL-P の IA core は全部 1 本の VCCCORE | datasheet 655258（`adl-ds-vol1.txt:10881`） | 確か |
| AMD: CPUID Fn8000_0008_EBX[27]、MSR C001_02B0 CAP1、02B1 ENABLE、02B3 REQUEST | AMD APM Vol 2（24593 r3.45）§17.6 | 確か |
| FreeBSD の hwpstate_intel は hybrid で package の制御を切る、resume で HWP を入れ直す | FreeBSD `hwpstate_intel.c`（BSD-2） | 確か（考え方だけ） |
| ACPI 6.5 の `_CPC`・`_PSD`・`_PSS` の節番号 | 記憶による推定 | 未確認（`_OSC` を呼ばないこの WS では使わない） |

### 2.3 5330 の Debian の読み（2026-10-29、P5、Linux 6.19.13、読みだけ）

条件: `uname` で Debian（chaos）を確かめ、centris の `/tmp/i915-hw.lock` を取った。CPUID は python の実行可能な page の小さな stub で各 CPU に束ねて読んだ（module を足さない）。MSR は既に読み込まれていた msr の module の `/dev/cpu/N/msr` を pread で読んだだけ（書きなし）。
負荷は CPU を 1 つずつ 1 秒 spin しただけ。再起動・package の導入・設定の変更はしていない。記録は `scratchpad/ws301-5330/`（`read.txt`・`spin.txt`・`sysfs.txt`、git に入れない）。

| 項目 | 値 | 読み |
| --- | --- | --- |
| CPUID.01H:ECX[31] | 0（全 CPU） | hypervisor 無し |
| CPUID.06H:EAX | 0x00DF8FF7（全 CPU） | HWP・通知・window・EPP・package の要求・Highest の変化・PECI の上書き・HFI・idle の兄弟の無視・HWP_CTL・Thread Director が有る |
| CPUID.06H:ECX | 0x409 | APERF/MPERF、EPB、Thread Director の class の数 4（[15:8]） |
| CPUID.06H:EDX | [7:0] = 3、[11:8] = 0、[31:16] = 0,0,1,1,2,2,2,2,3,3,3,3（CPU 0〜11） | HFI は性能と効率、1 page、**行は P-core の SMT の兄弟で 1 つ、E-core の 4 つ組（CPU 4〜7、8〜11）で 1 つ** |
| CPUID.1AH:EAX | CPU 0〜3 = 0x40000001、4〜11 = 0x20000001 | P-core 2（SMT 4 thread）、E-core 8 |
| CPUID.16H | P-core: 2500・4400・100、E-core: 2500・3300・100（MHz） | 最大の周波数が core の種類ごとに違う値で出る |
| CPUID.1FH | level 1（SMT、shift 1）、level 2（core、shift 6）、sub-leaf 2 は無効 | **module の level は無い**。x2APIC ID は P 0・1・8・9、E 0x10〜0x1E の偶数 |
| Linux の topology | `cluster_cpus` = 0-1、2-3、4-7、8-11 | Linux は module を L2 の共有（CPUID.04H）から作る。zedBSD も CPUID.04H を使う（§4.1、04H の値そのものは未読） |
| 0x770 PM_ENABLE | 1（全 CPU） | Linux が enable した（dmesg「intel_pstate: HWP enabled」）。firmware の状態は分からない |
| 0x771 CAPABILITIES | P: 0x01141538（Highest 56・Guaranteed 21・Efficient 20・Lowest 1）、E: 0x010F0C21（33・12・15・1） | |
| 0x774 REQUEST | P: 0x3806（Min 6・Max 56・Desired 0・EPP 0）、E: 0x2104（4・33・0・0） | Linux の今の要求（EPP 0 は platform_profile performance と EPP「performance」） |
| 0x772 REQUEST_PKG／0x776 HWP_CTL | 0x8000FF01／0 | HWP_CTL = 0・bit 42 = 0 → 論理 CPU の要求だけが効く |
| 0x777 STATUS／0x1B0 EPB | 0／0 | |
| 0xCE PLATFORM_INFO | 0x0804043DF0811900 | 最大の非 turbo 25（2500 MHz）、最小 4 |
| MPERF の rate | 2494 MHz | TSC（dmesg 2496 MHz）と同じ。基準は TSC（第 2 版 R9 を確かめた） |
| 1 秒の spin の実測 | P-core 4388 MHz、E-core 3292 MHz（1 つだけ spin、他は idle） | **P: 4400/56 = 78.6 MHz/単位、E: 3300/33 = 100 MHz/単位**。CPUID.16H の最大 ÷ Highest と実測が合う |
| `acpi_cppc`（Linux が `_OSC` の後に読んだ `_CPC`） | P: highest 56・nominal_perf 20・nominal_freq 1600・reference_perf 31、E: 33・12・1600・25 | nominal_freq は両方 1600 で E-core に当たらない（§2.1）。reference_perf × MHz/単位 ≒ 2500（P 31 × 80.6、E 25 × 100）は合う |
| HFI の MSR 0x17D0・0x17D1 | 0・0 | Linux 6.19 はこの機械で HFI を有効にしていない。**HFI の表の値は読めていない**（有効にするのは MSR の書きで、許されていない） |
| 0x1A2 TEMPERATURE_TARGET | 0x80640000 | **TjMax 100 °C**（WS303 にも効く: ACPI の TCPU の `_CRT` 110 °C より下で CPU が自分で絞る） |
| 0x610 PKG_POWER_LIMIT／0x614 | PL1 25 W・PL2 55 W／TDP 15 W（単位 1/8 W） | Dell の thermal mode が performance の時の値（WS303 p004 で他の mode と比べる候補） |

E-core の 4 つ組が clock を共有するかは、書かない読みでは決められない（同じ要求の下では共有しても同じ周波数になる）。HFI の行と L2 が 4 つ組で 1 つなのは共有を示唆するが、証拠ではない（未確認）。

## 3. 責務と境界

| 層 | 責務 | 持たない物 |
| --- | --- | --- |
| HAL | **変えない**（推し、Q301-1）。既存の `hal_cpu_notify`（`include/hal/hal.h:170`）で相手の CPU を起こし、kernel の `kernel_cpu_notify_handler`（`src/kern/clock.c:183`）がその CPU の上で枠組みを呼ぶ | CPU の性能の制御、topology の CPUID |
| kernel の枠組み（`src/kern/cpuperf.c`、`include/kern/cpuperf.h`、`CONFIG_CPU_PERF`、既定 n） | domain（周波数の群）と電圧の群の組み立て、相対の性能と基準の選択（§4.7）、strategy の受け手（§4.4）、root の直の設定（§4.5）、熱の上限（§4.6）、CPU ごとの最新の要求の箱と notify の道、provider の口、`/dev/thermal` の ioctl の中身、事象 | hardware の register、arch の命令 |
| x86 の CPU の性能の driver（`src/drivers/platform/pcat/cpuperf-x86.c` ほか、pcat の platform の build だけ。amd64 で試し、i386 は build だけ） | 各 CPU の上で CPUID・RDMSR・WRMSR を自分で実行する（inline の `__asm__`、前例は `pcat-ide.c:218` の port の命令・`rpi5-serror.c:35` の system register）。topology、HWP（Intel）、legacy の P-state（Intel の EIST、AMD の HwPstate）、HFI の表、MADT（`drv_acpi` の表の貸し）と `_PSS`（あれば）の読み。安全の関門（§4.2） | 方針の表（strategy → 要求は provider の表として持つが、strategy の意味は WS303） |
| UAPI（`include/uapi/thermal.h`、WS303 p003 が作る file に WS301 の struct を足す） | CPU の表、domain の表、直の設定、相対の性能の基準の選択、事象の record の CPU の種類 | — |
| 道具（`userland/base/cpuperf/` か WS303 の `/sbin/thermal` の sub command、p005 で決める） | 表示と root の直の設定・基準の選択 | strategy の選択（`/sbin/thermal` の物） |
| WS303 | `/dev/thermal` の device、strategy の選択と受け手の登録の口、passive の判定 | CPU の register |

## 4. 設計

### 4.1 topology（x86 の driver）

- driver は各 CPU の上で（§4.2 の notify の道、起動の後に 1 回）CPUID.1FH（無ければ 0BH）・04H・1AH を読み、x2APIC ID・package・die・module・core・thread・core の種類を provider の表に置く。package・die・core・thread は 1FH の level から。
  **module は 1FH に module の level があればそれ、無ければ L2 を共有する CPU の組（04H の L2 の「共有する論理 CPU の ID の数」と x2APIC ID）**（5330 は 1FH に module が無い、§2.3）。L2 の情報も無ければ module = core。
- firmware の CPU の番号（ACPI processor UID）は MADT（`drv_acpi` の表の貸し、`include/drivers/acpi/acpi.h:250`）の x2APIC ID の行から取る（`_PSS`・`_PSD` を CPU の object に結ぶ時だけ要る）。
- 5330 で期待する表: CPU 0・1 が core 0、2・3 が core 1（P、module = core）、4〜7 が module A、8〜11 が module B（E、core ごとに 1 thread）。
- kernel の枠組みは provider の `topology(cpu, &info)` で表を作る。provider の無い機械（arm64・riscv64、x86 の hypervisor の下、`CONFIG_CPU_PERF=n`）は既定の答え（CPU ごとに 1 core、core の種類は UNKNOWN）。使い手: WS302、この WS の domain、UAPI の CPU の表。
  WS302 が `CONFIG_CPU_PERF=n` でも topology を要る時は、topology の部分だけを別の config に分ける（WS302 の設計の時に決める、§10）。

### 4.2 性能の制御（x86 の driver、HAL を変えない）

- **HAL を変えない理由**（Q301-8 の原文「HALではなくドライバでできるはず」）: HAL の責務は「POSIX の kernel に要る低い層の操作」（`include/hal/hal.h:7〜13`）で、CPU の性能の制御はそれに入らない。platform の driver が arch の命令を直に使う前例がある（上の表）。
  CPU 間の呼び出しは既存の notify で足りる。→ 第 3 版までの HAL の差分（A topology・B 機構）は要らない。選ばなかった案 C として §4.12 に残す。
- **呼び方**: kernel の枠組みは CPU ごとの「最新の要求」の箱（latest wins）に要求を置き、`hal_cpu_notify()` で相手の CPU を起こし、`kernel_cpu_notify_handler()` の中で provider の `apply`（その CPU の上で MSR を書く）と `sample`（その CPU の計数と要求を読む）を呼ぶ。
  thread を CPU に束ねない（第 2 版 R8）。ioctl は全部の相手の確かめを 1 秒待ち、来なければ ETIMEDOUT（要求は箱に残り後で効く）。handler の中の provider の処理は割り込みを止めた短い区間（MSR の数回）だけ。
- **安全の関門**（HAL の #GP の回復が無いので、触る MSR を確かに有る物だけに絞る。存在しない MSR の RDMSR・WRMSR は #GP で、kernel の中では止まる）:
  1. CPUID.01H:ECX[31]（hypervisor）が立つ時は provider を作らない（仮想の CPU は MSR を真似しない事がある）。
  2. architectural な MSR だけを、それを示す CPUID の bit が立つ時だけ触る: HWP の 0x770〜0x777 は CPUID.06H:EAX[7]（と bit ごとの機能）、HFI の 0x17D0・0x17D1 は 06H:EAX[19]、APERF/MPERF は 06H:ECX[0]、IA32_PERF_CTL・STATUS（0x199・0x198）は 01H:ECX[7]（EIST）、AMD の P-state（C001_0061〜0063）は Fn8000_0007_EDX[HwPstate]、AMD の CPPC（C001_02B0〜）は Fn8000_0008_EBX[27]。
  3. model ごとの MSR（MSR_PLATFORM_INFO 0xCE など）は、SDM Vol 4 の表にその MSR がある family・model の allow list の CPU だけ（§4.9）。
  4. SDM の順: HWP の他の MSR は PM_ENABLE の後だけ。enable の前の能力の読みは CPUID だけ。
  5. 書く値は範囲を検めてから（lowest ≤ min ≤ max ≤ highest）。
  6. `CONFIG_CPU_PERF` は既定 n（Q301-4）。n の kernel は MSR に一切触らない。
  関門を通った物でも processor が拒めば止まる（HAL の案 C との違い）。その確率は CPUID の bit と allow list で十分に低いと見る（推定）。p007 の受け入れで 5330 の両方の道（HWP・legacy）を確かめる。
- HWP の順: probe（CPUID）→ PM_ENABLE（package、一度だけで reset まで消えない）→ CAPABILITIES を読む → REQUEST を書く。IA32_HWP_CTL がある時は bit 0 を 0 に保ち（reset の値、5330 は 0、§2.3）、REQUEST の bit 42 を 0 で書く（論理 CPU の要求だけが効く）。
- MHz の推定の元: CPUID.16H の最大の周波数（core の種類ごとに出る、§2.3）、基準は TSC の rate（APERF/MPERF の MPERF は TSC の rate で数える、§2.3）。
- PECI の上書き（CPUID.06H:EAX[16]、5330 は有る）: HWP_STATUS の bit を読み、UAPI に「platform が制限中」の flag を出す。
- `CONFIG_CPU_PERF=y` の kernel は起動の時に HWP を enable する（HWP の有る CPU）。legacy の道（§4.9）を試す時は、試験の起動の parameter `cpuperf.hwp=off` で HWP を enable せず legacy の道を使う（HWP の有る CPU でも PM_ENABLE が 0 なら EIST の PERF_CTL が効く。p007 で 5330 で確かめる、§10）。

### 4.3 domain（周波数の群）と電圧の群

- 周波数の群（domain）: 物理 core を 1 domain。SMT の兄弟は hardware が 2 つの要求を見て 1 つの周波数にする（SDM §17.4.7）ので、kernel は domain の要求を兄弟の全部に同じに書く。
  E-core の 4 つ組が clock を共有すると確かめられたら（p007 の書く試験、§6.3 の 3）、module を domain にする。確かめるまでは firmware の `_PSD` の読み（CPU ごとの HW_ALL、§2.1）に合わせて core ごと。package の制御（bit 42・REQUEST_PKG）は使わない（hybrid で最後に書いた core に揃う恐れ）。
- 電圧の群（voltage_group）: ユーザーの要求の「電圧や周波数を共有する群」に合わせて別の欄で出す。5330 は 1 つ（全 IA core が VCCCORE、§2.2）。電圧の群は provider の表（CPU の model ごと、根拠の資料つき）から。知らない CPU は「不明」。
- step（設定できる値）: HWP は lowest〜highest の連続の整数（kind = RANGE）。表の provider（後の JH7110 の OPP など）は最大 16 の step（kind = STEPS）。
- 範囲の検査: lowest ≤ min ≤ max ≤ highest、desired は 0 か min〜max。熱の上限（§4.6）があれば max を切る（設定の値は残し、切った事を flag で示す）。

### 4.4 strategy の受け手（Q301-7・Q303-11）

- WS301 は自分の policy の名前を持たない。WS303 p003 が作る受け手の口（`kern_thermal_strategy_receiver_register`、WS303 design の V5）に、WS301 の枠組みが受け手として登録する（登録の小さな作業は WS303 p007、WS301 p002 の後）。
- 受け手に渡る値: strategy（PERFORMANCE・BALANCED・POWERSAVE・MANUAL）と、MANUAL の時の 3 つの軸 speed・noise・gating（各 0〜100）。受け手は 1 秒以内に結果（適用・一部・対応外）を返す。
- x86 の provider の初期の対応（値は p007 の測りで直す。SDM §17.4.10 の推奨と EPP の範囲から）:

| strategy | min | max | EPP |
| --- | --- | --- | --- |
| PERFORMANCE | guaranteed | highest | 0 |
| BALANCED（firmware のままでない時） | lowest | highest | 0x80 |
| POWERSAVE | lowest | guaranteed | 0xC0 |
| MANUAL | lowest | `min(speed の上限, noise の上限)`: speed の上限 = efficient + (highest − efficient) × speed / 100、noise の上限 = guaranteed + (highest − guaranteed) × noise / 100 | 255 × (100 − speed) / 100 |

  gating の軸は CPU の性能の要求には使わない（C-state・device の省電力の軸、WS303 の Q303-16）。HWP の activity window は 0（processor に任せる）。
- 起動の時: strategy は「firmware のまま」（WS303 の Q303-15 の推し）。WS301 は何も書かない。`CONFIG_CPU_PERF=y` の kernel が enable した時の最初の要求は SDM の推奨（min = lowest、max = highest、desired 0、EPP 0x80）。

### 4.5 root の直の設定（診断）

- domain ごとの min・max・desired・EPP（2026-10-27 の決定）を root だけが書ける。Q301-7 の答え（Q1 の注「CPU ごとの直の値は root の診断用だけ」）から、利用者向けの UI（Keiland・sessiond）からは出さない。
- strategy との関係（WS303 の Q303-17、未回答、推し A）: 直の設定は strategy の対応より優先し、strategy の表示は MANUAL ではなく「CUSTOM（直の設定あり）」の flag にする。次に strategy が選ばれると直の設定は全部消える。

### 4.6 熱の上限（WS303 の passive）

- `kern_cpuperf_limit(source, domain_or_all, permille)`: domain の範囲の千分率（lowest = 0、highest = 1000）で上限を渡す（hybrid の単位の違いを WS301 の側で解く、WS303 V6）。下限は guaranteed（熱の上限で guaranteed より下げない）。source は `KERN_CPUPERF_LIMIT_THERMAL` の 1 つ（source ごとに 1 つの値、最小を採る）。
- 使い手は WS303 p007（passive）。WS303 の Q303-14 の推し A で、5330 では動く場面が無く、口と host 試験まで。

### 4.7 相対の性能（最速 = 100）と MHz の推定

- MHz/単位: CPUID.16H の最大 ÷ Highest（core の種類ごと）。5330 は P 78.6、E 100 MHz/単位で、1 秒の spin の実測と合う（§2.3）。表示の MHz は全部「推定」の flag つき。実際の周波数は APERF/MPERF の比 × TSC の rate（1 秒の平均）。
- **基準（相対の性能への写し方）は替えられる**（Q301-2 の原文「あとでマッピングを変えられるインタフェースにします」）:
  - provider は出せる基準の一覧（bit）を持つ: `FREQUENCY`（最大の周波数の比）、`FEEDBACK`（HFI の性能の byte）、`PROVIDER`（driver が自分の表で出す値、例: 後の arm64 の provider の DT の capacity）。
  - kernel は選ばれた基準で全 CPU の値を出し、最大を 100 にする（整数、四捨五入）。基準を替えると全 CPU を出し直し、事象を出す。
  - **既定は `FREQUENCY`**（Q301-2 の推し）。5330 は P = 100、E = 75（3300/4400）。周波数だけの比なので 1 clock あたりの仕事の差は入らない。
  - root は `/dev/thermal` の ioctl（§5.2 の `KERN_THERMAL_SET_CPU_RELATIVE`）で基準を選べる。provider の出せない基準は EOPNOTSUPP。選んだ基準は起動ごとに既定へ戻る（保存しない。要れば rc で道具を走らせる）。
  - どの基準を選んでも、HFI の値（`available`・`efficiency`）は別の欄で出し続ける（WS302 が今の状態を見られるように）。
  - 選ばなかった基準: Highest の比（P 56、E 33）は種類で単位が違うので誤り。基準の一覧に入れない。
- HFI の表: driver が物理に連続の page（5330 は 1 page、CPUID.06H:EDX[11:8] + 1）を取り package ごとに 1 つ、MSR 0x17D0 に番地、0x17D1 で有効にする。写しは「IA32_PACKAGE_THERM_STATUS の bit 26 が立っていたら表を写し、それから bit 26 を消して hardware に次を書かせる」順（bit 26 を先に消さない）。周期は 1 秒（割り込みは使わない）。
  5330 の行は P-core の SMT の兄弟で 1 つ、E-core の 4 つ組で 1 つ（§2.3）なので、値は行の単位。全部 0 の行は「今は使えない」の flag。WS302 には性能と効率と変化の事象を渡す。

### 4.8 状態遷移（x86 の CPU ごと）

```
 ABSENT（hypervisor・CONFIG_CPU_PERF=n・どの道の CPUID の bit も無い）
 PROBED（CPUID だけ読んだ）--HWP の bit 有り、cpuperf.hwp=off でない--> HWP_ENABLED（PM_ENABLE、能力を読む）--最初の要求（SDM の推奨）--> RUNNING(HWP)
 PROBED --HWP 無し・cpuperf.hwp=off、EIST か HwPstate 有り、step の表が作れた--> RUNNING(LEGACY)（§4.9）
 PROBED --step の表が作れない--> ABSENT（表は出す、設定は EOPNOTSUPP）
 RUNNING --strategy・直の設定・熱の上限--> RUNNING（箱に最新の要求、notify）
 RUNNING(HWP) --Highest/Guaranteed の変化（HWP_STATUS）--> RUNNING（能力を読み直し、範囲を検め直す、事象）
 RUNNING --S0 idle--> RUNNING（S0 idle の後に要求が保たれるかを p007 で確かめ、保たれなければ resume の hook で書き直す）
```

HWP の道と legacy の道は起動の時に CPU ごとでなく package で 1 つに決める（PM_ENABLE は package、混ぜない）。

### 4.9 HWP の無い CPU の P-state（Q301-8「P-stateを操作しますが、HALではなくドライバでできるはずです。」）

HWP は Intel の Skylake（第 6 世代、2015）からの機能で、必ずはない。無い場合: Skylake より前の Intel、AMD（CPPC の MSR は Zen 2 の一部から）、hypervisor の下（QEMU・KVM は HWP を出さない見込み、§10）、arm64・riscv64（x86 の機能でない）。
手元の Intel の機械（5330 = 第 12 世代、SV7 = 第 8 世代）は HWP を持つ（SV7 は世代からの推定、未確認）。OS が何もしないと、周波数は firmware の残した設定と hardware の自動の制御のまま（壊れはしないが省電力の制御が効かない）。

x86 の driver が legacy の P-state を扱う（HAL は変えない、§4.2）。step の表の源は次の順に試す:

| 順 | 源 | 使える時 | 中身 |
| --- | --- | --- | --- |
| 1 | ACPI の `_PSS`（と `_PPC`・`_PSD`） | CPU の object（ACPI0007・Processor）に `_OSC` を呼ばずに `_PSS` がある firmware（静的な SSDT に持つ古い機械。5330 は無い、§2.1） | 各段の MHz・電力・PERF_CTL（か AMD の P-state）に書く値。`_PSD` の群（SW_ALL・SW_ANY・HW_ALL）で domain。`_PPC` は firmware の上限 |
| 2 | Intel: MSR_PLATFORM_INFO（0xCE） | `_PSS` が無く、EIST（CPUID.01H:ECX[7]）が有り、family・model が SDM Vol 4 の 0xCE の表の allow list にある CPU | 最小の比（[47:40]）〜最大の非 turbo の比（[15:8]）を 100 MHz の段にし、PERF_CTL[15:8] に比を書く。turbo は「最大の非 turbo より上を許す」1 段（PERF_CTL の bit 32 の disengage を使わない）。domain は package（`_PSD` が無いので、共有を仮定した安全な側） |
| 3 | AMD: HwPstate（Fn8000_0007_EDX）と C001_0061 | `_PSS` が無い AMD | P0〜PstateMaxVal の index だけの段（MHz は不明、AMD APM §17.1 は段の周波数を定めない）。C001_0062 に index を書く。domain は core |

- 段: driver の legacy の道は「段の表（kind = STEPS、最大 16 段）」を出す。HWP と違い CPU は自分で段を選ばないので、**OS が負荷を見て段を選ぶ governor** が要る。
  governor は 1 つ: 枠組みの 100 ms の周期で、CPU ごとの APERF/MPERF（無ければ idle の時間の率）から使用率を出し、80% を超えたら 1 段上げ、30% を下回ったら 1 段下げる（初期値、p007 で直す）。
  strategy は段の範囲と閾値を変える: PERFORMANCE = 常に最上段、BALANCED = 全段で governor、POWERSAVE = 中の段（guaranteed に当たる最大の非 turbo）まで、MANUAL = speed で上限の段・noise で turbo の段の可否。root の直の設定は段の上限・下限。
- 5330 での試験: `cpuperf.hwp=off` で HWP を enable しない起動にすると、5330 も EIST の PERF_CTL が効く見込み（SDM の EIST の節は HWP の有無に依らない。§10、p007 で確かめる）。源は 2（0xCE、ADL-P は Vol 4 の表にある見込み、§10）。AMD の道は試す機械が無い（host 試験だけ、未検証のまま既定 n）。
- この道は Phase を 1 つ足す（p005、§7）。

### 4.10 資源・並行性・割り込み

- CPU ごとの箱、domain の表、HFI の page（package ごと）は kernel の寿命（CPU の hot-plug は無い）。HFI の page は止めの完了（PTR を 0 にし、hardware の書きが止んだ事を bit 26 で確かめた後）の前に解放しない。
- lock: 設定の入口（ioctl の直の設定・strategy の受け・熱の上限・基準の選択）は `cpuperf_mutex` で直列。CPU ごとの箱は spinlock（irqsave、notify の handler と共有）。読みの写しは spinlock。MSR の読み書きは notify の handler の中で割り込みを止めた短い区間だけ。
- legacy の governor は枠組みの 1 本の kernel の thread（100 ms の周期、CPU ごとの箱に要求を置くだけ）。HWP の道では走らない。
- strategy の受けは WS303 の strategy の mutex の下から呼ばれる。順は WS303 の strategy の mutex → `cpuperf_mutex` だけ（逆の順を作らない。WS301 から WS303 を呼ぶのは事象の発行だけで、mutex を持たずに出す。直の設定の「CUSTOM」の印も WS301 の中だけに持ち、WS303 の mutex を取らない。WS303 の第 4 版の review の M2）。
- 割り込みと DMA: 割り込みは使わない（HWP の通知と HFI は周期の読み）。HFI の表は hardware が書く記憶: page を 0 で埋め、止める時は書きが止んでから解放（SDM）。

### 4.11 失敗と回復

| 失敗 | 振る舞い |
| --- | --- |
| hypervisor・どの道の CPUID の bit も無い | provider 無し: CPU ごとに 1 domain、相対の性能は種類が 1 つなら 100（hybrid で値が無ければ「不明」）、設定は EOPNOTSUPP、strategy は「対応外」 |
| legacy の step の表が作れない（`_PSS` 無し、allow list の外の model、AMD の段が 1 つ） | 同上（表は出す） |
| 関門を通った MSR を processor が拒む（#GP） | kernel が止まる（回復の口が無い、§4.2）。既定 n と CPUID・allow list の関門で避ける。起きた時は `cpuperf=off` の起動で避けられる |
| HFI の行が 0 | 「今の使える性能」0 と flag、WS302 に任せる |
| notify の相手が 1 秒で確かめない | ioctl は ETIMEDOUT、要求は箱に残る |
| S0 idle の後に要求が乱れる | resume の hook で全部の要求を書き直す（p007 で要るかを確かめる） |
| PECI の上書き | 要求は書くが、UAPI に「platform が制限中」 |

### 4.12 選ばなかった案

- **C. HAL に性能の制御の口を足す**（第 3 版までの推し、[proposed/hal-cpu-perf.diff](proposed/hal-cpu-perf.diff)）: #GP の回復を HAL が持てる良さはあるが、HAL の API が 7 つ増え、CPU の性能の制御は HAL の責務（POSIX の kernel に要る低い層）の外。ユーザーの Q301-8 の原文の方向とも違う。差分は記録として残す。
- 案 (b) の HAL（MSR の汎用の読み書きの口）: kernel 全体に MSR の番号が散らばる。driver に閉じる今の推しの方が狭い。
- `_OSC` を呼んで `_CPC`・`_PSD` を読む: ユーザーの決定（Q301-3）で WS310 へ。しかも 5330 の `nominal_freq` は E-core に当たらない（§2.1）。
- package の制御: §4.3。MHz を user が書く口: 換算が推定で、HWP は抽象の単位で動く。HWP・HFI の割り込み: LVT の口が要る、周期の読みで足りる。
- WS301 独自の policy・起動の parameter: Q301-7 で取り下げ。
- CPU ごとの kernel の thread（第 1 版）: `sched_set_cpu` の呼び手が無く、thread は飢えうる（第 1 版の review）。

## 5. インタフェース（案。名前は実装の Phase で coding-style に合わせて決める）

### 5.1 kernel の中

```c
/* include/kern/cpuperf.h */
struct kern_cpuperf_topology {
	uint32_t package, die, module, core, thread;
	uint32_t core_type;		/* the processor's code (x86: CPUID 1AH EAX[31:24]), 0 unknown */
	uint32_t firmware_id;		/* the ACPI processor UID, KERN_CPUPERF_UNKNOWN */
};

struct kern_cpuperf_cpu {
	struct kern_cpuperf_topology topology;
	uint32_t domain;
	uint32_t voltage_group;		/* KERN_CPUPERF_UNKNOWN when not known */
	uint32_t relative;		/* 0..100 (static), KERN_CPUPERF_UNKNOWN */
	uint32_t available;		/* 0..255 from the feedback table, KERN_CPUPERF_UNKNOWN */
	uint32_t efficiency;		/* 0..255 from the feedback table, KERN_CPUPERF_UNKNOWN */
	uint8_t lowest, efficient, guaranteed, highest;
	uint32_t highest_mhz;		/* estimate */
	uint32_t delivered_mhz;		/* last second, estimate */
	uint32_t flags;
};
int kern_cpuperf_cpu(hal_cpu_id_t cpu, struct kern_cpuperf_cpu *info);	/* WS302 */
int kern_cpuperf_domain_set(uint32_t domain, const struct kern_cpuperf_setting *setting);	/* root's direct setting */
int kern_cpuperf_strategy(const struct kern_thermal_strategy *strategy);	/* the receiver WS303 calls */
int kern_cpuperf_limit(uint32_t source, uint32_t domain, uint32_t permille);	/* domain KERN_CPUPERF_ALL */
```

`struct kern_thermal_strategy`（strategy と 3 つの軸）は WS303 の `include/kern/thermal.h` が定める。

provider の口（x86 の driver が `kern_cpuperf_provider_register` で 1 つだけ登録する。`apply`・`sample` は notify の handler の中で対象の CPU の上で呼ばれ、割り込みは止まっている）:

```c
struct kern_cpuperf_provider_ops {
	int (*topology)(hal_cpu_id_t cpu, struct kern_cpuperf_topology *topology);	/* after start, from any CPU */
	int (*describe)(hal_cpu_id_t cpu, struct kern_cpuperf_levels *levels);		/* RANGE or STEPS, MHz estimates */
	uint32_t relative_sources;	/* KERN_CPUPERF_RELATIVE_FREQUENCY | _FEEDBACK | _PROVIDER */
	int (*relative)(hal_cpu_id_t cpu, uint32_t source, uint32_t *value);		/* before the scaling to 100 */
	void (*apply)(const struct kern_cpuperf_request *request);			/* on the target CPU */
	void (*sample)(struct kern_cpuperf_sample *sample);				/* on the target CPU */
	int (*feedback)(hal_cpu_id_t package_cpu, struct kern_cpuperf_feedback *copy);	/* 1: new copy, 0: none */
};
```

### 5.2 UAPI（`include/uapi/thermal.h` に足す、固定の幅、ILP32 と LP64 で同じ）

```c
struct thermal_cpu_info {		/* KERN_THERMAL_GET_CPU, _IOWR, in: cpu */
	uint32_t cpu;
	uint32_t count;
	uint32_t package, die, module, core, thread, core_type;
	uint32_t domain, voltage_group;
	uint32_t relative;		/* 0..100, 0xffffffff unknown */
	uint32_t available, efficiency;	/* 0..255, 0xffffffff unknown */
	uint32_t lowest, efficient, guaranteed, highest;
	uint32_t highest_mhz, delivered_mhz;	/* estimates */
	uint32_t flags;			/* HAS_CONTROL, ESTIMATED_MHZ, LIMITED, PLATFORM_LIMIT, CUSTOM */
	uint32_t reserved[4];
};	/* 96 bytes */

struct thermal_cpu_domain {		/* KERN_THERMAL_GET_CPU_DOMAIN, _IOWR, in: domain */
	uint32_t domain;
	uint32_t count;
	uint64_t cpus[8];		/* 512 CPUs as bits */
	uint32_t kind;			/* RANGE or STEPS */
	uint32_t step_count;
	uint32_t steps[16];		/* abstract units (RANGE: steps[0] lowest, steps[1] highest) */
	uint32_t step_mhz[16];		/* estimate, 0 unknown */
	uint32_t minimum, maximum, desired, preference;	/* in force */
	uint32_t limit;			/* the thermal ceiling in units, 0xffffffff none */
	uint32_t flags;			/* CUSTOM: a direct setting is in force */
	uint32_t reserved[2];
};	/* 240 bytes (64-bit aligned cpus) */

struct thermal_cpu_relative {		/* KERN_THERMAL_GET/SET_CPU_RELATIVE, root for SET */
	uint32_t available;		/* out: FREQUENCY, FEEDBACK, PROVIDER bits */
	uint32_t source;		/* the one in force; in: the one to choose */
	uint32_t reserved[2];
};	/* 16 bytes */

struct thermal_cpu_setting {		/* KERN_THERMAL_SET_CPU_DOMAIN, _IOW, root */
	uint32_t domain;		/* or 0xffffffff for every domain */
	uint32_t minimum, maximum, desired, preference;
	uint32_t reserved[3];
};	/* 32 bytes */
```

番号は WS303 p003 の `include/uapi/thermal.h` の案（ユーザーの review）と一緒に決める。事象は WS303 の事象の record（128 byte）に種類 CPU（能力・相対の性能・domain の設定・platform の制限の変化）を足す。
大きさは `_Static_assert` と `offsetof` で固める（第 1 版の review が -m64・-m32 で測った 240 byte の形を保つ）。

### 5.3 HAL

変えない（推し、§4.2）。第 3 版までの差分の案 [proposed/hal-cpu-perf.diff](proposed/hal-cpu-perf.diff) は選ばなかった案 C の記録（当てない）。説明は [hal-review.md](hal-review.md)。

## 6. 試験の計画

### 6.1 host 試験

domain の組み立て（5330 の topology の表: SMT・L2 の 4 つ組）、範囲の検査、strategy の対応の表（4 つと MANUAL の軸の端の値、HWP と legacy の段）、直の設定と strategy の優先、熱の上限の千分率と最小、
MHz/単位（5330 の 56・33 と 4400・3300）、相対の性能の 3 つの基準と切り替え（HFI の表の模擬: 行の共有・全部 0）、legacy の step の表の作り方（`_PSS` の模擬、0xCE の値、allow list の外）と governor の上げ下げ、
CPUID の関門（hypervisor の bit、bit ごとの道の選び）を CPUID・MSR の模擬の表で、UAPI の layout（`_Static_assert`、-m64・-m32）、provider の無い時の答え。
driver の CPUID・MSR の命令は 1 つの小さな file（inline の `__asm__`）に閉じ、host 試験では模擬に置き換える。

### 6.2 QEMU

q35 の既定の CPU（HWP 無し、hypervisor の bit）: `CONFIG_CPU_PERF=y` の試験の build で CPU の表が出て設定が EOPNOTSUPP、strategy の受けが「対応外」（関門 1 で MSR に触らない事の確かめ）。`boot-test.sh` の回帰。判定は ssh の道具の出力（console の log では判定しない）。

### 6.3 5330（素の起動、1 本の script、`CONFIG_CPU_PERF=y` の試験の build）

1. 表の読みが §2.3 と合う（12 CPU、core の種類、Highest 等、module の組、MHz/単位）。
2. HWP の道: strategy の 4 つと直の設定で、1 つの CPU の spin の APERF/MPERF の MHz が要求に従う（P・E ごと）。
3. E-core の 4 つ組の clock の共有: 同じ組の 2 つの E-core に違う max（例 33 と 8）を書いて両方を spin し、MHz が分かれるか揃うかを記録（zedBSD の上の書きなので Debian の状態は変えない）。揃えば module を domain にする（§4.3）。
4. legacy の道: `cpuperf.hwp=off` の起動で、段の表が 0xCE から作られ、段を固定した spin の MHz が段に従い、governor が負荷で段を上げ下げする。
5. 相対の性能の基準: 既定 FREQUENCY で E = 75、FEEDBACK に替えて HFI の値（性能・効率、行ごと）を記録。
6. powersave で idle の電力が下がる: 電池の放電の速さ（ACPI の `_BST`、AC を抜く。ユーザーの手が要るので Q1 の割当）。
7. S0 idle の回帰（`sleepctl devices` と `s0idle`）と、S0 idle の後に要求が保たれるか。

## 7. Phase と LW（±50%、1 LW ≈ 実時間 15〜25 分）

| Phase | 内容 | 依存 | LW | 受け入れ |
| --- | --- | --- | --- | --- |
| p001 | 調査と設計、design-reviewer、ユーザーの判断（Q301-1） | — | 残り 1 | 答え |
| p002 | 枠組み（domain・電圧の群・相対の性能と基準の選択・strategy の受け・直の設定・熱の上限・最新の要求の箱と notify の道・provider の口）と host 試験 | p001 | 5 | §6.1 の枠組みの部分 PASS、全 platform の build warning 0 |
| p003 | x86 の driver の基礎（CPUID・MSR の小さな file、関門、topology、MADT の UID、notify の handler の道） | p002 | 3 | host 試験（関門・topology）、amd64・i386 の build warning 0、QEMU で provider 無し |
| p004 | HWP の道（enable の順、能力、strategy の対応、HFI の写し、PECI の flag） | p003 | 4 | host 試験、5330 で §6.3 の 1・2 |
| p005 | legacy の P-state（Intel の `_PSS`・0xCE、AMD の HwPstate、governor） | p003 | 4 | host 試験、5330 で §6.3 の 4（AMD は host 試験だけ） |
| p006 | UAPI（`/dev/thermal` の CPU の ioctl、基準の選択）と道具 | p002、WS303 p003 | 3 | ioctl の host 試験、QEMU（ssh） |
| p007 | 5330 の受け入れ（§6.3 の 1〜7、1 本の script） | p004、p005、p006 | 4 | script の PASS |
| p008 | 規約の全文と文書（`docs/architecture/power-management.md`、WS303 と順を Q1） | p007 | 2 | 規約、文書 |

合計 約 25 LW（p001 を除く）。HAL の差分が無くなった分（第 3 版の p003 の 5 LW）で legacy の道（p005）を入れた。arm64・riscv64 の provider は Q301-5 で後の WS。
WS303 の p007（strategy の受け手の登録と passive）は WS301 の p002・p004 の後。

## 8. 出典と引用

- 値（MSR の番号・bit・CPUID の bit・HFI の表の形）は Intel SDM rev 093・AMD APM 24593 r3.45 から、block ごとに「Value(s) referenced: Intel SDM Vol 3B rev 093 §17.4.2 (IA32_HWP_CAPABILITIES)」の形で書く（複数行の comment）。手順（enable の順、HFI の止め方、HWP_CTL の極性）は「Procedure referenced:」。
- FreeBSD（BSD-2）は考え方の参考だけ。code・名前・構造は写さない。
- 5330 の観測の値（MHz/単位、HFI の行）は試験の期待の値として `plan/ws301/tests/` に置き、source の定数にしない（source は CPUID と MSR から読む）。
- GPL の source は読んでいない。Linux は黒箱として sysfs の値を読んだだけ。ACPI 6.5 の節番号は未確認。

## 9. ユーザーに決めてほしい点（推しつき）

- **Q301-1 HAL を変えない形でよいか**（§4.2、[hal-review.md](hal-review.md)）: (A, 推し) HAL を変えない。topology・HWP・legacy の P-state・HFI は x86 の driver が CPUID・MSR を自分で扱う（安全の関門は CPUID の bit・hypervisor・allow list・既定 n）。
  (B) topology だけ HAL に足す（WS302 が `CONFIG_CPU_PERF` と別に topology を要る時、arm64 の DT の topology も HAL が持つ方が揃う）。(C) 第 3 版の差分（HAL が #GP を回復する性能の制御の口）を承認する。
- Q301-2・Q301-8 は答えを受けた（§0）。WS303 の Q303-17（直の設定と strategy の関係）は §4.5 の推し A。

## 10. 未確認

1. E-core の 4 つ組の clock の共有（§6.3 の 3）。
2. HFI の表の値（Linux が有効にしていない）と更新の頻度、1 秒の周期で足りるか（§6.3 の 5）。
3. CPUID.04H の L2 の値そのもの（Linux の `cluster_cpus` からの推定）。
4. QEMU の KVM で HWP の CPUID が見えるか。
5. SV7 の HWP（世代からの推定）。
6. firmware が起動の時に PM_ENABLE を立てているか（Debian の値は Linux の物）。
7. HWP の有る CPU で PM_ENABLE が 0 の時に EIST の PERF_CTL が効くか、ADL-P（family 6 model 0x9A）が SDM Vol 4 の 0xCE の表にあるか（§4.9、p005・p007）。
8. WS302 が `CONFIG_CPU_PERF=n` でも topology を要るか（§4.1、Q301-1 の B）。
9. ACPI 6.5 の節番号（この WS では使わない）。

## 11. 第 2 版の改訂（R1〜R12）の行き先

R1 → §4.2（順・関門・hypervisor。#GP の回復は HAL を変えない推しで無くなった）、§0（既定 n は Q301-4 の答え）。R2 → §2.1（動的な SSDT は git にある。Nominal から MHz を取る案は §2.3 の読みで取り下げ）。R3 → §4.3。R4 → §4.1・§4.7。R5 → §4.7。R6 → §4.2。R7 → §5.3（HAL を変えない推しで差分は案 C の記録）。R8 → §4.2。R9 → §2.3（確かめた）。R10 → §6.3 の 4。R11 → §4.8・§6.3 の 5。R12 → §5.2（`/dev/thermal` に移し、policy の ioctl は Q301-7 で外した）。
