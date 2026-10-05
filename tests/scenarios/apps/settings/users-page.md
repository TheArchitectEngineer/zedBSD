---
id: apps.settings.users-page
title: Users の頁に自分の account と利用者の一覧
status: active
areas: [settings, accounts]
paths: [userland/desktop/settings/page-users.c, userland/desktop/settings/page-users-admin.c]
machine: either
human: look
since: ws160-p002
---

## 目的
Users の頁が自分の account・password の欄・利用者の一覧を出すことを確かめる（UAT 6.6）。

## 準備
Settings の Users の頁。

## 操作と確認
1. 操作: 頁を撮る。
   確認事項: 中身。正解: 「Your account」に kei、「Password」に 3 つの欄、利用者の一覧（kei ほか）、管理者なら Manage users の card。確認方法: log `ZSETTINGS USERS account name=kei`、`ZSETTINGS USERS list count=N`（N ≥ 1）、撮影（人が見る）。

## 合格
log の正解。見えは needs-person。
