<!-- awesome-plan project=zedbsd record=ws068p013 -->

# ws068-p013: desktop GL 3.0 の context（GLX）

Phase ID: `ws068-p013`
Parent: [WS068](../ws.md)
Status: cleared（2026-09-27。Venus で glx-p013 PASS、回帰 PASS。i915 実機は未実施）
Phase disposition: normal
承認: 2026-09-27 ユーザーの自走の指示（graphics が最優先）、WS068 の計画、2026-09-27 ユーザー「GL 3.0をラップアップさせるのがいいです。」。試験は 2026-09-27 のユーザーの方針（amd64 のみ、phase の最後に）。

## 範囲

- `glXCreateContextAttribsARB`（version 3.0 まで、core・compatibility の profile を受ける）、GLX の extension の文字列に
  GLX_ARB_create_context・GLX_ARB_create_context_profile。GLX の EGL の context は常に client version 3。
- context ごとの GL_VERSION「3.0 …」・GLSL「1.30」、GL_MAJOR_VERSION・GL_MINOR_VERSION・GL_CONTEXT_FLAGS。
- libGL が export する OpenGL ES 3.0 の API（libGLESv2 の全て）と GL 3.0 の関数: glColorMaski、glEnablei・glDisablei・
  glIsEnabledi・glGetBooleani_v、glBindFragDataLocation、glBeginConditionalRender・glEndConditionalRender、glClampColor、
  glTexParameterIiv・Iuiv と getter、glVertexAttribI1i〜3ui と v 形・I4bv・I4sv・I4ubv・I4usv、glFramebufferTexture1D（断る）・
  glFramebufferTexture3D（layer の道）、glGetBufferSubData、glMapBuffer、glGetTexImage、glGetQueryObjectiv、
  GL_SAMPLES_PASSED の query、fixed.c の glDrawBuffer。

## 設計（決めたこと）

- attachment ごとの colour mask と blend: `gles_state` に `indexed_masks[GLES_DRAW_BUFFERS][4]`・`indexed_masked`・
  `blend_buffers`・`blend_indexed`。glColorMask・glEnable/glDisable(GL_BLEND) は全ての buffer を同じに戻す。`draw_raster` と
  glClear・glClearBuffer* は buffer ごとの mask（`draw_channels`）を使い、channel が一つも書かれない attachment は clear しない。
- libGL だけの関数は新しい `userland/retro/libGL/gl3.c`（OpenGL ES 3.0 の API の組み合わせ）。libGLESv2 に入れるのは state を
  触るもの（indexed の 5 つは libGLESv2 からも export、glBindFragDataLocation・conditional render は libGL からだけ）。
- glBindFragDataLocation は link の時に fragment の出力の Location の word を書き換える（`program.c`）。
- conditional render は開始時に query の結果を待って `state->conditional_skip` を決め、draw・glClear・glClearBuffer* が見る
  （`query.c`）。全ての mode で待つ。
- GL_SAMPLES_PASSED（libGL だけ）: occlusion の slot の数の和。device に `occlusionQueryPrecise` があれば EGL の device で
  有効にし PRECISE で数える（`libegl/vulkan.c`）。
- glGetTexImage は level（3D・配列は layer ごと）を一時の framebuffer に付けて glReadPixels で読む（pack の state と pixel pack
  buffer が効く）。描ける colour の format だけ。
- glClampColor は target と値を確かめて何もしない。
- context の version は `glx.c` の `glx_version()`（現在の GLX context の version と flags）を fixed.c の hook が読む。
- `GL/gl.h` は `GLES3/gl3.h` を含み、GL 3.0 の追加の名前と宣言を持つ。`GL/glx.h` に GLX_ARB_create_context の名前と宣言。
- framebuffer object の pass の colour slot i は draw buffer i の名指す attachment（`framebuffer_slot_attachment`）。OpenGL ES では
  従来どおり attachment i か無し（無しの slot は自分の attachment を書かずに持つ）。desktop GL（libGL）の glDrawBuffers は
  どの slot にどの attachment でも受け（各 attachment は一度）、glDrawBuffer(GL_COLOR_ATTACHMENTn) はこれで効く。
  blit の書き先も draw buffer の名指す attachment。GL_MAJOR_VERSION・GL_MINOR_VERSION は libGLESv2（ES）でも答える（3 か 2・0）。

