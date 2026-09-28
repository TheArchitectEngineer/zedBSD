<!-- awesome-plan project=zedbsd record=ws035p114 -->

# ws035-p114: terminal の scrollback、出力の scroll に追う選択、窓の端を越えた drag の自動の scroll（p111 の残り）

Phase ID: `ws035-p114`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-28 main の割り当て「Kei desktop のデモの仕上げ」の 1。[p111](../phase111/phase.md) の残り）

## 範囲

割り当て: (1) 出力で画面が流れても選択が文字に付いて動く、(2) drag で窓の端を越えると scrollback が自動で scroll する。
調べると Wayland の terminal（`userland/desktop/terminal`）には scrollback そのものが無かった（画面の grid だけ、上へ流れた行は捨てていた）。
(2) は scrollback が前提なので、この Phase で最小の scrollback（行の保持、wheel と Shift+Page Up/Down での表示の移動）を足した。
X11 の zterm は retro の program なので触れない。

## 実装（2026-09-28）

- **行番号**（`terminal.h`・`screen.c`）: 行は画面が最初に出した行から数える（`unsigned long`）。画面の r 行目は `scrolled + r`。
  選択の範囲（`range_from`・`range_to`）は (column, line) に変えた。画面全体の scroll（line feed・CSI S、region が画面全体のとき）で
  上へ出た行は scrollback へ入り、行番号は変わらないので、範囲はそのまま同じ文字を指す。region の scroll・行の挿入と削除（CSI L・M・T、
  reverse index）は文字が行番号なしに動くので、範囲がそこに掛かれば消す。scrollback から落ちた行に始まる範囲も消す。
- **scrollback**: `TERMINAL_HISTORY` 1000 行の ring（1 行 `TERMINAL_MAX_COLUMNS` の cell、広い grid への resize の分は空白）。
  `terminal_screen_line_cell()`（行の cell、無い行は NULL）、`terminal_screen_view_line()`（窓の行が示す行）、
  `terminal_screen_scroll_view()`（表示を戻す・進める、範囲の中に留める）。表示が戻っている間の新しい出力は表示を同じ文字に留める
  （xterm の既定と同じ）。文字の key は live の画面へ戻す。resize は範囲を消して live へ。
  alternate screen（`?1049`・`?47`・`?1047`）は今まで `terminal_screen_init` で状態を全部消していた（scrollback も消える）ので、grid だけを
  空にする `screen_clear_grid()` に変えた（title・scrollback を保つ）。
- **描画**（`render.c`）: 窓の行ごとに表示の行の cell を描く。cursor は自分の行が見えるときだけ。Select All の塗りは live の行だけ。
- **入力**（`window.c`・`main.c`）: wheel（`axis_discrete` の縦、1 notch 3 行）、Shift+Page Up/Down（1 page = 行数 − 1）。
  選択の drag で pointer が grid の上端より上（title bar の上を含む）なら戻る向き、下端より下なら live の向きへ、押した直後に 1 step、
  以後 60 ms ごと（端から cell の高さごとに 1 行増え、最大 8 行）。pointer が止まっていても main loop の timeout で進む。
  選択は step ごとに端の行まで伸びる。zdesktop は focus の窓へ窓の外の motion も送る（`zwl_seat_motion_deliver`）ので、
  窓の外でも追える。log `ZTERM VIEW how=scroll|edge|key back= history= scrolled=`、`ZTERM SELECT` の from・to は (column, line)。
- 既存の関数の Boolean の式（`again`、`later`）を if に直した（書き換えた範囲の規約の修正）。

## 検証（amd64、Venus、lean な files の image、worktree の `build/amd64`、2026-09-28）

- `plan/ws035/tests/zdesktop-p114.sh`（新）PASS（1 回目で）:
  1. 60 行（row-1〜row-60）を出し、5 秒後に 6 行を出す背景の job の間に "row-50" を double click（`how=word from=0,49 to=5,49`）。
     出力で画面が 6 行以上流れた後に Ctrl+Shift+C → paste したファイルの中身は `row-50`（範囲が文字に付いて動いた。`follow.png`）。
  2. wheel を上へ 3 notch: `how=scroll back=3/6/9`（`wheel.png`）、key で `how=key back=0`。
  3. 1 行目を押して窓の上（title bar）へ drag して 1.5 秒: `how=edge` が 17 step（back=17）、離すと `how=drag from=4,26 to=4,43`、
     始まり（行 26）は scrollback の中（scrolled=43）（`edge.png`、row-31〜row-44 の選択）。
  4. 表示が戻った状態で最下行を押して窓の下へ drag: `how=edge back=0`（`back.png`）。zdesktop の log に ERROR なし。
- 回帰 PASS: zdesktop-p111（語・行の drag、Shift+click、PRIMARY）、zdesktop-p093（語・行・drag・範囲の drag out）、
  zdesktop-p100（2 つの terminal の間の PRIMARY と中 click）。行番号は scroll が無ければ行と同じなので、既存の試験の期待値は変わらない。
- build warning 0（terminal）。`plan/tools/style-check.py` main.c・screen.c・render.c・window.c 0。
- 画面: `/home/awe/zedBSD-rpi4/build/ws035-shots/p114-20260928-{follow,wheel,edge,back}.png`。
- 未実施: 実機。1000 行を越えて落ちる行の上の範囲（コードの経路のみ、試験なし）。

## 残り

- 範囲の文字の長さは clipboard の buffer（`MAIN_CLIPBOARD_MAX`、画面 1 枚分）で切れる。scrollback を丸ごと選ぶと途中で切れる。
- 表示を戻している間の目印（scroll bar など）は無い。
- 選択を消したとき PRIMARY を空にしない（p100・p111 の残りと同じ）。
- 1 つの tab の scrollback は約 3.8 MB（1000 行 × 240 列 × 16 byte）。tab 8 つで約 31 MB。
