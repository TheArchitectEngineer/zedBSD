<!-- awesome-plan project=zedbsd record=ws159-p002 -->

# ws159-p002: LPSS の DesignWare I2C と ACPI の I2cSerialBus・GpioInt

Status: in-progress（2026-10-05 P1 generation17 / q713-i01。実装・host の試験・build まで。実機は p003 と一緒、QEMU は T1 の試験待ち）
Disposition: normal
Parent: [WS159](../ws.md)
Queue: q713 / q713-i01（設計 [p001](../phase001/phase.md) の D2・D3）

## 範囲と受け入れ

- D3: `drv_acpi_resources_walk` が I2cSerialBusV2（address・速度・10-bit・controller の path）と GpioInt（pin・level/edge・極性・shared・wake・controller の path）を返す。
- D2: Alder Lake の LPSS の I2C（PCI 8086:51e8〜51eb・51c5・51c6）を D0 にし、DesignWare の core を master として動かす。I2C の bus の registry と、ACPI の path から bus を引く口。
- 受け入れ: host の ASL の試験と 5330 の実 table の `_CRS`、vmunix の build（warning 0）、style-check 0。実機で I2C1 の上の 0x2c から HID descriptor を読めること（p003 の UAT）。QEMU には LPSS が無いので、何も bind せず起動が変わらないこと（T1）。

## 実装（2026-10-05）

| file | 内容 |
| --- | --- |
| `include/drivers/acpi/acpi.h`・`src/drivers/acpi/acpi-resource.c` | `DRV_ACPI_RESOURCE_I2C`・`DRV_ACPI_RESOURCE_GPIO_INT`、resource に `source`・`speed`・`wake`・`ten_bit`。tag 0x8E（I2C だけ。SPI・UART は飛ばす）と 0x8C（interrupt だけ。GPIO の IO は飛ばす）を decode する |
| `include/drivers/i2c/i2c.h`・`src/drivers/i2c/i2c.c` | bus の registry（最大 8、bus ごとの mutex で転送を 1 つずつ）、`drv_i2c_bus_find_acpi(path)`（path の `_ADR` から PCI の device/function を引き、root bus 0 の bus と照合する）、`drv_i2c_transfer()` |
| `include/drivers/i2c/lpss-i2c.h`・`src/drivers/i2c/lpss-i2c.c` | PMCSR を D0（10 ms の回復）、BAR0 を uncached で map、LPSS の private の reset（0x204 = 0x7）、`IC_COMP_TYPE` = 0x44570140 の確認、FIFO の深さ、133 MHz から SCL の count（Fast は 750/1500 ns = 100/200、Standard は 4150/4900 ns）。転送は IC_DATA_CMD に command を積み（読みは RX の FIFO に入る分だけ）、何も動かない時は 1 tick 眠る。NACK は ENXIO、他の abort は EIO、100 ms で ETIMEDOUT。controller の割り込みは使わない |
| build | root の `Makefile`（`CONFIG_DRIVER_PCI_LPSS_I2C`、amd64 で既定 y、`-D`）、`platform/amd64/vmunix.mk`、`config/drivers/pci.drivers`、`src/kern/platform/pcat.c` の登録（Q1 がレビューして統合） |

## 確認

- `plan/ws049/tests/run-asl.py`: 21 passed（新しい `connection` の試験を含む: I2C 0x2C/400 kHz/`\_SB.PC00.I2C1`、GpioInt pin 0x147・level・low・wake、SPI と GpioIo は飛ばす、edge・shared の GpioInt、10-bit の I2C）。
- 5330 の DSDT・SSDT（`plan/ws049/tests/latitude5330/`）を harness で `--reg --init` の後に `\_SB.PC00.I2C1.TPD0` の `_CRS` を歩いた: `RESOURCE i2c address=0x2C speed=400000 … source=\_SB.PC00.I2C1`、`RESOURCE gpio-int … level=1 low=1 wake=1 source=\_SB.GPI0`（pin は host の NVS がゼロなので 0）。
- vmunix（`config/ci/config-amd64.mk`）warning 0・vmunix check PASS、style-check 0、menuconfig の round-trip PASS。Q1 が check-latitude5330 を再実行して PASS。
- 未実施: QEMU（T1）、実機。

## 残り

- controller の割り込み（PCI の INTx）は使っていない。転送の待ちは 1 tick の sleep。
- ACPI の `FMCN`・`SSCN` が有る機械の timing は未対応（5330 の I2C1 には無い）。
