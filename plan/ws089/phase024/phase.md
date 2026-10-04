<!-- awesome-plan project=zedbsd record=ws089-p024 -->

# ws089-p024: Mouse の頁を device ごと（マウス・タッチパッド）の設定にし、pointer の加速を足す。既定は base 150%・加速 強め、自然な方向のスクロールはタッチパッド ON・マウス OFF

Status: planning
Disposition: normal
Parent: [WS089](../ws.md)
Queue: 未定

## ユーザーの要望（2026-10-04 夜、UAT-3 の後、原文）

「Settingsのマウスは、Pointer Speedのほかに加速も設定できるようにしてほしい。デフォルトでベーススピード150%、加速強めにしてほしい。」

## 今の Settings（2026-10-04 夜 Q1 が source で確かめた）

Settings には既に **Mouse** と **Touchpad** の頁が別にある（`userland/desktop/settings/pages.c:39-40`）。Mouse の頁は実装済み（Pointer speed の slider と Natural scrolling の switch、`page-input.c`、設定の key は `pointer.speed`・`pointer.natural` で、今は 1 組を全ての device が共有）。**Touchpad の頁は stub**（`se_soon_draw`、「Gestures, tapping and scrolling.」）。compositor の pointer の速さは一定の倍率だけで加速は無い（`userland/desktop/wayland/input.c:722-726`、既定 100）。

## 範囲

1. Settings の Mouse の頁に、今の Pointer Speed（base の速さ）に加えて **加速（acceleration）** の設定を足す（slider か段階の選択、「無し」も選べるようにするかは設計で決める）。
2. compositor の pointer の加速の曲線（入力の速さに応じた倍率）を実装・調整する。設定は kl_settings_* → compositor の desktop.conf（Guardrail の「app と設定」: desktop.conf を書くのは compositor だけ）。
3. **既定の値**（ユーザー）: base の速さ 150%、加速は強め。既存の利用者の desktop.conf に値が無い時もこの既定にする。
4. **device ごとに設定を分ける**（Mouse の頁はマウスの設定、stub の **Touchpad の頁を実装**してタッチパッドの設定を置く。設定の key を `pointer.*` から `mouse.*`・`touchpad.*` に分け、今の `pointer.speed`・`pointer.natural` の値はマウスの既定へ移す）（2026-10-04 夜 ユーザー「Settingsのマウスは、マウスとタッチパッドなど、デバイスごとに設定を分けてほしい。自然な方向のスクロールは、タッチパッドではデフォルトでON、マウスではOFFにしてほしい。」）: マウスとタッチパッド（必要ならトラックポイントなど）で、Pointer Speed・加速・自然な方向のスクロールを別々に持つ。compositor は入力の device の種類（evdev の property・PS/2 か USB の HID か、タッチパッドの判定）で設定を選ぶ。種類ごとの設定か、device（名前・ID）ごとの設定かは設計で決める（まず種類ごと）。
5. **自然な方向のスクロールの既定**（ユーザー）: タッチパッドは ON、マウスは OFF。
6. Touchpad の頁の他の項目（tap で click、2 本指のスクロールの速さ、gesture の on・off）は [WS142](../../ws142/ws.md)（gesture）と [BUG-166](../../bugs/BUG-166.md)（押し込み＋タップドラッグの仕様）に合わせて設計で決める。
7. Linux・FreeBSD の Keiland も同じ compositor の曲線と種類ごとの設定を使う。

## 受け入れ（案）

- 加速の設定を変えると、同じ手の動きでも速い動きほど pointer が大きく動く（host の試験で曲線の値、QEMU で相対の移動の注入）。
- 新しい desktop.conf で base 150%・加速 強めが既定、自然な方向のスクロールがタッチパッド ON・マウス OFF の既定になる。Settings の表示と合う。マウスとタッチパッドの設定が互いに影響しない。
- 実機（5330 のタッチパッドと USB マウス）で操作感を UAT。
- C の全文の規約、build warning 0。

## 依存

compositor の入力（WS099・WS081）、kl_settings_*（WS135）。
