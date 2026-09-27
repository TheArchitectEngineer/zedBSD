<!-- awesome-plan project=zedbsd record=ws068p025 -->

# ws068-p025: OpenGL ES 3.0 の API（2）: sized の format の texture・glTexStorage2D・sampler object・depth texture

Phase ID: `ws068-p025`
Parent: [WS068](../ws.md)
Status: cleared（2026-09-27。Venus で egl-p025 PASS、回帰 PASS。rate limit で止まったサブエージェントの作業（`salvage/ws068` 3f9187b9、親 dc79cb3d）を片付けのサブエージェントが検証して commit した。Queue はメインが merge の時に記録する）
Phase disposition: normal
承認: 2026-09-27 ユーザーの自走の指示、WS068 の計画（ws068-p005「GLES 3.0」を p024〜p027 に分け、2026-09-27 に p025 の 3D・配列と
pixel buffer を p028 に分けた）

## 範囲

- texture の保存を format ごとに: level の texel を texture の内部 format の Vulkan の形（buffer copy の形）で CPU に持つ。OpenGL ES 3.0 の
  sized の内部 format（表 3.13）: R8・RG8・RGB8・RGBA8・SRGB8・SRGB8_ALPHA8・*_SNORM・RGB565・RGBA4・RGB5_A1・RGB10_A2・R16F〜RGBA32F・
  R11F_G11F_B10F・RGB9_E5・8/16/32 bit の UI・I・RGB10_A2UI・DEPTH_COMPONENT16/24/32F・DEPTH24_STENCIL8・DEPTH32F_STENCIL8。3 成分は
  4 成分の Vulkan の format に広げる（sample の対応が必須の format を選ぶ）。unsized（RGBA・RGB・LUMINANCE ほか）は従来の RGBA8。
- 変換: application の format と type（`*_INTEGER`、HALF_FLOAT、FLOAT、2_10_10_10_REV、10F_11F_11F_REV、5_9_9_9_REV、24_8、
  FLOAT_32_UNSIGNED_INT_24_8_REV ほか）から保存の形へ。組み合わせの誤りは GL_INVALID_OPERATION。glTexSubImage2D、glCopyTex*（RGBA8 の
  framebuffer から保存の形へ）、glGenerateMipmap（正規化と float の format。整数と depth は GL_INVALID_OPERATION）。
- glTexStorage2D（immutable、GL_TEXTURE_IMMUTABLE_FORMAT・IMMUTABLE_LEVELS）。
- texture の parameter: WRAP_R、MIN_LOD・MAX_LOD、BASE_LEVEL・MAX_LEVEL、COMPARE_MODE・COMPARE_FUNC、SWIZZLE_R/G/B/A（view の
  component mapping）、float の値の glTexParameterf。
- 完全性: 整数の format と float32 の format（filter できない）は linear の filter で不完全、depth は compare の無い linear で不完全。
  不完全な texture は種類（float・int・uint・depth）に合う黒。
- sampler object: glGenSamplers・glDeleteSamplers・glBindSampler・glIsSampler・glSamplerParameter*・glGetSamplerParameter*。unit に
  束ねた sampler object が texture の sampling の状態に代わる。
- shader の sampler の種類: isampler2D・usampler2D・sampler2DShadow・samplerCubeShadow・isamplerCube・usamplerCube の反射（GLSL と
  SPIR-V）、GL の型。
- 3D・2D 配列の texture と pixel の pack/unpack buffer は ws068-p028、FBO の sized の format と depth texture の取り付けは ws068-p026。

## 受け入れ

1. build warning 0、変えた・新しい C の style-check 0、GLSL の host 試験 PASS。
2. egltest の新しい場面（ES 3 の context）: R8（glTexStorage2D）・RGBA16F・RGBA32F（NEAREST）・RGBA8UI（usampler2D）・R32I（isampler2D）・
   RG8 の swizzle・depth の shadow の比較（sampler object で）・depth の直接の sample・SRGB8_ALPHA8・R11F_G11F_B10F・BASE_LEVEL・
   MIN_LOD と glGenerateMipmap（RGBA16F）・不完全（linear の RGBA32F）が Venus の display と Wayland で PASS（画面の点と readback）、API の
   検査（immutable、sampler object の parameter）。
