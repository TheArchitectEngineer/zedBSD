<!-- awesome-plan project=zedbsd record=ws074p025 -->

# ws074-p025: JS の compiler（ES5 の核）、`--js` の shell、test262 の runner

Phase ID: `ws074-p025`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-28）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）

## 範囲（正常系のワンパス、design.md §12.2）

- compiler: AST → 共通の bytecode。scope の解析（var・関数の巻き上げ、closure に捕まる変数は環境の cell へ、それ以外は
  register）、ES5 の核の文と式、strict mode。
- VM の拡張: ES5 の演算子（ToPrimitive を含む）、環境・closure・`this`・`arguments`・`new`、property の定義・削除・`in`・
  `instanceof`、global 変数、for-in の列挙、strict mode の誤り。
- `zdesktop-browser --js [--strict] FILE.js`（`print` を持つ realm で走らせる）。
- test262 の実行の runner（最初の数）と、自前の JS の試験（Chromium の出力を参照にする）。
- 後の Phase の構文（let・const、arrow、class、分割代入、spread、template、generator、async、正規表現、module、`with`、
  直接の `eval`）は「not supported yet」の compile error で断る。

## 受け入れ

1. amd64 の build（warning 0）、style-check 0（engine の変更した file と `host-js.c`）。
2. 自前の JS の試験が Chromium と同じ出力（host plain・ASan、guest）。test262 の数を記録。ASan で crash・報告なし。
3. 前の試験が下がらない。boot test。

## 結果（2026-09-28）

cleared。

- 書いたもの（`userland/base/zdesktop-browser/`）:
  - `js/compile.h`（compiler の内部）、`js/scope.c`（scope の解析: 関数ごとの binding、巻き上げ、captured の印、`arguments`、
    名前付き関数式の自身の名前、catch の引数）、`js/emit.c`（code unit の組み立て: label と jump の patch、例外 handler、
    定数の表（値ごとに 1 つ）、一時 register）、`js/compile.c`（入口 `js_compile`・`js_run_script`、関数の prologue（環境、
    captured の引数、関数宣言）、文: var・if・for・for-in・while・do-while・break・continue（label 付き）・return・throw・switch・
    label・try/catch/finally（完了の code で finally を通す: 正常・throw・return・各 jump））、`js/compile_expr.c`（式: literal、
    object literal（accessor、`__proto__`、computed key、shorthand、method）、array literal（穴）、関数式、単項・二項・論理・
    条件・代入（複合・論理代入）・更新、呼び出し（property の呼び出しは this 付き）、new、property、typeof・delete）、
    `js/script.c`（`print`、例外の文字列）。
  - `vm/`: 命令 47 を追加（算術・bit・関係・等値・`in`・`instanceof`・単項、global の読み書き・宣言・削除、property の定義・
    削除、環境（`new_env`・`get_env`・`put_env`）、`new_closure`・`construct`・`load_this`・`load_callee`、for-in、
    `to_property_key`）。`vm/access.c`（新: property の取得・設定（strict の TypeError）・削除・`in`・`instanceof`・object
    literal の定義・global・for-in の列挙）、`vm/operation.c`（ToPrimitive（valueOf・toString）、ToInt32・ToUint32、
    StringToNumber（空白・Infinity・0x・0o・0b・十進の検査）、`==`、関係の評価順、数値の演算子）、`vm/interpreter.c`（construct
    の frame、arguments object（mapped でない）、環境・scope の命令）、`vm/function.c`（環境の cell、closure と prototype
    object）、`vm/realm.c`（`undefined`・`NaN`・`Infinity`）。
  - `main.c`: `--js`、`--dump=code`（code unit の text）。
- 試験:
  - `plan/ws074/tests/js/*.js`（closures・control・objects・operators・strict、78 行の出力）と `run-js-tests.py`。期待の出力は
    host の Chromium 153 の headless が同じ script を走らせた出力（`--reference`）。host plain・ASan 5/5、guest 5/5（ただし
    operators の 1 行は BUG-078 の間の既知の差: guest の libc の `pow` が `exp(y*log(x))` で `14 ** 2` が
    195.99999999999994。browser では回避しない）。
  - `plan/ws074/tests/host-js.c`（test262 の batch driver: 試験ごとに heap と realm、harness を前に付ける、`print` の出力で
    async の印を見る）と `run-test262.py`（並列の chunk、落ちた chunk は 1 件ずつ再実行、ES5 の範囲（es5id があり features が
    無い）の数も出す）。
- test262（`test262-7ab7fafa`、intl402・staging・未予定の提案を除く）: **4992/47792（10.4%）、ES5 の範囲 1457/8087（18.0%）**
  （`plan/ws074/results/test262.txt`）。全 47792 件が 18 秒（host）。残りの内訳の多い順: 組み込みが無い（`Object is not
  defined` 等の ReferenceError 16079、TypeError 1168 の多く）→ p026、class 6668・let/const 5550・generator 3657・分割代入と
  引数の既定値 2577・arrow 1287・async 895・for-of 610・spread・template → p028・p029、正規表現 893 → p027、BigInt 686、
  module 701、`with` 262 と直接の `eval`（後の Phase）、parse で落とさない早期の誤り（p024 の残り）。
- ASan（UBSan 込み）: test262 の全件と JS の試験で crash・sanitizer の報告なし（runner が落ちた chunk の数を出す: 0）。
- guest（QEMU、plain）: test262 の language の 1500 件（2538 回）が host と全部同じ結果。host-object 98・host-interp 45 も通過。
- 前の試験: host-base 2038・host-heap 31・host-object 98・host-interp 45・host-link 22（plain・ASan）、golden 12/12、
  test262 parse 46876/47792 のまま。
- amd64 の build: warning 0。style-check: `js/*.c`・`vm/*.c`・`main.c`・`host-js.c` の全部 0。boot test: PASS
  （`/home/awe/zedBSD-rpi4/build/ws074-shots/p025-20260928-boot-login.png`）。実機: 未実施。
- commit: `342f4c10`（code・試験）、この後の修正（`var arguments`、computed key の変換の順）と記録の commit。

## 後回し（follow-up）

- `with` と直接の `eval`（環境を動的に引く遅い道、design.md §12.2）: 組み込みの `eval`（p026）の後に別の Phase で。
- sloppy mode の mapped arguments（引数と arguments の要素の連動）、strict の `arguments.callee` の throw する accessor（p026 の
  %ThrowTypeError%）。
- block 内の関数宣言は関数の先頭へ巻き上げる（ES5 の実装の慣行）。ES2015 の block scope は p028。
- inline cache、register の割り当ての最適化、completion value（`eval` の値）。
- 数の文字列は C の `%g` の最短（p026 の Number::toString で直す）。エラーは文字列の throw（p026 の Error の object で直す。
  runner の runtime の負例は文字列の先頭の名前で判定している）。
- BUG-078（libc の `pow` の精度）: libc の担当。直ったら guest の既知の差を外す。
