<!-- awesome-plan project=zedbsd record=ws074p087 -->

# ws074-p087: Symbol・iterator の protocol・for-of と Map・Set（p028 の残りから切り出した）

Phase ID: `ws074-p087`
Parent: [WS074](../ws.md)
Status: **cleared**（2026-09-29）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行。main の指示「ws074-p087: Symbol・iterator の protocol・for-of と
Map・Set（WeakMap・WeakSet も含めてよい）」）
依存: p079（反復の命令）、p086（generator）
由来: Amazon の capture の `not supported yet: for-of`（top 1、search 3）と `Set is not defined`（top 1）。

## 範囲

- Symbol: `Symbol()`（new は TypeError）、`Symbol.for`・`keyFor`、well-known の 13 の symbol（realm が作る）、`Symbol.prototype`
  （toString・valueOf・description・`@@toPrimitive`・`@@toStringTag`）、`String(symbol)`。
- iterator の protocol: `vm_iter_start` が `@@iterator` を呼び、`next` を読む（配列・arguments・文字列は built-in の iterator の
  ままなら直接）。`IteratorClose`（for-of の break・return・外への jump・例外、配列の分割代入の終わりと例外）。
- `for-of`（var・let・const・分割代入の左辺、各回の let の環境、label 付きの continue・break）。
- built-in の iterator: `%IteratorPrototype%`（`@@iterator`）、Array の `keys`・`values`・`entries`・`@@iterator`、arguments の
  `@@iterator`、String の `@@iterator`、Map・Set の iterator、generator は `%IteratorPrototype%` を継ぐ（spread・for-of ができる）。
- `Map`・`Set`・`WeakMap`・`WeakSet`（SameValueZero、挿入の順、反復中の削除と追加、`forEach`、`size`、`@@species`、各 method の
  brand の検査）。
- symbol を使う既存の操作: `@@toPrimitive`（ToPrimitive、`Date.prototype[@@toPrimitive]`）、`@@hasInstance`（instanceof、
  `Function.prototype[@@hasInstance]`）、`@@toStringTag`（Object.prototype.toString。Math・JSON・Promise・generator・Map 等に付けた）、
  `@@species`（Array・Promise・RegExp・Map・Set の getter、Promise の then が使う）、RegExp の `@@match`・`@@replace`・`@@search`・
  `@@split` と String の match・replace・search・split からの委譲。
- 追加（Amazon の async の後に出た `encodeURIComponent is not defined`）: `encodeURI`・`encodeURIComponent`・`decodeURI`・
  `decodeURIComponent`・`escape`・`unescape`（`js/builtin_uri.c`）。

この Phase に無いもの: WeakMap・WeakSet の弱さ（key を強く持つ。collector に ephemeron が無い）、Set の ES2025 の method（union 等）、
Map.groupBy・getOrInsert、iterator helper（Iterator.prototype.map 等）、`@@isConcatSpreadable`・`@@unscopables`・`@@matchAll`、
for-of の完了値と head の TDZ、`yield*`・for await・async generator（p029）。

## 設計と実装

- **VM**: `vm.h`（well-known の symbol の enum と `realm->symbols`、`vm_symbol.registered`、kind と intrinsic の追加、`vm_iter_close`・
  `vm_get_method`・`vm_ordinary_has_instance`・`vm_ordinary_to_primitive`・`vm_symbol_key`）、`realm.c`（symbol を作り trace）、
  `spread.c`（protocol の反復、built-in の判定、close）、`access.c`（GetMethod、`@@hasInstance`）、`operation.c`（`@@toPrimitive`）、
  `interpreter.c`・`bytecode.h`・`code.c`（`for_of_next`・`iter_close`、arguments の `@@iterator`）。
- **compiler**: `compile.c` の `compile_for_of`（本体を try の finally のように守り、break・return・外への jump・例外で close。
  例外のときの close は quiet）、`compile_expr.c`（配列の pattern の終わりと例外で close）、`emit.c`（`js_emit_for_of_next`）。
- **built-ins**（新: `builtin_symbol.c`・`builtin_iterator.c`・`builtin_collection.c`・`builtin_uri.c`。変更: `builtin.c`・`builtin.h`・
  `builtin_function.c`・`builtin_object.c`・`builtin_string.c`・`builtin_regexp.c`・`builtin_date.c`・`builtin_promise.c`・
  `builtin_generator.c`、`libbrowser/Makefile`）。Map・Set の表は挿入順の entry の配列（削除は場所を残す）と hash の chain。反復中は
  表を詰めない（iterator と forEach が `holds` を数える。最後まで回らなかった iterator は詰めるのを止めたままにする: memory だけの損）。
- bind/ の変更なし。
- 試験: `plan/ws074/tests/js/iteration.js`・`iteration.expected`（Chromium 153 と一致）。

## 確認（host は Debian の cc、guest は QEMU。実機は未実施）

- host の build（plain と ASan、-Werror）warning 0。style-check（新しい file と変えた file）: 指摘 0。
- test262: **24995 → 28381 / 47792**（ES5 7748 → 7758）。p086 の failures と比べて**新しく落ちた試験 0**。新しい failures を
  `build/ws074-test262-failures.txt` に置いた。`built-ins/Symbol` 75/92、`Map` 162/204、`Set` 221/383（残りは ES2025 の method）、
  `WeakMap` 115/141、`WeakSet` 83/85、`statements/for-of` 672/741、`ArrayIteratorPrototype` 17/27（残りは型付き配列）、
  `String/Map/SetIteratorPrototype` 全部、URI の 4 つ 全部、`built-ins/Promise` 476 → 599/732、`assignment/dstr` 346/368。
  ASan の driver でもこれらは同じ数で、crash・sanitizer の報告なし。
- 回帰（plain と ASan、同じ結果）: golden 76/76、host-view 59、form 28、link 22、position 19、text 20、relayout 161、base 2038・heap 31・
  interp 45・number 71・object 98（0 failed）、run-dom-tests 17/17、run-js-tests **14/14**（iteration を追加）、loader 11/11、
  http 14/14・`--async` 17/17、font 8/8。GC の負荷（200 の Map・Set と途中の iterator、ASan）で誤りなし。
- Amazon（script 付きの capture、host）: for-of・`Set`・`encodeURIComponent` の誤りは 0。`--run` の Uncaught: top 3（`fetch` 2・
  `IntersectionObserver` 1。どれも promise の中、DOM の側）、search **0**。`--render`（live-compare）の search は
  `document.elementsFromPoint` が無い `value is not a function` 1（DOM の側）。画素は不変（top 64.47%、search 76.04%）。
  ASan の `--render`（top・search）で報告 0。
- guest（QEMU、`config-amd64-browser.mk` の image、headless の guest）: browser の build の warning 0。run-js-tests `--outputs` 14/14、
  run-dom-tests `--outputs` 17/17。
- `boot-check.sh p087`: PASS、login prompt を画面で確認（`/home/awe/zedBSD-rpi4/build/ws074-shots/p087-20260929-boot-login.png`）。

## 写真

- host: `/home/awe/zedBSD-rpi4/build/ws074-shots/p087-20260929-amazon-top-local.png`・`…-search-local.png`（見た目は p086 と同じ）。

## 未実施・制限・残り

- 実機は未実施。guest の窓での確認と live の amazon.co.jp の取得は未実施。
- 範囲の節の「この Phase に無いもの」。
- Amazon の次（DOM の側）: `fetch`、`IntersectionObserver`、`document.elementsFromPoint`。JS の側の次は型付き配列。
