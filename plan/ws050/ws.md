<!-- awesome-plan project=zedbsd record=ws050 -->

# WS050: USB-C の UCSI driver

<!-- awesome-plan-current:start -->
Status: incomplete
Primary Milestone: MG003
Related Milestones: MG006
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: 2026-10-07 q834 P2: p004 の正常系（操作の口と 5 つの command、host 75/75）。次は p005（i915 との連携、WS051 の後）。それ以前: p003 の正常系（ACPI の transport・kernel の組み込み・/dev/typec、host 57/57、kernel warning 0、QEMU は T1 へ）。次は p004。それ以前: p002 の正常系（核・Type-C の層・host 31/31、仕様 1.2・3.1 と照合）。次は p003（ACPI の transport と kernel への組み込み）。それ以前: 2026-10-04 p001 の design.md 第 3 版
<!-- awesome-plan-current:end -->

## 目標

USB Type-C Connector System Software Interface（UCSI）の driver で、USB-C の connector の状態（接続・向き・電源の役割・データの役割・
Alternate Mode・USB PD の contract）を読み、変化を受け取り、必要な操作（role の切り替え、Alternate Mode の選択）ができる。

## きっかけ

2026-09-24 ユーザー指示: 「USB-C UCSIドライバの実装。」

## 前提と今あるもの

- PC の UCSI は ACPI の device（`_HID` の `USBC000`、`_CID` の `PNP0CA0`）で、共有の memory（OperationRegion）と `_DSM` で EC/PD controller と話す。
  → **WS049 の AML interpreter が前提。**
- USB の xHCI・hub・HID・storage の driver はある（`src/drivers/usb`、`src/drivers/pci/pci-xhci.c`）。Type-C の概念（connector・partner・Alternate Mode）の層は無い。
- 対象機は WS049 と同じ（仮定）。

## 範囲

- UCSI の command（`PPM_RESET`、`SET_NOTIFICATION_ENABLE`、`GET_CAPABILITY`、`GET_CONNECTOR_CAPABILITY`、`GET_CONNECTOR_STATUS`、`GET_ALTERNATE_MODES`、
  `GET_CAM_SUPPORTED`、`GET_CURRENT_CAM`、`SET_NEW_CAM`、`GET_PDOS`、`SET_UOR`・`SET_PDR`・`CONNECTOR_RESET`・`GET_CABLE_PROPERTY` ほか）と
  通知の割り込み（ACPI の `Notify`）。UCSI 1.x と 2.x の両方の配置と field（2026-10-04 ユーザーの決定 3、2.x は host の試験で）。
- Type-C の connector の層（状態、partner の Alternate Mode の一覧）。診断の text の `/dev/typec`（UAPI を足さない）、利用者への正式な通知は
  WS132 の `/dev/system`（決定 1）。
- DP Alt Mode の入口（WS051 が使う: mode に入る・出る（SET_NEW_CAM））。DP の HPD と pin の割り当ては、UCSI 2.0 以上で取れる時は UCSI から、
  i915 の TCSS・FIA から取れる時は i915 からも取り、両方を統合する（1.x は i915 だけ。決定 5 とその補足）。plug の向きは 2.0 以上は UCSI、
  1.x は i915 の TCSS から取る（決定 4、取れるかは WS051 と確かめる）。設計は [design.md](design.md) §13。

## 受け入れ

- 対象機で各 USB-C port の抜き差し、向き、電源の役割、partner の Alternate Mode が読め、抜き差しの通知が届く。
- 規約の全文、build、boot test。実機の証拠と QEMU の証拠を分ける（QEMU には UCSI が無いので、試験は実機が中心）。

## Phase 一覧

| Phase | 内容 | Status | 依存 | 対象 |
| --- | --- | --- | --- | --- |
| [ws050-p001](phase001/phase.md) | 調査と設計: UCSI の仕様（1.2/2.x）、対象機の `USBC000` の AML（共有 memory の配置、`_DSM` の function）、Type-C の層の設計、公開の形 | in-progress（2026-10-04。[design.md](design.md) 第 3 版、レビューとユーザーの決定を反映） | WS049 の namespace と評価器 | 設計文書 |
| [ws050-p002](phase002/phase.md) | UCSI の核・Type-C の層の状態と kernel 内の口、1.x・2.x の配置、疑似の PPM と記録の再生の host の試験 | in-progress（2026-10-07 P2: 正常系、host 31/31、kernel の flag で warning 0。記録の再生は p006） | p001、仕様書での確認、実機の VERSION と mailbox の記録（UAT） | `src/drivers/typec/` |
| [ws050-p003](phase003/phase.md) | ACPI の transport と kernel への組み込み（`ucsi-acpi.c`、attach、Notify、thread、`/dev/typec`） | in-progress（2026-10-07 P2: 正常系、host 57/57（5330 の table）、kernel warning 0。QEMU の起動は T1 へ） | p002、ws049-p017（q696、WS049 の公開の口）、ws049-p007・p008 | 同上 |
| [ws050-p004](phase004/phase.md) | 操作の command（CONNECTOR_RESET・SET_UOR・SET_PDR・SET_NEW_CAM・GET_CABLE_PROPERTY）と操作の口 | in-progress（2026-10-07 P2: 正常系、host 75/75、kernel warning 0） | p003 | 同上 |
| [ws050-p005](phase005/phase.md) | i915 との連携（HPD・pin・向きの二つの出所の統合、TC の port と connector の対応付け） | planned | p003、ws051-p002 | 同上 |
| ws050-p006 | 実機の確認と規約の全文の確認 | planned | p003〜p005、実機 | WS の全 source |

## 2026-10-04 予定（Q1）

ユーザー「次の新規実装項目は、USB-C の DisplayPort Alternate Modeの実現を目標にします。その次が電源管理です。これらは併走できると思います。共通のpredecessorがAMLですね。」→ [queue.md](../queue.md) の q679（WS050 p001）・q680（WS051 p001）・q681（WS052 p001）。設計は WS049 の q677（BUG-165、DSDT）と並走、実装は q677・q678（ws049-p007）の後。

## 2026-10-04 ユーザーの決定（§10 の 5 項目、Q1 経由、design.md の末尾に記録）

公開は `/dev/typec`（診断）→ 正式は `/dev/system`。role の切替・CONNECTOR_RESET・SET_NEW_CAM は**範囲に入れる**（目標は今のまま、Phase を足した）。
UCSI 1.x と 2.x の両方。向きは 1.x でも i915 から。HPD・pin は UCSI 2.0 以上と i915 の両方の経路（1.x は i915 だけ）。
