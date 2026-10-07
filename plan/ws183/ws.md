<!-- awesome-plan project=zedbsd record=ws183 -->

# WS183: I2C HID の割り込み（Extended Interrupt）と Tiger Lake の GPIO の group

<!-- awesome-plan-current:start -->
Status: planning
Primary Milestone: MG003
Related Milestones: —
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Target: ベータ2（他のベータ2 の WS と同じ優先度、後回し。2026-10-07 ユーザー）
Resume point: p001 から。
<!-- awesome-plan-current:end -->

## 目標（2026-10-07 ユーザー）

「タッチパッドは独立WSにして、ほかベータ2WSと同じ優先度で後回しにします。」

- 5320 の touchpad（TPD0、0488:1024）の `_CRS` は GpioInt でなく Extended Interrupt（APIC IRQ 51、level、active low）。i2c-hid は GpioInt だけを扱うので sampling（6・25 ms）で読んでいる（[ws118-p007](../ws118/phase007/phase.md)）。i2c-hid に Extended Interrupt の経路（`hal_irq_set_mode` で level・active low）を足し、割り込みで読む。
- Tiger Lake の `\_SB.GPCL` の group の package は 7 要素で、`intel-gpio.c` の GROUP_FIELDS 9・GROUP_FIRST_NUMBER 8（ADL の形）と合わない。TGL で GpioInt の機器の pad が見つかるようにする。
- BUG-247（bar の 2 回 tap）の原因が sampling なら、ここで直る。

## Phase

| Phase | 目的 | Status | 依存 |
| --- | --- | --- | --- |
| p001 | i2c-hid の Extended Interrupt の経路と TGL の GPIO の group、5320 の実機で確認 | planned | — |
