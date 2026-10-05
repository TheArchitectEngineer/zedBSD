---
id: apps.settings.change-password
title: Users の頁で password を変える（間違い・短い・正しい）
status: active
areas: [settings, accounts, compositor]
paths: [userland/desktop/settings/page-users.c, userland/desktop/wayland/system.c, userland/base/passwd/]
machine: either
human: look
since: ws160-p002
---

## 目的
Settings の password の変更が、今の password の誤りと短い password を拒み、正しい時に変えることを確かめる（UAT 6.7〜6.9）。

## 準備
root で `/etc/shadow` を控える（最後に戻す）。Settings の Users の頁。

## 操作と確認
1. 操作: 1 つ目の欄（今の password）を click し `wrong-pass`、Tab、`aat-pass-1`、Tab、`aat-pass-1`、Enter。
   確認事項: 拒否。正解: 「The current password is wrong.」、`ZSETTINGS USERS result request=N errno=…`（0 でない）、shadow が変わらない。確認方法: log、撮影、shadow の比較。
2. 操作: 欄に `kei`、Tab、`short`、Tab、`short`、Enter。
   確認事項: 拒否。正解: 「The new password is not accepted …」、shadow が変わらない。確認方法: 撮影、shadow の比較。
3. 操作: 欄に `kei`、Tab、`aat-pass-1`、Tab、`aat-pass-1`、Enter。
   確認事項: 変更。正解: 「Your password is changed.」、`ZSETTINGS USERS result … errno=0`、shadow の kei の行が変わり、欄が空になる。確認方法: log、撮影、shadow の比較。
4. 操作: 控えた `/etc/shadow` を戻す（root）。

## 合格
1〜3 の正解（文の見えは needs-person）。

## 注記
欄は control 1〜3（`USERS_FIELD_FIRST`）、Change Password は control 11。Esc で欄が空になる。
