<!-- awesome-plan project=zedbsd record=ws074p085 -->

# ws074-p085: class（p028 から分けた）

Phase ID: `ws074-p085`（p080 は DOM の側の TextEncoder 等の Phase が使ったので、衝突を避けて 085 にした。p081〜p084 は DOM の側の
ために空けてある）
Parent: [WS074](../ws.md)
Status: **cleared**（2026-09-29）
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

## 確認の続き（2026-09-29、引き継いだエージェント。host は Debian の cc、guest は QEMU。実機は未実施）

- test262 の全体: **21806 / 47792**（ES5 7748 / 8087）。p079 の終わりの commit（`a02f6b0e`、vm/spread.c を足した commit）を
  scratch に `git archive` して host-js を build し、同じ runner で全体を流した（18558）。失敗の一覧を比べて**新しく落ちた試験 0**。
  新しい失敗の一覧を `build/ws074-test262-failures.txt` に置いた（次の Phase の比較の基準）。全体の実行は 64 CPU で約 45 s。
- ASan の driver: `language/statements/class` 2035/4355、`expressions/class` 1820/4049、`expressions/super` 49/94、
  `expressions/new.target` 11/14。plain と同じ数で、やり直しの chunk（crash・sanitizer の報告）なし。
- 回帰（plain と ASan、同じ結果）: golden 76/76、host-view 59/59、host-form 28/28、host-link 22/22、host-position 19/19、host-text
  20/20、host-relayout 161/161、host-base 2038・heap 31・interp 45・number 71・object 98（0 failed）、run-dom-tests 14/14（DOM の側の
  試験が増えた）、run-js-tests 12/12、run-loader-tests 11/11、run-http-tests 14/14・`--async` 17/17、run-font-tests 8/8。
  （host-base は plain と ASan を同じ scratch の directory で同時に走らせた時に 2 failed、別々の directory で各 0 failed。試験の
  directory の衝突で、engine の誤りではない。）
- Amazon（script 付きの capture、host、main の DOM の p080 を merge した後）: Uncaught top **2**（async 1、for-of 1）、search **7**
  （async 4、for-of 1、`Cannot read properties of undefined or null (at 1:1)` 2: class の中の `async r=>` で script が止まる連鎖）。
  画素は不変（top 64.47%・ink 57.66%、search 76.04%・ink 33.01%）。ASan の `--render`（top・search）で sanitizer の報告 0。
- guest（QEMU、`config-amd64-browser.mk` の image、headless の guest）: browser の build の warning 0。run-js-tests `--outputs`
  12/12、run-dom-tests `--outputs` 14/14（guest で `--js`・`--run` を走らせ、出力を scp で持ち帰って比べた）。
- `boot-check.sh p085`: PASS、login prompt を画面で確認（`/home/awe/zedBSD-rpi4/build/ws074-shots/p085-20260929-boot-login.png`）。

## 写真

- host: `/home/awe/zedBSD-rpi4/build/ws074-shots/p085-20260929-amazon-top-local.png`・`…-search-local.png`（見た目は p079 と同じ）。

## 未実施・制限・残り

- 実機は未実施。guest の窓での確認は未実施。live の amazon.co.jp は取得しなかった（capture で比べた）。
- 範囲の節の「この Phase に無いもの」（arrow の中の `super()`・`new.target`、`super.x = v`、object literal の method の `super`、
  eval の中の class の要素、Symbol が要る試験）。
- Amazon の次の blocker: async function（top 1、search 4）→ p086 以降、for-of（各 1）。
