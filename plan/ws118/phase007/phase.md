<!-- awesome-plan project=zedbsd record=ws118-p007 -->
# ws118-p007: 5320 の touchpad（Tiger Lake の LPSS I2C）、最小の修正

Status: cleared（2026-10-07 Q1 の判定: 95ff56166、build warning 0、5320 実機の kernel.log で lpss-i2c ready 120000 kHz・TPD0 0488:1024 の attach・/dev/input/event2、ユーザーの目視「タッチパッドは5320で動きました！」。割り込みでなく sampling で読む（Extended Interrupt の経路は WS177 backlog-p1））
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

## q849-i01 の結果（P3、2026-10-07）

### 確かめた事実

- 5320 の `lspci`（実機、SSH）: `00:15.0 0c80: 8086:a0e8`、`00:15.1 0c80: 8086:a0e9`（TGL-LP の LPSS I2C0・I2C1）。他の LPSS I2C の function は無い。
- Linux master の `drivers/mfd/intel-lpss-pci.c`（2026-10-07 取得）: TGL-LP の I2C は `0xa0c5`・`0xa0c6`・`0xa0d8`・`0xa0d9`・`0xa0e8`〜`0xa0eb` の 8 つで、全部 `spt_i2c_info`（`clk_rate = 120000000`）。
  ADL-P の `0x51e8`〜`0x51eb`・`0x51c5`・`0x51c6` は `bxt_i2c_info`（133 MHz）で、今までの 133 MHz と同じ。
- GPIO（`intel-gpio.c`）は INT34C5 を TGL-LP の family として既に持つ。i2c-hid の ENODEV は `drv_i2c_bus_find_acpi` が bus を見つけない所（lpss-i2c が attach しない）だけと読んだ。

### 修正（commit `95ff56166`、`src/drivers/i2c/lpss-i2c.c` だけ）

- TGL-LP の 8 つの ID を識別子の表に足し、入力 clock を表の `driver_data`（kHz）に持たせる（ADL 133000、TGL-LP 120000）。
  controller の `clock_khz` に attach で写し、SCL の count（`lpss_count`）と attach の log がそれを使う。ADL の count は以前と同じ値（同じ式・同じ 133000）。

### 確認

- `make -j16 ZEDBSD_CONFIG=plan/ws118/tests/config-remote-log.mk BUILD=build/p3-ws118 vmunix`: rc=0、warning 0、kernel include check・amd64 vmunix check PASS。vmunix sha256 `eec9c3c6a612…`。
- host 試験: lpss-i2c に host 試験は無い（追加しない、変更は表と定数）。QEMU は未実施（QEMU には LPSS が無い）。
- 5320 実機: 新しい kernel を `/esp/vmunix` に複写（今までの物は `/esp/vmunix.prev`、`vmunix.orig` は残す、`zedbsd.cfg` は不変、`cmp` 一致、umount 済み）。ユーザーが再起動（2026-10-07 18:4x JST）。
- 再起動の後の 5320 の `/var/log/kernel.log`（実機、SSH、写しは P3 の worktree の `build/p3-ws118-hw2/`）:
  `lpss-i2c: 00:15.1 ready, FIFO tx 64 rx 64, clock 120000 kHz`、`00:15.0` も同じ、
  `input: /dev/input/event2: 0488:1024 Touchpad`、`i2c-hid: \_SB_.PC00.I2C1.TPD0 0488:1024 at 0x2c on \_SB.PC00.I2C1: 0488:1024 Touchpad, 1 fingers, 1 buttons`、
  `i2c-hid: ... TPD0 samples its input (line: 6)`。`lpss-i2c: transfer ...` と `reads failed` の行は無い。`/dev/input/event2` がある。
- **ユーザーの目視（pointer が動くか）は未確認**（Q1 経由で依頼）。

### 割り込みでなく sampling の理由（直していない）

- `line: 6` は ENOENT で、TPD0 の `_CRS` に GpioInt が無いため（`/dev/acpi` で評価: I2cSerialBus 0x2c の後は Extended Interrupt `89 06 00 15 01 33 00 00 00` = APIC の IRQ 51、level・active low・shared）。
  Dell の ASL は GpioInt の版（`SBFG`）と Interrupt の版（`SBFI`）を持ち、5320 は Interrupt の版を返す。i2c-hid は GpioInt の pad しか扱わないので、sampling（指が動く間 6 ms、静止中 25 ms 毎の read）に落ちる。touchpad は動く前提。
- 付記: TGL の `\_SB.GPCL` の group の package は 7 要素（ADL は 9、`intel-gpio.c` の `GROUP_FIELDS` 9・`GROUP_FIRST_NUMBER` 8）。TGL で GpioInt の touchpad が来たら pad が見つからない（ENOENT）。5320 では使われないので直していない。
- IRQ 51 を使う割り込みの経路（i2c-hid が Extended Interrupt を `kern_irq` で取る）は最小の範囲を越えるので、必要なら別の Phase（Q1 の判断）。
