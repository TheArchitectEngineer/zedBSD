---
id: apps.photos.browse
title: Photos で ~/Pictures の写真を月ごとに見て、1 枚を全面に、回転・お気に入り・slideshow を確かめる
status: active
areas: [photos]
paths: [userland/desktop/photos/, userland/desktop/picture/]
machine: either
human: look
since: ws157
---

## 目的
`~/Pictures` の JPEG・PNG・GIF が日付（EXIF、無ければ file の時刻）の新しい順に月ごとの grid に出て、album（`~/Pictures` の 1 段目の folder）に分かれ、
1 枚を全面に出し、前後へ移り、回転（元の file は書き換えない）とお気に入りが `~/.config/keiland/photos.conf` に残り、slideshow が進むことを確かめる（WS157 p002・p003）。

## 準備
host で `plan/ws157/tests/make-photos.py --view` が 9 枚を作る（2026 年 9 月 2 枚・8 月 3 枚・7 月 4 枚、album Family・Trips、EXIF の向きで回る JPEG（hill.jpg）、PNG、GIF、壊れた JPEG）。
kei の `~/Pictures` に置き、PNG・GIF・壊れた JPEG の時刻を `touch -t` で 7 月にする。

## 操作と確認
1. 操作: App Home から Photos を開く。
   確認事項: 一覧。正解: `PHOTOS LIBRARY photos=9 albums=2 error=0`、`PHOTOS THUMB photo=8 error=`（壊れた file だけ縮小画像ができない）。左に Timeline・Favorites・Family・Trips、右に September・August・July の見出しと縮小画像。確認方法: log、撮影。
2. 操作: September の 2 枚目を double click。
   確認事項: 全面。正解: `PHOTOS OPEN photo=1 name=hill.jpg`、`PHOTOS PICTURE photo=1 error=0 width=640 height=480`（EXIF の向きで横長に立つ）。確認方法: log、撮影。
3. 操作: →、R、F。
   確認事項: 次・回転・お気に入り。正解: `PHOTOS OPEN photo=2 name=sea.jpg`、`PHOTOS TURN photo=2 turns=1`、`PHOTOS FAVORITE photo=2 on=1`、`PHOTOS SAVE error=0`。`photos.conf` に `favorite …/Trips/sea.jpg` と `turn 1 …/Trips/sea.jpg`。確認方法: log、file、撮影（縦に回り、button が Unfavorite）。
4. 操作: Esc。
   確認事項: grid へ戻る。正解: `PHOTOS BACK photo=2`。確認方法: log。
5. 操作: Space、待つ、Space。
   確認事項: slideshow。正解: `PHOTOS SLIDESHOW on=1 photo=2`、6 秒ほどで `PHOTOS SLIDE photo=4`、`PHOTOS SLIDESHOW on=0`。確認方法: log、撮影。
6. 操作: Photos を閉じ、kei として `/bin/photos`、左の Favorites を click。
   確認事項: 印が残る。正解: `PHOTOS MARKS error=0`、`PHOTOS LIST list=1 album=0`、sea.jpg が心の印と一緒に 1 枚。確認方法: log、撮影。

## 合格
1〜6 の正解。撮影は人が見る（月の見出し、壊れた file の灰色の四角、回った写真、Favorites）。

## 注記
助け: `plan/tools/aat/scenarios/helpers_photos.py`（host の PIL で写真を作り、kei の `~/Pictures` に置き、終わりに `~/Pictures` と `photos.conf` を消す）。窓の中の位置は view.c の layout（左の列は窓の幅の 0.24（200〜260）、glass では 8 の隙間、grid は列の左から 20、最初の月の行は上から 116、約 150 の正方形が 6 の間隔）。
