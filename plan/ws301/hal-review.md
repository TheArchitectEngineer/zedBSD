<!-- awesome-plan project=zedbsd record=ws301-hal-review -->

# WS301 と HAL（ユーザーのレビュー用、Q301-1）

2026-10-29 P5（改訂 1: ユーザーの Q301-8 の答え「P-stateを操作しますが、HALではなくドライバでできるはずです。」を受けて、推しを「HAL を変えない」に替えた）。設計の本文は [design.md](design.md) §4.1・§4.2・§4.9。

## 1. 用語

- **HAL**: zedBSD の kernel と CPU・board の間の層。HAL の関数の宣言（`include/hal/hal.h`）を変えるにはユーザーの承認が要る。HAL の役目は「POSIX の kernel に要る低い層の操作」（`hal.h` の先頭の comment）。
- **driver**: kernel の中で特定の hardware を扱う部分。platform の driver（`src/drivers/platform/pcat/` など）は、その platform の CPU の命令を直に使ってよい（前例: PC の IDE の driver の port の命令、Pi 5 の driver の system register の命令）。
- **DVFS**: CPU の周波数（と電圧）を負荷に合わせて変える事。
- **HWP**: Intel の CPU の機能（PC 向けは 2015 年の第 6 世代以降、server の一部は Broadwell の Xeon D・E5 v4 から）。OS は「この範囲で、性能寄りか省電力寄りか」を伝えるだけで、細かい周波数は CPU が決める。5330 は持つ。
- **P-state**（legacy）: HWP の前からある方式。CPU の選べる周波数の段（例 0.4〜2.5 GHz の 100 MHz 刻み）から OS が段を選んで書く。OS が負荷を見て段を上げ下げする（governor）。
- **MSR**: CPU の中の設定の register。HWP も P-state も MSR に書く。論理 CPU ごとにあり、その CPU の上で走る命令でしか読み書きできない。CPU に無い MSR に触ると例外（#GP）が起き、kernel の中では止まる。
- **CPUID**: CPU に「どの機能を持つか」を聞く命令。MSR が有るかどうかも CPUID の bit で分かる。
- **topology**: どの CPU がどの core・束（module）・package に属するか。5330 は P-core 2 つ（各 2 thread）と E-core 8 つ（4 つずつ 2 束）、論理 CPU 12。
- **HFI**: CPU が「今それぞれの core がどれだけ速く・効率よく動けるか」の表を memory に書く仕組み。

## 2. 推し: HAL を変えない

WS301 が要る CPU の操作（topology の読み、HWP、legacy の P-state、HFI）は、全部 **x86 の CPU の性能の driver**（`src/drivers/platform/pcat/` に新しく置く）が CPUID・MSR の命令を自分で実行して行う。HAL の宣言は 1 つも足さない。

| 要る事 | どうするか | 使う既存の口 |
| --- | --- | --- |
| 別の CPU の MSR を読み書きする | kernel がその CPU を起こし、その CPU の上で driver の関数を走らせる | `hal_cpu_notify`（既存の HAL の関数）と kernel の `kernel_cpu_notify_handler` |
| topology（core・束・package・core の種類） | driver が各 CPU の上で CPUID を読む | 同上 |
| CPU と firmware の CPU の番号の対応 | driver が ACPI の MADT の表を読む | ACPI の driver の表の貸し（既存） |
| HWP・P-state・HFI | driver が MSR を書く・読む | 同上 |
| HFI の表の memory | kernel の物理の page | 既存の kernel の memory の口 |

理由:
1. ユーザーの方向（Q301-8 の原文）。
2. CPU の性能の制御は HAL の役目（POSIX の kernel に要る低い層）の外。
3. platform の driver が CPU の命令を直に使う前例がある。
4. HAL の API が増えない（第 3 版の案は 7 つ増えた）。全 12 の HAL の directory に既定の答えを書く作業も要らない。

## 3. 安全の仕組み（HAL を使わない時に失う物と、その代わり）

失う物: 第 3 版の案では、CPU が MSR の操作を拒んだ時（#GP）に HAL が受け止めて「対応外」で返せた。driver が直に命令を使うと、それは無い（kernel が止まる）。

代わりの関門（driver は次を全部満たす MSR にだけ触る）:
1. **仮想機械では触らない**: CPUID の hypervisor の印が立つ CPU では driver を動かさない（QEMU の上では何もしない）。
2. **CPU が持つと言う物だけ**: Intel・AMD が仕様で定めた（architectural な）MSR を、それを示す CPUID の bit が立つ時だけ（HWP、HFI、周波数の計数、Intel の P-state、AMD の P-state）。
3. **model ごとの MSR は表にある CPU だけ**: 仕様で定まらない MSR（Intel の周波数の段の範囲 0xCE など）は、Intel の手引き（SDM Vol 4）の表にその MSR がある CPU の model だけ。
4. **順を守る**: HWP は enable の前に他の HWP の MSR に触らない（Intel の手引きの規則）。
5. **値を検める**: 書く値は CPU の報告した範囲の中だけ。
6. **既定で入らない**: `CONFIG_CPU_PERF` は既定 n（Q301-4「試験が通るまで無効」）。n の kernel は MSR に一切触らない。y の kernel でも起動の parameter `cpuperf=off` で止められる。
7. **実機で両方の道を確かめる**: 5330 で HWP の道と、HWP を入れない起動（`cpuperf.hwp=off`）での P-state の道を試す（p007）。

関門を通っても CPU が拒めば止まる、という残りの危険は、上の 2・3 で十分に小さいと見る（推定。根拠は CPU の仕様が「その bit が立てば MSR が有る」と定める事）。

- **周波数の推定に CPU の「最大の周波数」の欄（CPUID.16H）を使わない**: Intel の手引きが「表示の用途だけ」と書くため。最大の周波数は起動の時に短く測る（Q301-2 の「最大の周波数の比」はこの測りで出す）。

## 4. 各機種の扱い

| 機種 | 扱い |
| --- | --- |
| Intel、HWP 有り（5330、SV7 は推定） | HWP の道。HFI が有れば HFI も |
| Intel、HWP 無し（第 5 世代以前） | P-state の道: firmware の表（`_PSS`、`_OSC` 無しで読める物）か、表にある model なら CPU の 0xCE から段を作り、OS の governor が段を選ぶ |
| AMD | P-state の道（段の番号だけ、周波数は firmware の表が無ければ不明）。試す機械が無いので host 試験だけ。AMD の CPPC（HWP に似た物）は後で同じ driver に足せる |
| 仮想機械の中（QEMU） | 何もしない（表は出す、設定は「対応外」） |
| arm64・riscv64 | この WS は枠だけ（Q301-5）。後の WS で各 board の driver（Pi の firmware、VF2 の clock）が同じ口に入る |

## 5. 判断の点（Q301-1）

- (A, 推し) **HAL を変えない**（上の 2〜4）。
- (B) topology だけ HAL に足す。topology は WS302（thread の置き場の判断）も使い、arm64 の device tree の topology も HAL が持つ方が揃う。WS302 が `CONFIG_CPU_PERF` と別に topology を要るなら意味がある（WS302 の設計で分かる）。
- (C) 第 3 版の差分（HAL が性能の制御の口を持ち、#GP を受け止める）を承認する。安全は一段高いが、HAL の API が 7 つ増え、ユーザーの Q301-8 の方向と違う。差分は [proposed/hal-cpu-perf.diff](proposed/hal-cpu-perf.diff) に記録として残す（当てない）。
