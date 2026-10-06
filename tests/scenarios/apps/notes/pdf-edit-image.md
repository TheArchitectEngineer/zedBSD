---
id: apps.notes.pdf-edit-image
title: Notes で PDF の中の画像を動かし、大きさを変え、差し替え、消す
status: draft
areas: [notes, libpdf]
paths: [userland/desktop/notes/, userland/base/libpdf/]
machine: either
human: none
since: ws175-p001
---

## 目的
Notes の Select の道具で、他の program が作った PDF の画像を移動・拡大縮小・差し替え・削除でき、undo で戻り、保存した PDF に反映されることを確かめる（WS175、ws175-p003・p007・p008）。

## 準備
- `plan/ws175/tests/make-edit-samples.py` が作った `edit-basic.pdf`（3 頁、1 頁に段落と JPEG 1 つ）を `/tmp/aat-samples/notes-edit.pdf` に置き、`sample.png`（`plan/tools/aat/scenarios/samples.py`）も同じ所に置く。
- Notes で `/tmp/aat-samples/notes-edit.pdf` を開いている（`NOTES OPENED pages=3`）。1 頁を表示。

## 操作と確認
1. 操作: toolbar の Select の道具を押す。
   確認事項: 道具。正解: Select が選ばれた状態で、toolbar の色と太さの代わりに Image・Replace・Delete・Reset が出る。確認方法: log `NOTES TOOL 16 name=select`、撮影。
2. 操作: 1 頁の JPEG の画像の中央を click。
   確認事項: 選択。正解: 画像に青い枠と四隅の handle、toolbar の Replace と Delete が押せる。確認方法: log `NOTES EDIT select page=0 object=I kind=image inserted=0 clipped=0`、撮影。
3. 操作: 画像の中央から右下へ 100 px drag。
   確認事項: 移動。正解: drag の間も画像が付いて動き（0.1 秒ごとに描き直す）、離すと右下に在り、元の場所は背景（白か元の下の物）になる。確認方法: log `NOTES EDIT move page=0 object=I dx= dy=`、撮影。
4. 操作: 画像の右下の handle を外側へ 80 px drag。
   確認事項: 大きさ。正解: 縦横比を保って大きくなる。確認方法: log `NOTES EDIT resize page=0 object=I sx=S sy=S`（sx と sy が等しい、S > 1）、撮影。
5. 操作: toolbar の Replace を押し、file chooser（題「Replace Image」）で `sample.png` を選ぶ。
   確認事項: 差し替え。正解: 枠の中に新しい画像が縦横比を保って入る。確認方法: log `NOTES EDIT replace page=0 object=I image=320x200`、撮影。
6. 操作: Ctrl+Z を 1 回。
   確認事項: undo。正解: 元の JPEG に戻る（位置と大きさは 4 の後）。確認方法: log `NOTES EDIT undo page=0 edits=N`、撮影。
7. 操作: Ctrl+Shift+Z、続けて Delete キー。
   確認事項: redo と削除。正解: PNG に戻った後、画像が消える（redo で選択は外れるので、Delete の前に画像を click して選び直す）。確認方法: log `NOTES EDIT redo`・`NOTES EDIT delete page=0 object=I`、撮影。
8. 操作: Ctrl+S。
   確認事項: 保存。正解: `NOTES SAVE reason=request … edits=N edited_pages=1 bytes=…`（N ≥ 1）。確認方法: log。
9. 操作: host へ保存した file を SSH で取り、`qpdf --check` と `pdftoppm -r 72 -f 1 -l 1` を走らせる。
   確認事項: 保存した PDF。正解: qpdf がエラーを出さない。1 頁の画素で元の画像の場所に画像が無い。2・3 頁は元の file と同じ画素。file の先頭の bytes が元の file と同じ（増分の更新）。確認方法: command の出力、画像の比較。

## 合格
1〜9 の正解。

## 注記
draft（ws175-p008 で log の文言と操作の名前を実装に合わせた。操作の帯は toolbar の Select の道具の時の Image・Replace・Delete・Reset のボタンで実装した。p010 で T1 が流して active にする）。画像の座標は撮影と `NOTES EDIT select` の行から決める。
