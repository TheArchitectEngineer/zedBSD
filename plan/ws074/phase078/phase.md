<!-- awesome-plan project=zedbsd record=ws074p078 -->

# ws074-p078: ES2015 の構文 1a（let・const・TDZ、arrow、template）

Phase ID: `ws074-p078`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-29）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行。main の指示「p028（let/const、arrow、template から）」）
依存: p025（compiler）、p077（Uncaught の位置）
由来: p028（ES2015 の意味 1）は大きいので、Amazon の script が最初に要る let・const・arrow・template をこの Phase に分けた。
p028 には class・destructuring・spread・Symbol・iterator・for-of・Map・Set・Weak* が残る。

## 範囲

- let・const: block scope（block・for・for-in・switch）、TDZ（初期化の前の読み書きは ReferenceError）、const への代入は TypeError、
  loop の反復ごとの binding（closure が捕まえる時は反復ごとの環境）、script の top level の let・const は script 間で共有する
  global の lexical な record（global object の property にはしない）。
- arrow function: 外の this・arguments、式の本体、new できない。
- template literal: `${}` の ToString と連結、tagged template（strings と raw の配列）。

この Phase に無いもの（p028 に残る）: class、destructuring・default・rest、spread、Symbol、iterator、for-of、generator、Map・Set。

## 設計と実装

- **VM**（`vm/bytecode.h`・`code.c`・`interpreter.c`・`access.c`・`operation.c`・`realm.c`・`vm.h`）: 命令 `to_string`（ToString）・
  `load_empty`（空の値 = TDZ の印）・`check_init`（空なら「Cannot access 'x' before initialization」の ReferenceError）・`throw_error`
  （種類と message の新しい Error を投げる）・`define_global_lexical`・`init_global_lexical`。realm の `lexicals`（script の top level の
  let・const の record、global object とは別、trace する）。`vm_get_global`・`vm_put_global`・`vm_delete_global` は先にこの record を見る
  （空なら ReferenceError、const への代入は TypeError、delete は false）。同じ名前の二度目の宣言は SyntaxError
  （「Identifier 'x' has already been declared」）。
- **scope の pass**（`js/scope.c`・`compile.h`）: scope の種類 `JS_SCOPE_BLOCK`、binding の種類 `JS_BINDING_LET`・`CONST`・`THIS`。
  block・for の head・switch の case 群は、let・const を直接宣言する時だけ scope を持つ（node->scope）。関数の本体の top level の
  let・const は関数の scope、program の top level のものは `global_lexicals`（realm の record）。入れ子の関数が捕まえる block の
  binding は block 自身の環境（`has_env`・`env_count`・`env_register`）に置き、解決の hop は環境を持つ block を出る時にも数える。
  scope を持つ block の関数宣言は block に入る時に作り（`JS_FLAG_BLOCK_FUNCTION`、block の binding が見える）、var がそれを受ける
  （Annex B）。早期の誤り: 同じ scope での二重の宣言、var・引数・関数と同じ名前の let・const、block の let・const と同じ名前の
  block の中の var と block の関数宣言。
- **code の pass**（`js/compile.c`・`compile_expr.c`）: block に入る時、環境を持つ block は `new_env`（入るたび、つまり loop の各回で
  新しい環境）、let・const に空の値を置く。let・const の読みは `check_init`、代入は先に `check_init`、const なら TypeError。
  宣言は `js_init_binding`（検査なし、program の top level は `init_global_lexical`）。`for (let …)` は各回の update の前に環境を
  複写（`compile_scope_renew`）、`for (let/const k in …)` は各回に新しい環境。block の環境は block ごとの register に置くので、
  break・continue・例外で元の環境に戻す code は要らない。
- **arrow**: 本体が式の時はその値を返す。this は外側の arrow でない関数（または program）の隠れた binding `#this` に prologue で
  入れ、arrow はそれを捕まえて読む。arguments は外の関数のもの（前から）。arrow は new できない（`VM_CODE_CONSTRUCTOR` なし、
  prototype なし）。
- **template**: 文字列と `to_string` した置換を順に連結。tagged は cooked（無効な escape は undefined）の配列に raw の配列を
  `raw` として付け、置換の値と一緒に tag を呼ぶ（property の tag は this に object）。

## 試験

