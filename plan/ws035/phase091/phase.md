<!-- awesome-plan project=zedbsd record=ws035p091 -->

# ws035-p091: terminal のタブの題を shell から（OSC 0・2）

Phase ID: `ws035-p091`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-28 main の割り当て「Terminal tab titles from the shell (OSC 0/2 title sequences → tab title), and the
terminal as a drag source if simple」。fg010）

## 範囲

terminal は OSC の文字列を読んで捨て、タブの題は「Shell N」、窓の題は「Terminal」で固定だった。

1. OSC 0 と 2（BEL か ESC \ で終わる）の text をそのタブの題にする。OSC 1（icon の名）などは捨てる。
2. タブの題は titlebar の TABS mode の strip に出る。窓の題（xdg_toplevel の title、1 タブの menu mode の titlebar に出る）は
   active なタブの題に従う（題の無いタブは「Terminal」）。
3. 端末を drag の source にすること: 簡単なら。

## 実装（2026-09-28）

- `terminal.h`: `TERMINAL_OSC`（256）・`TERMINAL_TITLE`（128）、screen に `osc`・`osc_length`・`title`。タブの題の長さ
  `TERMINAL_TAB_TITLE` を 32 → 64 byte。
- `screen.c`: OSC の byte を保ち（255 byte まで、それより後は捨てる）、終わりで `screen_osc`: `0;` か `2;` で始まれば
  制御文字を除いた text を題にする（127 byte まで、UTF-8 の文字の途中で切れたらその文字を落とす）。
- `main.c`: `main_tabs_show` がタブの題に screen の題（無ければ「Shell N」）を、UTF-8 を割らずに 63 byte まで
  （`main_title_copy`）。窓の題は active なタブの題か「Terminal」で、変わったときだけ `xdg_toplevel_set_title` と
  `ZTERM TITLE tab=N title=...`（fflush）。

## 設計の判断・制限

- **drag の source は入れない**: 端末には mouse での範囲選択が無く（Edit > Select All の画面全体だけ）、drag で運ぶ
  ものを決められない。範囲選択と一緒に Future Work 相当の残り（下）。
- zedBSD の sh は題を送らない（PS1 に OSC を入れれば送る）。題は program（または利用者の PS1）が決める。
- zdesktop の titlebar の app の icon は題の最初の文字なので、題が変わると icon の文字も変わる（`long.png` の「日」）。
  app_id から取る方がよいが zdesktop は app_id を持たない（残り）。

## 検証（amd64、Venus の guest、2026-09-28）

- `plan/ws035/tests/zdesktop-p091.sh`（新規、`p091-title.sh` を guest に置いて端末で実行）PASS: 最初の題「Terminal」、
  OSC 0（BEL）「Build logs」で窓の題、Ctrl+Shift+T の新しいタブは「Terminal」、そこで OSC 2（ESC \）「日本語 notes」、
  タブ 1 を click すると窓の題が「Build logs」に戻る、「日」50 個（150 byte）の題は 126 byte（42 文字）、OSC 1 は題を
  変えない。最初の実行は log の行が次の event まで出ず（stdout の flush が無かった）、`fflush` を足した。
- 回帰 PASS: zdesktop-p086（terminal のタブ）。
- 規約: 変えた file（`main.c`・`screen.c`・`terminal.h`）の style-check 0（前も 0）。build warning 0。
- 画面（`/home/awe/zedBSD-rpi4/build/ws035-shots/`）: `p091-20260928-venus-one-tab.png`（窓の題「Build logs」）、
  `-tabs.png`（strip に「Build logs」「日本語 notes」）、`-long.png`（長い題の省略）。
- 実機（i915）: 未実施。boot test: ユーザーの指示で無し。

## 残り

- 端末の mouse による範囲選択と、それを drag の source にすること。
- titlebar の icon を app_id から（題が変わっても変わらない）。
