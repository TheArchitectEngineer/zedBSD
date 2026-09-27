<!-- awesome-plan project=zedbsd record=ws074p026 -->

# ws074-p026: 組み込み 1a（Object・Function・Error・Boolean・Number・Math・global の関数）

Phase ID: `ws074-p026`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-28）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）

## 分割（2026-09-28）

着手の時に大きすぎたので、Array・String（正規表現の要らない method）・JSON を ws074-p046 へ分けた（ws.md の表と実行の順
p025 → p026 → p046 → p030）。

## 範囲（正常系のワンパス、design.md §12.3）

- Error の類（Error と native error 6 種、`cause`、`toString`、%ThrowTypeError%）。engine の誤りも realm の Error の object に。
- Object（構築子、defineProperty・create・keys・freeze・assign・getOwnPropertyDescriptor(s)・setPrototypeOf 等、prototype の
  hasOwnProperty・isPrototypeOf・propertyIsEnumerable・toString・valueOf・`__proto__`・Annex B の `__defineGetter__` 等）。
- Function（構築子: source text から global に、call・apply・bind・toString、strict の関数の caller・arguments）。
- Boolean・Number（wrapper の object、toString(radix)・toFixed・toExponential・toPrecision、定数、isFinite 等）。
- Math（定数と関数）、global の parseInt・parseFloat・isNaN・isFinite・eval（間接の eval）。
- 数と文字列の変換は自前で厳密に（design.md §12.3 の「最短の往復の十進表記は自前」）。

## 受け入れ

1. amd64 の build（warning 0）、style-check 0。
2. 自前の JS の試験が Chromium と同じ（host plain・ASan、guest）。数の変換の試験（host は glibc と照合）。test262 の数を記録。
3. 前の試験が下がらない。boot test。

## 結果（2026-09-28）

cleared。

- VM（`vm/`）:
  - `number.c`（新）: 大きな整数による厳密な変換。Number::toString の最短の桁（Steele & White / Burger & Dybvig、同点は偶数）、
    toFixed・toExponential・toPrecision（厳密な値を丸め、同点は大きい方）、十進の数字列の読み取り（最も近い double、同点は
    偶数、768 桁を越える分は sticky）、他の基数の整数の読み取り、V8 の方式の他の基数の文字列（整数部の剰余も自前で厳密に）。
    lexer・StringToNumber・parseInt・parseFloat が使う（C の strtod・printf・fmod に頼らない）。
  - realm の intrinsics（Error の各 prototype、Boolean・Number・String・Symbol の prototype、%ThrowTypeError%）、`callee` と
    `new_target`（native が自分と new.target を知る）。native の構築子（`vm_function.construct`）、`vm_construct`、
    `vm_construct_prototype`、`vm_interpret_construct`。関数の `data`（bound 関数の target 等）。
  - object の `kind`（toString の tag と wrapper の判定）と `internal`（wrapper の primitive）。ToObject（`vm_to_object`、String の
    wrapper は文字と length を自分の property に）、primitive の property は prototype から（getter・setter は primitive を this に）、
    sloppy の `this` の wrapper。
  - property の descriptor（`vm_get_own_descriptor`、`vm_define_own_property`: ValidateAndApplyPropertyDescriptor と配列の
    length・index の規則）、`vm_same_value`。
  - engine の誤り（`vm_throw_error`）は realm の Error の object に（組み込みの前は従来どおり文字列）。strict の arguments の
    callee は %ThrowTypeError% の accessor。
- 組み込み（`js/builtin*.c`、新）: `builtin.c`（`js_install_builtins` と共通の helper）、`builtin_error.c`、`builtin_object.c`、
  `builtin_function.c`（bound 関数、Function 構築子、Function.prototype の caller・arguments）、`builtin_number.c`（Boolean・
  Number）、`builtin_math.c`、`builtin_global.c`（parseInt・parseFloat・isNaN・isFinite・eval）。
- compiler: program の completion value（最後の式文の値。eval と Function 構築子が使う）。parser: `"use strict"` の関数の
  引数に eval・arguments・strict の予約語を禁じる（test262 parse の負例が 5 件増えた）。
- 試験:
  - `plan/ws074/tests/js/builtins.js`（38 行）を加えて自前の JS の試験は 6 件、全部 Chromium 153 と同じ出力（host plain・ASan、
    guest。guest の operators の 1 行は BUG-078 の既知の差のまま）。
  - `plan/ws074/tests/host-number.c`: 既知の値 71 件（期待値の基数の文字列は Chromium の出力）と、host では `--oracle` で乱数の
    double 20 万個を glibc の printf・strtod と照合（最短の桁、%.17e の読み戻し、短い形の読み取り、toFixed（厳密な同点は
    言語が上へ、glibc が偶数へ丸めるので除外））: 706748 件 0 失敗（plain・ASan）。guest は既知の値 71 件 0 失敗。
- test262（`test262-7ab7fafa`）: **9327/47792（19.5%）、ES5 の範囲 4822/8087（59.6%）**（p025 は 4992 と 1457）。parse は
  46881/47792。残りの多い順: Array・String・JSON が無い（`Array is not defined` だけで 4595、多くの試験が harness の
  compareArray 等で配列を使う）→ p046、class・let/const・generator・分割代入・arrow → p028・p029、TypedArray・Date・Symbol・
  Promise・Proxy 等 → 後の Phase、正規表現 → p027。
- ASan（UBSan 込み）: test262 の全件・JS の試験・host-number の照合・parse の全件で crash・報告なし。
- guest（QEMU、plain）: test262 の language の 1500 件（2538 回）が host と全部同じ結果（ok 556）。host-object 98・
  host-interp 45・host-number 71 も通過。
- 見つけた libc の不具合（BUG-078 を libm の精度・正しさ全般に広げた、main が記録）: guest で `1e17 % 7` が 0（正しくは 5）、
  `2**60 % 7` が 0（1）、`Math.exp(1)`・`Math.log(10)`・`Math.sin(1)` の末尾の桁が違う。`%` と Math.* は libc のまま（main の
  指示で engine では回避しない）。数の文字列化は自前で厳密なので libm に依らない。
- 前の試験: host-base 2038・host-heap 31・host-object 98・host-interp 45・host-link 22（plain・ASan）、golden 12/12。
- amd64 の build: warning 0。style-check: `js/*.c`・`vm/*.c`・`main.c`・`shell/window.c`・`host-js.c`・`host-number.c` の全部 0。
  boot test: PASS（`/home/awe/zedBSD-rpi4/build/ws074-shots/p026-20260928-boot-login.png`）。実機: 未実施。
- commit: `dfa0cef2`（code・試験）、この後の修正（整数部の剰余の自前化）と記録の commit。

## 後回し（follow-up）

- Array・String・JSON（p046）。Symbol と Symbol.toPrimitive・Symbol.hasInstance・Symbol.toStringTag、Object.fromEntries・
  groupBy（p028）。
- Function.prototype.toString が script の関数の source text を返す（今は全部 native code の形）。
- 直接の eval（呼び出し元の変数を見る）と `with`。strict の eval の var を eval の中に閉じる。
- 他の realm（$262.createRealm 等）、Error.prototype.stack・Error.isError（提案）。
- 配列の length を縮める時に configurable でない要素で止まる規則。
