---
id: desktop.touchpad.preview-swipe-step
title: 3 本指の tap の preview を 2 本指で選ぶ（1 回の swipe で 1 つ）、上端・下端の 2 本指
status: draft
areas: [touchpad, compositor, switcher]
paths: [userland/desktop/wayland/swipe.c, userland/desktop/wayland/switcher-shell.c, userland/desktop/wayland/touchpad.c]
machine: hardware
human: hands
since: ws142-p009
---

## 目的
preview（switcher）の 2 本指の swipe が 1 回で 1 つ（BUG-215）、上端からの 2 本指の下で最大化から窓（BUG-224）、全画面から下端の 2 本指の上で最大化（BUG-228）になることを人の指で確かめる。QEMU の注入の版は plan/ws142/tests/p010-guest.sh の 2.・7.・8.。

## 準備
5330 の desktop で 3 つ以上の app を開く。

## 操作と確認
1. 操作: 3 本指で tap、2 本指で長く右へ。
   確認事項: 選び。正解: 1 つだけ右へ（`ZWL SWITCH step … via=pad` が 1 回）。確認方法: 人、log。
2. 操作: 2 本指で下へ（または 1 本指で tap）。
   確認事項: 確定。正解: 選んだ app へ（`ZWL SWITCH commit … via=pad-swipe`・`via=pad`）。確認方法: 人、log。
3. 操作: 窓を最大化し、上端から 2 本指で下へ。
   確認事項: 窓。正解: 窓に戻る（`ZWL GLASS undock … via=top2`）。確認方法: 人、log。
4. 操作: Terminal を F11 で全画面にし、下端から 2 本指で上へ。
   確認事項: 最大化。正解: 全画面から最大化（`ZWL GLASS fullscreen-leave … via=bottom2`、WiseView は開かない）。確認方法: 人、log。

## 合格
人の判断（感触）と log の行。
