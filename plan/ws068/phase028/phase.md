<!-- awesome-plan project=zedbsd record=ws068p028 -->

# ws068-p028: OpenGL ES 3.0 の API（2b）: 3D・2D 配列の texture、pixel buffer と pixel store

Phase ID: `ws068-p028`
Parent: [WS068](../ws.md)
Status: cleared（2026-09-27。Venus で egl-p028 PASS、回帰は下の表。Queue はメインが merge の時に記録する）
Phase disposition: normal
承認: 2026-09-27 ユーザーの自走の指示（N=4 のサブエージェントの常時稼働、graphics が最優先）、WS068 の計画（p025 から 3D・配列と pixel buffer を分けた）

## 範囲

- 3D texture と 2D 配列の texture: `glTexImage3D`・`glTexSubImage3D`・`glCopyTexSubImage3D`・`glTexStorage3D`（immutable）、
  `glCompressedTexImage3D`・`glCompressedTexSubImage3D`（圧縮 format は無いので GL_INVALID_ENUM）、`glBindTexture` の
  GL_TEXTURE_3D・GL_TEXTURE_2D_ARRAY、texture の parameter と `glGenerateMipmap`（3D は深さも半分に、配列は layer ごと）。
  level は slice（layer）を順に並べて CPU に持ち、3D は Vulkan の 3D image、配列は layer の 2D image（view は 3D・2D_ARRAY）。
- shader の sampler3D・sampler2DArray・isampler/usampler・sampler2DArrayShadow を unit の 3D・配列の texture に結ぶ（黒い texture も
  形ごと）。GLSL の compiler は p020 で型と built-in を持っていた。
- pixel store（OpenGL ES 3）: UNPACK_ROW_LENGTH・SKIP_ROWS・SKIP_PIXELS・IMAGE_HEIGHT・SKIP_IMAGES、PACK_ROW_LENGTH・SKIP_ROWS・
  SKIP_PIXELS の `glPixelStorei` と `glGetIntegerv`（負の値は GL_INVALID_VALUE）。
- pixel buffer: GL_PIXEL_UNPACK_BUFFER を束ねた時の texture の呼び出し（pointer は buffer の offset）、GL_PIXEL_PACK_BUFFER への
  `glReadPixels`（device の写しを古くする）。map 中・範囲外は GL_INVALID_OPERATION。
- `glGetIntegerv` の GL_TEXTURE_BINDING_3D・GL_TEXTURE_BINDING_2D_ARRAY・GL_MAX_3D_TEXTURE_SIZE（2048 と device の小さい方）・
  GL_MAX_ARRAY_TEXTURE_LAYERS（同じく 2048）。
- 範囲外（p026 へ）: 3D・配列の layer への FBO の描画（`glFramebufferTextureLayer`）、sized の format の FBO と `glReadPixels` の
  他の format・type（GL_IMPLEMENTATION_COLOR_READ_*）。

## 受け入れ

1. build warning 0（-Werror）、変えた・新しい C の style-check 0、GLSL の host 試験 PASS。
2. egltest の新しい場面 `--scene=volumes`（ES 3 の context、12 の四角）が Venus の display と Wayland で PASS（画面の点と readback）:
   3D の slice、3D の slice 間の linear、glTexStorage3D の配列の layer、RGBA8UI の配列（usampler2DArray）、unpack buffer の offset、
   unpack の row length と skip、image height と skip images、glCopyTexSubImage3D、3D と RGBA16F 配列の mipmap（base level 1）、
   textureSize と texelFetch（sampler3D）、sampler2DArrayShadow。API の検査（束縛、上限、immutable、誤りの呼び出し、pack buffer と
   pack の skip）が failures=0。
3. 回帰: egl-p008・egl-p019・egl-p020・egl-p022・egl-p023・egl-p024・egl-p025・x11-p005、boot test。i915 実機は未実施でよい。

## 実行（2026-09-27）

### 変えた file

