<!-- awesome-plan project=zedbsd record=ws074p085 -->

# ws074-p085: class（p028 から分けた）

Phase ID: `ws074-p085`（p080 は DOM の側の TextEncoder 等の Phase が使ったので、衝突を避けて 085 にした。p081〜p084 は DOM の側の
ために空けてある）
Parent: [WS074](../ws.md)
Status: in-progress（2026-09-29。実装と host の回帰は済み、確認の残りがある。下の Resume point）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行。main の指示「p028 の残りを class → Symbol・iterator・for-of の順に」）
依存: p078（let・const）、p079（destructuring・rest）
由来: Amazon の search の inline script の `D=class{static …}` が class の SyntaxError で走らず、`window.grandprix.wrappers` が未定義に
なる連鎖（`(at 1:1)` の 2 件）。p028 の残りを class と Symbol・iterator・for-of に分けた前の方。

## 範囲

class の宣言と式、constructor（明示と既定、derived の既定は `constructor(...args) { super(...args) }` を compiler が作る）、method・
getter・setter（static も）、計算した key、`extends`（`extends null` を含む）、`super(...)`（spread も）、`super.x`・`super.m()`（arrow の
中も）、`new.target`、public の instance と static の field（計算した名前も）、static block、private name（`#x` の field・method・
accessor、static、`#x in o`）、class の名前の内側の const。class の code は strict。

この Phase に無いもの: arrow の中の `super()` と `new.target`、`super.x = v`（super の property への代入）、object literal の method の
`super`（home object を持たない）、eval の中の class の要素、Symbol（p086 予定）が要る試験。

## 設計と実装

- **VM**（`vm/class.c` 新、`bytecode.h`・`code.c`・`interpreter.c`・`function.c`・`object.c`・`vm.h`、`libbrowser/Makefile`）:
  - frame に new.target の slot（`FRAME_NEW_TARGET`、header は 7 slot）。`vm_interpret_construct` は new.target を取る。
  - code の flag `VM_CODE_CLASS`（new 無しの呼び出しは TypeError「Class constructor X cannot be invoked without 'new'」、closure は
    prototype を作らない）と `VM_CODE_DERIVED`（new は this を作らず空の値、super が束ねる。戻りで this が空なら ReferenceError、
    object でも undefined でもない戻り値は TypeError）。`load_this` は空の this で ReferenceError。
  - 命令: `load_new_target`、`class_setup`（prototype object、`constructor.prototype`（固定）、`prototype.constructor`、constructor の
    [[Prototype]] を heritage に。constructor の function の data を home object（prototype）に）、`define_method`（列挙しない method・
    getter・setter、function の data を home object に）、`load_home`、`get_super`（home の prototype から、this を receiver に）、
    `super_construct`・`super_construct_array`（constructor の [[Prototype]] を frame の new.target で構築し this を一度だけ束ねる）、
    `new_private_name`・`private_get`・`private_set`・`private_define`・`private_copy`・`private_in`。
  - private name は symbol に `private_name` の印（`vm_object_own_keys` が列挙しない）。private の member は object の own の property で、
    prototype は見ない。無い時は TypeError。instance の private method は prototype に置き、構築時に instance へ写す（`private_copy`）。
- **compiler**（`js/scope.c`・`compile.c`・`compile_expr.c`・`compile.h`・`js.h`）:
  - scope の pass: class の宣言の名前は let（program の top level は realm の record）。名前付きの class と private name・計算した field
    名を持つ class は block scope を持ち、名前・`#x`・`#0`（計算した field 名、ordinal）を const で置く。heritage は外の scope。method は
    class の scope で `scope_function`。constructor（無ければ `scope_default_constructor` が AST を作る）の scope で instance の field の
    初期化子と private name を解決する（compiler が後で使う名前も capture させる）。static の field と block は compiler が作る static の
    関数（`JS_FLAG_STATIC_INIT`）の scope で解決。class の node の third が constructor、fourth が static の関数。arrow の中の super は
    隠れた binding `#home`（と `#this`）。未宣言の private name・private name の重複（getter と setter の対を除く）・private の delete は
    早期の SyntaxError。
  - code の pass: `js_compile_class`（heritage → private name を作る → constructor の closure → `class_setup` → 順に method と計算した
    field 名 → 名前の const → static の関数を class を this に呼ぶ）。`js_compile_fields`（base の constructor は prologue の後、derived は
    `super()` の後。instance の private method の写し、field は `define_prop`・`define_elem`・`private_define`）。derived の constructor の
    `#this` は super の後に束ねる。
- 試験: `plan/ws074/tests/js/class.js` と `class.expected`（Chromium 153）。

## 確認（ここまで。host は Debian の cc）

- host の build（plain と ASan、-Werror）warning 0。style-check（変えた file）: 指摘 0。
- test262: 18558 → **21801 / 47792**（ES5 7748 のまま、inner の名前の束縛の変更の前の数）。新しく落ちた試験 0（p079 の failures と比べた）。
  `language/statements/class` 2035/4355、`language/expressions/class` 1820/4049、`expressions/super` 49/94（前の数）、`new.target` 11/14。
  残りの多くは generator・async・Symbol・Proxy・eval が要る試験。
- 回帰（plain と ASan、同じ結果）: golden 76/76、host-view 59/59、host-form 28/28、host-link 22/22、host-position 19/19、host-text 20/20、
  host-relayout 147/147、host-base 2038・heap 31・interp 45・number 71・object 98（全て 0 failed）、run-dom-tests 12/12、
  run-js-tests **12/12**（class を追加）、run-loader-tests 11/11、run-http-tests 14/14・`--async` 17/17。
- Amazon（script 付きの capture、host）: search の class の SyntaxError 2 → 0。その class の中の `async r=>` で止まるので、`(at 1:1)` の
  2 件はまだ残る（async は p029）。search の Uncaught: async 4、for-of 1、`TextEncoder` 1（この branch に DOM の p080 がまだ無い時点）、
  `Cannot read properties` 3。top: async 1、for-of 1、`TextEncoder` 1。

## Resume point（2026-09-29、wrap up）

- 全ての差分は commit 済み（wip.patch は無い）。build は通る。
- 残りの確認（この順）: (1) test262 の全体を流して p079 の failures（`build/ws074-test262-failures.txt` を前の run と比べる手順は
  p079 の phase.md）と比べ、新しく落ちた試験 0 を確かめる。(2) ASan の driver で `language/statements/class`・`expressions/class`・
  `expressions/super` の数が plain と同じか。(3) `live-compare.py` で Amazon の top-local・search-local を撮る（`--shots
  /home/awe/zedBSD-rpi4/build/ws074-shots --tag p085-…`）と ASan の `--render`。(4) guest: `make ZEDBSD_CONFIG=plan/ws074/tests/config-amd64-browser.mk
  build/amd64/bin/browser`、`sh plan/ws074/tests/build-browser-image.sh`（clang・libcxx を含まない config）、headless の guest で
  run-js-tests と run-dom-tests の `--outputs`（手順は p079 の phase.md）。(5) `sh plan/ws074/tests/boot-check.sh p085`。
  (6) この phase.md を cleared にし、ws.md の表と amazon-goal.md に自分の行を足す。
