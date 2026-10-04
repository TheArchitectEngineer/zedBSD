<!-- awesome-plan project=zedbsd record=ws159-p006 -->

# ws159-p006: GpioInt（Intel の GPIO）で sampling をやめる

Status: uncleared（2026-10-05 P1 generation17 / q713-i01。HAL を使わない部分は実装・host の試験・build まで。割り込みの部分は HAL の API の変更が要り、ユーザーの承認待ち）
Disposition: normal
Parent: [WS159](../ws.md)
Queue: q713 / q713-i01（Q1 が 2026-10-05 に追加、電池のため、ベータ1 の範囲）

## 範囲と受け入れ（Q1 の指示）

Intel の pinctrl（INTC1055）の GPIO の driver の最小（pad の IRQ の設定・level/active low・ack、ACPI の GpioInt の source から controller を引く）、I2C-HID を割り込みで動かし sampling をやめる（割り込みが取れない時は sampling に戻る）。HAL の API は変えない。IOAPIC の pin の割り当てが要るなら止めて相談。

## 止めた点（人間の判断が要る、2026-10-05）

- 5330 の `\_SB.GPI0`（INTC1055）の `_CRS` は `Interrupt (Level, ActiveLow, Shared)`、IRQ 14（Linux: "IR-IO-APIC 14-fasteoi INTC1055:00"）。5330 の MADT（`sudo cat /sys/firmware/acpi/tables/APIC`）には IRQ 14 の Interrupt Source Override が無い（ISO は 0→2 と 9 だけ）。amd64 の HAL（`src/hal/amd64/bsp-pcat/ioapic.c` の `write_route`）は IRQ 0〜15 の極性・trigger を MADT からだけ取るので、pin 14 は ISA の既定（edge・active high）になる。level・low の出力を edge・high で受けると、割り込みは使えない。
- touchpad の pad（233、DW0 0x80800102）は GPIO driver の mode（HOSTSW_OWN=1、IOxAPIC への route なし）で、CPU への経路はこの IRQ 14 だけ。pad を IOxAPIC へ route する道（INTSEL 0x33 = GSI 51）は、HAL が GSI 0〜15 しか扱わないので取れない。
- 提案（適用していない）: `include/hal/hal.h` に `hal_irq_set_trigger(int irq_num, unsigned flags)` と `HAL_IRQ_TRIGGER_LEVEL`・`HAL_IRQ_TRIGGER_ACTIVE_LOW`。差分の案は [proposed/hal-irq-trigger.diff](../proposed/hal-irq-trigger.diff)。Q1 が master の pending-decisions に記録（ユーザーの承認待ち）。
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
