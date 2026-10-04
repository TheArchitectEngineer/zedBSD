<!-- awesome-plan project=zedbsd record=ws089-p024 -->

# ws089-p024: Mouse の頁を device ごと（マウス・タッチパッド）の設定にし、pointer の加速を足す。既定は base 150%・加速 強め、自然な方向のスクロールはタッチパッド ON・マウス OFF

Status: cleared（2026-10-05 Q1: T1-152 の settings-p005 PASS（touchpad.png）と T1-154 の settings-p007 PASS、touchpad の頁の古い注記を Gestures の card に直した（P2 38972c47、settings-render で目視）。加速の効きは host の試験、体感は 5330 の UAT。タッチパッドの既定（100%・中）はユーザーの判断待ち（master））。以前: in-progress（実装済み、T1 待ち）
Disposition: normal
Parent: [WS089](../ws.md)
Queue: q728（P2、2026-10-05）

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
5b. **Touchpad の頁の項目**（2026-10-04 夜 ユーザー「タッチパッドにも速度と加速とスクロールを追加してほしいです。」）: 速度（Pointer speed）・加速・スクロール（自然な方向の switch、既定 ON。2 本指のスクロールの速さを入れるかは設計で決める）。
6. Touchpad の頁の他の項目（tap で click、2 本指のスクロールの速さ、gesture の on・off）は [WS142](../../ws142/ws.md)（gesture）と [BUG-166](../../bugs/BUG-166.md)（押し込み＋タップドラッグの仕様）に合わせて設計で決める。
7. Linux・FreeBSD の Keiland も同じ compositor の曲線と種類ごとの設定を使う。

## 受け入れ（案）

- 加速の設定を変えると、同じ手の動きでも速い動きほど pointer が大きく動く（host の試験で曲線の値、QEMU で相対の移動の注入）。
- 新しい desktop.conf で base 150%・加速 強めが既定、自然な方向のスクロールがタッチパッド ON・マウス OFF の既定になる。Settings の表示と合う。マウスとタッチパッドの設定が互いに影響しない。
- 実機（5330 のタッチパッドと USB マウス）で操作感を UAT。
- C の全文の規約、build warning 0。

## 依存

compositor の入力（WS099・WS081）、kl_settings_*（WS135）。

## 設計（2026-10-05、P2）

- **種類ごと**（device ごとではなく）: compositor の入力の device の種類で選ぶ。touchpad の層（`touchpad.c`、multitouch protocol B で INPUT_PROP_POINTER の node）に付いた device は **touchpad**、それ以外の相対の pointer（USB・PS/2 の mouse、PS/2 の mouse に見える touchpad、trackpoint）は **mouse**。絶対の pointer（tablet・touch screen）は対象外（今まで通り）。
- **設定の key**（`settings-keys.c`、compositor が解決し desktop.conf に保つ）:

| key | 範囲 | 既定 | 意味 |
| --- | --- | --- | --- |
| `mouse.speed` | 25〜300 | **150** | 速さ（%） |
| `mouse.acceleration` | 0〜3 | **3（Strong）** | 加速の段階（0 None・1 Mild・2 Medium・3 Strong） |
| `mouse.natural` | 0・1 | **0** | wheel の向きを逆に |
| `touchpad.speed` | 25〜300 | 100 | 速さ（%） |
| `touchpad.acceleration` | 0〜3 | 2（Medium） | 加速の段階（Medium は ws159-p004 で合わせた今の曲線） |
| `touchpad.natural` | 0・1 | **1** | 2 本指のスクロールが指に付いて動く |

