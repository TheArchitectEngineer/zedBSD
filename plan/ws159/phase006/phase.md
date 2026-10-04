<!-- awesome-plan project=zedbsd record=ws159-p006 -->

# ws159-p006: GpioInt（Intel の GPIO）で sampling をやめる

Status: in-progress（2026-10-05 P1 generation17 / q720-i01。ユーザーが hal.h に `hal_irq_set_mode` を入れた（main 6cc1bea）ので割り込みの部分を実装した。build・host の試験まで。実機（5330）の確認は未実施）
Disposition: normal
Parent: [WS159](../ws.md)
Queue: q713 / q713-i01（Q1 が 2026-10-05 に追加、電池のため、ベータ1 の範囲）→ q720 / q720-i01（割り込みの部分）

## 範囲と受け入れ（Q1 の指示）

Intel の pinctrl（INTC1055）の GPIO の driver の最小（pad の IRQ の設定・level/active low・ack、ACPI の GpioInt の source から controller を引く）、I2C-HID を割り込みで動かし sampling をやめる（割り込みが取れない時は sampling に戻る）。HAL の API は変えない。IOAPIC の pin の割り当てが要るなら止めて相談。

## 止めた点（人間の判断が要る、2026-10-05）

- 5330 の `\_SB.GPI0`（INTC1055）の `_CRS` は `Interrupt (Level, ActiveLow, Shared)`、IRQ 14（Linux: "IR-IO-APIC 14-fasteoi INTC1055:00"）。5330 の MADT（`sudo cat /sys/firmware/acpi/tables/APIC`）には IRQ 14 の Interrupt Source Override が無い（ISO は 0→2 と 9 だけ）。amd64 の HAL（`src/hal/amd64/bsp-pcat/ioapic.c` の `write_route`）は IRQ 0〜15 の極性・trigger を MADT からだけ取るので、pin 14 は ISA の既定（edge・active high）になる。level・low の出力を edge・high で受けると、割り込みは使えない。
- touchpad の pad（233、DW0 0x80800102）は GPIO driver の mode（HOSTSW_OWN=1、IOxAPIC への route なし）で、CPU への経路はこの IRQ 14 だけ。pad を IOxAPIC へ route する道（INTSEL 0x33 = GSI 51）は、HAL が GSI 0〜15 しか扱わないので取れない。
- 提案（不採用）: `hal_irq_set_trigger` の案（plan/ws159/proposed/hal-irq-trigger.diff、git の履歴に残る）。ユーザーが代わりに `hal_irq_set_mode(int irq, int trigger, int polarity)` を hal.h に入れた（2026-10-05、main 6cc1bea）。
- 別の道（HAL を変えない）: pad を ACPI mode（HOSTSW_OWN=0、GPIROUTSCI）にして GPE（SCI、IRQ 9）で受ける。firmware の所有を OS が変え、GPE の番号を NVS の対応から求めるので、推さない。

## 実装（HAL を使わない部分、2026-10-05）

- `src/drivers/gpio/intel-gpio.c`・`include/drivers/gpio/intel-gpio.h`（新）: `drv_intel_gpio_pad_find(controller, pin)` は firmware の Intel の reference の表 `\_SB.GPCL`（group ごとに community の offset・pad の数・最初の pad の offset・最初の GPIO 番号）と `\SBRG`（sideband の base）から pin の group と pad を引き、DW0 を SBRG＋community＋pad の offset＋16×pad と求め、controller の `_CRS` の memory の範囲の中にあることを確かめて map する。`drv_intel_gpio_pad_level()` は DW0 の bit 1（線の上の入力、反転の前）を返す。5330 では pin 327 = group 14（最初の番号 320）の pad 7、DW0 は 0xfd6a0ae0（`/proc/iomem` の `fd6a0000-fd6affff : INTC1055:00` の中）。
- `src/drivers/i2c/i2c-hid.c`: `_CRS` の GpioInt（controller・pin・極性）を覚え、start の後に pad を探す。見つかれば `watch_line()`: 4 ms ごとに pad を見て、line が assert（active low なら 0）の間だけ input の register を読む（一度に最大 8 回）。見つからなければ今までの `sample()`（6/25 ms）。log `i2c-hid: … reads on its line (\_SB.GPI0 pin 327)` か `… samples its input (line: N)`。
- build: `platform/amd64/vmunix.mk` の `AMD64_I2C_SOURCES` に intel-gpio.c（ACPI の時）。

