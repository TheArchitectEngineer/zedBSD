<!-- awesome-plan project=zedbsd record=ws050 -->

# WS050: USB-C の UCSI driver

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: p001〜p005 cleared、実機は 2026-10-08 夜 ユーザー「WS050・WS051・WS090・WS183 は実機で動作を確認」と 2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」。p006 の規約の全文は WS の外の一括の見直しへ）
Completed: 2026-10-27（q1608 の精査）
Primary Milestone: MG003
Related Milestones: MG006
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

USB Type-C Connector System Software Interface（UCSI）の driver で、USB-C の connector の状態（接続・向き・電源の役割・データの役割・Alternate Mode・USB PD の contract）を読み、変化を受け取り、必要な操作（role の切り替え、Alternate Mode の選択）ができる。PC の UCSI は ACPI の device（`USBC000`／`PNP0CA0`）で、WS049 の AML interpreter の上に作る。設計は [design.md](design.md)（review は [design-review-2026-10-04.md](design-review-2026-10-04.md)）。

## ユーザーの決定

- 2026-09-24: 「USB-C UCSIドライバの実装。」
- 2026-10-04: 「次の新規実装項目は、USB-C の DisplayPort Alternate Modeの実現を目標にします。その次が電源管理です。これらは併走できると思います。共通のpredecessorがAMLですね。」
- 2026-10-04（design.md §10 の 5 項目、Q1 経由）: 公開は `/dev/typec`（診断）→ 正式は WS132 の `/dev/system`。role の切替・CONNECTOR_RESET・SET_NEW_CAM は範囲に入れる。UCSI 1.x と 2.x の両方（2.x は host の試験で）。向きは 1.x でも i915 から。HPD・pin は UCSI 2.0 以上と i915 の両方の経路（1.x は i915 だけ）。
- 2026-10-08 夜: 「WS050・WS051・WS090・WS183 は実機で動作を確認」。2026-10-08: 規約の全文はベータ3（WS の外の一括の見直しへ）。
- 2026-10-13: 「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」
- 2026-10-27（q1608）: 「…masterとWSを修正して、completedやcanceledにしてください。」

## 結果

- `src/drivers/typec/`: UCSI の核と Type-C の層（1.x・2.x の配置、状態と kernel 内の口）、ACPI の transport（`ucsi-acpi.c`、Notify、thread、`/dev/typec`）、操作の command（CONNECTOR_RESET・SET_UOR・SET_PDR・SET_NEW_CAM・GET_CABLE_PROPERTY）、i915 との連携（HPD・pin・向きの二つの出所の統合、TC の port と connector の対応付け）。
- host 試験: 疑似の PPM と 5330 の ACPI の table（31 → 57 → 75 → 94 件、後に WS177 p003 で 124・43 件）。QEMU には UCSI が無く、T1-330 で「device 無し」で何もしないことを確かめた。
- 実機: 5330 の UAT の log で `typec: display port TC1/TC2 … bound (error 0)`（p005）。5320 の PPM の timeout（change だけの ACK の後に答えなくなる）を、GET_CAPABILITY と CONNECTOR_CHANGE を 1 回の ACK_CC_CI で返す形に直し、5320 の実機で timeout が消えた（2026-10-07 Q1、9f836b951）。ユーザーが実機で動作を確認（2026-10-08 夜）。

## 制限・移管

- 規約の全文の見直し（p006 の残り）は WS の外の一括の見直しへ。
- DP Alt Mode の利用は WS051。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws050-p001 | 調査と設計（UCSI 1.2/2.x、`USBC000` の AML、Type-C の層） | cleared（2026-10-08 Q1） |
| ws050-p002 | UCSI の核・Type-C の層、疑似の PPM の host 試験 | cleared（2026-10-07 Q1） |
| ws050-p003 | ACPI の transport と kernel への組み込み | cleared（2026-10-07 Q1、T1-330） |
| ws050-p004 | 操作の command と操作の口 | cleared（2026-10-07 Q1） |
| ws050-p005 | i915 との連携 | cleared（2026-10-08 Q1） |
| ws050-p006 | 実機の確認と規約の全文の確認 | 実機の分は 2026-10-13 ユーザーの判断で OK、規約の分は WS の外の一括の見直しへ（Phase の dir は作られていない） |

## 記録の所在

Phase の計画・結果・試験は削除する。git の commit `fb1f6afee` の `plan/ws050/` にある。Queue の履歴は [plan/history](../history/index.md)。
