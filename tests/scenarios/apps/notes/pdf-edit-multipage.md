---
id: apps.notes.pdf-edit-multipage
title: Notes で複数頁の PDF を頁ごとに編集し、pen の線と一緒に保存して開き直す
status: draft
areas: [notes, libpdf]
paths: [userland/desktop/notes/, userland/base/libpdf/]
machine: either
human: none
since: ws175-p001
---

## 目的
複数頁の PDF の、別々の頁（回転した頁を含む）の編集が頁ごとに保たれ、pen の書き込みも残り、保存して開き直した後も編集を戻せることを確かめる（WS175、ws175-p006・p007）。

## 準備
- `/tmp/aat-samples/notes-edit.pdf`（3 頁。3 頁は `/Rotate 90`）。Notes で開き、1 頁を表示。

## 操作と確認
1. 操作: Select の道具で 1 頁の画像を右へ drag。
   確認事項: 1 頁の編集。正解: 画像が動く。確認方法: log `NOTES EDIT move page=0`。
2. 操作: PageDown を 2 回で 3 頁へ。3 頁の文字の行を click して Delete。
   確認事項: 回転した頁の編集。正解: `NOTES PAGE current=2 count=3`、行が消える。確認方法: log `NOTES EDIT delete page=2`、撮影。
3. 操作: pen の道具で 3 頁に線を 1 本描く。
   確認事項: pen。正解: `NOTES STROKE page=2`。確認方法: log。
4. 操作: Ctrl+S、Notes を閉じ、同じ file を開き直す。
   確認事項: 開き直し。正解: `NOTES OPENED pages=3 strokes=1 … edits=2 dropped=0`。1 頁の画像は右に、3 頁の行は無く線が有る。確認方法: log、撮影（1 頁と 3 頁）。
5. 操作: 3 頁で Ctrl+Z を 2 回。
   確認事項: 開き直した後の undo。正解: 線が消え、続けて行が戻る（undo の履歴は開き直しで残らないなら、Select で行の場所を click して元に戻せないことを記録し、合否は p006 の設計に合わせる）。確認方法: log `NOTES UNDO`・`NOTES EDIT undo`、撮影。
6. 操作: host へ file を取り、`qpdf --check`、`pdftoppm` で 2 頁を元の file の 2 頁と比べる。
   確認事項: 編集していない頁。正解: 2 頁の画素が元と同じ。確認方法: 画像の比較。

## 合格
1〜4・6 の正解。5 は p006 で決める undo の範囲に合わせる。

## 注記
draft。今の Notes は undo の履歴を file に保存しない（開き直すと履歴は空）。5 は「編集を元に戻す」手段（Select で物を選び Reset）を p007 で足すかと合わせて確定する。
