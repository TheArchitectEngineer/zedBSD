<!-- awesome-plan project=zedbsd record=ws068p016 -->

# ws068-p016: GLSL compiler の前処理・字句・構文

Phase ID: `ws068-p016`
Parent: [WS068](../ws.md)
Status: planned
Phase disposition: normal
Queue: —（2026-09-27 ユーザーの指示でサブエージェントが実行）
設計: [glsl-design.md](../glsl-design.md) §2〜§4

## 範囲

1. `userland/base/libglesv2/glsl/`: `glsl.h`・`internal.h`・`arena.c`・`lex.c`・`preprocess.c`・`parse.c`。
2. 前処理: `#version`（100、110、120、130。`es` の profile）、`#define`（object と関数形式、`##`）、`#undef`、`#if`・`#ifdef`・`#ifndef`・
   `#elif`・`#else`・`#endif`、`defined`、`#error`、`#pragma`、`#extension`、`#line`、`__LINE__`・`__FILE__`・`__VERSION__`・`GL_ES`・
   `GL_FRAGMENT_PRECISION_HIGH`。
3. 字句: 識別子、10・8・16 進の整数（1.30 は `u`）、浮動小数（指数、1.20 以上は `f`）、区切り、comment。
4. 構文: 宣言（修飾子、精度、`invariant`、struct、配列、初期値）、関数の宣言と定義、文（block・if・for・while・do・switch・
   return・break・continue・discard）、式（優先順位のとおり、代入、`?:`、`,`、呼び出しと constructor、成分・swizzle・添字、
   前置・後置の増減）。版ごとの keyword と予約語。
5. host の試験 `plan/ws068/tests/glsl-host/`: 正の shader（egltest・libGL の固定機能の shader を GLSL ES 1.00・1.30 に書いたもの、
   前処理を多く使うもの）が構文を通り、誤りの shader が行つきの log を返す。

## 受け入れ

1. host の試験が PASS（正の shader がすべて通り、負の例がすべて期待の行と文言で失敗する）。
2. 新しい C の style-check 0、host の `cc -std=c11 -Wall -Wextra -Werror` で warning 0。
