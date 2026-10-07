<!-- awesome-plan project=zedbsd record=ws145-p004 -->

# ws145-p004: Settings の Printers の頁

Status: cleared（2026-10-07 Q1 の判定: T1-318 の AAT settings.printers（needs-person）で IPP・LPD の printer の追加と 3 つの job が Done、PDF Viewer の Printed、Printers の頁の PNG（Mock Printer が Default、LPD の printer、form）を Q1 が目視。T1-320 の full の中の fail は前の scenario の状態の残りの疑いで P2 が切り分ける）（旧: in-progress（2026-10-07 q831 P2））
Disposition: normal
Parent: [WS145](../ws.md)
Queue: q831（2026-10-07、P2）
依存: [p003](../phase003/phase.md)

## 実装（正常系）

`userland/desktop/settings/page-printers.c`（新規）、`settings.h`（`struct se_printers`）、`pages.c`（Printers を ready に）、`system.c`（result の振り分け）、`main.c`（poll）、Makefile 3 つ。

- Printers の card: 各 printer の名前・`host:port`・protocol・path、既定に「Default」、ほかに「Make Default」、「Remove」。
- Add a Printer の card: protocol（IPP・LPD の 2 つの button）、Address・Port（空なら 631・515）・Path or queue（D3 の詳しい設定、空なら `/ipp/print`・`lp`）、「Add Printer」（Enter でも）。Tab で欄を移る、Esc で keyboard を返す。
- Print Jobs の card: 新しい順に題・printer・状態（Waiting・Sending・Printing・Done・Failed (語)・Cancelled）、終わっていない job に「Cancel」。
- 答えは card の下の 1 行（足した・既にある・多すぎる・できない）。頁は `kl_system_printers_*` だけを使う。log `ZSETTINGS PRINTERS ask kind=… error=…`・`result kind=… errno=…`。
- AAT: [apps.settings.printers](../../../tests/scenarios/apps/settings/printers.md)（helper `plan/tools/aat/scenarios/helpers_printers.py`: host の mock の IPP・LPD に printtest で足して印刷、mock が受けた文書が試料と同じ、Printers の頁の撮影）。

## 確かめ（2026-10-07、P2）

- zedBSD: settings の build warning 0、style-check 0（page-printers.c）、check-scenarios PASS。
- host の頁の描画の試験は無い（Settings の頁の host の描画の道具が無い）。頁は T1 の撮影で見る。
