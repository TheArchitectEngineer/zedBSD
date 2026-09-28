<!-- awesome-plan project=zedbsd record=ws035p117 -->

# ws035-p117: system bar の launcher を Kei の印に、左上の「Kei」の語を外す

Phase ID: `ws035-p117`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-28 main 経由のユーザーの依頼、デモの仕上げの一部）

## 範囲（依頼）

- 画面の左上の「Kei」の語を外す。
- 左上の launcher（app grid に見える青い四角）を Kei の印（`userland/desktop/artwork/mark.c` のガラスの棒と葉）に、bar の大きさで。
- launcher の振る舞いは同じ（click で App Home）。右の docked の title の領域が空いた所を使うこと、左上の角のジェスチャーの zone が
  そのまま効くことを確かめる。前後の画面を撮る。

## 実装（2026-09-28、`userland/desktop/wayland`）

- `shell.c`: launcher は `glass_draw_mark()` で印を 26 px の四角に（`BAR_LAUNCHER_X` 10、`BAR_LAUNCHER_Y` 4、`BAR_LAUNCHER_SIZE` 26）。
  App Home の間の ring は印の周り 2 px（30 px、radius 9）。「Kei」の語の描画を削除。`bar_layout()` の区切りの線は launcher の後ろ 12 px
  （`menu_line` 48、docked の title の始まり `title_x` 65。前は「Kei」の語の幅の後ろ）。docked の title・Wiseview の名前・docking の
  slot はこの値に従う。使わなくなった色の定数を削除。
- `glass.c`: 印の 7 層を launcher の大きさ（26 px）でも atlas に描く（`mark_small`、128 px の層の行の右の空き列に 4×2、cache の位置は不変）。
  `glass_draw_mark()` は 39 px 以下なら小さい層を使う（1:1 で描くので滲まない。128 px を 5 分の 1 に縮めない）。
- App Home の press の範囲（`home.c` の bar の最初の 40 px と左上の 28×28）は変えていない。印（x 10〜36）はその中。

## 検証（2026-09-28、QEMU の Venus。実機は未実施）

- 前後の画面（1280x800、demo の image）: 前 `p117-20260929-bar-before-{desktop,docked}.png`、後 `p117-20260929-bar-after-{desktop,docked,home-click,home-swipe}.png`、
  比較 `p117-20260929-bar-compare.png`、印の拡大 `p117-20260929-bar-mark-zoom.png`、1920x1280 の bar `p117-20260929-bar-after-1920-strip.png`
  （いずれも `/home/awe/zedBSD-rpi4/build/ws035-shots/`）。
  - docked の Terminal: `ZWL GLASS dock ... title=65`、title と menu（Shell Edit View Session Help）が左へ詰まる。
  - launcher の click: `ZWL HOME open via=launcher`（印の周りに ring）、もう一度の click で `HOME close via=launcher`。
  - 左上の角からのスワイプ: `ZWL HOME open via=drag`。1920x1280 の通し（[p116](../phase116/phase.md) の `demo-walk.sh`）でも角のスワイプで
    App Home が開き、Wiseview の名前は線の後ろへ詰まる。
- 回帰 PASS（lean な files の image）: zdesktop-p059・p062・p064（docking と docked の title の位置は log の `title=` から取る）、zdesktop-p053、
  ws079 の zdesktop-p010（左上・右上・下端のジェスチャー、docking）。p059・p064 の起動の待ちの修正は p116 に記録。
- build warning 0。`plan/tools/style-check.py` shell.c・glass.c 0。
- 未実施: 実機。

## 残り

- 印は splash の半透明の色のままなので、明るい bar の上では棒の部分が淡い（依頼どおり同じ印）。濃くするなら色の調整が要る。
