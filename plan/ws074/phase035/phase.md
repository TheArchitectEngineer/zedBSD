<!-- awesome-plan project=zedbsd record=ws074p035 -->

# ws074-p035: flexbox（最小）

Phase ID: `ws074-p035`
Parent: [WS074](../ws.md)
Status: uncleared（2026-09-28。実装と host の確認は済み、ASan の回帰と guest は未実施のまま main の wrap-up の依頼で止めた）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: p013、p069

## 範囲（amazon-goal.md §4 の 5）

row・column（と reverse）、wrap、`flex` の shorthand、grow・shrink・basis、`justify-content`・`align-items`・`align-self`、
`gap`、`order`、auto margin、`inline-flex`（block として）、`-webkit-` の別名。

## 設計と実装

- CSS（`css/values.c`・`cascade.c`・`css.h`・`internal.h`）: `flex-direction`・`flex-wrap`・`justify-content`・`align-items`・
  `align-content`・`align-self`・`row-gap`・`column-gap`（`grid-*-gap`）・`flex-grow`・`flex-shrink`・`flex-basis`・`order`、
  shorthand `flex`（none・auto・数と basis の組）・`flex-flow`・`gap`、`-webkit-` の別名。display の `-webkit-flex` は flex、
  `-webkit-box` は block（line clamp 用の使われ方のため）。
- box（`layout/box.c`）: flex container の子を item に（要素の box は inline・replaced も block-level に、float を止める、
  連続する text は anonymous の item、空白だけの text は捨てる）。
- layout（`layout/flex.c` 新、`block.c`・`position.c`）: item を order で安定に並べ、仮の主軸の大きさ（basis・width/height、
  無ければ内容: row は広い幅で測った最長の行、column は高さ）を min/max で挟み、wrap なら行に分け、grow・shrink（basis で
  重み付け）で余りを分け、残りを auto margin に。各 item を block として主軸の大きさで layout し、justify-content で主軸、
  align で交差軸に置く（stretch は行の太さへ）。flex container は formatting context、item も。行の flex の内容の幅は item の
  flex 前の margin box の和（shrink-to-fit と入れ子の測定のため）。
- 試験: `tests/pages/flex.html`（新、golden 4 つ）。

## 確認（host の証拠。guest・実機は未実施）

- host の build（plain、-Werror）warning 0。style-check: 新しい指摘 0（`cascade.c` の 1 件は既存）。
- 回帰（plain のみ）: golden 48/48（flex の 4 つを追加、他は不変）、host-view 59/59、host-form 28/28、run-js-tests 7/7、
  run-dom-tests 6/6、run-loader-tests 11/11、run-http-tests 14/14・`--async` 17/17、host-base・heap・interp・link・number・
  object・position・text 0 failed。**ASan の build と回帰は未実施**（wrap-up のため）。
- `chrome-boxes.py flex.html`（800x700）: 67 の box のうち 65 が 1px 以内（違う 2 つは太字の glyph の幅の差）。
  `render-compare.py flex.html`（修正前の値 87.77%、修正後は未計測）。
- Amazon（p068 と同じ capture の `*-local-noscript.html`、1280x900）:

  | page | p069 の後 | p035 の後 |
  | --- | --- | --- |
  | トップ | 画素 27.89%、ink 21.30% | 画素 33.49%、ink 24.38% |
  | 検索 | 画素 75.16%、ink 19.89% | 画素 78.51%、ink 28.10% |

  検索の `--render` は host で 1.57 s。

## 写真（`/home/awe/zedBSD-rpi4/.claude/worktrees/agent-ae19962dd0a453495/build/ws074-shots/`）

- `p035-20260928-flex.png`（修正前の比較）。Amazon の比較は `build/ws074-live/p035-top-side.png`・`p035-search-side.png`。

## 未実施・再開の点

- ASan の build と回帰、`render-compare.py flex.html` の再計測、guest の窓の試験（worktree の image に target の sysroot が要る。
  main の許可待ち、p068 を参照）。これらを通したら cleared にする。
- 範囲の外に残したもの: `align-content`（start 以外）、`wrap-reverse` の行の順、baseline（start として）、自動の最小の大きさ
  （min-content）、不定の高さの %、`inline-flex` の inline-level、`-webkit-box` の flex としての扱い。
- style: `flex.c` の一部に比較式を真偽値に直接代入する箇所（`item->auto_start = … == …`）が残る（p044 の規約の照合で直す）。
