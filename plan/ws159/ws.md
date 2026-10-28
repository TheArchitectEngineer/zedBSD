<!-- awesome-plan project=zedbsd record=ws159 -->

# WS159: native のタッチパッド（Intel LPSS の I2C・ACPI の I2C-HID・HID の touchpad）と compositor のタッチパッドの層

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」。根拠: p001〜p004・p006 cleared、2026-10-08 夜 ユーザー「Touchpadは動作確認できたので、下記をすべてclose します。」、p005 の実機の UAT は 2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」）
<!-- retained-after-completion --> phase001 の phase.md は残す: src/drivers/i2c/i2c-hid.c:49・529 と userland/desktop/wayland/touchpad.c:14 の comment が設計（D1・D7）を参照している。source の comment を直す（または仕様を docs/ へ移す）までの間
Completed: 2026-10-27（q1608）
Primary Milestone: MG006
Related Milestones: MG003
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

5330 などのタッチパッドを PS/2 の互換の mouse ではなく I2C-HID の native の device として扱い、多点（MT）と clickpad の押し込みを kernel の evdev に出し、compositor のタッチパッドの層で tap・tap-drag・押し込み・2 本指のスクロール・gesture を作る。

## ユーザーの決定

- 2026-10-05「タップダウンを取れない？それは違うと思います。PS/2でもタップダウンもイベントとして取れているように見えました。でも、2本指スクロールは取れないですね。ACPI AMLを実装したあと、I2C-HIDを実装しましょう。compositorのtouchpad層も作りましょう。WSを立ててください。」（タップダウンは BUG-190 で PS/2 の経路のまま、2 本指のスクロール BUG-156・押し込み BUG-167・tap-drag BUG-166 はこの WS）
- 2026-10-05「5330を起動しました。操作はすべて許可します。」（5330 の Linux でタッチパッドの調べに要る操作。採った物は tests/latitude5330-linux/）
- 2026-10-05: ベータ1（ユーザーのクリックの回答、q713）
- 2026-10-06（BUG-210）: 実機で touchpad と 2 本指の scroll が動いたユーザー「タッチパッドがI2Cで使えるようになりました。2本指でスクロールができています。」（BUG-210 の記録）
- 2026-10-08 夜「Touchpadは動作確認できたので、下記をすべてclose します。」
- 2026-10-13「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」

## 結果

- kernel: Intel LPSS の DesignWare I2C の driver（ACPI の I2cSerialBus・GpioInt、firmware が割り当てない BAR0 を PCI の root の窓から割り当てる、67b70410）、I2C-HID と Precision Touchpad、EVIOCGPROP、evdev の MT と clickpad、GPIO の割り込み（INTC1055、level・active low）で sampling をやめる、input-inject の touchpad。I2C-HID が動く時は PS/2 の aux を使わない。
- compositor の touchpad の層（tap・tap-drag・押し込み・2 本指のスクロール）。
- 証拠: QEMU の T1-100（p003）・T1-125 と T1-101（p004）・T1-113 の boot（p002、QEMU に LPSS は無い）、host の試験。実機（5330）: 2026-10-06 のユーザーの確かめ（touchpad と 2 本指の scroll）、2026-10-08 夜「Touchpadは動作確認できた」。

## 制限・移管

- 規約の全文（p005 の後半）は WS の外の一括の見直しへ。
- 端からの gesture は WS142、Touchpad の設定の頁は ws089-p024。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws159-p001 | 設計（LPSS I2C・I2C-HID・HID の digitizer・evdev の MT・PS/2 との切替・touchpad の層・試験） | cleared |
| ws159-p002 | LPSS の DesignWare I2C の driver と ACPI の I2cSerialBus・GpioInt | cleared（2026-10-13 ユーザーの一括、実機は BUG-210 の後に動作） |
| ws159-p003 | I2C-HID と Precision Touchpad、EVIOCGPROP、input-inject の touchpad | cleared（T1-100） |
| ws159-p004 | compositor の touchpad の層 | cleared（T1-125・T1-101） |
| ws159-p005 | 実機の UAT と全文の規約 | planned のまま WS を閉じた（UAT は 2026-10-13 の一括で OK、規約の全文は WS の外の一括の見直しへ） |
| ws159-p006 | GPIO の割り込みで sampling をやめる | cleared（2026-10-13 ユーザーの一括） |

## 記録の所在

phase001 は上の理由で残す。他の Phase の記録と試験は削除する（Q1）。git の commit `fb1f6afee` の `plan/ws159/` にある。5330 の Linux の採取（latitude5330-linux/）と I2C-HID の host 試験は plan/tools/ へ移す。Queue の履歴は [plan/history](../history/index.md)。
