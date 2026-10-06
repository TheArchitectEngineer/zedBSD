---
id: apps.calendar.event
title: Calendar で予定を足して保存し、system bar の時計から開く
status: active
areas: [calendar, compositor]
paths: [userland/desktop/calendar/, userland/desktop/wayland/shell.c, userland/desktop/wayland/home.c]
machine: either
human: look
since: ws155
---

## 目的
予定が `~/Documents/Calendar` に iCalendar の file として残り、system bar の時計で Calendar が開くことを確かめる（WS155 p002〜p004）。

## 準備
kei の `~/Documents/Calendar` を消す。App Home から Calendar を開く（`CALENDAR READY`、`CALENDAR CELL today x= y= width= height=`）。

## 操作と確認
1. 操作: 右の panel の Work の card を今日の cell へ drag。
   確認事項: 編集。正解: `CALENDAR EDIT new list=Work`、panel に「New Event」の form（題「New Work Event」、From 09:00 to 10:00、Work）。確認方法: log、撮影。
2. 操作: Save。
   確認事項: 保存。正解: `CALENDAR EVENT saved index=N list=Work … all_day=0 start=540 end=600 error=0`、`~/Documents/Calendar/Work/` に `SUMMARY:New Work Event` の .ics が 1 つ、今日の cell に pill。確認方法: log、target の file、撮影。
3. 操作: Calendar を閉じ、画面右上の時計を click。
   確認事項: 時計から開く。正解: `KWL HOME open name=Calendar via=clock`、`CALENDAR READY`。確認方法: log、撮影。

## 合格
1〜3 の正解。

## 注記
助け: `plan/tools/aat/scenarios/helpers_calendar.py`（前後に folder を消す）。
