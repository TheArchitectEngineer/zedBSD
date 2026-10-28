<!-- awesome-plan project=zedbsd record=ws301 -->

# WS301 CPU の省電力の driver の枠組み（DVFS と P-core/E-core の性能）

- Master: [plan/master.md](../master.md)（2026-10-27 作成、q1615 P1 の計画）。Primary Milestone: MG004（既定構成の性能と電力）。Related: MG003、MG008（HAL の契約）、
  [WS302](../ws302/ws.md)（この WS の口を使う）、[WS052](../ws052/ws.md)（電源の UAPI と文書）、[WS132](../ws132/ws.md)（`/dev/system` の事象）。
- Status: planning（2026-10-27 新設。計画だけ）
- 出典: 2026-10-27 ユーザー
  「CPUの省電力機能のドライバフレームワークを追加（WSを立てて検討）
  - コアごとのDVFSのステップ(設定可能な値)を取得する機能
  - どのコア群が電圧や周波数のを共有しているかのグルーピングも取得できる機能
  - コアごとのDVFSの状態を設定する機能
  - コアごとのDVFSの状態を取得する機能
  - 一番速いコアを100としたときのコアごとの性能を取得するP-core/E-core機能」
  補足（2026-10-27 ユーザー、Q1 経由の原文）「DVFSとP-core/E-coreは、/dev/systemに入れるのが適切ならそれでもいいですとお伝えください。ファン制御もあるといいですね。」
- 全体の依存と着手の順: [plan/agents/P1/pm-plan.md](../agents/P1/pm-plan.md)。

- 2026-10-27 09:39 ユーザー（クリック）: UAPI は /dev/system、設定は domain ごとの min・max・desired・EPP、設定できるのは root だけ、HAL の差分の案を作る（承認は差分ごと）。対象はまず amd64（5330 の HWP）、arm64・riscv64 は枠だけ。fan は別の WS（[WS303](../ws303/ws.md)）。

## 単一の到達目標

kernel に CPU の性能と電力の枠組み（provider の登録の口）を作り、5330（Alder Lake-P、i5-1245U、P-core 2 と E-core 8 の hybrid）で
(1) core ごとの DVFS のステップ（設定できる値）、(2) 電圧・周波数を共有する core の群、(3) core ごとの DVFS の状態の設定、(4) 状態の取得、
(5) 一番速い core を 100 とした core ごとの相対の性能、を kernel の中（WS302 などの利用者）と user（UAPI）から使えるようにする。
他の機種（Pi 4・Pi 5・VisionFive 2・AMD）は provider を足す後の Phase で、枠組みはそれらを入れられる形にする。

## 今の事実（2026-10-27 の調べ、根拠は file:行）

- CPU の周波数・DVFS・P-state の code は **どの arch にも無い**（HWP・IA32_PERF_CTL・EPP・CPPC の `_CPC`・`_PSS`・`_PSD`・CPUID 0x1A・HFI・operating-points-v2 の grep が 0）。
- topology: amd64 は MADT の LAPIC・x2APIC の平らな list（`src/hal/amd64/bsp-pcat/acpi.c:155-245`、`smp.c:44-100`）で、SMT・core・cluster・package は出していない。
  CPUID 0xB/0x1F は console の lock の x2APIC の ID だけ（`bsp-pcat/cons.c:586-616`）。arm64 は MPIDR（`bsp-rpi4/fdt.c:679-708`）、riscv64 は `cpu-map` を読まない（`src/hal/riscv64/fdt.c:1121`）。
- HAL の CPU の口は `hal_cpu_count`・`hal_cpu_current`・mask・notify・`hal_cpu_idle`・`hal_cpu_idle_suspend*`（`include/hal/hal.h:120-232`）だけ。MSR の読み書きは HAL の中だけ（`src/hal/amd64/asm.c:270-295`）。
- 似た前例: GPU の DVFS は i915 の RPS（`src/drivers/gpu/i915/gt-power.c:252-723`）、遠隔測定は `hw.gputelemetry`（`include/uapi/sysctl.h:179-237`）。熱は `hw.thermal`（ACPI の `_TMP`・`_PSV`・`_CRT`、Pi 5 の fan、`src/drivers/acpi/acpi-thermal.c:112`、`rpi5-fan.c:330`）。
  Pi 4 の firmware の mailbox は core の clock だけ扱う（`src/drivers/platform/rpi4/rpi4-firmware.c:132-137`、ARM の clock 3 は使っていない）。VF2 の CPU の clock には触らない（`vf2-crg.c:18-23`）、PMIC の driver は無い。
- `docs/architecture/power-management.md:22-24` は「processor frequency control and thermal management」を対応外と書いている（ユーザーの review 待ちの文書）。この WS で範囲を変える。

## 範囲

