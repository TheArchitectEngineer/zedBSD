<!-- awesome-plan project=zedbsd record=ws074p011 -->

# ws074-p011: layout の最小（ワンパス）

Phase ID: `ws074-p011`
Parent: [WS074](../ws.md)
Status: in-progress（2026-09-27 21 時、rate limit の前の wrap up で中断。コードは build が通る状態で commit 済み）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）

## 範囲

正常系のワンパス: box tree（display: none・contents、anonymous block、inline の中の block は block 扱い、list の marker）、block
（幅、auto の margin の中央寄せ、min/max、兄弟・空の block・親と最初と最後の子の margin の相殺）、inline（空白の畳み込み、
white-space、改行の機会での貪欲な行の分割、`<br>`、baseline と half-leading、text-align）、`--dump=layout`、Chrome の box との比較。

## 今の状態（2026-09-27 21:15）

- 書いた: `layout/{layout.h,box.c,block.c,inline.c,dump.c}`、`page` の `page_open_fonts`・`page_layout`、`main.c` の
  `--dump=layout` と `--font=`・`--mono-font=`・`--fallback-font=`。
- libtruetype に関数を 2 つ足した（既存は変えない、main の了解済み）: `truetype_design_metrics`・`truetype_glyph_design_advance`
  （新しい file `userland/base/libtruetype/design.c`、`include/libc/truetype.h`、`exports.map`、Makefile）。text は design unit から
  小数の大きさの advance（1/64 px）と Chromium と同じ丸めの ascent・descent・line gap を出す（`text_font.size`、
  `text_glyph.advance_units`）。
- **p007 の回帰を直した**: p007 の style の手直し（values.c の表への置き換え）で dimension の単位が照合されなくなり、em・px の
  長さが全部落ちていた（p007 の commit 10c63ac6 の golden はこの誤りを含んでいた）。`values_unit` で直し、golden の
  `first.style` を作り直した（h1 32px・margin 21.44 などを再確認）。
- Chrome との比較の道具: `plan/ws074/tests/chrome-fonts.sh`（Chromium に Inter・JetBrains Mono・Droid を使わせる fontconfig）、
  `plan/ws074/tests/chrome-boxes.py`（Chromium の `getBoundingClientRect` と `--dump=layout` の block の box を ±1px で比べる）。
  first.html: 5/12 が一致（html・body・h1・p・`#footer`）。残りの差は 2 つの原因:
  1. Chromium は `font-family: monospace` で既定の大きさ（medium）を 13px にする（`<code>` の行が 1px 高い）。
  2. line-height: normal の行は fallback の font（日本語の Droid）の ascent・descent も含める（日本語の行が 1px 低い）。
- build: host（plain）と amd64（warning 0）が通る。golden `style`・`dom` 2/2。

## 再開の手順

1. `git merge main`。`python3 plan/tools/style-check.py userland/base/zdesktop-browser/layout/*.c userland/base/zdesktop-browser/text/*.c userland/base/libtruetype/design.c`
   で規約の違反を直す（layout の file はまだ確かめていない。`/tmp/.../scratchpad/blanks.py` の方式: 閉じ括弧の後の空行、
   段落の comment、条件の中の呼び出し、条件演算子）。
2. monospace の 13px（css の cascade: font-size が medium の keyword から来たことを style に持ち、family が monospace だけなら
   13px にする）と、fallback の font の行の高さ（inline.c: piece が fallback の face の glyph を含むなら、その face の metrics を
   normal の行の高さに入れる）。`python3 plan/ws074/tests/chrome-boxes.py plan/ws074/tests/pages/first.html` で 12/12 近くを目標。
3. `sh plan/ws074/tests/golden-dumps.sh --update layout` の後に値を確かめ、Phase の試験（host plain・ASan の golden、
   tree construction の回帰、guest で `--dump=layout` が golden と一致、`sh plan/ws074/tests/boot-check.sh p011`）。
4. phase.md を cleared にし、ws.md の表と Resume point を直して commit、main に報告。次は p012（display list と CPU の描画、`--render`）。

## 後回し（follow-up）

- float・position・table・flex・grid（block として扱う）、inline-block（block として扱う）、inline の box の border・padding・
  背景、vertical-align、text-align: justify、block の中の inline の分割（block-in-inline）、list の marker の block の子への付け替え。
