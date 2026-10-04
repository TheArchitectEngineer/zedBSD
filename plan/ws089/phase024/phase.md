<!-- awesome-plan project=zedbsd record=ws089-p024 -->

# ws089-p024: Mouse の頁に pointer の加速の設定を足す、既定を base の速さ 150%・加速は強めに

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
4. マウスとタッチパッドで同じ設定にするか分けるかを設計で決める（タッチパッドの頁があれば揃える）。Linux・FreeBSD の Keiland も同じ compositor の曲線を使う。

## 受け入れ（案）

- 加速の設定を変えると、同じ手の動きでも速い動きほど pointer が大きく動く（host の試験で曲線の値、QEMU で相対の移動の注入）。
- 新しい desktop.conf で base 150%・加速 強めが既定になる。Settings の表示と合う。
- 実機（5330 のタッチパッドと USB マウス）で操作感を UAT。
- C の全文の規約、build warning 0。

## 依存

compositor の入力（WS099・WS081）、kl_settings_*（WS135）。
