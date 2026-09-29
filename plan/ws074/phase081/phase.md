<!-- awesome-plan project=zedbsd record=ws074p081 -->

# ws074-p081: HTML の断片の parse と直列化（innerHTML・outerHTML・insertAdjacentHTML）

Phase ID: `ws074-p081`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-29）
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

## 設計と実装

- `html/parser.c`・`parser.h`・`html.h`: `html_parser_create_fragment`・`html_parser_fragment_root`（context による tokenizer の状態、
  root の html 要素、template の mode、insertion mode の reset、form の pointer）。template の context は form の pointer について
  template の中身として扱う（`tb_template_contents`、`body.c` の form の 3 箇所）。context の名前を last start tag にしない（html5lib の
  期待どおり、raw text の中の同名の end tag は文字）。
- `dom/serialize.c`（新）: `dom_serialize_children`・`dom_serialize_node`（HTML の直列化。属性の値の `<`・`>` も escape: Chromium 153 と
  同じ。tag の名前は `serialize_tag_name`、属性の名前は `serialize_attribute_name` に分け、各 append の失敗をその場で調べる）。
- `bind/markup.c`（新）: `innerHTML`・`outerHTML`（読み書き）、`insertAdjacentHTML`・`insertAdjacentElement`・`insertAdjacentText`、
  `HTMLTemplateElement`（`content`）。template の中身は `bind_template_contents` が探し、script が作った・複写した template には
  最初に要る時に作る（`innerHTML` の書き込みと `content`）。
- `bind/node.c`: template の prototype を `HTMLTemplateElement` に、`cloneNode(true)` は template の中身も複写。
- `bind/element.c`: `Element.namespaceURI`（Amazon の script に 13 箇所。試験の foreign の行で要った）。
- `bind/internal.h`・`window.c`（interface の表）、`libbrowser/Makefile`。`js/`・`vm/` は変えていない。

## 試験

- `plan/ws074/tests/host-tree.c`・`run-html5lib-tree.py`: fragment の case も走らせる（`SCRIPTING\tNS:NAME\tINPUT`）。
- `plan/ws074/tests/dom/markup.html`（24 行、新）と Chromium 153 の expected（`run-dom-tests.py --reference`、他の expected は不変）:
  直列化（属性と text の escape、void、raw text、svg、noscript、template）、`innerHTML` の読み書き（script は走らない）、
  `insertAdjacent*`（位置の大文字小文字、親の無い要素、誤った位置）、table・select の context、`outerHTML` の書き込み（親の無い要素、
  document の子）、`template.content`（script が作ったものと parse したもの、`cloneNode`）、pre・textarea・listing の先頭の改行、
  MathML・SVG、空の文字列、誤りのある断片。DOMException の `name` は出さない（下の制限）。

## 確認（host は Debian の cc と Chromium 153。guest は QEMU。実機は未実施）

- host の build（plain と ASan、-Werror）warning 0。style-check（`plan/tools/style-check.py`）: `bind/markup.c`・`dom/serialize.c`・
  `bind/element.c`・`bind/node.c`・`html/parser.c`・`html/body.c` に指摘 0。
- 回帰（plain と ASan、同じ結果）: golden 76/76、host-view 59/59、host-form 28/28、host-link 22/22、host-position 19/19、
  host-text 20/20、host-relayout 161/161、host-base 2038・heap 31・interp 45・number 71・object 98（全て 0 failed）、run-dom-tests
  **15/15**（markup を追加）、run-js-tests 12/12、run-loader-tests 11/11、run-http-tests 14/14・`--async` 17/17、run-font-tests 8/8、
  **html5lib（WPT 2d66b9b7）の tree construction 1854/1959、fragment 206/206**（前は document 1648/1753 で fragment は走らせず。document の
  数は不変）。main（p085 class）を取り込んだ後にも plain で全て同じ結果。ASan の `--render`（top-local・search-local）に報告 0。
- Amazon（capture、`--run`、main の p085 の後）: top-local の Uncaught 2（async 1・for-of 1）、search-local 7（async 4・for-of 1・
  `(at 1:1)` の `Cannot read properties` 2）。DOM の側の Uncaught は無い（p080 の後と同じ種類。class が async に置き換わった）。
- `live-compare.py`（script 付き）: top-local 画素 64.47%・ink 57.66%、search-local 76.04%・ink 33.01%（不変）。
- guest（QEMU、worktree の image を作り直した（main の p085 の取り込みの前）。clang・libcxx を含まない config、browser の compile に
  warning 0）: run-dom-tests `--outputs` **15/15**（`XDG_DATA_HOME=/tmp/xdg`）。live の `https://www.amazon.co.jp/` の `--run`（取得 1 回）:
  Uncaught 0（console の出力 0 行）、`--dump=dom`（取得 1 回）で 921 KB の DOM（`nav-search` 22 箇所）を確かめた。
- boot test: PASS（worktree の `build/p081/boot-test/login.png`）。
- 実機: 未実施。guest の窓（zdesktop）での表示: 未実施。

## 写真

- host: worktree の `build/ws074-shots/p081-20260929-amazon-top-local-side.png`・`…-search-local-side.png`。

## 残り・制限

- DOMException が無い: `bind_throw_dom` は `Error`（message が「名前: 文」）を投げ、`e.name` は "Error"（Chromium は "SyntaxError" 等）。
  DOM の全体に関わるので別の Phase（main に計画を依頼）。
- `DOMParser`、`Range.createContextualFragment`、`document.write`、XML の直列化、`ShadowRoot`・`DocumentFragment` の `innerHTML`。
- 画素の一致は不変: Amazon の表示の差は innerHTML ではなく、script の残り（async・for-of）と layout の側。

## 結果

- cleared。Amazon の script で最も多い DOM の API（innerHTML・outerHTML・insertAdjacentHTML）と `template.content` が入り、
  Chromium と一致する試験 1 つと html5lib の fragment 206 case を足した。host（plain・ASan）と guest で全て通った。

## 経緯（2026-09-29、前任の wrap up の時の記録）

- 前任は断片の parse・直列化・binding を commit して引き継いだ。残り（template.content、試験の頁、ASan・style-check・guest・boot、
  Amazon、記録）をこのエージェントが仕上げた。
