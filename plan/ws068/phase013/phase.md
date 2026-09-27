<!-- awesome-plan project=zedbsd record=ws068p013 -->

# ws068-p013: desktop GL 3.0 の context（GLX）

Phase ID: `ws068-p013`
Parent: [WS068](../ws.md)
Status: in-progress（2026-09-27 に着手し、使用量の上限でメインの指示で中断。ソースは未変更、途中の差分は `wip.patch`）
Phase disposition: normal
承認: 2026-09-27 ユーザーの自走の指示（graphics が最優先）、WS068 の計画。試験は 2026-09-27 のユーザーの方針（amd64 のみ、phase の最後に）。

## 範囲

- `glXCreateContextAttribsARB`（version 3.0 まで、core・compatibility の profile を受ける）、GLX の extension の文字列に
  GLX_ARB_create_context・GLX_ARB_create_context_profile。EGL の context は常に client version 3。
- context ごとの GL_VERSION「3.0 …」・GLSL「1.30」（fixed.c の `fixed_string` を GLX の current version で分ける）。
- libGL が export する GL 3.0 の関数: glColorMaski、glEnablei・glDisablei・glIsEnabledi・glGetBooleani_v、glBindFragDataLocation、
  glBeginConditionalRender・glEndConditionalRender、glClampColor、glTexParameterIiv・Iuiv と getter、glVertexAttribI1i〜3ui と v 形・
  I4bv・I4sv・I4ubv・I4usv、glFramebufferTexture1D（断る）・glFramebufferTexture3D（layer の道）、glGetBufferSubData、glMapBuffer、
  glGetTexImage、fixed.c の glDrawBuffer の対応。

## 設計（決めたこと）

- attachment ごとの colour mask と blend: `gles_state` に `indexed_masks[GLES_DRAW_BUFFERS][4]`・`indexed_masked`・
  `blend_buffers`・`blend_indexed`。glColorMask・glEnable/glDisable(GL_BLEND) は全ての buffer を同じに戻す。`draw_raster` は
  indexed のとき buffer ごとの mask と blend の bit を使う。
- libGL だけの関数は新しい `userland/X11/libGL/gl3.c`（libGL の Makefile に足す）。libGLESv2 に入れるのは state を触るもの。
- glBindFragDataLocation は link の時に fragment の出力の Location の word を書き換える（`program.c`）。
- conditional render は `state->conditional_skip` を draw・clear・blit が見る（`query.c`）。
- glClampColor は何もしない。

## 途中の状態（2026-09-27）

`wip.patch`（`git apply plan/ws068/phase013/wip.patch` で戻す。compile は未実施）:

- `userland/X11/libGL/exports.map`: `scratchpad/libglmap.py` で libGLESv2 の全 gl* と上の GL 3.0 の名前を足した（396 名）。
  **上の関数を実装するまで libGL は link できない**ので、この map だけを先に commit しない。
- `userland/base/libglesv2/gles.h`・`gles.c`・`draw.c`: indexed の colour mask と blend（glColorMaski・glEnablei・glDisablei・
  glIsEnabledi・glGetBooleani_v、`gles_blend_buffer`、`draw_raster`）。libGLESv2 の exports.map には未追加。

## 再開の手順

1. `git apply plan/ws068/phase013/wip.patch`、libGLESv2 の exports.map に indexed の 5 つを足して build、style-check。
2. glBindFragDataLocation（program.c）、conditional render（query.c）、glTexParameterI*（texture.c）。
3. `userland/X11/libGL/gl3.c`（glVertexAttribI* の包み、glFramebufferTexture1D/3D、glGetBufferSubData、glMapBuffer、
   glGetTexImage、glClampColor）と Makefile。fixed.c の glDrawBuffer。
4. glx.c: glXCreateContextAttribsARB、context の version、extension の文字列。fixed.c の version の文字列を context ごとに。
5. libGL.so を build して export map が全て解決することを確かめる。
6. glxtest の `--gl3` の場面と `plan/ws068/tests/glx-p013.sh`（x11-p004 の形）。
7. phase の最後に amd64 Venus で glx-p013、x11-p004・x11-p005、egl-p026・p030 など関係する回帰、boot test。

## 判断が要る点（既定を選んで進める）

- 旧来の `glXCreateContext` は 1.4 の fixed function の context のまま（3.0 は glXCreateContextAttribsARB だけ）。戻せる既定。

## 検証

未実施（build も試験もまだ）。
