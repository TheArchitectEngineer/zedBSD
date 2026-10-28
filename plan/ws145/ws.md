<!-- awesome-plan project=zedbsd record=ws145 -->

# WS145: 印刷（printer の daemon・IPP/LPD で PDF を送る、libkeiland の印刷の口、Settings の Printers の頁）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」。根拠: p001〜p004・p007 cleared（printd・IPP/LPD・Settings の Printers・PDF Viewer の Print、T1-318）、p005 は 2026-10-14 Q1 の判定で T1-587 の Debian の印刷）
Completed: 2026-10-27（q1608）
Primary Milestone: MG006
Related Milestones: MG005
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

app が libkeiland に PDF の場所を渡して印刷を依頼すると、network の printer（IPP か LPD）で印刷される。printer の一覧と既定の printer を libkeiland から compositor 経由で取れ、Settings の Printers の頁で printer の IP address・port・protocol を設定できる。

## ユーザーの決定

- 2026-10-04 夜「SettingsのPrintersは、プリンタのIPアドレス、ポート、プロトコルだけ設定できる画面を追加してほしいです。libkeilandでプリンタ一覧やデフォルトプリンタをコンポジタ経由で取得可能にしましょう。実際のプリントは、libkeilandにPDFの場所を書いて印刷依頼を投げると、libkeiland-backendがプリンタデーモンが未起動なら起動し、デーモンがIPPやLPDのプロトコルでPDFを投げる、という形にしたいです。PDFが最初のフォーマットでOKで、そのあとPostScriptやベンダー形式への変換フィルタなども実装して広げていけばいいと思います。」
- 設計 [design.md](design.md) の D2〜D9 はユーザーが決定（p001、2026-10-05〜08）。D6: PDF Viewer の File > Print を含める。
- 2026-10-08 ユーザー「規約の整形（各 WS の全文規約の Phase）はベータ3」→ p006 は WS の外の一括の見直しへ。

## 結果

- `keiland-printd`（利用者の権限の daemon、spool、IPP（RFC 8011 の Print-Job）と LPD（RFC 1179）で PDF を送る）、libkeiland-backend の print（設定の file `~/.config/keiland/printers.conf`、printd の起動と通信）、compositor の printers の object、libkeiland の印刷の口、`printtest`、Settings の Printers の頁、PDF Viewer の File > Print が main にある。
- 証拠: QEMU の T1-318 の AAT settings.printers（needs-person）で IPP・LPD の printer の追加と 3 つの job が Done、PDF Viewer の Printed（p002〜p004・p007）。Linux は Debian の QEMU+KVM の T1-587 で IPP・LPD の印刷（p005、2026-10-14 Q1 の判定）。受け入れの機種は Brother MFC-L3770CDW。

## 制限・移管

- FreeBSD は build と install まで（GPU の無い guest で compositor が起きず印刷は未実施）。
- p005 の phase.md は test-wait のまま（判定は 2026-10-14 Q1 で cleared 相当）。
- 変換の filter（PDF → PWG raster・PostScript・PCL）、printer の発見（mDNS/DNS-SD）は範囲の外（新しい WS で）。準正常系は WS177 の案 Q と BUG-271。
- 規約の全文（p006）は WS の外の一括の見直しへ。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws145-p001 | 調査と設計（design.md） | cleared |
| ws145-p002 | keiland-printd（約束の行・spool・IPP・LPD・寿命）と host 試験 | cleared（T1-318） |
| ws145-p003 | backend の print、compositor の printers の object、libkeiland の口、`printtest` | cleared（T1-318） |
| ws145-p004 | Settings の Printers の頁 | cleared（T1-318） |
| ws145-p005 | Linux・FreeBSD の build と install、Debian・FreeBSD の guest の確認 | phase.md は test-wait のまま。2026-10-14 Q1 の判定: T1-587 の Debian で印刷、FreeBSD は build・install まで |
| ws145-p006 | 規約の全文と回帰（directory なし） | 規約の全文は WS の外の一括の見直しへ |
| ws145-p007 | PDF Viewer の File > Print | cleared（T1-318） |

## 記録の所在

設計は [design.md](design.md)。Phase の記録と試験は削除する（Q1）。git の commit `fb1f6afee` の `plan/ws145/` にある。AAT が使う `mock-printers.py` は plan/tools/aat/ へ移す。Queue の履歴は [plan/history](../history/index.md)。