| file | 内容 |
| --- | --- |
| `userland/desktop/libglesv2/pixels.c`（新） | `gles_unpack`（unpack buffer と pixel store から texel を詰めた行に集める）、`gles_pack_target`（`glReadPixels` の書き先：pack buffer と pack store） |
| `userland/desktop/libglesv2/texture.c` | 3D・配列の texture（level の slice、image と view の形、upload の copy、mipmap の鎖）、`glTexImage3D` ほかの新しい entry point、target を bit で受ける `texture_bound`、unit の 3D・配列、texel の変換を `gles_unpack` 経由に |
| `userland/desktop/libglesv2/gles.h`・`gles.c` | `GLES_SHAPE_*`、level の `depth`、unit の `volume_units`・`array_units`、黒い texture を形ごと、pixel store の状態と `glPixelStorei`・`glGetIntegerv` |
| `userland/desktop/libglesv2/format.c` | `gles_pixel_size`、`gles_texels_halve` に深さ（3D は 8 texel の平均、配列は layer ごと） |
| `userland/desktop/libglesv2/draw.c` | sampler の形（`draw_sampler_shape`）で unit の texture を選ぶ、`glReadPixels` を pack buffer と pack store に |
| `userland/desktop/libglesv2/exports.map`・`Makefile`、`userland/X11/libGL/exports.map`・`Makefile` | 新しい entry point と `pixels.c`（libGL も GL 1.2 の glTexImage3D・glTexSubImage3D・glCopyTexSubImage3D を出す） |
| `userland/desktop/egltest/volumes.c`・`.h`（新）・`main.c`・`Makefile` | `--scene=volumes` |
| `plan/ws068/tests/egl-p028.sh`（新） | display と Wayland の画面の点、readback、API の検査 |

### 検証（QEMU の Venus。i915 実機は未実施）

| 確認 | 結果 |
| --- | --- |
| build（lean image、`plan/ws068/tests/build-glsl-image.sh build/amd64`） | status 0。libGLESv2・libGL・egltest は -Werror で warning 0 |
| style-check（`pixels.c`・`texture.c`・`format.c`・`gles.c`・`draw.c`・`gles.h`・egltest の `volumes.c`・`.h`・`main.c`） | 0 |
| egl-p028（`plan/ws035/tests/zdesktop-guest.sh start build/amd64/hdd-image.img` の後 `plan/ws068/tests/egl-p028.sh build/ws068-p028`、`GUEST_RUNTIME=build/ws068-run`） | **PASS**: API の検査 17 件 failures=0、readback 13 点 failures=0（display と Wayland）、display と Wayland の画面の点 14 点が全て期待どおり（画面を目で確かめた: 12 の四角、shadow は左黒・右白）。最初の実行は guest の起動前で display の段が走らず、起動後の再実行で PASS |
| 回帰 | 下の「回帰」 |
| i915 実機 | 未実施 |

画面: `build/ws068-shots/p028-20260927-volumes-display.png`、`build/ws068-shots/p028-20260927-volumes-wayland.png`（main の tree）。

### 回帰

| 確認 | 結果 |
| --- | --- |
| egl-p008・egl-p019・egl-p020・egl-p022・egl-p023・egl-p024・egl-p025（同じ guest、`GUEST_RUNTIME=build/ws068-run`） | 全て PASS |
| x11-p005（`plan/tools/x11/x11-p005.sh`） | PASS（zgears 300 frame、failures 0。gears.png を目で確かめた） |
| GLSL の host 試験 `sh plan/ws068/tests/glsl-host/run.sh` | PASS（vk-run 19 試験 0 failed） |
| boot test `OUTPUT=build/ws068-p028-boot plan/tools/boot-test.sh build/amd64/hdd-image.img` | PASS（login prompt、PNG を目で確かめた） |

### 制限・移管

- 3D・配列の layer への FBO の描画（`glFramebufferTextureLayer`）は p026（READ/DRAW の framebuffer と共に）。3D・配列の texture は
  colour attachment の view を作らない。
- GL_VERSION は「OpenGL ES 2.0 zedBSD」のまま（p027 で 3.0 に）。
