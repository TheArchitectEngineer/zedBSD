<!-- awesome-plan project=zedbsd record=ws145-p007 -->

# ws145-p007: PDF Viewer の File > Print（D6）

Status: cleared（2026-10-07 Q1 の判定: T1-318 の AAT settings.printers（needs-person）で IPP・LPD の printer の追加と 3 つの job が Done、PDF Viewer の Printed、Printers の頁の PNG（Mock Printer が Default、LPD の printer、form）を Q1 が目視。T1-320 の full の中の fail は前の scenario の状態の残りの疑いで P2 が切り分ける）（旧: in-progress（2026-10-07 q831 P2））
Disposition: normal
Parent: [WS145](../ws.md)
Queue: q831（2026-10-07、P2）
依存: [p003](../phase003/phase.md)。D6「PDF Viewer の印刷を含める」（2026-10-05 夜、ユーザー）

## 実装（正常系）

`userland/desktop/pdfviewer/`: File に「Print」（Ctrl+P、文書がある時）、`PV_ACTION_PRINT`、`main.c` の `main_print`（`kl_app_system` の printers が無ければ「This desktop cannot print.」、既定の printer へ文書の file を file 名を題に `kl_system_printers_print`、「Sending to the printer...」）と `main_print_follow`（result、`kl_system_print_job_of` の job が終わると「Printed.」・「The printing was cancelled.」・「The printer could not print the document.」）。log `PDFVIEWER PRINT asked error=…`・`result error=…`・`job=… state=… detail=…`。

## 確かめ（2026-10-07、P2）

- zedBSD: pdfviewer の build warning 0、style-check（変えた所）0。
- AAT: apps.settings.printers の 3 段目（PDF Viewer で Ctrl+P、mock の `ipp-2.pdf` が試料と同じ）。QEMU は T1。
