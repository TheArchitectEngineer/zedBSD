<!-- awesome-plan project=zedbsd record=ws074p079 -->

# ws074-p079: ES2015 の構文 1b（destructuring、default・rest の引数、spread、optional chaining）

Phase ID: `ws074-p079`
Parent: [WS074](../ws.md)
Status: in-progress（2026-09-29）
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

## Resume point

- 2026-09-29: 着手。