## 確認

- `plan/ws159/tests/run-host-intel-gpio.sh`: ok（19 checks、ASan・UBSan でも ok）。5330 の GPCL の 18 group・SBRG 0xfd000000・GPI0 の 4 つの範囲で、pin 327 が DW0 0xfd6a0ae0 の page を map、DW0 0x80800102 は high・bit 1 を消すと low、pin 400 は group 無しで拒む、範囲の外は拒む。
- `plan/ws159/tests/run-host-i2c-hid.sh`: line の run は ok（71 checks、line が idle の間は 1 回も読まない）、sample の run は ok（80 checks）。ASan・UBSan でも ok。
- vmunix（CI の config、LPSS y）warning 0・check PASS。style-check: 新しい file 0（host の試験の setjmp の 1 件は C の規則で条件の中）。
- 未実施: QEMU（LPSS も INTC1055 も無いので、起動が変わらないことだけ）、実機（5330 で `reads on its line` が出て、触れた時だけ読むこと）。

## 再開の条件

- ユーザーが HAL の案を承認したら: `hal_irq_set_trigger` を実装（amd64 の ioapic.c・irq.c、他の arch は UNSUPPORTED）、kernel の wrapper、intel-gpio に GPI_IS・GPI_IE（GPCL の field 6・7）と IRQ 14 の handler（共有、pad の status を見て ack、I2C-HID の thread を起こす）、I2C-HID の `watch_line` を割り込みの待ちに替える（取れない時は今の監視に戻る）。
- 承認されなければ: 今の 4 ms の監視のまま（I2C の転送は触れた時だけ）で、電池の残りを記録して閉じるかを Q1・ユーザーが決める。

## 割り込みの実装（2026-10-05 P1 / q720-i01）

ユーザーが hal.h に `hal_irq_set_mode(irq, trigger, polarity)`（HAL_IRQ_TRIGGER_EDGE/LEVEL、HAL_IRQ_POLARITY_HIGH/LOW、mask の間に変える、表せなければ HAL_ERR_UNSUPPORTED）を入れた。hal.h は変えずに実装した:

