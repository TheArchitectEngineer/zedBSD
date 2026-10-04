<!-- awesome-plan project=zedbsd record=ws099-p031 -->

# ws099-p031: 上部の system bar の高さを窓の title bar に揃える（34 → 44 px）

Status: planning
Disposition: normal
Parent: [WS099](../ws.md)
Queue: 未定（q700 の窓の操作の Bug と同じ担当が続けて行う案）

## ユーザーの指示（2026-10-04 夜、原文）

「スクリーン上部のバーが、ウィンドウタイトルバーより高さが小さいです。サイズを揃えて、スクリーン上部のバーの高さを大きくします。」

## 今の値（Q1 が source で確かめた）

`userland/desktop/wayland/zwl.h:78-79`: system bar `ZWL_GLASS_BAR` = 34、title bar `ZWL_GLASS_TITLE` = 44（px、scale 1 の時）。

## 範囲

1. system bar の高さを title bar と同じ 44 にする（`ZWL_GLASS_BAR` を 44 に。値を 1 か所の定数から導き、二つが離れないように `ZWL_GLASS_BAR` を `ZWL_GLASS_TITLE` から定義するかを決める）。
2. 高さに依る物を全部合わせる: `ZWL_GLASS_TOP`・`ZWL_GLASS_DOCK_TOP`（最大化した窓の本体の上端）、system bar の中の要素の縦の位置と大きさ（`shell.c` の `BAR_LAUNCHER_Y`・`BAR_LAUNCHER_SIZE` など、時計・IME の status・WiFi・音量・電池の icon、popup の位置）、最大化の時に title が bar へ吸着する表示（ws035-p062 のドッキング）、HiDPI の scale。
3. 固定の座標を持つ試験（title bar・menu・system bar の click の位置、PNG の比較）を合わせて直す（見つけた担当がその場で直す）。
4. [WS142](../../ws142/ws.md)（上部の bar のアプリの一覧）・[BUG-180](../../bugs/BUG-180.md)（最大化からのドラッグ）と同じ所なので、順を合わせる。

## 受け入れ（案）

- system bar と窓の title bar の高さが同じに見える（QEMU の PNG、実機の UAT）。最大化した窓の title の吸着の位置、bar の要素の縦の中央、popup の位置がずれない。
- 既存の C 基準・title bar・menu の試験が直した座標で PASS（T1）。
- C の全文の規約、build warning 0。