- **移行**: 旧 `pointer.speed`・`pointer.natural` は表に残し（`KEPT | READ_ONLY`: file からは読むが client からは変えられない）、session の開始で file に旧い値があり新しい key に値が無ければ、その値を `mouse.speed`・`mouse.natural` に選ぶ（`ZWL SETTINGS migrated ...`、session の終わりに新しい key で書かれる）。値が無ければ新しい既定。
- **mouse の加速の曲線**（`wayland/pointer-accel.c`、新規、host で試験）: 報告ごとの移動（count）を速さ（%）× gain（1/256）で pixel に。gain は mouse の速さ（count/秒、前の動いた報告からの時間で 1〜50 ms に制限、休みの後の最初の報告は 50 ms）で、400 以下は 1、4000 以上は段階の最大（None 1.0・Mild 1.5・Medium 2.25・Strong 3.0）、間は比例。端数は次の報告へ持ち越す（device ごと）。None・100% は以前と同じ（1 count = 1 pixel）。
- **touchpad の曲線**（`touchpad.c`）: 既存の速さで gain が増える曲線（px/mm）の両端を段階で選ぶ: None 8→8、Mild 6→11、Medium 5→15（今の値）、Strong 4→20。速さ（%）は今まで通り層の出力に掛ける（`pointer_move`）。`zwl_touchpad_set_feel` で段階と自然なスクロールを渡し、設定が変わると付いている全ての touchpad に渡し直す（`zwl_input_touchpads_changed`）。
- **Settings**: Mouse の頁に Pointer speed・Acceleration（None〜Strong の 4 段の slider）・Natural scrolling。stub だった Touchpad の頁を実装し、同じ 3 項目。Home の summary・検索の項目も。tap・gesture の on/off・2 本指のスクロールの速さは範囲の 6 のとおり WS142・BUG-166 に合わせて後（頁の注記に「coming later」）。
- 控えの数: compositor の設定の entry の上限 `ZWL_SETTINGS_ENTRIES` を 16 → 24（compositor の key が 15 になった）。
- **Q1・ユーザーに確かめたい点**: touchpad の既定は速さ 100%・Medium（WS159 で 5330 に合わせた感触を保つ）にした。要望の「150%・強め」は Mouse の既定として入れた。touchpad も 150%・Strong にするかは判断を仰ぐ。

## 確認（2026-10-05）

- host: `plan/ws089/tests/run-host-pointer-accel.sh` PASS（None 100% で 1:1、gain の両端と中間、段階の順、同じ 2000 count が遅いと 2000 px・速いと約 3 倍、150% の端数の持ち越し、休みの後の最初の報告）。`plan/ws159/tests/run-host-touchpad.sh`（25）・`plan/ws142/tests/run-host-gesture.sh`（50）PASS（Medium で以前と同じ）。`plan/ws089/tests/host-build.sh` の settings-render で Mouse・Touchpad の頁を描いて目視。
- build（warning 0）: zedBSD の wayland・settings・keiland-settings、Linux の Keiland（-Werror）。`plan/tools/keiland-os-boundary/check.sh` PASS。style-check: 新しい file は違反 0、変えた既存の file に新しい違反 0。
- QEMU（T1 に依頼）: `plan/ws089/tests/settings-p005.sh`（Mouse の speed・acceleration・natural、Touchpad の頁の speed・natural）、`settings-p007.sh`（新しい key の適用、`pointer.speed=200` の移行）。QEMU の pointer は tablet（絶対）なので加速の効き目は host の試験で見る。
- 未実施: 実機（5330 の touchpad と USB mouse の操作感）は UAT。

## T1-152・154 の後（2026-10-05）

- Q1: Touchpad の頁の注記が「Tapping and gestures are coming in a later version of Kei.」のまま → 注記を **Gestures の card** に替えた（Click: Tap with one finger、Scroll: Move two fingers、Switch windows: Tap with three fingers、Wiseview, the desktop: Two fingers in from an edge）。settings-render で幅 900 と 1180 を目視。

## Q1 の判定（2026-10-05）

T1-152 の settings-p005 PASS（touchpad.png）と T1-154 の settings-p007 PASS、touchpad の頁の古い注記を Gestures の card に直した（P2 38972c47、settings-render で目視）。加速の効きは host の試験、体感は 5330 の UAT。タッチパッドの既定（100%・中）はユーザーの判断待ち（master）。**cleared**。
