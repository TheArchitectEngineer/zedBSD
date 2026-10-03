<!-- awesome-plan project=zedbsd record=ws074p083 -->

# ws074-p083: DOMException

Phase ID: `ws074-p083`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-29）
Phase disposition: normal
Queue: なし（main の指示でサブエージェントが worktree `wt/ws074-dom` で実行、2026-09-29。main（p086 の後）を取り込んでから）
依存: p030（DOM の binding）、p081、p082
由来: p081 の報告（`bind_throw_dom` が `Error` を投げ `e.name` が "Error"）。main の指示（2026-09-29）で p082 の後に。

## 範囲

- `DOMException` の interface（WebIDL: 構築子 `new DOMException(message, name)`、`name`・`message`・`code`、旧来の code の定数
  `INDEX_SIZE_ERR` ほか、prototype は `Error.prototype` を継ぐ）。
- `bind_throw_dom` を DOMException の object を投げるように（name から code を引く）。
- 既存の試験の頁（dom/）で例外を `instanceof Error` だけで見ている行を、`e.name`・`e.code`・`instanceof DOMException` で見るように
  直し、Chromium の expected を作り直す。

## 設計と実装

- `bind/exception.c`（新）: `DOMException`。platform object の cell に name・message（文字列、trace で mark）と legacy code。
  prototype は `Error.prototype` を継ぐ（`window.c` の `window_make_interface` で `BIND_DOM_EXCEPTION` だけ親を realm の
  `VM_INTRINSIC_ERROR_PROTOTYPE` に）ので `instanceof Error` は真、`Error.prototype.toString` が "Name: message" と書く（console の
  "Uncaught HierarchyRequestError: …" も同じ）。`name`・`message`・`code` は prototype の accessor、code の定数 25（`INDEX_SIZE_ERR`〜
  `DATA_CLONE_ERR`）は interface object と prototype に。構築子 `new DOMException(message = "", name = "Error")`、code は WebIDL の名前の
  表から（表に無い名前は 0）。
- `bind_throw_dom`（`node.c` から `exception.c` へ移した）: DOMException を作って投げる。window が interface を作る前は前と同じ
  `Error`。呼ぶ側（20 箇所: HierarchyRequestError・InvalidCharacterError・InvalidStateError・NoModificationAllowedError・
  NotFoundError・NotSupportedError・QuotaExceededError・SyntaxError）は変えていない。
- `bind/internal.h`・`window.c`（interface の表）、`libbrowser/Makefile`。`js/`・`vm/`・`page/` は変えていない。

## 試験

- `plan/ws074/tests/dom/exception.html`（19 行、新）と Chromium 153 の expected: 構築子（既定値、表に無い名前）、`instanceof`、
  prototype の鎖、`String()`、定数、名前ごとの code（15 個）、DOM の操作の例外（appendChild・removeChild・createElement・
  querySelector・classList・insertAdjacentHTML・computed style への代入）と TypeError のままのもの、script の throw。
- 既存の頁で例外を `instanceof Error` か「投げた」だけで見ていた行を name・code で見るように直し、expected を Chromium で作り直した:
  `tree.html`（2 行）、`classlist.html`（1 行）、`query.html`（2 行）、`markup.html`（2 行）、`computed.html`（1 行）。他の expected は不変。

## 確認（host は Debian の cc と Chromium 153。guest は QEMU。実機は未実施）

- host の build（plain と ASan、-Werror）warning 0。style-check: `bind/exception.c`・`window.c`・`node.c` に指摘 0。
- 回帰（plain と ASan、同じ結果）: golden 76/76、host-view 59、host-form 28、host-link 22、host-position 19、host-text 20、
  host-relayout 161、host-base 2038・heap 31・interp 45・number 71・object 98（全て 0 failed）、run-dom-tests **18/18**（exception を追加）、
  run-js-tests 13/13、loader 11/11、http 14/14・`--async` 17/17、font 8/8、html5lib tree 1854/1959・fragment 206/206。ASan の `--render`
  （top-local・search-local）に報告 0。
- Amazon（capture、`--run`、main の p086 の後）: top-local の Uncaught 2（`Set is not defined` 1・for-of 1）、search-local 3（for-of 3）。
  DOM の側の Uncaught は無い。`live-compare.py`（script 付き）: top-local 画素 64.47%・ink 57.66%、search-local 76.04%・ink 33.01%（不変）。
- guest（QEMU、worktree の image を作り直した。clang・libcxx を含まない config、browser の compile に warning 0）: run-dom-tests
  `--outputs` **18/18**。live の `https://www.amazon.co.jp/` の `--run`（取得 1 回）: Uncaught 0。
- boot test: PASS（worktree の `build/p081/boot-test83/login.png`）。
- 実機: 未実施。

## 写真

- host: worktree の `build/ws074-shots/p083-20260929-amazon-top-local-side.png`・`…-search-local-side.png`。

## 残り・制限

- `Object.prototype.toString` は "[object Object]"（`Symbol.toStringTag` が engine に無い。p087 の後に足せる）。`stack` は無い。
- DOM の例外の message の文は Chromium（"Failed to execute '…' on '…': …"）と違う（試験は name と code だけを見る）。
- QuotaExceededError は DOMException のまま（最新の仕様では DOMException の subclass の interface）。

## 結果

- cleared。DOM の操作の例外が DOMException になり、name・code・instanceof が Chromium と一致した。試験 1 つを足し、既存の 5 つの頁を
  name・code で見るようにした。host（plain・ASan）と guest で全て通った。
