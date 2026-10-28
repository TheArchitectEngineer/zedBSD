<!-- awesome-plan project=zedbsd record=ws301-hal-review -->

# WS301 の HAL の追加の説明（ユーザーのレビュー用、Q301-1）

2026-10-29 P5。対象は [proposed/hal-cpu-perf.diff](proposed/hal-cpu-perf.diff)（第 3 版、`include/hal/hal.h` への追加だけ、まだ当てていない）。設計の本文は [design.md](design.md)。

## 1. 用語

- **HAL**: zedBSD の kernel と CPU・board の間の層。CPU の register や firmware の表を直に触るのは HAL だけで、kernel は HAL の関数を呼ぶ。HAL の関数の宣言（`include/hal/hal.h`）を変えるにはユーザーの承認が要る。
- **DVFS**: CPU の周波数（と電圧）を負荷に合わせて変える事。速くすると電力と熱が増え、遅くすると減る。
- **HWP**（Hardware P-states）: Intel の CPU（第 6 世代以降）が持つ仕組み。OS は「この範囲で、性能寄りか省電力寄りか」を CPU に伝えるだけで、細かい周波数は CPU が自分で決める。5330 の CPU（i5-1245U）は持つ。
- **MSR**: CPU の中の設定の register。HWP の設定も MSR に書く。CPU ごと（論理 CPU ごと）にあり、その CPU の上で走る命令でしか読み書きできない。存在しない MSR を触ると CPU が例外（#GP）を起こす。
- **topology**: どの CPU がどの core・どの束（module）・どの package に属するか。5330 は P-core 2 つ（それぞれ 2 thread、計 4）と E-core 8 つ（4 つずつの 2 束）で、論理 CPU は 12。
- **core の種類**: 5330 は速い P-core と省電力の E-core の 2 種類（hybrid）。CPU が種類の番号を返す。
- **HFI**（Hardware Feedback Interface）: CPU が「今それぞれの core がどれだけ速く・効率よく動けるか」を表にして memory に書く仕組み。熱や電力で値が変わる。
- **相対の性能**: 一番速い core を 100 とした各 core の速さ（ユーザーの要求 (5)）。

## 2. 何のために足すか

ユーザーの要求（ws.md の 5 つ）のうち、kernel が CPU の register か firmware の表を読まないと答えられない物があり、それは HAL の中でしか読めない:

| ユーザーの要求 | 要る HAL の口 |
| --- | --- |
| (2) どの core 群が電圧や周波数を共有しているか | A: topology（core・module・package、core の種類） |
| (1) core ごとの DVFS のステップ、(4) 状態の取得 | B: 能力の読み（最低・保証・効率・最高の値）、今の要求と実際の周波数の計数の読み |
| (3) core ごとの DVFS の状態の設定 | B: enable と要求の書き |
| (5) 一番速い core を 100 とした相対の性能 | A の core の種類、B の最高の値と周波数、HFI の表 |

今の HAL には CPU の数と番号しか無く、MSR を読む口も無い（`include/hal/hal.h:113〜241`）。

## 3. 足す型と関数（第 3 版）

### A. topology（1 関数）

- `hal_cpu_topology(cpu, &topology)`: CPU の package・die・module・core・thread の番号、core の種類の番号、firmware の CPU の番号を返す。番号が同じなら同じ物を共有する。
- 知らない HAL（今の arm64・riscv64 など）は「CPU ごとに別の core、種類は不明」と答える。起動の後は変わらない。
- 5330 では module を「L2 cache を共有する組」から作る（CPU が module の level を直に報告しないため、design.md §2.3）。

### B. 今の CPU の性能の制御（6 関数、機構だけ）

| 関数 | 何をするか |
| --- | --- |
| `hal_cpu_perf_capabilities(&caps)` | 呼んだ CPU の制御の能力: 有る機能の flag、最低・効率・保証・最高の値（0〜255 の CPU の単位）、HFI の表の大きさと行、基準の周波数（TSC）、最高の時の周波数（CPUID.16H、第 3 版で足した） |
| `hal_cpu_perf_enable()` | 呼んだ CPU の HWP を入れる（x86 は reset まで切れない。package 全体） |
| `hal_cpu_perf_set(&req)` | 呼んだ CPU に要求を書く: 最低・最高・希望（0 は CPU に任せる）・性能と省電力の寄り（EPP） |
| `hal_cpu_perf_get(&req, &counters)` | 今の要求と、実際の周波数を出すための 2 つの計数（実際の clock と基準の clock）、能力の変化の事象 |
| `hal_cpu_perf_feedback(address)` | kernel が用意した HFI の表の page を CPU に教える（0 で止める） |
| `hal_cpu_perf_feedback_copy(buffer, size)` | HFI の表が新しく書かれていたら写し、それから CPU に次を書かせる |

