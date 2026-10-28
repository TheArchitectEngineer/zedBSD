<!-- awesome-plan project=zedbsd record=ws189 -->

# WS189: app の間の drag and drop（画像・受け入れの見た目・dock・desktop・画面をまたぐ）

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」: p001・p004・p005 cleared、p002・p003 は実装済み（main）で T1 の手操作の大半を確認（T1-433〜T1-442））
<!-- retained-after-completion --> Phase phase001 はその場に残す: src/・userland/・platform/・Makefile・config/ の source か build の file が path を名指す（AGENTS.md §10 の source の comment の掃除の後に消す。2026-10-28 Q1 の方針、q1660）
Completed: 2026-10-27（q1608 の精査）
Primary Milestone: MG006
Related Milestones: なし
Objectives: O2
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

app の間で、選択した文字や画像を drag すると compositor が受ける窓に受け入れを問い合わせ、可否が見た目で分かり、落とすと専用の受け渡しでデータが別の app に届く。dock の spring-loaded、desktop への file の保存、画面をまたぐ drag を含む。

## ユーザーの決定

- 2026-10-08（由来）「Text Editorなどアプリで選択範囲があったり、画像オブジェクトがあるアプリで、選択範囲をマウスドラッグ開始したり、画像オブジェクトをドラッグ開始したときに、コンポジタに通知しておき、別なウィンドウの上に持って行くと、そのウィンドウに通知が行って、ドロップ受け入れ可能かをコンポジタが問い合わせて、可能ならビジュアル的にそれがわかり、さらにドロップすると、クリップボード経由でデータが別アプリに送られる。システムが一体となって操作できる感じ。これを実現したい」
- 2026-10-08（クリックの回答）受け渡しは DnD の専用の受け渡し（wl_data_source・wl_data_offer、clipboard の中身は変えない）。同じ app の中の drag は app の中で処理し、この WS は app の間。最初に足す型は画像（PNG）。受け入れの見た目は compositor（badge のコピー可・不可）と app（落とす場所を光らせる）で分担。dock の app の icon の上で止まると前に（spring-loaded）、desktop に落とすと file、画面をまたいで drag。X11 の app との橋渡し（XDND）は入れない。
- 2026-10-08「WS189 で添付も作る」（p004、Mail の添付）。

## 結果

- libkeiland の画像の drag の口（`kl_window_drag_image`・KL_DROP_IMAGE、受け入れの答え）、compositor の badge・dock の spring-loaded・画面をまたぐ drag、app の画像の元（Photos・Notes・PDF Viewer・Browser）と受け（Notes・desktop の file）、文字の drag の元と受け（Text Editor・PDF Viewer）、Mail の添付（multipart/mixed・base64）と drop の受けを実装した（main）。
- QEMU の T1 の手操作: T1-433・434・436・437・439・442 で early-receive・refused-mark・dock-spring・即離し・same-program-windows・text-between-windows 1〜4・photo-to-notes 1〜2・browser の image-drag を確認。T1-441 で添付 PASS。

## 制限・移管

- T1 で未確認の項目: desktop.dnd.across-displays（2 出力の guest）・content-to-desktop・apps.pdfviewer.drag-out・desktop.dnd.photo-to-notes 3〜4・text-between-windows 5 以降。dnd のシナリオは helper が無く run-aat.sh では by-agent。
- p004 のシナリオの期待（Photos からの drop は uri-list・type=2）を直す件は記録の上では未確認。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws189-p001 | 設計（画像の型、libkeiland の口、badge、落とす場所、spring-loaded、desktop、画面をまたぐ、AAT）、design-reviewer | cleared（2026-10-08） |
| ws189-p002 | libkeiland と compositor（画像の型、badge、dock の spring-loaded、画面をまたぐ） | test-wait のまま WS を閉じた（実装は main、T1 の手操作の大半を確認、across-displays・content-to-desktop は未確認） |
| ws189-p003 | app の画像・文字の drag の元と受け | test-wait のまま WS を閉じた（実装は main、pdfviewer の drag-out・photo-to-notes 3〜4 は未確認） |
| ws189-p004 | Mail の作成の添付と drop の受け | cleared（2026-10-08、T1-441） |
| ws189-p005 | 規約の全文の見直し | cleared（2026-10-09、style-check 0） |

## 記録の所在

Phase の計画・設計・結果・試験は削除した。git の commit `fb1f6afee` の `plan/ws189/` にある。Queue の履歴は [plan/history](../history/index.md)。
