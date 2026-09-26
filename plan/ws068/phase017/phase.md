<!-- awesome-plan project=zedbsd record=ws068p017 -->

# ws068-p017: GLSL compiler の型・意味解析・定数

Phase ID: `ws068-p017`
Parent: [WS068](../ws.md)
Status: planned
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
