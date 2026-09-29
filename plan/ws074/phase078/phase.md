<!-- awesome-plan project=zedbsd record=ws074p078 -->

# ws074-p078: ES2015 の構文 1a（let・const・TDZ、arrow、template）

Phase ID: `ws074-p078`
Parent: [WS074](../ws.md)
Status: in-progress（2026-09-29）
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

## Resume point

- 2026-09-29: 着手。設計は下の「設計」。