3. 回帰: egl-p008・egl-p019・egl-p020・egl-p022・egl-p023・egl-p024・x11-p005、boot test。i915 実機は未実施でよい。

## 実行（2026-09-27）

### 変えた file

| file | 内容 |
| --- | --- |
| `userland/base/libglesv2/format.c`（新） | sized の内部 format の表（保存の Vulkan の format、成分、種類）と、application の format・type から保存の形への変換と逆変換（`*_INTEGER`、HALF_FLOAT、FLOAT、2_10_10_10_REV、10F_11F_11F_REV、5_9_9_9_REV、24_8、FLOAT_32_UNSIGNED_INT_24_8_REV ほか） |
| `userland/base/libglesv2/texture.c`・`gles.h`・`gles.c` | level の texel を format ごとの形で持つ、glTexStorage2D（immutable）、ES 3 の texture の parameter、sampler object（glGenSamplers ほか）、完全性と種類に合う黒 |
| `userland/base/libglesv2/draw.c`・`program.c`・`spirv.c`・`framebuffer.c`・`exports.map` | unit の sampler object、shader の sampler の種類（isampler・usampler・shadow）の反射と view、新しい entry point |
| `userland/base/libglesv2/glsl/glsl.h`・`link.c` | uniform の情報に `arrayed` |
| `userland/X11/libGL/Makefile`・`fixed.c` | libGL にも `format.c`、`gles_texture_complete()` の引数の追従 |
| `userland/base/egltest/formats.c`・`.h`（新）・`main.c`・`Makefile` | `--scene=formats`（12 の format の四角と API の検査） |
| `plan/ws068/tests/egl-p025.sh`（新） | display と Wayland の画面の点、readback、API の検査 |

片付けのサブエージェントは style の違反 10 件（閉じ括弧の後の空行 7、文字列の配列の段落の comment 3）を comment と空行で直した（振る舞いは不変）。

### 検証（QEMU の Venus。i915 実機は未実施）

| 確認 | 結果 |
| --- | --- |
| build（lean image、`plan/ws068/tests/build-glsl-image.sh build/amd64`） | status 0。我々の source の warning 0（openssl・openssh の外部の既存の warning だけ） |
| style-check（libGLESv2 の全 `.c`・`gles.h`・`glsl/link.c`・`glsl.h`・egltest の `formats.c`・`.h`・`main.c`・libGL の `fixed.c`） | 0 |
| GLSL の host 試験 `sh plan/ws068/tests/glsl-host/run.sh` | PASS（vk-run 19 試験 0 failed） |
| egl-p025（`plan/ws035/tests/zdesktop-guest.sh start build/amd64/hdd-image.img` の後 `plan/ws068/tests/egl-p025.sh build/ws068-p025`） | **PASS**: API の検査 11 件（immutable の format と levels、immutable への glTexImage の GL_INVALID_OPERATION、swizzle、整数の format への正規化の data と mipmap の誤り、sampler object の parameter と束縛）failures=0。readback 13 点 failures=0。display と Wayland の画面の点が全て期待どおり（画面を目で確かめた: 12 の四角、shadow は左黒・右白、linear の RGBA32F は黒） |
| 回帰 egl-p008・egl-p019・egl-p020・egl-p022・egl-p023・egl-p024 | 全て PASS |
| 回帰 x11-p005（`plan/tools/x11/x11-p005.sh`） | PASS（zgears 300 frame、failures 0。gears.png を目で確かめた） |
| boot test `OUTPUT=build/ws068-p025-boot plan/tools/boot-test.sh build/amd64/hdd-image.img` | PASS（login prompt） |
| i915 実機 | 未実施 |

### 制限・移管

- GL_VERSION は「OpenGL ES 2.0 zedBSD」のまま（必須の機能が揃う p027 で 3.0 に）。
- 3D・2D 配列の texture と pixel の pack/unpack buffer は p028、FBO の sized の format と depth texture の取り付けは p026。
