<!-- awesome-plan project=zedbsd record=ws077 -->

# WS077: PC-98 の PCI を有効にする（BUG-024）

<!-- awesome-plan-current:start -->
Status: canceled（2026-10-05 ユーザーの判断でキャンセル。前の状態: planning）
Disposition: canceled（2026-10-04 user「PC-98対応をもうやらないです。」、BUG-024 も「対応不要で終了」）
Primary Milestone: MG001
Related Milestones: MG002
Objectives: O1
Parent: [Master](../master.md)
Queue: なし
Resume point: p001（調査と設計）から。2026-09-28 ユーザー「Bug024は優先度を下げます。」→ 低い優先度、着手は未定
2026-10-05 **キャンセル**: 2026-10-05 user「WS037, WS044,WS048,WS141, WS112, WS118, WS124, WS125, WS126, WS119, WS096, WS097, WS039, WS038, WS144, WS143, WS146,WS147, WS152,  WS119, WS080, は、ベータ4以降としてください。…WS027, WS015, WS047, WS028, WS017,  WS077, はキャンセルします。」
<!-- awesome-plan-current:end -->

## 目標

2026-09-28 ユーザー:「Bug024は、PCIを有効にします。」（[BUG-024](../bugs/BUG-024.md) の案 B）

PCI の bus を持つ PC-9821 の後期の機種で、PC-98 の kernel が PCI の bus を列挙し、PCI の driver（menuconfig の PCI の選択肢）を使えるようにする。
USB の host controller（PCI の上の UHCI・OHCI・EHCI）は PCI の後に続けて判断する。

## 制約

- PC-98 の PCI の config の方式（PC-9821 の PCI の bridge、config mechanism）と割り込みの経路（PC-98 の PIC の IRQ への割り当て）の調査が要る。
- HAL の責務の追加（PCI の config の access・IRQ の routing）を伴うなら、差分ごとにユーザーの事前の承認が要る（plan/ws077/proposed/）。
- 試験は amd64 だけの方針（2026-09-27）の例外として、この WS は PC-98 の QEMU（plan/tools/pc98-boot.py、PCI を模すかは未確認）と実機が要る。
  着手の前にユーザーと確認する。

## Phase の案

| Phase | 内容 | Status |
| --- | --- | --- |
| ws077-p001 | 調査と設計: PC-9821 の PCI の方式、QEMU の PC-98 の PCI の有無、HAL の差分の案、menuconfig の pc98 の PCI の対応 | planning |
| ws077-p002 | PC-98 の PCI の backend と IRQ の経路、pci.drivers の platform に pc98 | planning |
| ws077-p003 | 規約と回帰（PC-98 の boot） | planning |

2026-10-04 Q1: user の判断で PC-98 の対応をやめるので、この WS は canceled（実装は未着手、BUG-024 は終了）。
