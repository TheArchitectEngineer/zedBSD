<!-- awesome-plan project=zedbsd record=ws068p051 -->

# ws068-p051: GL 4.0 の transform feedback 2・3

Phase ID: `ws068-p051`
Parent: [WS068](../ws.md)
Status: planned
Phase disposition: normal
依存: p037
大きさの見積もり: 6 LW
出どころ: p036 e（2026-10-29 に p035・p036 を分けた。2026-10-29 ユーザー「WS068ですが、i915の試験を受け入れ条件から外して、VenusのみでOKとします。また、残りが大きい件は了解しましたので、phaseに分解して、目標を管理しながら、空いた時間に細々と取り組んで、WS完了に近づいていけるように、準備してください。」）

## 目的と範囲

- glDrawTransformFeedback・Instanced、transform feedback object の数え（今は ES 3.0 の pause・resume まで）、glBeginQueryIndexed・EndQueryIndexed・glGetQueryIndexediv、GL_MAX_TRANSFORM_FEEDBACK_BUFFERS（4）、gl_SkipComponents・gl_NextBuffer、stream 0 だけ（stream > 0 は vertex shader の storage の書き込みの写し（p030 の方式）で出来る範囲を設計で決める）、GL_ARB_transform_feedback2・3 の名乗り。

## 受け入れ

- Venus で `sh plan/ws068/tests/venus-all.sh OUTDIR` の glx-p037（glxtest `--gl33`）が PASS し、その中の outcome「transform-feedback2」が緑（`GLXTEST GL33 check` の行に FAILED が無い）。回帰（venus-all の他の試験）も PASS。egl-p030 の回帰も PASS
- build: amd64・arm64・riscv64 の libGL・libGLESv2・libEGL・glxtest が warning 0。i915 実機は受け入れに含めない（2026-10-29 の決定）。
