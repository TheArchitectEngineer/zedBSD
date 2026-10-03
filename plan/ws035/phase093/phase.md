<!-- awesome-plan project=zedbsd record=ws035p093 -->

# ws035-p093: terminal の pointer による範囲選択と、選択した text の drag

Phase ID: `ws035-p093`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-28 main の割り当て「mouse range selection in terminal (press-drag to select, double-click word,
triple-click line; highlight; Ctrl+Shift+C copies the selection instead of the whole screen; PRIMARY selection if simple), then the
terminal as a drag source for the selected text」。fg010）

## 範囲

terminal は pointer を扱わず、選択は Edit > Select All（画面全体）だけだった（p091 で drag の source を見送った理由）。

## 実装（2026-09-28）

- `window.c`: seat の pointer（version 5 の listener）。左 button の press・release と motion（続く motion は 1 つにまとめる）を
  `pointer_events` に積み、main loop が取る。
- `terminal.h`・`screen.c`: screen に pointer の範囲（`range`・`range_from`・`range_to`、読む順）。`terminal_screen_in_range`。
  `terminal_screen_text` は画面全体が選ばれていなければ範囲の text（行ごとに末尾の空白を除き、行の間に改行）。
- `render.c`: 範囲の cell も選択の背景。
- `main.c`（`main_pointer`・`main_pointer_press/motion/release`・`main_cell`・`main_range`・`main_word_character`）:
  - 1 回の press から pointer に従って範囲（release で `ZTERM SELECT how=drag`）。
  - 同じ cell で 400 ms 以内の 2 回目は語（空白と shell の区切り文字 `"'`()[]{}<>|;&,` 以外の続き、`-` や `/` は語の内）、
    3 回目は行（`how=word`・`how=line`）。
  - 範囲の中の press（連続 click でない）は、6 px 動くと範囲の text の drag（`terminal_clipboard_drag`）、動かずに離すと範囲を
    消す（click）。
  - key を打つと範囲も消える（Select All と同じ）。Edit > Copy（Ctrl+Shift+C）は範囲があれば範囲を copy
    （menu の Copy は範囲でも有効）。
- `clipboard.c`: `terminal_clipboard_drag`（source は `text/plain;charset=utf-8`・`text/plain`、action は copy、icon 無し）。
  send は drag の source なら drag の text を送り、finished・cancelled で source を壊す（`ZTERM DRAG done dropped=1/0`）。
  自分の窓への drop は、pipe で読むと同じ loop がまだ dispatch していない send を待って空になるので、drag の text をそのまま
  貼る。

## 設計の判断・制限

- **PRIMARY は入れない**: zdesktop に primary selection の protocol（zwp_primary_selection）が無い。入れるなら zdesktop と
  client の両方の新しい protocol（残り）。
- 範囲は画面の cell の位置で持ち、出力で画面が動いても（scroll）範囲は動かない。scrollback は無い。
- 2・3 回 click の後の drag による語・行単位の延長は無い（1 回 click の drag だけが延びる）。

## 検証（amd64、Venus の guest、lean image、2026-09-28）

- `plan/ws035/tests/zdesktop-p093.sh`（新規）PASS: 画面を消して `alpha beta-gamma delta`。double click で
  `beta-gamma`（`how=word from=6,0 to=15,0 bytes=10`）、Ctrl+Shift+C で `COPY bytes=10`、triple click で行（`bytes=22`）、
  範囲の中の click で消し、press を動かして `alpha`（`how=drag bytes=5`）、範囲の中から動かして drag（`DRAG start bytes=5`）、
  自分の下の行へ落として shell に `alpha`（`DROP bytes=5`、`DRAG done dropped=1`）。最初の実行で、Copy の menu 項目が範囲
  では無効だったこと、自分への drop が空だったことを見つけて直した。
- 回帰 PASS: zdesktop-p087（clipboard の橋、terminal の Select All と Copy）、zdesktop-p088（terminal への file の drop）。
- 規約: 変えた 6 file の style-check 0（前も 0）。build warning 0。
- 画面（`/home/awe/zedBSD-rpi4/build/ws035-shots/`）: `p093-20260928-venus-word.png`（語の選択）、`-drag.png`（範囲）、
  `-dropped.png`（drag した `alpha` が prompt に）。
- 実機（i915）: 未実施。boot test: ユーザーの指示で無し。

## 残り

- PRIMARY selection（zdesktop の protocol から）。中 button の paste。
- 語・行単位の drag での延長、Shift+click での延長、scroll と範囲の追従。
