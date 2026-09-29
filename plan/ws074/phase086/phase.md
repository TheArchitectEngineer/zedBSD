<!-- awesome-plan project=zedbsd record=ws074p086 -->

# ws074-p086: async function（と generator・Promise・microtask。p029 から切り出した）

Phase ID: `ws074-p086`
Parent: [WS074](../ws.md)
Status: **cleared**（2026-09-29）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行。main の指示「p086 = p029 から切り出した async function（と、それに要る
generator・Promise・microtask の queue）。p029 の残り（async generator 等）は p029 に残す」）
依存: p030（microtask の queue）、p078・p079・p085（arrow・class の method）
由来: Amazon の capture の `not supported yet: async functions`（top 1、search 4）と、search の class の中の `async r=>` で止まる連鎖の
`Cannot read properties of undefined or null (at 1:1)` 2。

## 範囲

- generator function（宣言・式・object と class の method・static）、`yield`（`yield*` を除く）、`next`・`return`（finally を通る）・
  `throw`、`%GeneratorPrototype%`・`%GeneratorFunction.prototype%`。
- Promise: constructor（subclass の new.target）、`then`・`catch`・`finally`、`Promise.resolve`・`reject`・`all`・`allSettled`・`any`・
  `race`・`withResolvers`、thenable の解決、microtask の順、処理されない reject の報告（`Uncaught (in promise) …`）。
- async function（宣言・式・arrow・method・static）、`await`、`%AsyncFunction.prototype%`。

この Phase に無いもの（p029 に残す、または後の Phase）: async generator と `for await`、`yield*`（iterator の protocol が要る）、
Symbol（`Symbol.iterator`・`Symbol.species`・`Symbol.toStringTag`: Object.prototype.toString は kind で Promise・Generator を返す代わり）、
GeneratorFunction・AsyncFunction の constructor（source の文字列から）、AggregateError の constructor（any は name が AggregateError の
Error を作る）、組み合わせの関数の iterator は vm_iter_start が扱えるもの（配列・文字列・arguments）だけ、IteratorClose。

## 設計と実装

- **VM**（`vm/generator.c`・`vm/promise.c` 新、`interpreter.c`・`realm.c`・`function.c`・`object.c`・`code.c`・`bytecode.h`・
  `internal.h`・`vm.h`、`libbrowser/Makefile`）:
  - 命令 `suspend R value, R sent, R how`。generator と async function の code は常に自分の run の entry frame で走る（call は
    `vm_generator_call` へ回し、C から `vm_interpret_start` で入る）。suspend は frame の register・this・argc・new.target と次の pc を
    `struct vm_generator`（cell、register は malloc で trace する）に写し、frame を pop して run を終える。`vm_interpret_resume` は frame を
    stack に戻し、sent と how（`VM_RESUME_NEXT`・`THROW`・`RETURN`）を suspend の register に入れて続きを走らせる。
  - code の flag `VM_CODE_GENERATOR`・`VM_CODE_ASYNC`（constructor ではない）。generator function の closure は `prototype`（
    `%GeneratorPrototype%` を継ぐ、constructor の property なし）を持ち、[[Prototype]] は `%GeneratorFunction.prototype%`、async
    function は `%AsyncFunction.prototype%`。
  - generator の呼び出しは引数の prologue を走らせ、compiler が本体の先頭に置く suspend で止めて generator object（kind
    `VM_KIND_GENERATOR`、internal が run の cell）を返す。状態は START・SUSPENDED・RUNNING・DONE。
  - async function の呼び出しは promise を作り、最初の await まで走らせる。await は `vm_promise_await`（Promise が作った promise は
    そのまま、他は新しい promise で解決）に await の job を足し、settle で job が run を next か throw で再開する。終わりは promise を
    resolve、例外は reject。
  - promise は `struct vm_promise`（object、cell type `vm_promise_type`、`vm_value_is_object` が object と認める）。反応は
    `struct vm_promise_job`（反応・thenable・await の 3 種）の list で、settle で順に realm の microtask の queue に入る（queue の
    callback が job の cell のとき `vm_run_jobs` が `vm_promise_run_job` に渡す）。resolving function の対は record を共有する native
    function。Promise 自身の then の派生 promise は function を作らず直接 settle する。
  - 処理されない reject: reject の時に handled でなければ realm の `rejections` に入れ、checkpoint の終わりにまだ handled でないものを
    report に渡す（`realm->reporting_rejection` を立てる）。
