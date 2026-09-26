<!-- awesome-plan project=zedbsd record=ws068p019 -->

# ws068-p019: GLSL compiler を libGLESv2 へ接続し Venus で確かめる

Phase ID: `ws068-p019`
Parent: [WS068](../ws.md)
Status: planned
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
