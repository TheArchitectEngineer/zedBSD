---
id: apps.settings.about
title: About に OS の版と kernel が出る
status: active
areas: [settings]
paths: [userland/desktop/settings/page-about.c, userland/desktop/settings/about.c]
machine: either
human: none
since: ws089-p027
---

## 目的
About の頁の版の名前が `/etc/os-release` の PRETTY_NAME、kernel の行が uname と合うことを確かめる（UAT 7.1）。

## 準備
Settings の About の頁。

## 操作と確認
1. 操作: 頁を撮る。
   確認事項: 版と kernel。正解: `ZSETTINGS ABOUT system=… kernel=…` の system が PRETTY_NAME（`Kei/zedBSD 1.0.0 Beta 1 …`）、kernel が `uname` の出力と同じ。確認方法: log と root の `cat /etc/os-release`・`uname -a`、撮影。

## 合格
値が合う。
