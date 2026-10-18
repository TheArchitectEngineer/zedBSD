<!-- awesome-plan project=zedbsd record=ws260-p015 -->

# ws260-p015 規約の全文の見直し、写しの点検、T1 への実機の回帰の依頼

Status: in-progress（2026-10-18 q1253-i02、P1。T1 の結果を Q1 が判定するまで cleared にしない）

- 親: [WS260](../ws.md)。Queue: q1253-i02。設計: [design.md](../design.md) §1.2・§3.3。

## 規約の見直し（plan/coding-style.md の全文、WS260 が足した・変えた code）

- 範囲: `display/xd9-*.c/h`、WS260 で足した xd の関数（xd-output・xd-pipe・xd-dbuf・xd-hpd・xd-ddi・xd-clock・xd-power・xd-panel・xd-takeover・xd-aux・xd-gmbus・xd-dp・xd-platform の display 9 の分）、`intel/display-regs-xd9.h`・`display-tables-xd9.h`、caller の付け替え、試験の移植。
- 機械で見た所（`git diff b9fe9a6d6` の追加行）: 条件の中の関数の呼び出し、関数の結果の直の return、条件演算子（log の短い対称の選択だけが残る）、public の関数が static の後に来る file（xd-hpd.c・xd-takeover.c の display 9 の関数を static の前へ移した。xd-clock.c・xd-ddi.c・xd-display.c の同じ形は WS245 以前からの物で範囲外）、`git diff --check`。
- 直した物: xd-pipe（plane_flip・plane_surf_live・plane_fault の読みを変数に）、xd9-outputs（mode_fits を条件の外へ、outputs_live）、xd9-step（step・report）。動作は変えていない（golden 4 つ same）。
- 制限: 規約の全項目を 1 行ずつ人が読んだのではない（上の機械の点検と、書いた時の確かめ）。p013・p014 で gen9-*.c から移した node・flip・backlight の本体（WS237 で規約に合わせて書かれた物）は、名前と型の置き換え以外を読み直していない。

## 写しの点検（design §1.2 の方法を新しい code に）

- 道具: scratchpad の check.py（Linux 6.8.12 の drivers/gpu/drm/i915・drivers/gpu/drm/display を build/tmp/ws260-p015-linux/ に展開）。対象: xd9-*.c/h の全文と、WS260 の起点 b9fe9a6d6 から xd-*.c/h・display-regs-xd9.h・display-tables-xd9.h に足した行。
- comment の 7 語の並び: 12,900 個のうち Linux と一致 0。
- code の 5 token の並び（識別子 3 個以上を含む物）: Linux にもある物は file ごとに 0〜2 %（xd9-node.c 17/1227、xd9-outputs.c 10/773、xd9-flip.c 8/939 ほか。`for (index = 0U; index <` などの決まった形）。行の写しは無い。
- 手順の出典: 新しい code の `Procedure referenced: Linux` は 2 つ（xd-ddi.c の HDMI の buffer の開始（Display WA #1143）、xd-gmbus.c の GMBUS の clock gating（Display WA #0868））。どちらも workaround の手順で、PRM に載らない。値の出典の Linux の参照は GT の register の header 側（WS260 の外、main の取り込みで入った物を含む）。display 9 の手順は PRM と hardware の単位（xd の部品）で組み、gen9 の Linux の関数の分け方（hsw_crtc_enable の 1 対 1 など）は残っていない。

## T1 への依頼の文面（Q1 が T1 に渡す）

> WS260（i915 の Gen9 の display を xd に書き直し）の実機の回帰を 1 回お願いします。main の tip（WS260 の p011〜p015 を含む）の amd64 の image で:
> 1. SV7（10.0.10.27、Gen9）: `plan/tools/boot-test.sh` で起動し graphical の login まで（PNG を撮る）。dmesg に `i915: xd9-display: using full`・`i915: xd9-flip: flip on pipe`・`i915: backlight: the panel's light is a backlight device` が出ること、`i915: xd9-display: ... failed` が無いこと。desktop が内蔵の画面に出ること（firmware の mode のまま）。`backlight-probe` で get と set（0・50・100 %）が通ること。外の display は繋がない（ユーザーの決定 5）ので未実施と書く。
> 2. 5330（display 13）: ws245/ws.md の「5330 の素の起動での段ごとの回帰の手順」を 1 回（xd の部品に display 9 の分岐を足したので、display 13 が壊れていない事を見る）。
> 3. 結果: 各機の PASS/FAIL、PNG の path、dmesg の i915 の行。壊れていたら段の境（p011・p012・p013・p014 の merge）で二分するので、どの段の commit の image かを書く。

## 残り

- T1 の結果（Q1 が判定）。外の display の実機（SV7 に繋がない）はユーザーの試験の一覧へ。
