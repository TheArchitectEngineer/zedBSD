<!-- awesome-plan project=zedbsd record=ws074p023 -->

# ws074-p023: 共通の bytecode と interpreter、呼び出し規約、例外の unwind

Phase ID: `ws074-p023`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）

## 範囲（正常系のワンパス、design.md §11.5・§11.6）

- bytecode: register machine。1 命令 = opcode の 32 bit の word + opcode の表が決める数の 32 bit の operand（register・定数の番号・
  即値・相対の jump・数）。code の単位（`vm_code` の cell: word の列、定数の表、register の数、引数の数、例外の handler の表、名前）は
  作る時に検査する（operand が範囲の中、jump が命令の頭に落ちる、最後が抜けない）。interpreter は検査済みの code だけを走らせる。
- 命令: 共通（nop・mov・load_const・load_int・jump・jump_if_true/false・call・return・throw・loop_hint）、JS の動的な型の最小
  （add・sub・mul・less・strict_eq・new_object・new_array・get/put_prop・get/put_elem・get/put_global）、Wasm の静的な型の最小
  （i32・i64・f64 の const・add・sub・mul・i32 の lt_s・eqz・br_if、register に生の 64 bit、JS の値へ box する命令）。
- 呼び出し規約: VM の stack（realm ごと、64 bit の slot の配列）に frame の header（呼び出し元・戻りの pc・関数・結果の register・
  this・引数の数）と register。bytecode → bytecode の呼び出しは interpreter の loop の中で frame を積む（C の再帰なし）。native の
  関数はそのまま呼ぶ。C（`vm_call`）から bytecode の関数を呼ぶと interpreter に入り直す（深さの上限で RangeError に相当する例外）。
  VM の stack は保守的に走査する（Wasm の生の値が混ざる）。
- 例外: 関数ごとの handler の表（pc の範囲 → handler の pc と例外を受ける register）で unwind し、呼び出し元の frame へ遡る。
  入口の frame まで無ければ `VM_THROWN`。
- JS の意味の最小: ToBoolean、ToNumber・ToString（数・boolean・null・undefined・string。double の文字列は p026 の最短の十進表記まで
  仮）、accessor の getter・setter の呼び出し（C から入り直す）。TypeError は Error の object が来る p026 まで文字列で throw する。
- 試験: 手で組んだ JS 型と Wasm 型の code（再帰の fib、loop、例外の catch と rethrow、native と bytecode の相互の呼び出し、accessor、
  深い再帰の上限、不正な code の拒否）。

## 受け入れ

1. amd64 の build（warning 0）、style-check 0。
2. host `host-interp` の plain・ASan、guest で同じ結果。前の試験（golden・host-object 等）が下がらない。
3. boot test。

## 結果（2026-09-27）

cleared。

- 書いたもの:
  - `vm/bytecode.h`: 37 の opcode（共通 11、JS 13、Wasm 13）、operand の種類、`vm_code`（word・定数・handler・register の数・
    引数の数・名前）、`vm_handler`。
  - `vm/code.c`: opcode の表（名前と operand）、`vm_code_create`（検査: 知らない opcode、命令が code の外へはみ出す、register・
    定数の番号、jump と handler が命令の頭、call の引数の列が frame の中、最後の命令が抜けない（return・throw・jump だけが最後に
    なれる））、`vm_code_dump`（試験と debug 用の text）。
  - `vm/interpreter.c`: realm の VM の stack の frame（header 6 slot + register）、1 つの loop で bytecode の呼び出し（frame を積む）と
    return（下ろす）、native はその場で C の呼び出し、`vm_interpret`（C から入る。入る回数の上限 256）、例外の unwind（投げた命令、
    呼び出し元では call の命令の位置で handler を探す。入口の frame まで無ければ `VM_THROWN`）、それ以外の失敗は run の frame を
    捨てて返す。Wasm の命令は register の生の 64 bit（i32 は下位 32 bit）。
  - `vm/operation.c`: ToBoolean・ToNumber（数の文字列）・ToString（数は int32 の十進、double は読み戻せる最短の `%g`（p026 で
    Number::toString に））・ToPropertyKey、`===`、`+`（int32 の速い道、どちらかが文字列なら連結）、`<`（文字列は code unit）、
    `vm_get`・`vm_put`（object の chain、accessor の getter・setter の呼び出し、文字列の length と文字、undefined・null は
    TypeError）、TypeError・RangeError（Error の object が来る p026 まで文字列で throw）。
  - `vm/function.c`: `vm_function_create`（bytecode の関数、name・length）、`vm_call` は bytecode なら interpreter へ、関数の trace は
    code も印を付ける。`vm/realm.c`: VM の stack（256K slot = 2 MiB）と、使っている部分の全 word の保守的な印付け。
- 試験 `plan/ws074/tests/host-interp.c`（手で組む assembler で）45 検査: 再帰の fib(20) = 6765（global から自分を呼ぶ）、loop の
  sum(1000)・sum(100000) は int32 を越える、例外（catch して 43、catch の中の rethrow を外で 101、捕まえない throw は run を出る、
  文字列の throw）、native と bytecode（`twice(apply(fib, 10))` = 110、数を呼ぶと TypeError）、bytecode の getter と setter（get_prop・
  put_prop から）、undefined.x は throw、文字列（"n=" + 42・0.1・true、"abc"[1]、配列の a[4] で length 5）、終わらない再帰は
  RangeError で stack は空に戻る、検査の拒否 7 件、Wasm（i32 の 10! と 13! の wrap、i64 の桁上がり、f64 の 1.5×2+0.25、box）、
  dump、20 万個の object を作る run の途中の GC（最初の 2000 個は値ごと生き残る）、全部の後に stack が空で深さ 0。
  - host plain・ASan（UBSan 込み）45/45（UBSan が `memcpy(NULL, 0)` を見つけたので直した）、guest（QEMU、plain）45/45。
  - 前の試験: host-object 98/98（plain・ASan・guest）、golden 12/12。
  - amd64 の build: warning 0。boot test: PASS（`/home/awe/zedBSD-rpi4/build/ws074-shots/p023-20260927-boot-login.png`）。実機: 未実施。
- commit: `c9246692`（code・試験）と、この記録の commit。

## 後回し（follow-up）

- inline cache（get_prop・put_prop の命令に IC の番号の operand を足す）、`this` を読む命令、closure と環境（p025 の compiler と一緒に）。
- accessor の呼び出しは C から入り直す（getter が bytecode でも C の再帰）。frame を積む形は後。
- Wasm の命令の残り（load・store・memory・table・call_indirect・br_table・変換・trap）は p033。loop_hint の回数（JIT の入口）。
- ToPrimitive（object の valueOf・toString）、Number::toString、Error の object（p026）。
