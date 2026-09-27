<!-- awesome-plan project=zedbsd record=ws074p023 -->

# ws074-p023: 共通の bytecode と interpreter、呼び出し規約、例外の unwind

Phase ID: `ws074-p023`
Parent: [WS074](../ws.md)
Status: in-progress（2026-09-27）
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
