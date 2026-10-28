<!-- awesome-plan project=zedbsd record=ws134 -->

# WS134: システムモニターのアプリ（Analytic Spatial UI）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608。p001〜p008・p011〜p013 cleared、p003 は実機の fps 21（UAT 2026-10-04）で cleared。p009・p010 の実機の分は 2026-10-13 ユーザー「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」で OK。Pi 5 でも 2026-10-22 ユーザー「System Monitor を開いて、中身が出ました。」）
Completed: 2026-10-27（q1608）
Primary Milestone: MG006
Related Milestones: —
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

2026-10-03 ユーザー「P2が空いているので、システムモニターのアプリを作ってほしいです。添付がイメージです。」「イメージの通りじゃなくてよくて、要素を採用してほしいです。」

CPU（全体と各 core）、GPU（Util・VRAM・温度・電力、複数）、RAM、Network、Disk I/O と最近の出来事を、中央の「システム状態の立体コア」を顔にした層構造の画面で見せる Keiland の app。設計は [design.md](design.md)、コンセプトは [design/user-concept.md](design/user-concept.md)、参考の画像は [design/reference-image.webp](design/reference-image.webp)。

## ユーザーの決定（2026-10-03）

- 「P2はコードを書いてOKだと思います。衝突しないです。」→ 設計（p001）の後、user のレビューを待たずに実装へ。
- 「P2はとりあえずOSから取れない情報はスタブデータでそれっぽいアニメーションを表示しましょう。」
- 「simという表記はつけなくていいです。私がどの項目がスタブか把握できていればいいです。」→ 画面に stub の印は付けず、stub の項目の一覧を design.md に書く。
- 「システムモニターは私に確認しなくていいので、どんどん実装して動かしてください。」
- 2026-10-04（UAT）: p003 の fps の条件（15 以上）は実機で判定する。
- 2026-10-13「UATの結果待ちのものは、すべてテスト済みなのでclearedにして進めたりcloseしたりしてください。」
- 2026-10-22（Pi 5）「System Monitor を開いて、中身が出ました。」

## 結果

- app: package・3 OS の build・窓と自前の Vulkan・data source の層（sim・replay）・履歴・plate と文字・`ZMON` の log（p002）、状態コア・CPU のタイル面・GPU のカード・Network/Disk の流れ・Memory の層・視差・警告・Events（p003）、tap の展開・長押しの固定・swipe の時間軸・pinch の俯瞰（p004）、本物の値の system の source（p013）。
- kernel: CPU ごとの時間 `hw.cputimes`（p005）、disk ごとの統計 `hw.diskstats`（p006）、GPU の telemetry `hw.gputelemetry`（p007）、ACPI の温度 `hw.thermal`（p009）。
- backend の monitor の領域（zedBSD p008、Linux・FreeBSD p011）、compositor の `kl_system_monitor_v1` と libkeiland の `kl_system_monitor_*`（p012）。
- 証拠: QEMU の T1-041・T1-063〜T1-070、5330 の実機の UAT（2026-10-04 の fps 21、2026-10-13 の一括の OK）、Pi 5 の 2026-10-22 のユーザーの確認。

## 制限・移管

- p009 の T1-482（QEMU）と p010 の QEMU の回帰（T1）の Q1 の判定の印は付かないまま閉じた（全文規約の見直しと直しは q663 で済み）。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws134-p001 | 設計 | cleared（2026-10-18 Q1 の照合） |
| ws134-p002 | M1 骨組み | cleared（T1-041） |
| ws134-p003 | M2 3D と動き | cleared（実機の fps 21、UAT 2026-10-04） |
| ws134-p004 | M3a 操作 | cleared（q660、T1-063） |
| ws134-p005 | K1 `hw.cputimes` | cleared（q661、T1-064） |
| ws134-p006 | K2 `hw.diskstats` | cleared（q661、T1-065） |
| ws134-p007 | K3 `hw.gputelemetry` | cleared（2026-10-08 Q1 の判定、T1-066） |
| ws134-p008 | M3a 本物の値（zedBSD の backend） | cleared（q662、T1-067） |
| ws134-p009 | K4 ACPI の温度 `hw.thermal` | test-wait のまま WS を閉じた（実機の分は 2026-10-13 ユーザーの OK、T1-482 の判定の印なし） |
| ws134-p010 | M4 全文規約・回帰・デモの通し | in-progress のまま WS を閉じた（全文規約は q663 で済み、実機の分は 2026-10-13 ユーザーの OK、QEMU の回帰の判定の印なし） |
| ws134-p011 | M3b Linux・FreeBSD の backend | cleared（q662、T1-068） |
| ws134-p012 | M3c `kl_system_monitor_v1` | cleared（q662、T1-069） |
| ws134-p013 | M3d app の system の source | cleared（2026-10-08 Q1 の判定、T1-070） |

## 記録の所在

Phase の計画・結果（`phaseNNN/`）は削除する（Q1 の pipeline）。試験（`tests/`: host・replay・guest）は source の comment と `plan/tools/files/build-files-image.sh` が引くので plan/tools/ へ移す案（Q1 の判断）。git の commit `fb1f6afee` の `plan/ws134/` にある。Queue の履歴は [plan/history](../history/index.md)。
