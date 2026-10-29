<!-- awesome-plan project=zedbsd record=ws068p055 -->

# ws068-p055: GL 4.1 の残りと 4.1 の名乗り

Phase ID: `ws068-p055`
Parent: [WS068](../ws.md)
Status: planned
Phase disposition: normal
依存: p052、p054
大きさの見積もり: 8 LW
出どころ: p036 l（2026-10-29 に p035・p036 を分けた。2026-10-29 ユーザー「WS068ですが、i915の試験を受け入れ条件から外して、VenusのみでOKとします。また、残りが大きい件は了解しましたので、phaseに分解して、目標を管理しながら、空いた時間に細々と取り組んで、WS完了に近づいていけるように、準備してください。」）

## 目的と範囲

- GL_ARB_vertex_attrib_64bit（glVertexAttribL*、fp64 の後）、GL_ARB_viewport_array（device の multiViewport、glViewportIndexed・glScissorIndexed・gl_ViewportIndex）、GL_ARB_ES2_compatibility の確認（glGetShaderPrecisionFormat、RGB565、glClearDepthf）、GL_ARB_get_program_binary（format 0 個で名乗る）、GL_ARB_shader_precision。4.1 の context（GLSL 4.10）と glxtest `--gl41`・glx-p055.sh（新）。

## 受け入れ

- Venus で glx-p055（新）が PASS（4.1 の context と各 extension の outcome）。回帰 PASS
- build: amd64・arm64・riscv64 の libGL・libGLESv2・libEGL・glxtest が warning 0。i915 実機は受け入れに含めない（2026-10-29 の決定）。