- `plan/ws074/tests/js/lexical.js`（新）と `lexical.expected`（Chromium 153）: block の let、for の反復ごとの binding と closure、
  block の closure、for-in の const、TDZ（読み・typeof）、const への代入、switch の let、block の関数、label 付きの break・continue、
  arrow（this・arguments・new・式の本体）、template と tagged template。
- `plan/ws074/tests/dom/lexical.html`（新）と `lexical.expected`（Chromium）: script 間で共有する top level の let・const、window の
  property にならないこと、関数からの TDZ、const への代入、二度目の宣言の SyntaxError（その script だけ止まる）、timer の arrow と
  template。

## 確認（host は Debian の cc。guest は QEMU。実機は未実施）

- host の build（plain と ASan、-Werror）warning 0。zedBSD の amd64 の build と image（clang・libcxx を含まない config）: browser の
  warning 0、toolchain への書き込みなし（Permission denied 0）。
- style-check（変えた file）: 新しい指摘 0（js.h・bytecode.h の各 1 件と vm.h の 3 件は前から）。
- test262: **16393 → 17190 / 47792**、ES5 7732 → 7748 / 8087。新しく落ちた試験 0（p077 の failures と比べた）。
  `language/statements/let` 55/145・`const` 45/138・`expressions/arrow-function` 134/343・`template-literal` 58/59・
  `tagged-template` 16/27・`block-scope` 85/145・`statements/for/` 99/385・`for-in` 98/126・`switch` 54/112。残りの多くは
  destructuring・default・rest・generator・async・class が要る試験。ASan の driver でもこれらの範囲は同じ数（crash なし）。
- 回帰（plain と ASan、同じ結果）: golden 76/76、host-view 59/59、host-form 28/28、host-link 22/22、host-position 19/19、
  host-text 20/20、host-relayout 147/147、host-base 2038・heap 31・interp 45・number 71・object 98（全て 0 failed）、
  run-dom-tests **8/8**、run-js-tests **10/10**、run-loader-tests 11/11、run-http-tests 14/14・`--async` 17/17。
- Amazon（script 付きの capture、host）: Uncaught top **10 → 7**、search **15 → 9**。let/const・arrow・template の誤りは 0。
  残り: top は default・rest の引数 2、`value is not a function` 4（`getBoundingClientRect`・`querySelector`、p031）、
  `TextEncoder is not defined` 1（arrow が通って先へ進んだ script）。search は default・rest 4、optional chaining 1、
  `Cannot read properties` 3、`TextEncoder` 1。`live-compare.py` の画素は不変（top 64.47%・ink 57.66%、search 76.04%・ink 33.01%）。
  ASan の `--render` で報告 0。
- guest（QEMU、worktree の image、headless の guest）: run-js-tests `--outputs` 10/10、run-dom-tests `--outputs` 8/8。live の
  `https://www.amazon.co.jp/` の `--run`（取得 1 回）: Uncaught は `value is not a function` 4、`TextEncoder` 1（p077 の後の
  arrow 1 は消えた）。
- `plan/tools/boot-test.sh`（`boot-check.sh p078`）: PASS、login prompt を画面で確認
  （`/home/awe/zedBSD-rpi4/build/ws074-shots/p078-20260929-boot-login.png`）。

## 写真

- host: `/home/awe/zedBSD-rpi4/build/ws074-shots/p078-20260929-amazon-top-local.png`・`…-search-local.png`（見た目は p077 と同じ）。

## 未実施・制限・残り

- 実機は未実施。guest の窓での確認は未実施（headless の guest で確かめた）。
- tagged template の strings の配列は呼ぶたびに新しく作り、freeze しない（仕様は場所ごとに一つで frozen）。
- for の反復ごとの環境の複写は update の前だけ（仕様は最初の test の前にも一度複写する。初期化式の closure だけが違いを見る）。
- block の関数宣言（scope を持つ block の中）は block に入る時に作る。ES5 の巻き上げと違い、block より前には undefined。
  strict mode でも var に入る（仕様では block の外から見えない）。
- `using`（explicit resource management）は let と同じに扱う（試験は除外）。class 宣言は未対応のまま。
- p028 に残るもの: class、destructuring・default・rest の引数、spread、Symbol、iterator・for-of、generator、Map・Set・Weak*。
  Amazon の次の blocker は default・rest の引数（top 2、search 4）と optional chaining（search 1）、`TextEncoder`。
