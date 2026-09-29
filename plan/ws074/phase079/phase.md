<!-- awesome-plan project=zedbsd record=ws074p079 -->

# ws074-p079: ES2015 の構文 1b（destructuring、default・rest の引数、spread、optional chaining）

Phase ID: `ws074-p079`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-29）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行。main の指示「p028 の続き（default・rest の引数、destructuring、spread、
optional chaining と TextEncoder も）」）
依存: p078（let・const）
由来: p078 の後の Amazon の blocker（default・rest の引数 top 2・search 4、optional chaining search 1）。p028 から分けた。

## 範囲

- destructuring: 宣言（var・let・const）、代入、関数の引数、catch の引数、for-in の左辺。object の pattern（計算した key、default、
  rest）、array の pattern（穴、default、rest）。
- 関数の引数: default、rest、pattern（`length` は最初の default・rest の前までの数）。
- spread: 配列の literal、呼び出しと new の引数、object の literal（`{...o}`）。
- optional chaining（`a?.b`・`a?.[k]`・`f?.()`・`a.b?.()`）。

反復は Symbol と iterator の protocol（p028 の残り）がまだ無いので、この Phase では配列・文字列（code point ごと）・arguments
object だけを反復でき、他の値は TypeError（「is not iterable」）。p028 で Symbol.iterator を入れる時に同じ命令を広げる。
TextEncoder は外した: Amazon での使い道は誤りを記録する関数の中だけで（他の誤りが起きた時に呼ばれる）、`encode` は Uint8Array
（typed array、p029）を返すため。

## 設計と実装

- **VM**（`vm/spread.c` 新、`bytecode.h`・`code.c`・`interpreter.c`・`function.c`・`vm.h`、`libbrowser/Makefile`）: 命令 `iter_start`・
  `iter_next`（終わった後は undefined）・`iter_rest`・`array_spread`・`copy_data`（object の spread）・`object_rest`（除く key の配列）・
  `call_array`・`construct_array`（spread のある呼び出し）・`check_coercible`（undefined・null は TypeError）・`args_rest`（arguments
  object の i 番目から）。反復の状態は cell（値を trace）で、配列・arguments object は index と毎回読む長さ、文字列は code point。
  他の値は「value is not iterable」の TypeError（Symbol.iterator は p028 の残り）。own の key の複写は key を heap の配列に移して
  から getter を呼ぶ（collector が見える）。code unit に `length`（`VM_CODE_LENGTH`、default・rest より前の引数の数）。
- **compiler**（`js/compile_expr.c`・`compile.c`・`scope.c`・`compile.h`・`emit.c`）: `js_bind_pattern`（代入・var・let/const の 3 つの
  mode）が object・array の pattern、default、rest を再帰で束ねる。宣言（var・let・const、script の top level の let・const の pattern の
  名前も realm の record へ）、代入式（値は右辺）、catch の引数、for-in の左辺、関数の引数。引数: 名前だけの引数は今までどおり
  register、default・pattern・rest の名前は var で、prologue（`compile_parameters`、hoist の前）が n 番目の引数の register から束ねる。
  rest は arguments object を読む（名前の無い時も register を取り、`VM_CODE_ARGUMENTS`）。optional chaining: chain ごとの脱出の
  label（`fc->chain_label`）へ、`?.` の前の値が undefined・null なら飛び、chain の値は undefined。`(a?.b)()` は this を保つ。

## 試験

- `plan/ws074/tests/js/destructuring.js`（新）と `destructuring.expected`（Chromium 153）: object・array の pattern（default・rest・
  計算した key・穴・文字列の code point）、swap、代入の値、member への代入、spread（配列・呼び出し・new・object・null）、誤り
  （null の分解、iterable でない値）、catch の pattern、for-in の const の pattern、for の let の pattern と closure、default・pattern・rest
  の引数と `length`・arguments、arrow の引数、optional chaining（property・計算・呼び出し・短絡の副作用・括弧）。

## 確認（host は Debian の cc。guest は QEMU。実機は未実施）

- host の build（plain と ASan、-Werror）warning 0。zedBSD の amd64 の build と image（clang・libcxx を含まない config）: browser の
  warning 0、toolchain への書き込みなし。style-check（変えた file）: 指摘 0。
- test262: **17190 → 18558 / 47792**（ES5 7748 のまま）。新しく落ちた試験 0（p078 の failures と比べた）。`dstr`（名前に dstr を含む
  試験）1928/8788、`optional-chaining` 27/38、`array/spread`・`call/spread` 各 24/41、`rest-parameters` 10/11、`expressions/object/`
  553/1170。残りの多くは class・generator・async・for-of・Symbol が要る試験。ASan の driver でも同じ数（crash なし）。
- 回帰（plain と ASan、同じ結果）: golden 76/76、host-view 59/59、host-form 28/28、host-link 22/22、host-position 19/19、
  host-text 20/20、host-relayout 147/147、host-base 2038・heap 31・interp 45・number 71・object 98（全て 0 failed）、
  run-dom-tests 8/8、run-js-tests **11/11**、run-loader-tests 11/11、run-http-tests 14/14・`--async` 17/17。
- Amazon（script 付きの capture、host）: Uncaught の数は top 7・search 9 のまま（default・rest・optional chaining の誤りは 0 になり、
  その先の構文 async function・for-of・class で止まる）。画素は不変（top 64.47%・ink 57.66%、search 76.04%・ink 33.01%）。ASan の
  `--render` で報告 0。
- guest（QEMU、headless の guest）: run-js-tests `--outputs` 11/11、run-dom-tests `--outputs` 8/8。live の amazon.co.jp の `--run`
  （取得 1 回）: Uncaught は `value is not a function` 4（p031）と `TextEncoder` 1。
- `plan/tools/boot-test.sh`（`boot-check.sh p079`）: PASS、login prompt を画面で確認
  （`/home/awe/zedBSD-rpi4/build/ws074-shots/p079-20260929-boot-login.png`）。

## 写真

- host: `/home/awe/zedBSD-rpi4/build/ws074-shots/p079-20260929-amazon-top-local.png`・`…-search-local.png`（見た目は p078 と同じ）。

## 未実施・制限・残り

- 実機は未実施。guest の窓での確認は未実施。
- 反復は配列・arguments・文字列だけ（Symbol.iterator、Map・Set・iterator の object は p028 の残り）。途中で止めた反復の
  IteratorClose も無い（user の iterator が無いので観測できない）。
- 引数の default は引数の scope を別に作らない（default の中の closure が本体の var を見る、default が後の引数を読むと TDZ でなく
  undefined）。重複した引数名の早期の誤りは parser に任せた。
- 分解の代入の評価の順が仕様と一部違う（`keyed-destructuring-property-reference-target-evaluation-order`: target の参照を値の取得の
  後に評価する）。
- TextEncoder は外した（範囲の節）。
- Amazon の次の blocker: async function（top 1、search 2）、for-of（top 1、search 1）、class（search 2）、`getBoundingClientRect`・
  `querySelector`（p031）。search の `Cannot read properties of undefined or null`（at 299:16 と 1:1）は未調査。
