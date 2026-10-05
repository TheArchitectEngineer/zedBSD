---
id: apps.notes.pdf-insert-image
title: Notes で PDF に PNG と JPEG の画像を挿入する
status: draft
areas: [notes, libpdf]
paths: [userland/desktop/notes/, userland/base/libpdf/, userland/base/libz-compat/]
machine: either
human: none
since: ws175-p001
---

## 目的
Image の道具で PNG（alpha つき）と JPEG を PDF の頁に挿入でき、保存・開き直しの後も編集できることを確かめる（WS175、ws175-p005・p006・p007）。

## 準備
- `/tmp/aat-samples/notes-edit.pdf`（`edit-basic.pdf`）、`sample.jpg`（`samples.py`）、`alpha.png`（`make-edit-samples.py` が作る半透明の PNG）。
- Notes で `notes-edit.pdf` を開き、2 頁を表示（`>` を 1 回）。

## 操作と確認
1. 操作: toolbar の Image の道具を押し、file chooser で `alpha.png` を選ぶ。
   確認事項: 挿入。正解: 頁の中央に画像が選ばれた状態で現れ、透けた部分から下の頁が見える。確認方法: log `NOTES EDIT insert page=1 kind=image object=I`、撮影。
2. 操作: 挿入した画像を左上へ drag。
   確認事項: 移動。正解: 画像が動く。確認方法: log `NOTES EDIT move page=1 object=I`。
3. 操作: もう一度 Image の道具で `sample.jpg` を選ぶ。
   確認事項: 2 つ目の挿入。正解: 中央に JPEG。確認方法: log `NOTES EDIT insert page=1 kind=image object=J`（J ≠ I）。
4. 操作: Ctrl+S、Notes を閉じ、同じ file を Notes で開き直して 2 頁へ。
   確認事項: 開き直し。正解: 2 つの画像が同じ場所に見え、click で選べる。確認方法: log `NOTES OPENED … edits=N dropped=0`、`NOTES EDIT select page=1 … kind=image`、撮影。
5. 操作: host へ file を取り、`qpdf --check`、`pdfimages -list`（無ければ `pdftoppm`）。
   確認事項: 保存した PDF。正解: qpdf がエラーを出さない。2 頁に画像が 2 つ増え、PNG の画像に SMask が有る。確認方法: command の出力。

## 合格
1〜5 の正解。

## 注記
draft。file の大きさ（deflate の有無、D4）は記録するが合否に使わない。
