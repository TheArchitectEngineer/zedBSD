<!-- awesome-plan project=zedbsd record=ws183-p001 -->
# ws183-p001: i2c-hid の Extended Interrupt の経路と Tiger Lake の GPIO の group

Status: test-wait（2026-10-08 P1: 実装と build・host 試験まで。5320 の実機の確認待ち（ユーザーの時期））
Disposition: normal
Parent: [WS183](../ws.md)
Queue: q858 の後の P1 の列（Q1、ユーザーの優先順の 6 番）

## 範囲

1. i2c-hid: `_CRS` の Interrupt（Extended Interrupt、APIC の line）で読む（GpioInt の pad が無い時）。
2. intel-gpio: Tiger Lake の `\_SB.GPCL` の 7 要素の group の package。
3. HAL の API は変えない（kernel の `kern_irq_register`・`kern_irq_set_mode`・`kern_irq_mask`・`kern_irq_unmask` を使う）。

## 実装（2026-10-08、P1）

- `src/drivers/i2c/i2c-hid.c`: `resource_visitor` が最初の consumer の `DRV_ACPI_RESOURCE_IRQ`（番号・level・active low）を覚える（`device->irq`、無ければ -1）。`worker` は GpioInt の pad が無く Interrupt がある時、`irq_take`（`kern_irq_register` → `kern_irq_set_mode`（_CRS のとおり、5320 は level・active low）→ unmask、失敗なら返す）→ `wait_irq`。handler `irq_interrupt` は line を mask して thread を起こし EOI。`wait_irq` は発火か 1 秒（`I2C_HID_IRQ_CHECK_MS`、失った発火の見回り）で起き、空の report まで最大 16 回読み、発火の後なら unmask（level が立ったままならすぐまた来る）。読む物の無い発火が 200 回続いたら line を手放して sampling に戻る（共有の line を他の機器が鳴らし続ける時）。log: `i2c-hid: … reads on its interrupt (irq 51 level=1 active_low=1)`、取れない・手放した時は `samples its input (irq 51: N)`。
- `src/drivers/gpio/intel-gpio.c`: group の package が 9 要素（ADL）なら今どおり第 9 要素が最初の GPIO 番号、7 要素（TGL）なら表の順番×32（Linux の pinctrl-tigerlake の TGL-LP の group の base 0・32・64…が community の順に続くのと同じ）。`group_field` は 7 要素から受ける。
- 試験: `plan/ws159/tests/host-intel-gpio.c` に `tgl` の run（5330 の表を 7 要素にして、pin 327 が第 11 の group の pad 7、DW0 0xfd6d0c40 の page、pin 700 は拒む）、`run-host-intel-gpio.sh` が流す。

## 確認

- `make -j16 vmunix`（worktree の build/amd64）: exit 0、warning 0、kernel include check PASS、amd64 vmunix check PASS。
- `CC=clang sh plan/ws159/tests/run-host-intel-gpio.sh`: `ok (nomode=0, 56 checks)`・`ok (nomode=1, 32 checks)`・`ok (tgl, 11 checks)`、`EXTRA_CFLAGS=-fsanitize=address,undefined` でも同じ。
- style-check 0（変えた 3 file）。
- i2c-hid の host 試験は無い（追加しない）。QEMU に I2C HID は無い（未実施）。

## 未実施・未確認

- 5320 の実機（ユーザーの時期）: kernel.log に `reads on its interrupt (irq 51 level=1 active_low=1)` が出て `samples its input` が無いこと、touchpad の動きと tap、BUG-247（bar の 2 回 tap）が出なくなるか。IRQ 51（GSI 51）が IOAPIC で取れるか（`kern_irq_register` の失敗なら `samples its input (irq 51: N)`）。
- TGL の 7 要素の第 5〜7 の意味と番号の規則（表の順番×32）は 5320 の `\_SB.GPCL` の dump で確かめていない（5320 の touchpad は GpioInt でないので、この経路は 5320 では使われない）。
