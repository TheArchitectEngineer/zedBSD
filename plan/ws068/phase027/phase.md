<!-- awesome-plan project=zedbsd record=ws068p027 -->

# ws068-p027: OpenGL ES 3.0 の API（4）: occlusion query、fence sync、残りの entry point

Phase ID: `ws068-p027`
Parent: [WS068](../ws.md)
Status: cleared（2026-09-27。Venus で egl-p027 PASS、回帰は下の表。Queue はメインが merge の時に記録する）
Phase disposition: normal
承認: 2026-09-27 ユーザーの自走の指示（graphics が最優先）、WS068 の計画（2026-09-27 に transform feedback と GL_VERSION を p030 に
分けた）。試験は 2026-09-27 のユーザーの方針（amd64 のみ、phase の最後に）に従う。

## 範囲

- query object: `glGenQueries`・`glDeleteQueries`・`glIsQuery`・`glBeginQuery`・`glEndQuery`・`glGetQueryiv`（GL_CURRENT_QUERY）・
  `glGetQueryObjectuiv`（GL_QUERY_RESULT・GL_QUERY_RESULT_AVAILABLE）。target は GL_ANY_SAMPLES_PASSED・
  GL_ANY_SAMPLES_PASSED_CONSERVATIVE（occlusion、同時に一つ）と GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN（数は p030 で数える。今は 0）。
- fence sync: `glFenceSync`・`glClientWaitSync`・`glWaitSync`・`glGetSynciv`・`glDeleteSync`・`glIsSync`。
- 残りの ES 3.0 の entry point: `glGetFragDataLocation`、`glProgramParameteri`（hint は受けて何もしない）、`glGetProgramBinary`・
  `glProgramBinary`（GL_NUM_PROGRAM_BINARY_FORMATS 0 で拒否）。transform feedback の entry point は p030。

## 設計

- occlusion query は render pass をまたいで active でいられるが、Vulkan の query は一つの pass の中にしか無い。active な間の一つの
  pass の中の一続きの draw を segment とし、context の query pool（1024 slot）の slot を一つずつ使う。segment は pass の最初の draw の
  前に始め（`gles_queries_draw`）、pass を終える全ての所（`gles_target_close`、窓の pass を抜ける `framebuffer_leave`、frame を閉じる
  hook）で先に終える（`gles_queries_suspend`）。slot は取る時に upload の queue で reset する。結果はどれかの segment に通った
  sample があるか。
- 結果と fence sync の待ちは、記録中の frame に及ぶなら、その frame をそこまで submit して待つ（`zegl_frame_flush`。
  glReadPixels と同じ）。fence は作った時の frame で、frame が進めば signalled。`glWaitSync` は一つの queue で順に走るので何もしない。

## 受け入れ

1. build warning 0（-Werror）、変えた・新しい C の style-check 0。
2. egltest の新しい場面 `--scene=queries`（ES 3 の context）が Venus の display と Wayland で PASS: 奥の四角（false）、手前の四角
   （true）、FBO の pass と窓の pass をまたぐ conservative の query（true）、draw の無い query（false）、fence の待ち（signalled）の
   5 つの四角が緑。API の検査（current query、available、誤りの query、sync の property、glGetFragDataLocation、program binary の
   拒否）が failures=0。
3. 回帰（phase の最後に）: egl-p026（p029 で直した 3D の slice の期待を含む）・egl-p029・egl-p008・egl-p022・egl-p024、boot test。
   i915 実機は未実施でよい。

## 実行（2026-09-27）

### 変えた file

| file | 内容 |
| --- | --- |
| `userland/base/libglesv2/query.c`（新） | query object、segment、query pool、fence sync |
| `userland/base/libglesv2/program.c` | fragment の出力の名前と location、`glGetFragDataLocation`・`glProgramParameteri`・`glGetProgramBinary`・`glProgramBinary` |
| `userland/base/libglesv2/framebuffer.c`・`draw.c`・`gles.c`・`gles.h`・`exports.map`・`Makefile`、`userland/X11/libGL/Makefile` | pass を終える所の segment の終わり、draw の前の segment の始まり、GL_NUM_PROGRAM_BINARY_FORMATS、新しい entry point、`query.c` |
| `userland/base/egltest/queries.c`・`.h`（新）・`main.c`・`Makefile` | `--scene=queries` |
| `plan/ws068/tests/egl-p027.sh`（新） | display と Wayland の画面の点、readback、API の検査 |

### 検証（QEMU の Venus。i915 実機は未実施）

| 確認 | 結果 |
| --- | --- |
| build（lean image） | status 0。warning 0 |
| style-check（`query.c`・`program.c`・`framebuffer.c`・`draw.c`・`gles.c`・`gles.h`、egltest の `queries.c`・`.h`・`main.c`） | 0 |
| egl-p027 | **PASS**: API の検査 18 件 failures=0、5 つの四角が display と Wayland で緑 |

| 回帰 egl-p026・egl-p029・egl-p008・egl-p022・egl-p024（新しく起こした guest） | 全て PASS |
| 回帰 x11-p005 | PASS（gears.png を目で確かめた） |
| GLSL の host 試験 | PASS |
| boot test（`build/ws068-p027-regress/boot/login.png`） | PASS（PNG を目で確かめた） |
| i915 実機 | 未実施 |

画面: `build/ws068-shots/p027-20260927-queries-display.png`、`build/ws068-shots/p027-20260927-queries-wayland.png`（main の tree）。

### 制限・移管

- GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN の数と transform feedback は p030。
- 結果を読む・fence を待つと記録中の frame をそこまで submit して待つ（性能は受け入れではない）。
