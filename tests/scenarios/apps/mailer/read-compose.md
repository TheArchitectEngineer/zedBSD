---
id: apps.mailer.read-compose
title: Mail で次の message を開き、新しい message を書き始めて取りやめる
status: active
areas: [mailer]
paths: [userland/desktop/mailer/]
machine: either
human: look
since: ws169
---

## 目的
Mail の mock（受信箱の一覧・本文・作成、backend は無い）が動くことを確かめる。

## 準備
App Home から Mail を開く（`MAIL READY`）。

## 操作と確認
1. 操作: 窓の中（一覧）を click、下矢印。
   確認事項: 本文。正解: `MAIL OPEN message=N`。確認方法: log、撮影。
2. 操作: Ctrl+N。
   確認事項: 作成。正解: `MAIL COMPOSE kind=new`、宛先・件名・本文の欄。確認方法: log、撮影。
3. 操作: Esc（または取りやめの button）。
   確認事項: 取りやめ。正解: 作成が閉じる（`MAIL COMPOSE kind=cancel` か一覧に戻る）。確認方法: log、撮影。

## 合格
1・2 の log。3 と見えは撮影。

## 注記
送信・受信・Archive・Delete は `MAIL NOBACKEND action=…` だけ。
