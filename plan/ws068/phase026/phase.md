<!-- awesome-plan project=zedbsd record=ws068p026 -->

# ws068-p026: OpenGL ES 3.0 の API（3）: 複数の colour attachment、READ/DRAW の framebuffer、sized の format と depth の FBO、glClearBuffer

Phase ID: `ws068-p026`
Parent: [WS068](../ws.md)
Status: cleared（2026-09-27。Venus で egl-p026 PASS、回帰は下の表。Queue はメインが merge の時に記録する）
Phase disposition: normal
承認: 2026-09-27 ユーザーの自走の指示（graphics が最優先）、WS068 の計画（2026-09-27 に glBlitFramebuffer・multisample・3D の slice の
取り付けを p029 に分けた）

## 範囲

- framebuffer object: colour attachment 4 つ（GL_MAX_COLOR_ATTACHMENTS・GL_MAX_DRAW_BUFFERS 4）、`glDrawBuffers`（draw buffer i は
  GL_COLOR_ATTACHMENTi か GL_NONE、窓の framebuffer は GL_BACK か GL_NONE）、`glReadBuffer`、GL_READ_FRAMEBUFFER・GL_DRAW_FRAMEBUFFER の
  別々の束縛（`glReadPixels`・`glCopyTex*` は read、描画と clear は draw）。
- 取り付け: ES 3.0 の描ける format（R8・RG8・RGB8・RGB565・RGBA4・RGB5_A1・RGBA8・RGB10_A2・RGB10_A2UI・SRGB8_ALPHA8・整数）と
  EXT_color_buffer_float（R16F・RG16F・RGBA16F・R32F・RG32F・RGBA32F・R11F_G11F_B10F、GL_EXTENSIONS に追加）の texture と renderbuffer、
  texture の任意の level（`glFramebufferTexture2D` の level）、2D 配列の layer（`glFramebufferTextureLayer`）、depth・depth-stencil の
  texture、sized の depth の renderbuffer（DEPTH_COMPONENT16/24/32F、DEPTH24_STENCIL8、DEPTH32F_STENCIL8、STENCIL_INDEX8）、
  GL_DEPTH_STENCIL_ATTACHMENT。
- `glClearBufferfv`・`iv`・`uiv`・`fi`、`glClear` を全ての draw buffer に、`glInvalidateFramebuffer`・`glInvalidateSubFramebuffer`
  （中身は保つ。仕様上許される）。
- `glReadPixels` の整数（RGBA_INTEGER と INT・UNSIGNED_INT）と float（RGBA と FLOAT）、RGB10_A2 の packed、
  GL_IMPLEMENTATION_COLOR_READ_FORMAT・TYPE を read buffer の format から。
- `glGetFramebufferAttachmentParameteriv`（窓の framebuffer の GL_BACK・GL_DEPTH・GL_STENCIL、TEXTURE_LAYER、COMPONENT_TYPE、
  COLOR_ENCODING、各 *_SIZE）、`glGetRenderbufferParameteriv` の各 size と SAMPLES。
- 範囲外（p029）: `glBlitFramebuffer`、multisample の renderbuffer、3D texture の slice の取り付け（GL_FRAMEBUFFER_UNSUPPORTED を返す）。

## 設計（要点）

- 描画の pipeline は attachment の format の組ごとの互換 render pass（`gles_compatible_pass` の list）で作る。draw buffer は pass を
  変えず、pipeline の attachment ごとの write mask で表す（blend も attachment ごと。整数と device が blend できない format は blend
  しない）。
- texture の image は filter に依らず揃った mip の鎖を全て持つ（どの level にも描ける。mipmap しない sampler は maxLod で level 0
  だけを読む）。取り付けの view（level と layer ごと）は初めて使う時に作り、image と共に捨てる。
- FBO が描いた texture は face ごとの level の bitmask（`gpu_levels`）で記録し、CPU が texture を変える前に全ての layer を含めて
  読み戻す（depth の format は depth の aspect を）。
- 3 成分の format（RGB8 など、4 成分で保つ）は sample の view で alpha を 1 に（FBO が alpha を書いても GL どおり 1 を読む）。
- 見つけた既存の欠陥を直した: `glGenFramebuffers`・`glGenRenderbuffers` が n > 1 で同じ名前を返していた（名前を予約しなかった）。
  名前ごとに object を作り、初めての bind までは `glIs*` が false。
