---
id: apps.photos.browse
title: Photos で写真を取り込み、月ごとに見て、1 枚を全面に、回転・お気に入り・album・slideshow を確かめる
status: active
areas: [photos]
paths: [userland/desktop/photos/, userland/desktop/picture/]
machine: either
human: look
since: ws157
---

## 目的
写真が取り込みで `~/Pictures/Library/img/YYYY/MM/DD/<元の名前>` に複写され、データベース（`~/Pictures/Library/db/photos/YYYY-MM.tsv`、`db/albums/<id>.album`）に
入り、日付の新しい順に月ごとの grid に出て、1 枚を全面に出し、前後へ移り、回転とお気に入りが月の行に、album が album の file に残り、slideshow が進み、
同じ写真は 2 度取り込まれないことを確かめる（WS157 p004・p005、2026-10-07 ユーザーの要件）。

## 準備
host で `plan/ws157/tests/make-photos.py --view` が 9 枚を作る（2026 年 9 月 2 枚・8 月 3 枚・7 月 4 枚、EXIF の向きで回る JPEG（hill.jpg）、PNG、GIF、壊れた JPEG）。
kei の `~/AATPhotos` に置き、PNG・GIF・壊れた JPEG の時刻を `touch -t` で 7 月にする。`~/Pictures/Library` と縮小画像の cache（`~/.cache/keiland/photos`）は無い。

## 操作と確認
1. 操作: kei として `/bin/photos --import=/home/kei/AATPhotos`。
   確認事項: 取り込みと一覧。正解: `PHOTOS IMPORT done ... imported=9 duplicates=0 failed=0 error=0 save=0`、`img/2026/09/20/lake.jpg`・`img/2026/07/14/sticker.png`・
   `db/photos/2026-09.tsv`・`2026-07.tsv` がある、`PHOTOS THUMB photo=8 error=`（壊れた file だけ縮小画像ができない）、September・August・July の見出し。確認方法: log、ls、撮影。
2. 操作: September の 2 枚目を double click。
   確認事項: 全面。正解: `PHOTOS OPEN photo=1 name=hill.jpg`、`PHOTOS PICTURE photo=1 error=0 width=640 height=480`。確認方法: log、撮影。
3. 操作: →、R、F。
   確認事項: 次・回転・お気に入り。正解: `PHOTOS OPEN photo=2 name=sea.jpg`、`PHOTOS TURN photo=2 turns=1`、`PHOTOS FAVORITE photo=2 on=1`、`PHOTOS SAVE error=0`、
   `db/photos/2026-08.tsv` の sea.jpg の行の終わりが `1<TAB>1<TAB>sea.jpg`。確認方法: log、file、撮影。
4. 操作: A、card の名前の欄を click、`Sea`、Enter。
   確認事項: album。正解: `PHOTOS CARD album photo=2`、`PHOTOS ALBUM create name=Sea error=0`、`PHOTOS ALBUM add album=Sea photo=2 error=0`、`db/albums/*.album` に
   `name<TAB>Sea` と `photo<TAB>` の 1 行。確認方法: log、file。
5. 操作: Esc、Space、待つ、Space、Esc。
   確認事項: slideshow。正解: `PHOTOS SLIDESHOW on=1 photo=2`、6 秒ほどで `PHOTOS SLIDE photo=4`、`PHOTOS SLIDESHOW on=0`。確認方法: log、撮影。
6. 操作: Photos を閉じ、もう一度 `/bin/photos --import=/home/kei/AATPhotos`、左の album（Sea）を click。
   確認事項: 読み戻しと重複。正解: `imported=0 duplicates=9`、`PHOTOS LIBRARY ... photos=9 albums=1`、`PHOTOS LIST list=2 album=0`、cache に縮小画像が 8 つ。確認方法: log、ls、撮影。

## 合格
1〜6 の正解。撮影は人が見る（月の見出し、壊れた file の灰色の四角、回った写真、album の Sea と心の印）。

## 注記
助け: `plan/tools/aat/scenarios/helpers_photos.py`（host の PIL で写真を作り、kei の `~/AATPhotos` に置き、終わりに `~/AATPhotos`・`~/Pictures/Library`・cache を消す）。
窓の中の位置は view.c の layout（左の列は窓の幅の 0.24（200〜260）、glass では 8 の隙間、grid は列の左から 20、最初の月の行は上から 116、約 150 の正方形が 6 の間隔。
album の card は幅 360 で真ん中、album が無い時の高さ 160、名前の欄は上から 56・左から 16）。
