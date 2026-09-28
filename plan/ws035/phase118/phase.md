<!-- awesome-plan project=zedbsd record=ws035p118 -->

# ws035-p118: system bar の Kei の印をバー用に濃く

Phase ID: `ws035-p118`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-28 ユーザーの決定「バーの Kei の印・全画面」、main 経由の割り当て）

## 範囲

master の決定「バーの Kei の印・全画面」の前半: バーの左上の Kei の印（p117 の launcher）を**バー用に濃くする**（透明を減らし、少し深い青）。
起動画面の印（kei-boot-splash.png、loader の splash）と greeter・lock の印はそのまま。前後の画面を撮る。

## 実装（`userland/desktop/wayland`）

- `glass.h`: `enum glass_mark_look`（`GLASS_MARK_SPLASH`・`GLASS_MARK_BAR`）。`glass_draw_mark()` に look の引数。
- `glass.c`: バーの 7 層の色（`bar_colours`）: 棒・葉とその影・重なりを少し深い青で不透明度 0.90〜0.96（前は 0.35〜0.78）、縁の白い光 0.50・
  sheen 0.18（前は 0.67・0.24）。splash の色（`splash_colours`）は前と同じ値。
- `shell.c`: launcher は `GLASS_MARK_BAR`、`greeter.c`（greeter・lock の左下の印）は `GLASS_MARK_SPLASH`。
- 色は host で候補を 3 つ並べて選んだ（`plan/ws035/tests/p107/mark-host.c` の層を p117 の画面のバーの地に合成。scratch の script、未 commit）。

## 検証（2026-09-28、QEMU の Venus 1920x1280。実機は未実施）

- 前後: `/home/awe/zedBSD-rpi4/build/ws035-shots/p118-20260929-bar-mark-compare.png`（上が p117 の印、下がこの Phase の印、6 倍）、
  バーを含む画面 `p118-20260929-kei-02-desktop.png`・`p118-20260929-kei-04-files.png`。greeter・lock の印は変わらない
  （`p118-20260929-kei-01-greeter.png`・`p118-20260929-kei-09-lock.png`）。
- build warning 0（-Werror）、`plan/tools/style-check.py` glass.c・greeter.c 0、shell.c 0。
- 回帰は [p119](../phase119/phase.md) と同じ run（zdesktop-p059・p062・p064、ws079 zdesktop-p010）。
