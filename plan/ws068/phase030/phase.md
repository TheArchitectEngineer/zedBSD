<!-- awesome-plan project=zedbsd record=ws068p030 -->

# ws068-p030: OpenGL ES 3.0 の API（5）: transform feedback と GL_VERSION 3.0

Phase ID: `ws068-p030`
Parent: [WS068](../ws.md)
Status: cleared（2026-09-27。Venus で egl-p030 PASS、回帰は下の表。Queue はメインが merge の時に記録する）
Phase disposition: normal
承認: 2026-09-27 ユーザーの自走の指示（graphics が最優先）、WS068 の計画（2026-09-27 に p027 から分けた）。試験は 2026-09-27 の
ユーザーの方針（amd64 のみ、phase の最後に）に従う。

## 範囲

- transform feedback: `glGenTransformFeedbacks`・`glDeleteTransformFeedbacks`・`glIsTransformFeedback`・`glBindTransformFeedback`
  （object ごとの binding point）・`glBeginTransformFeedback`・`glEndTransformFeedback`・`glPauseTransformFeedback`・
  `glResumeTransformFeedback`・`glTransformFeedbackVaryings`（INTERLEAVED・SEPARATE）・`glGetTransformFeedbackVarying`、
  `glGetProgramiv` の TRANSFORM_FEEDBACK_VARYINGS・BUFFER_MODE・VARYING_MAX_LENGTH、`glGetIntegerv` の TRANSFORM_FEEDBACK_BINDING・
  ACTIVE・PAUSED と上限、GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN の query の数、GL_RASTERIZER_DISCARD（描画と clear）。
  capture 中の glDrawElements・種類の違う mode・TF の binding の変更は GL_INVALID_OPERATION。
- GL_VERSION を「OpenGL ES 3.0 zedBSD」、GL_SHADING_LANGUAGE_VERSION を「OpenGL ES GLSL ES 3.00」に（device が vertex shader からの
  書き込みを持つとき。無ければ 2.0 のまま）。

## 設計

- libvulkan に VK_EXT_transform_feedback が無いので、GLSL の compiler が capture を作る（`glsl_link_captured`）: vertex shader に
  storage buffer（set 0、binding 48、`GLSL_CAPTURE_BINDING`）を足し、main の各 return の前で capture する出力を成分ごとに word
  として書く。record の場所は instance × header の頂点数 + gl_VertexID。
- libGLESv2 は capture する program の draw ごとに stream に capture buffer を作り（header に first + count）、capture 中の draw の
  後、pass の外で GL の順（strip・fan を list にした頂点の順、instance ごと）に transform feedback の buffer の device の写しへ
  `vkCmdCopyBuffer` する（続く頂点は一つの region）。device が書いた buffer は `gpu_written` で、CPU が読む・変える前に
  `gles_buffer_fetch` が frame を待って読み戻す。
- libEGL は device の持つ optional な feature（independentBlend、vertexPipelineStoresAndAtomics、fragmentStoresAndAtomics、
  wideLines、largePoints、geometryShader、tessellationShader、shaderClipDistance、depthClamp、fillModeNonSolid、imageCubeArray、
  sampleRateShading）を有効にする（`zegl_display.features`）。independentBlend が無い device では attachment ごとに違う blend を
  やめる。
- libGLESv2 の SPIR-V の反射は capture の storage buffer を interface に数えない。

## 受け入れ

1. build warning 0（-Werror）、変えた・新しい C の style-check 0、GLSL の host 試験 PASS。
2. egltest の新しい場面 `--scene=feedback`（ES 3 の context）が Venus の display と Wayland で PASS: interleaved の値、strip の順と
   primitives written の query、separate の二つの buffer、instance、pause 中は書かない、capture した位置を頂点配列にした四角、
   GL_VERSION 3.0 の 7 つの四角が緑。API の検査が failures=0。
3. 回帰（phase の最後に）: egl-p008・p019・p020・p022・p023・p024・p025・p026・p027・p028・p029、x11-p005、boot test。i915 実機は
   未実施でよい。

## 実行（2026-09-27）

### 変えた file

| file | 内容 |
| --- | --- |
| `userland/desktop/libglesv2/glsl/glsl.h`・`link.c`・`emit.c`・`emit.h` | `glsl_link_captured`、capture の出力の型と場所（`glsl_capture_info`）、capture の storage buffer と main の return の前の書き込み |
| `userland/desktop/libglesv2/feedback.c`（新） | transform feedback の object と API、draw の確かめ・capture buffer・copy、`gles_buffer_fetch` |
| `userland/desktop/libglesv2/program.c` | `glTransformFeedbackVaryings`・`glGetTransformFeedbackVarying`、link の capture、set layout の storage buffer、`glGetProgramiv` |
| `userland/desktop/libglesv2/draw.c`・`buffer.c`・`query.c`・`spirv.c`・`gles.c`・`gles.h`・`exports.map`・`Makefile`、`userland/X11/libGL/Makefile` | draw の capture、descriptor の storage buffer、rasterizer discard、independentBlend の代わり、buffer の読み戻しと TF の binding の確かめ、stream の STORAGE usage、primitives written、反射、GL_VERSION と glGet |
| `userland/desktop/libegl/vulkan.c`・`zegl.h` | device の optional な feature |
| `userland/desktop/egltest/feedback.c`・`.h`（新）・`main.c`・`Makefile` | `--scene=feedback` |
| `plan/ws068/tests/egl-p030.sh`（新） | display と Wayland の画面の点、readback、API の検査 |

### 検証（QEMU の Venus。i915 実機は未実施）

| 確認 | 結果 |
| --- | --- |
| build（lean image） | status 0。warning 0 |
| style-check（上の C の file） | 0 |
| egl-p030 | **PASS**: API の検査 10 件 failures=0、7 つの四角が display と Wayland で緑、GL_VERSION「OpenGL ES 3.0 zedBSD」。最初の実行は libGLESv2 の SPIR-V の反射が capture の storage buffer を拒んで link に失敗、反射で飛ばすように直して PASS |

| 回帰 egl-p008・p019・p020・p022・p023・p024・p025・p026・p027・p028・p029（新しく起こした guest） | 全て PASS |
| 回帰 x11-p005 | PASS（gears.png を目で確かめた） |
| GLSL の host 試験 | PASS |
| boot test（`build/ws068-p030-regress/boot/login.png`） | PASS |
| i915 実機 | 未実施 |

画面: `build/ws068-shots/p030-20260927-feedback-display.png`、`build/ws068-shots/p030-20260927-feedback-wayland.png`（main の tree）。

### 制限・移管

- capture する program は transform feedback が非 active の draw でも capture buffer に書く（書き先は stream。性能は受け入れではない）。
- CPU で変換する頂点の format（`draw_spread`）は device が書いた buffer の bytes を読み戻さずに使う（fetch していない）。
- capture 中の `glUseProgram`・`glLinkProgram` の GL_INVALID_OPERATION は未実装。
- capture は GLSL ES 3.00・GLSL 1.30 以上の vertex shader だけ（gl_VertexID が要る）。SPIR-V binary の program は capture しない。
