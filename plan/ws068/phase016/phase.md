<!-- awesome-plan project=zedbsd record=ws068p016 -->

# ws068-p016: GLSL compiler の前処理・字句・構文

Phase ID: `ws068-p016`
Parent: [WS068](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: —（2026-09-27 ユーザーの指示でサブエージェントが実行）
設計: [glsl-design.md](../glsl-design.md) §2〜§4

## 範囲

1. `userland/desktop/libglesv2/glsl/`: `glsl.h`・`internal.h`・`arena.c`・`lex.c`・`preprocess.c`・`parse.c`。
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

## 結果（2026-09-27）

cleared。受け入れ 1・2 を満たした。

### 実装

- `userland/desktop/libglesv2/glsl/`: `glsl.h`（公開の interface）、`internal.h`、`arena.c`（arena、失敗は longjmp、info log）、`lex.c`、
  `preprocess.c`（directive、関数形式の macro、`##`、`defined`、`#if` の式、`#line`、`#extension`（`GL_OES_standard_derivatives`）、
  `__LINE__`・`__FILE__`・`__VERSION__`・`GL_ES`、expansion は「展開中の macro は再び展開しない」印の付いた frame の stack）、
  `parse.c`（再帰下降と precedence climbing、版ごとの keyword と予約語、struct の型名の scope）、`glsl.c`（`glsl_compile`）。
- 版: `#version 100`（ES）、110・120・130。無い時は呼び出し側の既定（libGLESv2 は 100、libGL は 110）。

### 検証（host）

- `plan/ws068/tests/glsl-host/run.sh` の 1・2: pass/ の 8 shader（egltest の場面、libGL の固定機能の shader の GLSL ES 1.00 版、
  前処理と言語の機能を多く使う language.vert/frag、GLSL 1.30 の modern.vert/frag）がすべて compile、fail/ の 29 の誤りの shader が
  すべて期待の行と文言で失敗（構文、版、予約語、未定義の macro、`#error`、macro の再定義を含む）。ASan・UBSan 付きで警告 0。
- style-check 0（新しい file すべて）。
