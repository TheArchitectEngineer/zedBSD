<!-- awesome-plan project=zedbsd record=ws089-p024 -->

# ws089-p024: Mouse の頁を device ごと（マウス・タッチパッド）の設定にし、pointer の加速を足す。既定は base 150%・加速 強め、自然な方向のスクロールはタッチパッド ON・マウス OFF

Status: planning
Disposition: normal
Parent: [WS089](../ws.md)
Queue: 未定

## ユーザーの要望（2026-10-04 夜、UAT-3 の後、原文）

「Settingsのマウスは、Pointer Speedのほかに加速も設定できるようにしてほしい。デフォルトでベーススピード150%、加速強めにしてほしい。」

## 範囲

1. Settings の Mouse の頁に、今の Pointer Speed（base の速さ）に加えて **加速（acceleration）** の設定を足す（slider か段階の選択、「無し」も選べるようにするかは設計で決める）。
2. compositor の pointer の加速の曲線（入力の速さに応じた倍率）を実装・調整する。設定は kl_settings_* → compositor の desktop.conf（Guardrail の「app と設定」: desktop.conf を書くのは compositor だけ）。
3. **既定の値**（ユーザー）: base の速さ 150%、加速は強め。既存の利用者の desktop.conf に値が無い時もこの既定にする。
4. **device ごとに設定を分ける**（2026-10-04 夜 ユーザー「Settingsのマウスは、マウスとタッチパッドなど、デバイスごとに設定を分けてほしい。自然な方向のスクロールは、タッチパッドではデフォルトでON、マウスではOFFにしてほしい。」）: マウスとタッチパッド（必要ならトラックポイントなど）で、Pointer Speed・加速・自然な方向のスクロールを別々に持つ。compositor は入力の device の種類（evdev の property・PS/2 か USB の HID か、タッチパッドの判定）で設定を選ぶ。種類ごとの設定か、device（名前・ID）ごとの設定かは設計で決める（まず種類ごと）。
5. **自然な方向のスクロールの既定**（ユーザー）: タッチパッドは ON、マウスは OFF。
6. Linux・FreeBSD の Keiland も同じ compositor の曲線と種類ごとの設定を使う。

## 受け入れ（案）

- 加速の設定を変えると、同じ手の動きでも速い動きほど pointer が大きく動く（host の試験で曲線の値、QEMU で相対の移動の注入）。
- 新しい desktop.conf で base 150%・加速 強めが既定、自然な方向のスクロールがタッチパッド ON・マウス OFF の既定になる。Settings の表示と合う。マウスとタッチパッドの設定が互いに影響しない。
- 実機（5330 のタッチパッドと USB マウス）で操作感を UAT。
- C の全文の規約、build warning 0。

## 依存

compositor の入力（WS099・WS081）、kl_settings_*（WS135）。
