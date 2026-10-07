<!-- awesome-plan project=zedbsd record=ws118-p007 -->
# ws118-p007: 5320 の touchpad（Tiger Lake の LPSS I2C）、最小の修正

Status: in-progress（q849-i01、P3、2026-10-07）
Disposition: normal
Parent: [WS118](../ws.md)
Queue: q849

## 由来

2026-10-07 ユーザー:「5320ではタッチパッドが使えないみたいです。そこだけ最小限の修正で対処できそうですか？」

## Q1 の調査（5320 の /var/log/kernel.log）

- `i2c-hid: \_SB_.PC00.I2C1.TPD0 not taken (13)`（13 = ENODEV）。input は PS/2 の mouse・keyboard（event0・1）だけ。
- `src/drivers/i2c/lpss-i2c.c` は Alder Lake の PCI ID（0x51e8〜0x51eb・0x51c5・0x51c6）だけを持ち、input の clock を 133 MHz（ADL）に固定。Tiger Lake-LP の LPSS I2C（Linux の intel-lpss-pci: 0xa0e8〜0xa0eb・0xa0c5・0xa0c6）は attach されず、`drv_i2c_bus_find_acpi` が ENODEV。
- GPIO（`src/drivers/gpio/intel-gpio.c`）は INT34C5（TGL）を既に持つ。

## 範囲（最小）

1. lpss-i2c に TGL-LP の ID と、その input clock（Linux の該当の platform info の値を確かめる）を足す。ADL の挙動は変えない。
2. i2c-hid が 5320 の TPD0 を取り、touchpad の event が出るかを実機で確かめる（kernel の入れ替えは Q1 経由、再起動はユーザー）。他に止まる所があれば最小に直すか、Q1 に返す。

## 受け入れ

- build warning 0、短い host 試験（あれば）。
- 実機 5320: kernel.log に TPD0 の attach、/dev/input に touchpad、ユーザーが touchpad で pointer を動かせる。
