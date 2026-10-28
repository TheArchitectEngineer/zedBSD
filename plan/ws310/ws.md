<!-- awesome-plan project=zedbsd record=ws310 -->

# WS310: ACPI の _OSC を呼び、システム全体の挙動を厳重に確かめる

Status: planning
Primary Milestone: MG004
Resume point: p001（どの _OSC（processor・PCIe・platform-wide）を何の bit で呼ぶか、呼ぶ前後で firmware の振る舞いの差を確かめる方法の設計）。WS301 の後。

## ユーザーの決定（原文、2026-10-29）

WS301 の Q301-3（ACPI の _OSC を呼ぶか）への回答:「最終的には呼ぶが、別なWSにして、システム全体の挙動が問題ないかを厳重にチェックする。今は呼ばない。」

## 目標

zedBSD が ACPI の _OSC で OS の能力を firmware に申告し、firmware が出す情報（_CPC など）を使えるようにする。呼ぶ前と後で、電源・PCIe・熱・CPU の性能などシステム全体の挙動に問題が無いことを、実機（まず 5330）で厳重に確かめる。

## Phase

| Phase | 目的 | 状態 | 依存 |
| --- | --- | --- | --- |
| p001 | 調べと設計（呼ぶ _OSC と bit、確かめの方法と項目） | planning | WS301 |
