<!-- awesome-plan project=zedbsd record=ws175 -->
# WS175: Notes で PDF の画像と文字を編集する

<!-- awesome-plan-current:start -->
Status: completed（2026-10-27 P3 の精査 q1608、ユーザー「…masterとWSを修正して、completedやcanceledにしてください。」「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」: 画像・文字の編集と Save Clean Copy は p001・p003〜p009 cleared、p010 は T1-265b で fail 0・needs-person 2 で人の目視だけ残り）
<!-- retained-after-completion --> Phase phase001 はその場に残す: src/・userland/・platform/・Makefile・config/ の source か build の file が path を名指す（AGENTS.md §10 の source の comment の掃除の後に消す。2026-10-28 Q1 の方針、q1660）
Completed: 2026-10-27（q1608 の精査）
Primary Milestone: MG006
Related Milestones: なし
Objectives: —
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標

Notes で PDF を開き、基本の編集ができて PDF として保存できる:

- 画像: 移動・大きさの変更・差し替え・新しい画像の挿入・削除。
- 文字: 既存の文字の編集・削除・新しい文字の挿入・font の変更。
- 複数の頁の PDF（頁の移動、頁ごとの編集）。
- 今の pen の書き込み（注釈）は保つ。

範囲外: Acrobat の全機能（form・署名・OCR・頁の並べ替え以上の頁の操作など）。

## ユーザーの決定

- 2026-10-06（由来）「Notesアプリの機能追加をお願いしたいです。PDF内の画像、テキストを編集できるようにしたいです。コメント的にペンを入れるだけでなくて、画像の位置を変えたり、サイズを変えたり、画像そのものを差し替えたり、新たに画像を入れたり。テキストも編集したり、削除したり、新たに挿入したり、フォントを変えたり。Acrobatみたいになんでもできる必要はないんです。ただ、基本的な編集ができればうれしいと思います。複数ページあるPDFも対応していなければ対応したいです。」
- 2026-10-06（設計の判断 D1〜D7、クリックの回答）「全部推奨どおり」: D1 増分の更新＋Save Clean Copy、D2 font の種類、D3 太字・斜体は出さない、D4 deflate は libz-compat に足す、D5 文字の抽出は WS175 p002、D6 画像を先・文字を後、D7 既存の文字の編集は行ごと。
- D2 の後の font の変更（ws090-p020 のユーザーの決定）: UI の font を Mahora にし、fallback は JetBrains Mono と Droid Sans Fallback の 2 つ（Inter は使わない）。置き換えの font は `keiland.ttf`・`keiland-bold.ttf`・`keiland-mono.ttf`（Mahora、Zlib）、無い字は `keiland-fallback-mono.ttf` → `keiland-fallback.ttf`。
- 2026-10-27（完了の根拠、q1608 経由）「実機確認だけが残っているWSやPhaseは、古いものはclearedにしてしまいます。」

## 結果

- libpdf: 画像・図形と文字の行の走査と Unicode の抽出、画像・図形の editor（削除・移動・大きさ・差し替え・挿入）、文字の書き換え（移動・削除・元の font での変更・ActualText）、置き換えの font の subset の埋め込みと文字の挿入、libz-compat の deflate、Save Clean Copy（全体の書き直し）。
- Notes: 物の編集の model（undo・Reset、ZNOT 2.0、journal 版 2、保存と開く時の照合）、Select・Text の道具、IME、Font/Size、File > Save Clean Copy…。
- host 試験（edit-scan・text-change・notes-edit・deflate・intake ほか）PASS。QEMU: T1-285（Save Clean Copy）PASS、T1-333 で notes.pdf-edit-text が fail でない、AAT 5 本は T1-265b で fail 0・needs-person 2（人の PNG の目視は未実施）。

## 制限・移管

- AAT の needs-person 2 本の PNG の目視は未実施。
- 規約の全文の見直し（p011、未作成）は WS の外の一括の見直しへ（2026-10-08 ユーザー「規約の整形（各 WS の全文規約の Phase）はベータ3」）。
- serif の font は範囲の外（Future Work）。

## Phase 一覧

| Phase | 目的 | 結果 |
| --- | --- | --- |
| ws175-p001 | 設計（libpdf の書き出し、content stream、font、複数頁、Notes の UI、試験） | cleared（2026-10-06、D1〜D7 は推奨どおり） |
| ws175-p002 | libpdf の走査（p002a 画像と図形、p002b 文字の行と Unicode の抽出） | in-progress のまま WS を閉じた（実装は main、host PASS、後続の Phase で使用） |
| ws175-p003 | 画像・図形の editor と content の組み立て | cleared（2026-10-06） |
| ws175-p004 | 文字の書き換えと Notes の文字の model | cleared（2026-10-06） |
| ws175-p005 | 置き換えの font と文字の挿入 | cleared（2026-10-06） |
| ws175-p006 | libz-compat の deflate、画像の取り込み | cleared（2026-10-06） |
| ws175-p007 | Notes の model（物の編集、undo、保存と照合） | cleared（2026-10-06） |
| ws175-p008 | Notes の UI（画像の段・文字の段） | cleared（2026-10-07、T1-333） |
| ws175-p009 | Save Clean Copy | cleared（2026-10-07、T1-285） |
| ws175-p010 | T1 の QEMU で AAT 5 本 | in-progress のまま WS を閉じた（T1-265b で fail 0・needs-person 2、目視は未実施） |
| ws175-p011 | 規約の全文の見直し | 未作成。WS の外の一括の見直しへ |

## 記録の所在

Phase の計画・設計・結果・試験は削除した。git の commit `fb1f6afee` の `plan/ws175/` にある。Queue の履歴は [plan/history](../history/index.md)。