- **compiler**（`js/compile.c`・`compile_expr.c`）: flag、generator の先頭の suspend、`yield`（how で next・throw・return を分け、
  return は `js_emit_return` で finally を通る）、`await`（throw は THROW）。async generator と `yield*` は unsupported のまま。
- **built-ins**（`js/builtin_promise.c`・`builtin_generator.c` 新、`builtin.c`・`builtin.h`・`builtin_object.c`）。
- **embedder**: `js/tool.c`（`--js` は script の後に microtask を走らせ、未処理の reject を `Uncaught (in promise)` と書く）、
  `bind/window.c`（**bind/ の変更は 1 箇所**: `bind_report_exception` が `reporting_rejection` のとき `Uncaught (in promise) ` と書く）、
  `plan/ws074/tests/host-js.c`（test262 の driver が script の後に microtask を走らせる。async の試験のため）。
- 試験: `plan/ws074/tests/js/async.js`・`async.expected`（generator・Promise・microtask の順・async function。Chromium 153 と一致）、
  `plan/ws074/tests/dom/promise.html`・`promise.expected`（page の script・timer・event の後の checkpoint、await と timer、未処理の reject）。

## 確認（host は Debian の cc、guest は QEMU。実機は未実施）

- host の build（plain と ASan、-Werror）warning 0。style-check（新しい file と変えた file）: 指摘 0（変えた file は変更前も 0）。
- test262: **21806 → 24995 / 47792**（ES5 7748 のまま）。p085 の failures（`build/ws074-test262-failures.txt`）と比べて**新しく落ちた
  試験 0**。新しい failures を同じ path に置いた。`built-ins/Promise` 476/732、`expressions/async-function` 73/93、
  `statements/async-function` 64/74、`expressions/await` 20/22、`statements/generators` 214/266、`expressions/generators` 232/290、
  `built-ins/GeneratorPrototype` 57/61、`expressions/async-arrow-function` 43/60、`expressions/yield` 20/63、`statements/class`
  2035 → 2896/4353。ASan の driver でもこれらは同じ数で、crash・sanitizer の報告なし。残りの多くは async generator（3898）、for-of、
  Symbol・Proxy・Reflect、`yield*`。
- 回帰（plain と ASan、同じ結果）: golden 76/76、host-view 59、form 28、link 22、position 19、text 20、relayout 161、base 2038・heap 31・
  interp 45・number 71・object 98（0 failed）、run-dom-tests **15/15**（promise を追加）、run-js-tests **13/13**（async を追加）、
  loader 11/11、http 14/14・`--async` 17/17、font 8/8。GC の負荷（300 の停止中の generator と async function、ASan）で誤りなし。
- Amazon（script 付きの capture、host）: async の SyntaxError は **0**（top 1 → 0、search 4 → 0）、連鎖の `(at 1:1)` 2 も 0。
  Uncaught: top 2（`Set is not defined` 1: async の後に走るようになった code、for-of 1）、search 7 → **3**（for-of 3）。画素は
  ほぼ不変（top 64.47%・ink 57.66%、search 76.04%・ink 33.01%）。ASan の `--render`（top・search）で報告 0。
- guest（QEMU、`config-amd64-browser.mk` の image、headless の guest）: browser の build の warning 0。run-js-tests `--outputs` 13/13、
  run-dom-tests `--outputs` 15/15。
- `boot-check.sh p086`: PASS、login prompt を画面で確認（`/home/awe/zedBSD-rpi4/build/ws074-shots/p086-20260929-boot-login.png`）。

## 写真

- host: `/home/awe/zedBSD-rpi4/build/ws074-shots/p086-20260929-amazon-top-local.png`・`…-search-local.png`（見た目は p085 と同じ）。

## 未実施・制限・残り

- 実機は未実施。guest の窓での確認と live の amazon.co.jp の取得は未実施。
- 範囲の節の「この Phase に無いもの」。引数の default の TDZ（p079 の制限）が async の試験の一部に出る。
- Amazon の次の blocker: for-of（top 1、search 3）と `Set`（top 1）→ 次の Phase（Symbol・iterator・for-of、Map・Set）。
