<!-- awesome-plan project=zedbsd record=ws068p019 -->

# ws068-p019: GLSL compiler を libGLESv2 へ接続し Venus で確かめる

Phase ID: `ws068-p019`
Parent: [WS068](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: —（2026-09-27 ユーザーの指示でサブエージェントが実行）
設計: [glsl-design.md](../glsl-design.md) §7・§8

## 範囲

1. program.c: `glCompileShader`（compile と log）、`glLinkProgram`（GLSL の組は `glsl_link` の SPIR-V を既存の後段へ）、uniform の
   GL の型、`GL_SHADER_COMPILER`。libGLESv2 と libGL の Makefile に compiler の source を足す。
2. egltest `--scene=glsl`: p008 の場面を GLSL ES 1.00 の source で描く（glReadPixels の 11 点と画面の 10 点）。
3. 試験 `plan/ws068/tests/egl-p019.sh`（Venus、display 直接と zdesktop の窓）。試験用の小さな image の設定
   `plan/ws068/tests/config-amd64-glsl.mk`。
4. 回帰: `egl-p008.sh`、`plan/ws069/tests/x11-p005.sh`（BUG-057 の frame 数の検査は「turn:」と CHECK の行で判断）、
   `spirv-host/run.sh`、boot test。

## 受け入れ

1. Venus で egl-p019 PASS（GLSL の source の場面が p008 と同じ色）。
2. 回帰が PASS。build warning 0、新しい C の style-check 0、既存の file の style-check が増えない。
3. i915 実機は範囲外（未実施と書く）。

## 結果（2026-09-27）

cleared。受け入れ 1〜3 を満たした。

### 実装

- program.c: `glCompileShader` が `glsl_compile`（log は `glGetShaderInfoLog`）、`glLinkProgram` は両方が GLSL なら `glsl_link` の
  SPIR-V を既存の後段（反射、書き換え、併合、layout）へ。bool と sampler の種類の GL の型を compiler の記述で直す。GLSL と SPIR-V の
  混在は link の誤り。`glShaderBinary` は GLSL の結果を捨てる。gles.c: `GL_SHADER_COMPILER` は `GL_TRUE`。
- Makefile: libGLESv2 と libGL（libGLESv2 の source を含む）に compiler の 13 の source。libGL の既定の版は 110（GL_VERSION は
  1.4 のまま。上げるのは p013）。
- egltest: `--scene=glsl`（`egltest_scene_start_glsl`、p008 の場面を GLSL ES 1.00 の source で。glxtest の使う
  `egltest_scene_start` はそのまま）。
- 試験: `plan/ws068/tests/egl-p019.sh`、`config-amd64-glsl.mk`（zdesktop の image から clang・libc++・noct を外したもの）、
  `build-glsl-image.sh`（既定 `build/ws068-glsl`）。

### 検証（QEMU・Venus。i915 実機は未実施）

- `plan/ws068/tests/egl-p019.sh` PASS（build/ws068-p019.log）: display 直接・zdesktop の窓・docked の画面の 10 点が期待の色、
  egltest の glReadPixels の 11 点も一致（failures=0、glerror=0）、両 shader が GLSL から compile（`EGLTEST SCENE shader 0x8b31/0x8b30
  compiled from GLSL`）。画面: build/ws068-p019/{display,wayland,resized}.png。
- 回帰（同じ image・guest）: `plan/ws068/tests/egl-p008.sh` PASS（build/ws068-p019-egl-p008.log）、`plan/ws069/tests/x11-p005.sh`
  PASS（turn: differs ok、ZGEARS CHECK failures=0、frames=300。build/ws068-p019-x11-p005.log、gears.png）、
  `plan/ws068/tests/spirv-host/run.sh` PASS、`plan/ws068/tests/glsl-host/run.sh` PASS。
- boot test: `plan/tools/boot-test.sh build/ws068-glsl/hdd-image.img` PASS（build/ws068-p019-boot/login.png）。
- build warning 0（cross、-Werror）。新しい C の style-check 0、既存の file（program.c、gles.c、egltest）の style-check 0 のまま。

### 制限（残り）

- `gl_FragCoord` の y は Vulkan の向き（上から）。GL の向きには framebuffer の高さの隠れた uniform が要る（p004 か p013 で）。
- sampler の配列、`gl_DepthRange`、desktop の compatibility の built-in の状態（`gl_Vertex`・`gl_ModelViewMatrix` 等、p013）、
  `continue` を含む `switch`、uniform の初期値（1.20）の API への反映。
- 同じ shader を 2 つの thread で同時に link しない（link が symbol に結果を書く）。
