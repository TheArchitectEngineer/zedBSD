<!-- awesome-plan project=zedbsd record=ws074p081 -->

# ws074-p081: HTML の断片の parse と直列化（innerHTML・outerHTML・insertAdjacentHTML）

Phase ID: `ws074-p081`
Parent: [WS074](../ws.md)
Status: in-progress
Phase disposition: normal
Queue: なし（main の指示でサブエージェントが worktree `wt/ws074-dom` で実行、2026-09-29）
依存: p005（tree builder）、p030（DOM の binding）、p031
並行: WS074 の別のエージェントが `js/`・`vm/` を進めている。この Phase は `html/`・`bind/` だけを変え、`js/`・`vm/` は変えない。

## 由来と順（2026-09-29 の調べ）

main の指示: p031 で入れなかった `getComputedStyle`・`innerHTML`・`outerHTML`・`insertAdjacentHTML`・`offsetTop`・`offsetLeft`・
`offsetParent`・`scrollTo`・`scrollIntoView` を、Amazon の表示に効く順に。Amazon の top と search の外の script（43 本、6.7 MB、私たちの
UA で取得、`build/p081/js/`）と inline script での使われ方:

| API | 外 | inline | 表示への効き方 |
| --- | --- | --- | --- |
| `innerHTML`（代入 144、読み 66） | 227 | 8 | 部品の中身を作る（carousel・popover・候補）。無いと中身が出ない |
| `getComputedStyle(` | 57 | 8 | 表示の判定・大きさの計算 |
| `outerHTML` | 51 | 0 | 部品の複製・置き換え |
| `offsetTop`・`offsetParent`・`offsetLeft` | 35・21・13 | 0 | popover・sticky の位置 |
| `scrollTo(`・`scrollIntoView(`・`scrollBy(` | 18・10・3 | 3 | carousel の送り（操作） |
| `insertAdjacentHTML` | 15 | 0 | 部品の差し込み |

順: 1. この Phase（p081）: HTML の断片の parse と直列化（`innerHTML`・`outerHTML`・`insertAdjacentHTML`・`insertAdjacentElement`・
`insertAdjacentText`）。2. 次の Phase（予定の p082）: `getComputedStyle`、`offsetTop`・`offsetLeft`・`offsetParent`、`scrollTo`・`scrollBy`・
`scroll`・`scrollIntoView`。

## 範囲

- `html/`: HTML の断片の parse（HTML Standard の「parsing HTML fragments」: context の要素による tokenizer の状態と insertion mode、
  root の html 要素、form の要素の pointer、template の mode）。html5lib の tree construction の fragment の case で確かめる
  （`host-tree` と `run-html5lib-tree.py` に fragment の case を足す）。
- 直列化（HTML Standard の「serializing HTML fragments」: 属性と text の escape、void 要素、raw text の要素、template の中身、
  外の namespace の名前）。
- `bind/`: `Element.innerHTML`・`outerHTML`（読み書き）、`insertAdjacentHTML`・`insertAdjacentElement`・`insertAdjacentText`、
  `DocumentFragment`・`ShadowRoot` は無い（`innerHTML` は Element だけ）。断片の中の script は走らない（他の browser と同じ）。

この Phase に無いもの: `DOMParser`、`Range.createContextualFragment`、`document.write`、XML の直列化。

## 進み具合（2026-09-29、wrap up で引き継ぎ。main の指示で browser の作業は別のエージェントへ）

済み（commit 済み、host の build は plain で warning 0）:
- `html/parser.c`・`parser.h`・`html.h`: `html_parser_create_fragment`・`html_parser_fragment_root`（context による tokenizer の状態、
  root の html 要素、template の mode、insertion mode の reset、form の pointer）。template の context は form の pointer について
  template の中身として扱う（`tb_template_contents`、`body.c` の form の 3 箇所）。context の名前を last start tag にしない（html5lib の
  期待どおり、raw text の中の同名の end tag は文字）。
- `plan/ws074/tests/host-tree.c`・`run-html5lib-tree.py`: fragment の case も走らせる（`SCRIPTING\tNS:NAME\tINPUT`）。
  **html5lib（WPT 2d66b9b7）の tree construction: 1854/1959、fragment 206/206**（前は document 1648/1753 で fragment は走らせず。
  document の数は不変）。suite は worktree の `build/ws074-suites/wpt`（`fetch-suites.sh wpt` で取得）。
- `dom/serialize.c`（新）: `dom_serialize_children`・`dom_serialize_node`（HTML の直列化。属性の値の `<`・`>` も escape: Chromium 153 と同じ）。
- `bind/markup.c`（新）: `innerHTML`・`outerHTML`（読み書き）、`insertAdjacentHTML`・`insertAdjacentElement`・`insertAdjacentText`。
  `element.c` の表、`internal.h`、`libbrowser/Makefile`。
- 確認（host、plain）: 回帰は全て前と同じ（golden 76、host-* 全て 0 failed、run-dom 14/14、run-js 11/11、loader 11/11、http 14・17、
  font 8）。worktree の `build/p031/t5.html`（innerHTML・insertAdjacent*・outerHTML・table の中の tr・script が走らないこと）を
  Chromium と比べ、`template.content` の行以外は一致。

残り（次のエージェント）:
1. `HTMLTemplateElement.content` の binding（`t.content` が undefined。上の比較で唯一違った行）。
2. `plan/ws074/tests/dom/markup.html` と Chromium の expected（`build/p031/t4.html`・`t5.html` を元に。`run-dom-tests.py --reference`）。
3. ASan の build と回帰、style-check（`bind/markup.c`・`dom/serialize.c`・`html/parser.c` の新しい部分: 複合の条件の中の呼び出し、
   閉じ括弧の後の空行など、まだ直していない）、guest（`build-browser-image.sh`、run-dom-tests `--outputs`、live の Amazon）、boot test。
4. Amazon の capture の `--run` の Uncaught と `live-compare.py` の画素の比較（p080 の後: top 64.47%、search 76.04%）。
5. phase.md を cleared の形にして ws.md の p081 の行を足す。

その次の Phase（予定の p082、上の表の順）: `getComputedStyle`（computed の値の直列化）、`offsetTop`・`offsetLeft`・`offsetParent`
（layout の box の位置と positioned の祖先）、`scrollTo`・`scrollBy`・`scroll`・`scrollIntoView`（view への scroll の要求の host callback）。

patch は無い（全て commit 済み）。
