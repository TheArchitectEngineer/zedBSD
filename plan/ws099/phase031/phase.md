<!-- awesome-plan project=zedbsd record=ws099-p031 -->

# ws099-p031: 上部の system bar の高さを窓の title bar に揃える（34 → 44 px）

Status: in-progress（実装済み・T1 の試験待ち）
Disposition: normal
Parent: [WS099](../ws.md)
Queue: q700-i01（P1 generation17、2026-10-05）

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

## 実装（2026-10-05 P1 generation17 / q700-i01。build まで、QEMU・実機は未実施）

- 決め: `ZWL_GLASS_BAR` を `ZWL_GLASS_TITLE`（44）から定義した（`zwl.h`。二つが離れない）。bar の中に描く物は大きさを変えず、bar の中央の線 `ZWL_GLASS_BAR_MIDDLE`（`ZWL_GLASS_BAR / 2`）から置く。そのため 34 px の時の見た目（中央からの位置）がそのまま 44 px の中央に来る。
- 変えた所:
  - `shell.c`: launcher の `BAR_LAUNCHER_Y`、区切りの線（`BAR_LINE_TOP`・`BAR_LINE_LENGTH`）、desktops の pill（`BAR_PILL_TOP`・`BAR_PILL_HEIGHT` = 26）、窓のある desktop の点、時計と "Wiseview" の baseline（`BAR_BASELINE`）、電池。
  - `network.c`: Wi-Fi の棒・取り消し線・有線の木、menu を開いた時の青い背景（28 px で中央）。
  - `volume.c`: スピーカーの icon・取り消し線・青い背景。
  - `input-method.c`: 言語の chip の top と baseline。
  - press の領域（network・volume の icon の `icon_y` = 3、高さ `ZWL_GLASS_BAR - 6`）は bar の高さに合わせて広がる。log の `ZWL NETWORK/VOLUME icon` はこの領域を出す。
- `ZWL_GLASS_BAR` から導かれていた物は自動で 44 に合う: `ZWL_GLASS_TOP`（窓の最も高い位置）、`ZWL_GLASS_DOCK_TOP`（docked の本体の上端 38 → 48。高さは 1280x800 で 762 → 752）、docked の title と button の中央（`ZWL_GLASS_BAR / 2`）、docked の titlebar・menu の control（area の中で中央）、popup・menu の位置（`ZWL_GLASS_BAR + 6`）、desktop の surface（y 34 → 44、高さ 766 → 756）、OSK の flick の panel（y 44）、Wiseview の見出し、App Home の格子。
- HiDPI: bar の値は scale 1 の px で、今までと同じ扱い（scale ごとの調整は無い）。
- 直した試験（固定の座標と大きさ）: `plan/ws094/tests/desktop-guest.sh`・`files-desktop-guest.sh`・`desktop-p010.sh`（desktop の surface y=44、1280x756、surface 座標 −10）、`plan/ws102/tests/osk-guest.sh`（flick y=44、756・1036、docked 752）、`plan/ws035/tests/zdesktop-p059.sh`・`p062.sh`・`p134.sh`（docked 1280x752、y=48）、`plan/ws090/tests/sheet-guest.sh`（docked の親の sheet y=48）、`plan/ws005/tests/menu-bug148.sh`（menu の高さの上限 172）、`plan/ws031/tests/i915-capture.py`（実機の取り込みの座標）。bar の中の y=17 の click は 44 px の bar の中に残るので変えていない。
- 確認: `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/q700/img build/q700/img/bin/wayland` exit 0、warning 0。`plan/tools/keiland-os-boundary/check.sh` PASS。`plan/tools/style-check.py` は変えた行に違反 0（network.c・volume.c・input-method.c の既存の違反は p022 の全文規約の Phase へ）。
- 未実施: QEMU（T1。PNG で bar と title bar が同じ高さ、bar の要素が縦の中央、docked の title の吸着、popup の位置。上の直した試験と criteria.sh C2・C3・C9）、実機の UAT。