| 所 | 内容 |
| --- | --- |
| `src/hal/amd64/bsp-pcat/ioapic.c`・`.h` | `amd64_ioapic_set_mode`: ISA の route（IRQ 0〜15）の MPS INTI flags（極性 bit 0-1、trigger bit 2-3）を置き換え、masked で書き直す（失敗なら元の flags に戻す）。以後の mask・unmask・route も新しい flags で書く |
| `src/hal/amd64/irq.c` | `hal_irq_set_mode`: 外の line の IRQ（timer・範囲外は INVALID、MSI は UNSUPPORTED）、service の lock の下で配達中なら BUSY、mask して書き換え、mask の状態を戻す |
| `src/hal/i386/irq.c`・`arm64/irq.c`・`sparcv9/irq.c`・`m68k/bsp-x68k/irq.c` | 引数を確かめて HAL_ERR_UNSUPPORTED（設定を変えられない） |
| `include/kern/irq.h`・`src/kern/irq.c` | `kern_irq_set_mode(irq, KERN_IRQ_TRIGGER_*, KERN_IRQ_POLARITY_*)`（HAL の値に写し、errno で返す: EINVAL・EBUSY・ENOTSUP） |
| `src/drivers/gpio/intel-gpio.c`・`.h` | pad の割り込み: `drv_intel_gpio_pad_irq_enable(pad, handler, argument)`・`_arm`・`_alive`。controller の `_HID` が Tiger Lake LP の系統（INTC1055、INT34C5。Linux の pinctrl-tigerlake と同じ register: HOSTSW_OWN 0xb0、GPI_IS 0x100、GPI_IE 0x120、group ごとに 4 byte、group の番号は GPCL の 4 番目の field の HOSTSW_OWN の位置から）でなければ ENOTSUP。pad の HOSTSW_OWN が 0（firmware の物）なら EPERM。controller の `_CRS` の Interrupt（5330: 14、level、active low）を `kern_irq_register` → `kern_irq_set_mode` → unmask。handler（割り込みの文脈）は、GPI_IS と GPI_IE の両方が立つ pad ごとに GPI_IE の bit を消し GPI_IS を 1 書きで消して user の handler を呼び、EOI。どの pad でもない発火は line を mask して諦め（firmware が有効にした pad の無限の発火を防ぐ）、`_alive` が 0 になる。5330 では pin 327 の GPI_IS は 0xfd6a010c、GPI_IE は 0xfd6a012c（DW0 と同じ page） |
| `src/drivers/i2c/i2c-hid.c` | pad が見つかれば割り込みを有効にし、`wait_interrupt`: 発火か 1 秒（`I2C_HID_IRQ_CHECK_MS`）まで眠り、line が assert の間読み（8 回ごとに 1 ms 譲る）、pad を arm し直す。割り込みが諦められたら今の 4 ms の監視へ。有効にできなければ（ENOTSUP・EPERM・mode の失敗）最初から 4 ms の監視。log `i2c-hid: … reads on its interrupt (…)` か `… reads on its line (…; interrupt: N)` |
| `plan/ws159/proposed/hal-irq-trigger.diff` | 不採用の案を削除（git の履歴に残る） |

### 確認（2026-10-05）

| 確認 | 結果 |
| --- | --- |
| amd64 の vmunix（`config/ci/config-amd64.mk`、kernel include check を含む） | 成功、warning 0 |
| pcat（i386）・pc98・rpi4（arm64）の vmunix（`config/ci/config-*.mk`） | 成功（PC/AT の contract OK、pc98 の stage2、arm64 vmunix check PASS） |
| sparcv9（sun4u）・x68k の vmunix | **link まで行かない（この変更の前から）**: main で `src/hal/pmem-constraints.c` が無く（make の規則が無い）、`src/hal/sparcv9/irq.c`・`m68k/bsp-x68k/irq.c` は `hal_irq_get_affinity` の型が今の hal.h と違って compile できない。足した `hal_irq_set_mode` には error が無い（compiler の error は全部 `hal_irq_affinity`） |
| `plan/ws159/tests/run-host-intel-gpio.sh`（ASan・UBSan でも） | mode の run 56 checks、nomode の run 32 checks。別の系統・firmware の pad は拒む、IRQ 14 を level・low で取り unmask、GPI_IE の bit 7、発火で user の handler 1 回・GPI_IE の bit が消え GPI_IS の bit 7 だけ消える・EOI、arm で戻る、どの pad でもない発火で mask と諦め。nomode では有効にならず IRQ 14 は返され unmask されない |
| `plan/ws159/tests/run-host-i2c-hid.sh`（ASan・UBSan でも） | line 74・sample 81・irq 75 checks。irq: 有効・発火・arm、眠りは最大 1 秒、line が idle の間の読みは 0 |
| style-check | 新しい・変えた所の指摘 0（host の試験の setjmp は従来の注記どおり） |
| QEMU | **未実施**（QEMU に INTC1055 も LPSS も無い。起動が変わらないことを T1 の boot-test で見る） |
| 実機（5330） | **未実施**。見る物: log `intel-gpio: pin 327 interrupts through irq 14` と `i2c-hid: … reads on its interrupt`、指を置いた時だけ I2C を読む（`irq 14 fired for no pad` が出ないこと）、操作の遅れ、電池の減り |

### 残り

- 5330 での確認（Q1 経由、ユーザーの操作）。`irq 14 fired for no pad` が出たら、firmware が有効にした別の pad がある（GPI_IE を読んで調べる）。
- sparcv9・x68k の build が main で壊れているのは別件（この Phase の範囲の外）。
