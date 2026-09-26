<!-- awesome-plan project=zedbsd record=ws068p017 -->

# ws068-p017: GLSL compiler の型・意味解析・定数

Phase ID: `ws068-p017`
Parent: [WS068](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: —（2026-09-27 ユーザーの指示でサブエージェントが実行）
設計: [glsl-design.md](../glsl-design.md) §4・§6

## 範囲

1. `types.c`・`builtins.c`・`check.c`・`fold.c`: scope と symbol、型（scalar・vector・matrix（1.20 の非正方も）・sampler・struct・配列）、
   暗黙の変換（desktop 1.20 以上）、演算子の型、lvalue、constructor、overload の解決、built-in の関数（角度・三角・指数・共通・
   幾何・matrix・vector の関係・texture（ES 1.00 と 1.10〜1.30 の形）・微分・noise）、built-in の変数と定数、精度の既定、
   定数式の畳み込み、`main` から届く global の印。
2. host の試験: 正と負の例（未宣言、型の不一致、書けない lvalue、版の差、ES の精度の要求、overload の失敗、定数でない配列の長さ）。

## 受け入れ

1. host の試験が PASS。p016 の正の shader がすべて検査も通る。
2. 新しい C の style-check 0、warning 0。

## 結果（2026-09-27）

cleared。受け入れ 1・2 を満たした。

### 実装

- `types.c`（scalar・vector・matrix（非正方も）・sampler（float・int・uint、1D・2D・3D・cube、shadow）の静的な表、配列、std140）、
  `builtins.c`（built-in の関数 150 余りの署名の表と照合、built-in の変数と定数）、`check.c`（scope、宣言と修飾子の規則、
  暗黙の変換（desktop 1.20 以上）、演算子、lvalue、constructor、overload、switch の label、ES の float の精度の要求、`main` から
  届く global の印、static な再帰と未定義の関数の検出）、`fold.c`（定数式: 算術、比較、constructor、swizzle、添字、built-in の一部）。

### 検証（host）

- glsl-host の 1・2（上の p016 と同じ試験: 正の shader は検査も通り、負の例は未宣言、型の不一致、書けない lvalue、版の差、精度、
  overload の失敗、定数でない配列の長さ、再帰、ES の `%`・bit 演算、`discard` の stage、flat でない整数の入力、等で期待どおり失敗）。
- style-check 0。
