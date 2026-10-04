<!-- awesome-plan project=zedbsd record=ws052p002 -->

# ws052-p002: 詳細の調査と HAL の差分の案

Phase ID: `ws052-p002`
Parent: [WS052](../ws.md)
Status: in-progress（2026-10-05 P1 generation17 / q727。HAL の差分の案を置いた。承認と FACP・LPIT の取り出しの許可を待つ）
Phase disposition: normal
Queue: q727（Q1 の投入、ベータ2 の準備）

## 範囲

- HAL の API の差分の案（design §6: MWAIT の idle、tick の停止と再開、AP の停止、wake の割り込みの mask）を `plan/ws052/proposed/` に差分ごとに置く。
  `include/hal/hal.h` は変えない。承認は差分ごと。
- §10 の決定（入れない device は中止して理由を返す、i915・NVMe・xHCI は必須で HDA・Wi-Fi は後（止めれば入れる）、契機は蓋・電源ボタンの短押し・
  無操作の時間）に合わせて p002 以降の Phase の設計を更新する。
- code は書かない（`src/hal` の実装の準備も、承認の後）。

## 結果（2026-10-05）

- [proposed/README.md](../proposed/README.md) と 4 つの差分（どれも `include/hal/hal.h` だけ、今の tree に `patch --dry-run` で当たることを確かめた）:

| # | 差分 | 内容 |
| --- | --- | --- |
| H1 | `hal-cpu-idle-deep.diff` | `int hal_cpu_idle_deep(uint32_t hint)`: 最も深い C-state の idle（hint は `_CST` の FFixedHW の MWAIT の hint、0 で HAL が CPUID leaf 5 から選ぶ）。深い state が無ければ `HAL_ERR_UNSUPPORTED` |
| H2 | `hal-timer-stop.diff` | `int hal_timer_stop(void)`・`int hal_timer_resume(void)`: 今の CPU の periodic の tick の停止と再開。`hal_rtc_read_counter` は数え続ける（無理なら `HAL_ERR_UNSUPPORTED`） |
| H3 | `hal-cpu-notify-wake.diff` | `hal_cpu_notify()` の契約の明記（API の追加なし）: H1 の深い idle・tick の停止中・H4 の mask 中も必ず起こす。AP の停止は新しい API にせず、kernel が各 CPU の idle で H2・H1 を使い notify で起こす |
| H4 | `hal-irq-suspend.diff` | `int hal_irq_suspend(const int *wake_irqs, unsigned count)`・`int hal_irq_resume(void)`: wake の源以外（HAL の内部の LAPIC の LVT、登録の無い I/O APIC の pin を含む）を止め、元の mask を HAL の中に保って戻す |

- 当初の案（design §6）の「AP の停止」は、新しい API の代わりに H3 の契約の明記にした（Linux の suspend-to-idle も CPU を offline にせず、各 CPU を
  深い idle に置く）。HAL の API の追加が 3 つ（H1・H2・H4）と契約の明記が 1 つ（H3）になる。
- [design.md](../design.md) の §6 に差分の案、§9 の Phase を §10 の決定に合わせて p002〜p008 に改訂（p004 は必須の 3 device と中止の規則、
  p005 は HDA・Wi-Fi などの「止めて入る」経路、p006 は承認された HAL の差分と入口・出口と `/dev/system`、p007 は Keiland の 3 つの契機）。
  [ws.md](../ws.md) の Phase の表も合わせた。

## 人間の判断が要る点

- H1〜H4 の承認（差分ごと）。
- design §10-4: 5330 の FACP・LPIT を 5330 の Linux から読み取り専用で取り出す許可（`_CST` の C10 の hint、LPIT の SLP_S0 の counter、FADT の
  `LOW_POWER_S0_IDLE_CAPABLE` を確かめるため）。

## 残り（p002 の中）

- 許可の後: FACP・LPIT の読み、`_PRW` の wake の GPE の一覧（DSDT から）、`_CST` の hint、PMC の SLP_S0 の residency の register の所在。
- 承認の後: p006 で H1〜H4 の amd64 の実装（他の architecture は `HAL_ERR_UNSUPPORTED`）。

## 確認

- 差分は `include/hal/hal.h` に `patch -p1 --dry-run` で当たる（4 つとも、2026-10-05 の main で）。build は無し（code を書いていない）。
