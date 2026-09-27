<!-- awesome-plan project=zedbsd record=ws068p029 -->

# ws068-p029: OpenGL ES 3.0 の API（3b）: glBlitFramebuffer、multisample の renderbuffer、3D の slice の取り付け

Phase ID: `ws068-p029`
Parent: [WS068](../ws.md)
Status: cleared（2026-09-27。Venus で egl-p029 PASS、回帰は下の表。Queue はメインが merge の時に記録する）
Phase disposition: normal
承認: 2026-09-27 ユーザーの自走の指示（graphics が最優先）、WS068 の計画（2026-09-27 に p026 から分けた）

## 範囲

- `glBlitFramebuffer`: read framebuffer の read buffer から draw framebuffer の各 draw buffer へ（colour。拡大縮小と NEAREST・LINEAR、
  角を入れ替えた反転）、depth と stencil（NEAREST、同じ format）。窓の framebuffer との間（行が上からなので y を反転）。scissor の
  内側だけ、image の外は切る。
- multisample の renderbuffer: `glRenderbufferStorageMultisample`（GL_MAX_SAMPLES は device の colour・depth・stencil に共通の最大、
  8 まで。samples は device にある数へ切り上げ。整数の format は GL_INVALID_OPERATION）、multisample の FBO への描画（pass の
  attachment と pipeline の rasterizationSamples）、GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE、`glBlitFramebuffer` での resolve（同じ
  大きさと format のみ）、multisample の read framebuffer からの `glReadPixels`・`glCopyTex*` は GL_INVALID_OPERATION、
  GL_SAMPLES・GL_SAMPLE_BUFFERS（draw framebuffer の）・GL_MAX_SAMPLES・GL_RENDERBUFFER_SAMPLES、`glGetInternalformativ`
  （GL_NUM_SAMPLE_COUNTS・GL_SAMPLES）。
- 3D texture の slice の取り付け（`glFramebufferTextureLayer`）。

## 設計

- blit は transfer で行う（`vkCmdBlitImage`、multisample は一度 `vkCmdResolveImage` で 1 sample の image へ resolve してから）。
  libEGL の窓の swapchain（device が許すとき、`zegl_surface.writable`）と pbuffer の image に TRANSFER_DST、窓の depth の image に
  TRANSFER_SRC、renderbuffer に TRANSFER_DST を足した。窓の framebuffer へ blit できない device では GL_INVALID_OPERATION。窓の
  framebuffer から読む blit は read surface が draw surface のときだけ（別の surface の image はその frame のもの）。
- 軸ごとに、draw 側の大きさと scissor の範囲と read 側の大きさの範囲で、両方の矩形を比例して切る（`framebuffer_blit_axis`）。
- multisample の sample 数は互換 pass の format の組（`gles_pass_format.samples`）と target の samples に入れた。
- 3D の slice: libvulkan は Vulkan 1.0 で 3D image の slice を 2D の view にできず（2D_ARRAY_COMPATIBLE が無い）、型の違う image の
  間の copy もできないので、slice の大きさの 2D の image（`gles_slice_image`、FBO の colour attachment ごと）に描き、pass の前に
  slice から、pass の後に slice へ、stream の buffer を通して写す。blit と read は 3D の image を z の offset で直接読み書きする。

## 受け入れ

1. build warning 0（-Werror）、変えた・新しい C の style-check 0、GLSL の host 試験 PASS。
2. egltest の新しい場面 `--scene=blits`（ES 3 の context）が Venus の display と Wayland で PASS（画面の点と readback）: 4x の
   multisample の renderbuffer に描いた三角形の resolve（左下赤・右上青、斜めの縁の画素が混ざる）、四分割の texture の 2 倍と左右の
   反転、別の framebuffer から blit した depth での depth test、3D の slice 1 への clear と読み戻し、窓の framebuffer から texture への
   blit、毎 frame の texture から窓への blit。API の検査（sample 数、sample の count、multisample の read と異なる大きさの resolve と
   LINEAR の depth の誤り）が failures=0。
3. 回帰: egl-p008・egl-p019・egl-p020・egl-p022・egl-p023・egl-p024・egl-p025・egl-p026・egl-p028・x11-p005、boot test。i915 実機は
   未実施でよい。

## 実行（2026-09-27）

### 変えた file

| file | 内容 |
| --- | --- |
| `userland/base/libglesv2/framebuffer.c` | `glBlitFramebuffer`（side の取得、resolve、軸ごとの clip、`vkCmdBlitImage`）、`glRenderbufferStorageMultisample`、`glGetInternalformativ`、`gles_samples_max`・`gles_framebuffer_samples`、sample 数の完全性、3D の slice の 2D の image と copy |
| `userland/base/libglesv2/gles.h`・`gles.c`・`draw.c`・`exports.map` | renderbuffer・pass・target の samples、`gles_slice_image`、read の slice、GL_SAMPLES・GL_SAMPLE_BUFFERS・GL_MAX_SAMPLES、pipeline の rasterizationSamples、新しい entry point |
| `userland/base/libegl/vulkan.c`・`zegl.h` | 窓と pbuffer の image の TRANSFER_DST（`writable`）、窓の depth の TRANSFER_SRC |
| `userland/base/egltest/blits.c`・`.h`（新）・`main.c`・`Makefile` | `--scene=blits` |
| `plan/ws068/tests/egl-p029.sh`（新） | display と Wayland の画面の点、readback、API の検査 |

### 検証（QEMU の Venus。i915 実機は未実施）

| 確認 | 結果 |
| --- | --- |
| build（lean image、`plan/ws068/tests/build-glsl-image.sh build/amd64`） | status 0。libGLESv2・libGL・libEGL・egltest は -Werror で warning 0 |
| style-check（`framebuffer.c`・`draw.c`・`gles.c`・`gles.h`、libEGL の `vulkan.c`・`zegl.h`、egltest の `blits.c`・`.h`・`main.c`） | 0 |
| egl-p029 | **PASS**: API の検査 13 件 failures=0、readback 10 点 failures=0（display と Wayland）、画面の点 11 点が全て期待どおり（画面を目で確かめた: resolve の斜めの縁が階段状に混ざる） |

| 回帰 egl-p008・egl-p019・egl-p020・egl-p022・egl-p023・egl-p024・egl-p025・egl-p028（新しく起こした guest） | 全て PASS |
| 回帰 egl-p026 | 最初は FAIL（p026 の場面が 3D の slice の取り付けに GL_FRAMEBUFFER_UNSUPPORTED を期待していた。p029 で取り付けられるようになったので期待を COMPLETE に直した）。直した後 p027 の回帰の中で PASS |
| 回帰 x11-p005 | PASS（gears.png を目で確かめた） |
| GLSL の host 試験 | PASS |
| boot test（`build/ws068-p029-regress/boot/login.png`） | PASS（PNG を目で確かめた） |
| i915 実機 | 未実施 |

画面: `build/ws068-shots/p029-20260927-blits-display.png`、`build/ws068-shots/p029-20260927-blits-wayland.png`（main の tree）。

### 制限・移管

- multisample の depth・stencil の blit（resolve）は GL_INVALID_OPERATION（`vkCmdResolveImage` は depth を resolve しない）。
- 窓の framebuffer から読む blit は read surface と draw surface が同じときだけ。
- blit の clip の端は整数の texel に丸める（拡大縮小で切られた端の texel が半分ずれることがある）。
