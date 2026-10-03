<!-- awesome-plan project=zedbsd record=ws074p007 -->

# ws074-p007: CSS の最小（ワンパス）

Phase ID: `ws074-p007`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）

## 範囲

正常系のワンパス（2026-09-27 ユーザー）: CSS Syntax 3 の tokenizer、style sheet と style 属性の parser、selector（type・universal・
class・id・属性の 7 種の比較・4 つの結合子・`:root`・`:first-child`・`:last-child`・`:only-child`・`:empty`・`:link`）、cascade
（origin と `!important`、specificity、出現順、継承、`inherit`・`initial`・`unset`）、約 40 の longhand と 13 の shorthand、UA
stylesheet、`<style>` と `style` 属性。page の読み込み（`page/`）と headless の `--dump=dom|style`。

## 受け入れ

1. amd64 の build が通る（warning 0）。style-check 0 件。
2. 最初の試験の page の computed style が正しい（golden の dump を人が確かめる）。guest で同じ。
3. boot test。

## 結果（2026-09-27）

cleared。

- `css/{css.h,internal.h,tokenizer.c,parser.c,values.c,cascade.c,ua.c}`、`page/{page.h,page.c}`、`main.c` の `--dump=dom|style`。
  名前は heap の atom（永続なので style sheet は trace なしで持てる）。色は 148 の名前（Pillow の CSS の表から生成した事実の表）、
  `#rgb`〜`#rrggbbaa`、`rgb()`/`rgba()`。長さは px・em・rem・ex/ch・%・pt・pc・in・cm・mm・vw・vh。
- 試験: `plan/ws074/tests/pages/first.html`（見出し、段落、class・id・子孫の selector、link、list、日本語）、
  `plan/ws074/tests/golden-dumps.sh`（page ごとの `--dump` を golden と比べる）、golden `first.style`・`first.dom`（値を確かめた:
  h1 は 32px・bold・margin 21.44px、`.note` は背景・border・padding、`#footer` は 12px・gray・margin 24px/12px、li は list-item、
  `a:link` は #0000ee）。
  - host plain・ASan: golden 2/2 一致。tree construction の回帰 1648/1753（変わらず）。
  - guest（zedBSD の build の image の `/usr/share/browser-tests/first.html`）: dump が golden と byte 単位で一致。
- boot test: PASS（`/home/awe/zedBSD-rpi4/build/ws074-shots/p007-20260927-boot-login.png`）。画面に出る変化はまだ無い。

## 後回し（follow-up）

- at-rule（`@media`・`@import`・`@font-face`・`@supports`）は丸ごと読み飛ばす（p009）。
- pseudo-element（`::before` 等）と状態の pseudo-class（`:hover`・`:focus` 等）、`:not()`・`:is()`・`:nth-*` は一致しない（p008）。
  未知の pseudo-class は rule を捨てずにその selector だけが一致しない。
- `var()`・`calc()`、shorthand の `background`（色だけ）・`font`（名前・大きさ・行の高さ・weight・style）・`list-style` の残り（p009）。
- `text-decoration` は underline を継承する近似。border の groove などは solid として扱う。
- rule の索引は無く、要素ごとに全 rule を試す（p008）。quirks mode の class・id の大文字小文字の無視は無い。