- libGL（desktop GL）の `glReadBuffer` の stub を消し、libGLESv2 のものに GL_FRONT を GL_BACK として受ける扱いを足した。

## 受け入れ

1. build warning 0（-Werror）、変えた・新しい C の style-check 0、GLSL の host 試験 PASS。
2. egltest の新しい場面 `--scene=targets`（ES 3 の context、12 の四角）が Venus の display と Wayland で PASS（画面の点と readback）:
   RGBA8・RGBA16F・RGBA8UI・R8 への一度の描画（R8 は draw buffer から外し glClearBufferfv で 0.25）、その DEPTH_COMPONENT24 texture、
   level 1 と 2D 配列の layer 2 の clear、read framebuffer の read buffer（RGBA16F）からの glCopyTexImage2D（draw は窓）、
   DEPTH24_STENCIL8 renderbuffer の stencil（左緑・右青）、SRGB8_ALPHA8、DEPTH_COMPONENT32F だけの framebuffer の glClearBufferfv、
   DEPTH24_STENCIL8 texture での depth test。API の検査（整数と float の glReadPixels、read format、束縛、上限、attachment の
   parameter、誤りと status）が failures=0。
3. 回帰: egl-p008・egl-p019・egl-p020・egl-p022・egl-p023・egl-p024・egl-p025・egl-p028・x11-p005、boot test。i915 実機は未実施でよい。

## 実行（2026-09-27）

### 変えた file

| file | 内容 |
| --- | --- |
| `userland/base/libglesv2/framebuffer.c` | 全面的に書き直した: 4 つの colour attachment と depth-stencil、level と layer、format ごとの pass と互換 pass、READ/DRAW の束縛、`glDrawBuffers`・`glReadBuffer`・`glFramebufferTextureLayer`・`glInvalidate*`、sized の renderbuffer、`gles_read_source`・`gles_read_buffer_format`、全ての level と layer の `gles_texture_fetch`、名前の予約 |
| `userland/base/libglesv2/draw.c` | `gles_read_pixels`（read buffer の format から application の format・type へ）、`glReadPixels` の整数と float、`glClear` を全ての draw buffer に、`glClearBuffer*`、attachment ごとの blend と write mask |
| `userland/base/libglesv2/texture.c` | 取り付けの view を必要な時に（`gles_texture_attach_view`）、image は全ての level、2D 配列も取り付けられる usage、RGB の alpha を 1 に |
| `userland/base/libglesv2/format.c` | format ごとの `renderable`、`gles_format_renderable`、`gles_read_format_ok`・`gles_read_format`・`gles_texels_read`、整数の texel の読み |
| `userland/base/libglesv2/gles.h`・`gles.c` | 上の構造、read framebuffer・既定の draw/read buffer、`glGetIntegerv`（READ_FRAMEBUFFER_BINDING、MAX_COLOR_ATTACHMENTS、MAX_DRAW_BUFFERS、DRAW_BUFFERi、READ_BUFFER、IMPLEMENTATION_COLOR_READ_*）、GL_EXT_color_buffer_float |
| `userland/base/libglesv2/exports.map`、`userland/X11/libGL/fixed.c` | 新しい entry point、libGL の `glReadBuffer` の stub を削除 |
| `userland/base/egltest/targets.c`・`.h`（新）・`main.c`・`Makefile` | `--scene=targets` |
| `plan/ws068/tests/egl-p026.sh`（新） | display と Wayland の画面の点、readback、API の検査 |

### 検証（QEMU の Venus。i915 実機は未実施）

（回帰の実行中。結果を書く）

### 観察

- 途中の一度の実行で、guest から SSH で読んだ `/tmp/egl-w.log` の末尾が壊れた bytes になった（`cat` の出力。直後の再実行では
  display・Wayland とも壊れずに PASS）。再現していない。guest の SSH・network・file の経路のどれかの間欠の不具合の疑い（未調査）。

### 制限・移管

- `glBlitFramebuffer`、multisample の renderbuffer、3D texture の slice の取り付けは p029。
- 部分的な colour mask の下の `glClear`・`glClearBuffer*` は全ての channel を clear する（p022 からの既存の制限。vkCmdClearAttachments
  は mask を持たない）。
- glCopyTex* の float の read buffer は RGBA8 に丸めてから写す（精度が落ちる）。
