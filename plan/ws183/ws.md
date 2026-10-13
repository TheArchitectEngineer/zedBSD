<!-- awesome-plan project=zedbsd record=ws183 -->

# WS183: I2C HID の割り込み（Extended Interrupt）と Tiger Lake の GPIO の group

<!-- awesome-plan-current:start -->
Status: completed（2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」。全 Phase が cleared。旧: incomplete、p001 は 5320 の実機の確認待ち）
Primary Milestone: MG003
Related Milestones: —
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
<!-- awesome-plan-current:end -->

## 目的

ユーザー（2026-10-07）「タッチパッドは独立WSにして、ほかベータ2WSと同じ優先度で後回しにします。」
5320 の touchpad（TPD0、0488:1024）の `_CRS` は GpioInt でなく Extended Interrupt（APIC IRQ 51、level、active low）なので、i2c-hid が sampling（6・25 ms）で読んでいた。i2c-hid に Extended Interrupt の経路を足して割り込みで読み、Tiger Lake の `\_SB.GPCL` の 7 要素の group の package を扱う。2026-10-08 の UAT の 1 本指のタップのクリックの遅れも直す。

## 結果（2026-10-13 完了）

- `src/drivers/i2c/i2c-hid.c`: GpioInt の pad が無く Interrupt がある時、`kern_irq_register`・`kern_irq_set_mode` で level・active low の割り込みで読む（HAL の API は変えていない）。
- `src/drivers/gpio/intel-gpio.c`: group の package が 9 要素（ADL）なら第 9 要素、7 要素（TGL）なら表の順番×32 を最初の GPIO 番号にする。host 試験は `plan/ws159/tests/host-intel-gpio.c` の `tgl` の run（11 checks）。
- 1 本指のタップのクリック（p002）: 離した時に押す・離すを続けて送り、TAP_DRAG_MS の内の次の touch で押し直してドラッグにする（`touchpad.c`）。5330 で「ドックしたウィンドウをフローティングに戻すダブルタップは遅延がなかった」（2026-10-10 ユーザー）。
- 規約の全文の見直し（p003、fbd1da9e7）。vmunix の build warning 0、style-check 0。
- 5320 の実機の確認（p001）: 2026-10-13 ユーザーの指示（上の原文）で確認済みの扱い。

## 制限・移管

- i2c-hid の host 試験は無い。QEMU に I2C HID は無い。5320 の kernel.log の `reads on its interrupt (irq 51 …)` の記録は無い。
- TGL の 7 要素の第 5〜7 の意味と番号の規則（表の順番×32）は 5320 の `\_SB.GPCL` の dump で確かめていない（5320 の touchpad はこの経路を使わない）。
- 5330 の touchpad の GPIO の割り込みが止まる件は [BUG-261](../bugs/BUG-261.md)。

## Phase

| Phase | 内容 | 最終の状態 |
| --- | --- | --- |
| ws183-p001 | i2c-hid の Extended Interrupt の経路と TGL の GPIO の group、5320 の実機で確認 | cleared（2026-10-13 ユーザー、上の原文） |
| ws183-p002 | 1 本指のタップのクリックの遅れ | cleared（2026-10-10 ユーザー「これで一通りUATの確認事項は確認したと思います。CloseできるものはCloseしましょう。…」） |
| ws183-p003 | 規約の全文の見直し（WS183 が変えた C） | cleared（2026-10-09 Q1、fbd1da9e7） |

Phase の directory は、完了の規則（AGENTS.md）で削除する（git の履歴に残る、削除は Q1）。この WS だけの試験の directory は無い（試験は WS159 の `tests/` にある）。