## 判断が要る点（既定を選んで進める）

- 旧来の `glXCreateContext`・`glXCreateNewContext` は 1.4 の fixed function の context のまま（3.0 は
  glXCreateContextAttribsARB だけ）。戻せる既定。
- glXCreateContextAttribsARB は 1.0〜3.0 の要求に 3.0 の compatibility の context を返し（3.0 は以前の版と互換、profile は 3.2 から）、
  3.1 以上は NULL（X の error は送らない）。forward-compatible の flag は GL_CONTEXT_FLAGS に出すだけで、非推奨の機能は消さない。
  戻せる既定。

## 検証（QEMU の Venus。i915 実機は未実施）

image: `plan/ws068/tests/build-glsl-image.sh build/amd64`（外部 package が build/amd64/dynamic に link するため build/amd64 に作る）。

| 確認 | 結果 |
| --- | --- |
| build（lean image） | status 0。warning 0 |
| style-check | 新しい file（`libGL/gl3.c`、`glxtest/gl3.c`・`gl3.h`）0、変えた file（glx.c・fixed.c・main.c・gles.c・draw.c・query.c・program.c・framebuffer.c・vulkan.c）0（前も 0） |
| glx-p013（`plan/ws068/tests/glx-p013.sh`） | **PASS**: 3.3 の拒否、1.4 の context の GL_VERSION「1.4 …」と major 1、forward-compatible の GL_CONTEXT_FLAGS 1、3.0 の context の GL_VERSION「3.0 zedBSD (OpenGL ES 3.0 on Vulkan)」・GLSL「1.30」、API の検査 failures=0（GL_SAMPLES_PASSED は 64/64 で正確）、11 の結果の四角と固定機能の四角が緑。最初の実行は glDrawBuffer(GL_COLOR_ATTACHMENT1) が OpenGL ES の規則で断られて 3 検査が失敗し、framebuffer の slot を draw buffer に従わせて PASS |
| 回帰 x11-p004・x11-p005 | PASS（gears.png を目で確かめた） |
| 回帰 egl-p022・p024・p026・p027・p029・p030 | 全て PASS |
| GLSL の host 試験（`plan/ws068/tests/glsl-host/run.sh`） | PASS（vk-run 19 件） |
| boot test（`build/ws068-p013-regress/boot/login.png`） | PASS |
| i915 実機 | 未実施 |

画面: `build/ws068-shots/p013-20260927-gl3-glx.png`、`build/ws068-shots/p013-20260927-regress-gears.png`（main の tree）。

### 制限・移管

- 1D texture・border colour は無い（glFramebufferTexture1D は外すだけ、GL_TEXTURE_BORDER_COLOR は GL_INVALID_ENUM）。
- glGetTexImage は描ける colour の format だけ（depth・stencil・圧縮・描けない format は GL_INVALID_OPERATION）。
- conditional render は全ての mode で結果を待つ。glBlitFramebuffer は conditional render に従わない。
- glClear・glClearBuffer* は一部の channel だけの colour mask を守らない（全部 off の attachment は clear しない。従来からの制限）。
- forward-compatible の context でも非推奨の機能（固定機能等）は残る。context の共有（shareList）は無い（従来どおり）。
- GL_EXTENSIONS は OpenGL ES の名前のまま（desktop の ARB の名前は出さない）。
- desktop の compatibility の GLSL の built-in の状態（`gl_Vertex`・`gl_ModelViewMatrix`・`ftransform` 等、glsl-design.md §1 で
  p013 に回したもの）は未実装。p014 の分割で扱いを決める。
- `#version 140` 以上の shader も 3.0 の context で受ける（GLSL の版を context の版で絞らない）。
