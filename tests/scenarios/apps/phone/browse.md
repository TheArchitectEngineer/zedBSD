---
id: apps.phone.browse
title: Phone の連絡先を keyboard で選び、timeline を見る
status: active
areas: [phone]
paths: [userland/desktop/phone/]
machine: either
human: look
since: ws170
---

## 目的
Phone の mock（連絡先・timeline、backend は無い）が動き、keyboard で連絡先を移れることを確かめる。

## 準備
App Home から Phone を開く（`PHONE READY width= height=`）。

## 操作と確認
1. 操作: 窓の中（連絡先の一覧）を click して焦点を移し、下矢印。
   確認事項: 選択。正解: `PHONE SELECT contact=1`（2 人目）、右に timeline。確認方法: log、撮影。
2. 操作: 下矢印、上矢印。
   確認事項: 選択。正解: `SELECT contact=2`、`SELECT contact=1`。確認方法: log。
3. 操作: Ctrl+Q。
   確認事項: 終わり。正解: `PHONE DONE reason=close`、窓が消える。確認方法: log。

## 合格
1〜3 の log。見え（timeline の吹き出し、チャネルの名前）は needs-person。

## 注記
送信・通話・添付は backend が無く `PHONE NOBACKEND action=…` だけ。
