---
id: desktop.touchpad.preview-swipe-step
title: 3 本指の tap の preview を 2 本指で選ぶ（1 回の swipe で 1 つ）、上端・下端・横の端の 2 本指
status: draft
areas: [touchpad, compositor, switcher]
paths: [userland/desktop/wayland/swipe.c, userland/desktop/wayland/switcher-shell.c, userland/desktop/wayland/touchpad.c]
machine: hardware
human: hands
since: ws142-p009
---

## 目的
preview（switcher）の 2 本指の swipe が 1 回で 1 つ（BUG-215）、上端からの 2 本指の下で App Home（ws181-p008）、全画面から下端の 2 本指の上で最大化（BUG-228）、端の帯に 2 本のうち 1 本があれば端の gesture（ws181-p008）になることを人の指で確かめる。QEMU の注入の版は plan/ws142/tests/p010-guest.sh の 2.・7.・8. と plan/ws142/tests/p003-guest.sh の 7b.・7c.。

## 準備
5330 の desktop で 3 つ以上の app を開く。

## 操作と確認
1. 操作: 3 本指で tap、2 本指で長く右へ。
   確認事項: 選び。正解: 1 つだけ右へ（`ZWL SWITCH step … via=pad` が 1 回）。確認方法: 人、log。
2. 操作: 2 本指で下へ（または 1 本指で tap）。
   確認事項: 確定。正解: 選んだ app へ（`ZWL SWITCH commit … via=pad-swipe`・`via=pad`）。確認方法: 人、log。
3. 操作: 上端から 2 本指で下へ（1 本は端に、もう 1 本は少し内側に置く）。
   確認事項: App Home。正解: desktop が縮んで奥へ消え、App Home が奥から大きくなって現れる（指に付く。`KWL HOME pad swipe`・`KWL HOME open via=pad`）。少しだけ下げて離すと戻る（`KWL HOME pad back`）。確認方法: 人、log。
4. 操作: Terminal を F11 で全画面にし、下端から 2 本指で上へ。
   確認事項: 最大化。正解: 全画面から最大化（`ZWL GLASS fullscreen-leave … via=bottom2`、WiseView は開かない）。確認方法: 人、log。
5. 操作: 左の端に 1 本、内側に 1 本を置いて 2 本指で右へ（右の端から左へも）。
   確認事項: 仮想 desktop。正解: 隣の desktop へ（`KWL GLASS desktop=N via=pad`）。確認方法: 人、log。

## 合格
人の判断（感触）と log の行。