「機構だけ」の意味: HAL はどの値を書くか（性能寄りか省電力寄りか）を決めない。決めるのは kernel（WS303 の strategy から WS301 の枠組みが作る要求）。今の `hal_cpu_idle_suspend` と同じ分け方。

全部「呼んだ CPU」に効く。別の CPU の設定は、kernel がその CPU を起こし（既存の `hal_cpu_notify`）、その CPU の上で呼ぶ。HAL に CPU 間の呼び出しは足さない。

## 4. 各機種の扱い

| 機種 | A topology | B 性能の制御 |
| --- | --- | --- |
| amd64 の Intel、HWP 有り（5330、SV7 は推定） | CPUID で全部 | HWP で全部（HFI は有る機械だけ） |
| amd64 の Intel、HWP 無し（第 5 世代以前）・AMD | CPUID で全部 | 全部「対応外」（HAL_ERR_UNSUPPORTED）。AMD の CPPC は後で同じ口に入れられる |
| amd64 の hypervisor の下（QEMU） | CPUID で | 全部「対応外」 |
| i386・pc98・arm64・riscv64・他（12 の HAL の directory 全部） | 既定の答え（CPU ごとに別の core） | 全部「対応外」 |

HWP の無い CPU で OS が周波数を制御する道（design.md §4.9、Q301-8）は、この差分には入れていない（推し: 枠だけ）。

## 5. 安全の仕組み

- **起動を壊さない**: CPU が MSR の読み書きを拒んだ時（#GP）、HAL は例外で止まらずに「対応外」を返す（今の HAL の MSR の読み書きは拒みから回復できないので、HAL の実装の中に回復の道を作る）。
- **順を守る**: Intel の手引き（SDM）は「HWP を入れる前に他の HWP の register に触るな」と書く。HAL は enable の前に能力の値を読まず（0 を返す）、要求の書き・読みは「まだ」（HAL_ERR_STATE）を返す。
- **既定で入らない**: これらを使う kernel の部分は `CONFIG_CPU_PERF` が既定 n（Q301-4「試験が通るまで無効」）。n の kernel は HAL の B を一度も呼ばず、CPU は今と同じ firmware の設定のまま。
- **仮想機械では触らない**: hypervisor の印が立つ CPU では B は全部「対応外」。
- **一度入れると切れない**: HWP の enable は reset まで戻せない（Intel の仕様）。再起動で消えるので、Debian に戻っても困らない。
- **範囲の検査**: 要求は「最低 ≤ 最小 ≤ 最大 ≤ 最高」を HAL が検め、外れたら書かない（HAL_ERR_INVALID）。
- **HFI の表**: CPU が書く memory なので、止めが終わる（HAL_OK）まで kernel は page を解放しない。

## 6. 判断の点

1. **A と B を 1 つの差分で承認するか**（推し: 1 つ）。B の値（最高の値）は core の種類（A）が無いと比べられないため、別々に承認しても B だけでは使えない。
2. **案 (a)「機構だけの口」か、案 (b)「MSR を何でも読み書きできる汎用の口」か**（推し: (a)）。(b) は HAL の関数が少なくて済むが、kernel の中に Intel・AMD の register の番号が散らばり、HAL の「CPU の register は HAL だけ」の境界が崩れる。
3. **`highest_khz`（第 3 版で足した欄）**: 5330 の firmware が P-core と E-core に同じ周波数（1600 MHz）を報告し、それでは E-core の MHz を正しく出せないため、CPU が core の種類ごとに返す最大の周波数（P 4400・E 3300 MHz）を HAL から渡す。5330 の実測と合った（design.md §2.3）。

選択肢: (A, 推し) 第 3 版をこのまま承認（p003 で実装、p002 は先に stub で進める）。(B) A（topology）だけ先に承認。(C) 今は承認しない（WS301 は HAL の要らない枠組みの host 試験まで）。