- 含む: kernel の枠組み（perf domain・step の表か連続の範囲・状態の set/get・相対の性能・topology）、利用者の口（kernel の内部の API と UAPI）、
  既定の policy（performance・balanced・powersave 程度、熱の passive cooling の上限の口）、x86 の provider（HWP と、無い機械の `_PSS`＋PERF_CTL、Intel hybrid の core の種類と性能の比、ACPI `_CPC`・`_PSD`）、
  後の Phase で arm64（Pi の firmware の ARM clock）・riscv64（JH7110 の CPU の PLL と OPP、電圧は PMIC が要る）・AMD（CPPC）の provider。
- 含まない（別の WS か後）: thread の割り付けの判断（[WS302](../ws302/ws.md)）、S0ix（WS052）、**fan の制御**（下の「fan の扱い」、別の WS の案）、C-state の governor（今の idle は `hlt`、別の話）。

## UAPI の置き場所（ユーザーの補足への答え、推し）

| 案 | 良い所 | 悪い所 |
| --- | --- | --- |
| **A. `/dev/system` の ioctl（推し）** | 電源の操作（`KERN_SYSTEM_GET_POWER`・`KERN_SYSTEM_SLEEP`）と事象の購読（WS132、class ごとの filter）が既に一か所にある。権限（root か session の user か）の扱いが揃う。policy の変化・熱の制限を事象で知らせられる。WS052 の電源の文書と同じ所に書ける | `system-device.c` が育つ。core ごとの表は可変長（`process_info` と同じ配列の ioctl で足りる） |
| B. sysctl（`hw.cpuperf`） | `hw.cputimes`・`hw.thermal`・`hw.gputelemetry` と同じ形で、監視の道具（top など）が読みやすい | 固定の表の読みだけの口で、設定・権限・事象に向かない（設定は結局 A が要る） |
| C. 新しい device（`/dev/cpuperf`） | 独立 | 口が増え、権限と事象の仕組みを作り直す |

推し: **A**。topology・domain・step・相対の性能・今の状態の読みと、policy と domain の上限・下限の設定を `/dev/system` に、変化は事象の class で。
秒ごとの監視で要れば、今の周波数だけ B の読みを後で足す（決めるのは p001）。UAPI の変更はユーザーの明示の決定（`plan/master-design-policy.md:189`）。

「core ごとの DVFS の状態の設定」は、利用者が直接 MHz を書くより、domain ごとの min・max・desired（と EPP）を渡し、hardware（HWP）か provider が決める形を推す
（HWP は logical CPU ごとの要求を hardware が domain で解く。共有の domain の core に違う値を書いた時の規則を p001 で決める）。

## fan の扱い（ユーザーの補足「ファン制御もあるといいですね。」、案）

- 推し: **別の WS（番号は Q1）「熱と fan の制御」**にし、この WS とは口だけ揃える。理由: 到達目標と hardware が違う（DVFS は CPU の register・firmware、fan は ACPI の `_FAN`・active cooling の `_ACx`/`_ALx`、
  Dell の EC・SMM・thermal mode、Pi 5 の PWM（既存 `rpi5-fan.c`）、AMD の GPU の board の SMU の fan（[WS039](../ws039/ws.md) p012、`include/uapi/amdrdna.h:93-99`・`242-245`、GPU に閉じた物））。
  5330 の fan は EC が回し、OS から直に duty を書けない見込み（読みと Dell の thermal mode の選択が現実的、要調査）。
- 関係: 熱の policy は passive（この WS の DVFS の上限）と active（fan）を合わせて決める。passive の口（domain の上限を熱の側が下げる）はこの WS の p002 に入れ、熱と fan の WS がそれを使う。
- UAPI は同じ置き場所（推し A の `/dev/system`、温度は今の `hw.thermal`）。

## 制約

- HAL の API の変更は差分ごとの事前承認（[Guardrail](../guardrail.md)）。この WS で要る見込みの物（p001 で差分の案を `plan/ws301/proposed/` に置く、適用しない）:
  - topology: `hal_cpu_topology(cpu, &info)`（package・core・SMT・cluster の ID、core の種類）。MADT・APIC ID・MPIDR は HAL が持つため。
  - x86 の性能の register: core ごとに MSR を読み書きする口。案 (a) HAL が HWP の機構（caps の読み・要求の書き）を持ち policy は持たない（`hal_cpu_idle_suspend` と同じ分け方）、
    案 (b) 指定の CPU で関数を走らせる口と MSR の口。推しは (a)（HAL の責務を「機構だけ」に保つ）。arm64・riscv64 は driver（firmware・clock）が provider で HAL は ENOTSUP。
- 値と手順: Intel SDM（HWP・CPUID 0x1A・HFI）、ACPI 仕様（`_CPC`・`_PSS`・`_PSD`）、各 SoC の公開資料を優先。Linux の intel_pstate・cpufreq（GPL）は参照だけ、code は写さず block ごとに出典。
- 規約: 新しいコードに `plan/coding-style.md` の全文。config は `config/kernel-options.list`・`Makefile`・`platform/<arch>/vmunix.mk` の形（`CONFIG_CPU_PERF` など）。
- 試験: host 試験（domain の組み立て、相対の性能の計算、policy の解決、hybrid の scaling）と 5330 の実機（周波数の観測は APERF/MPERF の比か HWP の status、負荷の時と idle の時の電力は Debian の RAPL との比較か zedBSD の RAPL の読み）。
  QEMU は口の回帰（provider が無い時に ENOTSUP・空の表）だけ。

