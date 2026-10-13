<!-- awesome-plan project=zedbsd record=ws049 -->

# WS049: kernel 内の ACPI AML interpreter

<!-- awesome-plan-current:start -->
Status: completed（2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」。全 Phase が cleared（p007・p008・p016 は 2026-10-13 ユーザーの実機の確かめ「OKでした」、電源ボタン・蓋）。旧: incomplete）
Primary Milestone: MG003
Related Milestones: MG006, MG008
Objectives: O2, O4
Parent: [Master](../master.md)
Queue: なし
<!-- awesome-plan-current:end -->

## 目的

ユーザー（2026-09-24）「下記をそれぞれWSとして追加してほしいです。カーネル内ACPI AMLインタプリタの実装。…」
kernel の中に ACPI の AML interpreter を持ち、DSDT・SSDT を読み込んで namespace を作り、driver が AML の method（`_STA`・`_CRS`・`_PS0`・`_PS3`・`_DSM`・`_Lxx`/`_Exx` ほか）を評価できるようにする。UCSI（WS050）、USB-C の DP Alt Mode（WS051）、S0i3 の電源管理（WS052）、/dev/system の事象（WS132）の土台。設計は [design.md](design.md)。

## 結果（2026-10-13 完了）

- `src/drivers/acpi/`: object・namespace・DefinitionBlock の読み込み（p002）、評価器（p003）、OperationRegion・Field 類と region の handler・`_REG`（p004）、同期と OS の口（Mutex・Event・Sleep・Notify・`_OSI`・Load 系・`_INI`、p005）。
- kernel への組み込み（amd64、p006）: 承認済みの HAL の差分 `hal_get_arch_handoff("acpi.rsdp")`（Guardrail の表、2026-09-27）、firmware の table の発見と kernel の glue（p010）、診断の口 `/dev/acpi`（p013）、driver 向けの公開の口（p017）。
- SCI・GPE・固定 event・EC（p011・p007）、FACS の Global Lock（p014）、ECDT（p015）、橋の下の機能の PCI_Config の region（p016）、壊れた table への堅牢性の fuzz（p012）。
- 5330 の table（BUG-165: DSDT が無い PCI 機能の PCI_Config で止まった件）の直しと host の確認（p008）。BUG-165 は 2026-10-13 resolved。
- 規約の全文の確認と最終の確認（p009、T1-091 の QEMU の回帰 PASS）。
- 実機（5330）: 電源ボタン・蓋・AC・EC の事象（p007・p008・p016）は 2026-10-13 ユーザーの実機の確かめ「OKでした」。

## 制限・移管

- 診断の口 `/dev/acpi`（device 番号 `0x000B0000`、text の read・write）は UAPI を足さない形のまま。ioctl や `/dev/system` への統合は行っていない。
- `_OSI` は design §8 の案（`Windows 2022` まで真）のまま。
- p017 の ⑤（memory map の型を問う口、HAL の API が要る）は 2026-10-05 に不要として閉じた。
- 電源の切断（BUG-119）・蓋を閉じた間の HDMI（BUG-255）・電池の 5 fps（BUG-159）は Bug Board で扱う。S0i3 は WS052。
- 試験の道具 `tests/`（AML の host の harness、ASL の試験、5330 の table `tests/latitude5330/`、QEMU の QMP の `tests/qmp-send.py`）は他の WS の試験と Bug の記録が参照している。移す先は Q1 が決める。

## Phase

| Phase | 内容 | 最終の状態 |
| --- | --- | --- |
| ws049-p001 | 調査と設計（table の道、利用者の要求、構成、評価の方式、HAL の差分の案、試験の方法） | cleared（2026-09-27） |
| ws049-p002 | object・namespace・byte 列・DefinitionBlock の読み込み、host の harness | cleared（2026-09-27） |
| ws049-p003 | 評価器: method、制御、式の opcode、参照、変換、Store の規則 | cleared（2026-09-27） |
| ws049-p004 | OperationRegion・Field・IndexField・BankField・BufferField、region の handler と `_REG` | cleared（2026-09-27） |
| ws049-p005 | 同期と OS の口（Mutex・Event・Sleep・Notify・`_OSI`・Load 系・`_INI`、stack の予算） | cleared（2026-09-27） |
| ws049-p006 | kernel への組み込み（amd64）と QEMU（q35・OVMF）での確認 | cleared（2026-09-27） |
| ws049-p007 | SCI・GPE・固定 event・EC の kernel での確認 | cleared（2026-10-13 ユーザーの実機の確かめ「OKでした」） |
| ws049-p008 | 対象機（Latitude 5330）の table と実機の確認（BUG-165） | cleared（2026-10-13 ユーザーの実機の確かめ「OKでした」） |
| ws049-p009 | 規約の全文の確認と最終の確認 | cleared（2026-10-04 Q1、T1-091） |
| ws049-p010 | firmware の table の発見と kernel の glue | cleared（2026-09-27） |
| ws049-p011 | SCI・GPE・固定 event・EC の核（host の疑似の hardware） | cleared（2026-09-27） |
| ws049-p012 | 壊れた table への堅牢性（fuzz） | cleared（2026-09-27） |
| ws049-p013 | 診断の口 `/dev/acpi` | cleared（2026-09-27） |
| ws049-p014 | FACS の Global Lock を firmware と取り合う | cleared（2026-09-27） |
| ws049-p015 | ECDT（`_REG`・`_INI` の前の Embedded Controller） | cleared（2026-09-27） |
| ws049-p016 | 橋の下の機能の PCI_Config の region の bus | cleared（2026-10-13 ユーザーの実機の確かめ「OKでした」） |
| ws049-p017 | driver 向けの公開の口 | cleared（2026-10-08 Q1、T1-093） |

Phase の directory とこの WS だけの試験は、完了の規則（AGENTS.md）で削除する（git の履歴に残る、削除は Q1）。
