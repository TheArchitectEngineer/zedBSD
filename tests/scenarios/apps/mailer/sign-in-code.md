---
id: apps.mailer.sign-in-code
title: メールで届いた認証 code を Browser が受け取り、欄に入れる
status: active
areas: [mailer, browser, compositor]
paths: [userland/desktop/mailer/, userland/desktop/wayland/mail-shell.c, userland/desktop/browser/shell/mail.c, plan/tools/mail/fake-mail-server.py]
machine: either
human: hands
since: ws169
---

## 目的
Mail が受けた新しいメールの認証 code が compositor（kl_system_mail_v1）を通って、許された Browser に届き、Browser の titlebar の control から page の欄に入ることを確かめる（WS169 p002・p005）。code は log に出ない。

## 準備
read-compose と同じ偽の server（`--arrivals 1`: Mail が IDLE に入ると 0.5 秒後に code 7351 のメールが 1 通届く）と target の準備。page `/tmp/aat-work/code.html`（autofocus の input、入力の長さを console に出す）。

## 操作と確認
1. 操作: Browser で `/tmp/aat-work/code.html` を開き、page を click。
   確認事項: 聞く。正解: `ZBROWSER MAIL listen error=0`。確認方法: log。
2. 操作: Mail を起動し、form に read-compose と同じく入れ、「Sign-in codes」の switch を on（`MAIL CODES allowed=1`）、Sign In。
   確認事項: 新着と code。正解: `MAIL MESSAGE account=0 folder=Inbox uid=N arrived=1 code=1`、`KWL MAIL arrived … code=4 told=1`、`ZBROWSER MAIL code length=4 titlebar=1`。どの log の行にも `7351` が無い。確認方法: log。
3. 操作: Mail を閉じ、Browser を前に。titlebar の先頭の control「Code 7351」を click。
   確認事項: 入力。正解: `ZBROWSER MAIL fill length=4 error=0`、`ZBROWSER CONSOLE` の `code-length=4`、欄に 7351。titlebar の control が消える（`ZBROWSER TITLEBAR code=0`）。確認方法: log、撮影。

## 合格
1〜3 の正解。

## 注記
titlebar の control の位置は log に無いので、3 は撮影を見て人（または T1 の QMP の pointer）が押す。通知の popup（WS156 p003）ができたら、通知の click でも入る（`ZBROWSER MAIL code … notified=1`）。助け: `plan/tools/aat/scenarios/helpers_mailer.py`。