## 受け入れ（案）

1. 5330 で、12 の logical CPU について、domain（共有の群）、step か連続の範囲、今の状態、相対の性能（最速の P-core = 100、E-core の値は p001 で根拠を示す）が UAPI で読め、Debian（`/sys/devices/system/cpu`・`intel_pstate`）の値と矛盾しない。
2. policy と domain の min・max の設定で、負荷の時の実際の周波数（APERF/MPERF）が設定に従う事を数で示す。powersave で idle の電力が下がる（測れる範囲で）。
3. kernel の内部の API（WS302 が使う相対の性能・domain・topology）が文書と host 試験で固まる。
4. 提供者の無い機械（QEMU、他の arch）で壊れない（ENOTSUP か 1 domain の表）。build warning 0（全 platform）、`boot-test.sh` の回帰（T）、5330 の素の起動の回帰。
5. 規約の全文、`docs/architecture/power-management.md`（か新しい文書）の更新（ユーザーの review）。

## Phase の案と見積もり（LW、±50%）

| Phase | 内容 | 依存 | LW |
| --- | --- | --- | --- |
| p001 | 調査と設計: 5330 の HWP・CPUID 0x1A・HFI・`_CPC`・`_PSD`・RAPL を Debian で読む（cpuid・rdmsr・acpidump）、Pi・VF2・AMD の口の下見、枠組みの API・UAPI（推し A の詳細）・HAL の差分の案・config、design-reviewer | — | 6 |
| p002 | kernel の枠組み（domain・step・状態・相対の性能・policy・熱の上限の口）と host 試験、provider の無い時の形 | p001、HAL・UAPI の決定 | 5 |
| p003 | HAL: topology と x86 の HWP の機構（承認された差分だけ） | p001 の承認 | 4 |
| p004 | x86 の provider: HWP（と `_PSS`＋PERF_CTL の fallback）、Intel hybrid の core の種類と性能の比、`_PSD` の domain | p002、p003 | 6 |
| p005 | UAPI（`/dev/system` の ioctl と事象）、`cpuperf` の道具（読みと設定） | p002、UAPI の決定 | 4 |
| p006 | 5330 の受け入れ（1 本の script: 表の読み、設定と APERF/MPERF、電力） | p004、p005 | 3 |
| p007 | arm64 の provider（Pi 4・Pi 5 の firmware の ARM clock、domain は 4 core で 1 つ） | p002 | 4 |
| p008 | riscv64 の provider（JH7110 の CPU の PLL と OPP、電圧の PMIC が要るなら別の Phase か WS に） | p002 | 5 |
| p009 | 規約の全文の見直しと文書 | p006〜p008 | 3 |

合計 約 40 LW（p001〜p006・p009 の 5330 だけなら 31 LW）。p007・p008 は 5330 の後で、ユーザーの判断で後に回せる。

## 判断が要る点

- UAPI の置き場所（推し A、`/dev/system`）と、設定を誰に許すか（root だけか、session の user が policy を選べるか）。
- HAL の差分（topology、x86 の HWP の機構の案 (a)）の承認。
- fan を別の WS にする（推し）か、この WS に入れるか。
- 範囲: 5330 だけで WS を閉じ、他の arch は別の WS にするか、p007・p008 まで入れるか。
- `docs/architecture/power-management.md` の「CPU の周波数と熱は対応外」を変えること。

## 設計と再開点（2026-10-29 P5、q1666-i02）

- 設計: [design.md](design.md)（第 2 版）、レビュー [design-review.md](design-review.md)、HAL の差分の案 [proposed/hal-cpu-perf.diff](proposed/hal-cpu-perf.diff)（第 2 版、適用しない、承認待ち）。Phase と LW は design.md の「第 2 版の Phase の表」が本体で、下の「Phase の案と見積もり」より優先する（5330 だけで約 25 LW）。
- 再開点: ユーザーの判断 Q301-1〜7（design.md の第 2 版の問い）を Q1 経由で待つ。§6.0 の 5330 の Debian の読み（CPUID・MSR）は msr の module の読み込みの許可（Q1）が要る。ACPI 6.5 の節番号は記憶による推定で未確認（ユーザーに PDF を置けるかを Q1 が聞いている）。
- 2026-10-29 ユーザー（Q1 の中継、原文）「CPUのスピードは/dev/systemと言ってしまいましたが、/dev/thermalに変更してください。」→ 解釈: UAPI は WS303 の `/dev/thermal`（上の 2026-10-27 の「UAPI は /dev/system」を置き換える）。policy は WS303 の strategy の受け手（design.md の「第 2 版への追記」）。
