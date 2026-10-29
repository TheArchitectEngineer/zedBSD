<!-- awesome-plan project=zedbsd record=ws068p048 -->

# ws068-p048: GL 4.0 の fp64（2）: GL の API と GL_ARB_gpu_shader_fp64

Phase ID: `ws068-p048`
Parent: [WS068](../ws.md)
Status: planned
Phase disposition: normal
依存: p047
大きさの見積もり: 4 LW
出どころ: p036 b（2026-10-29 に p035・p036 を分けた。2026-10-29 ユーザー「WS068ですが、i915の試験を受け入れ条件から外して、VenusのみでOKとします。また、残りが大きい件は了解しましたので、phaseに分解して、目標を管理しながら、空いた時間に細々と取り組んで、WS完了に近づいていけるように、準備してください。」）

## 目的と範囲

- glUniform1d〜4d(v)・glUniformMatrix*dv・glGetUniformdv、uniform の型の名前（GL_DOUBLE 等）、UBO の double、device の shaderFloat64 の時だけ extension を名乗る（fixed.c の features の欄）。
- glxtest `--gl33` に outcome「fp64」。

## 受け入れ

- Venus で `sh plan/ws068/tests/venus-all.sh OUTDIR` の glx-p037（glxtest `--gl33`）が PASS し、その中の outcome「fp64」が緑（`GLXTEST GL33 check` の行に FAILED が無い）。回帰（venus-all の他の試験）も PASS（device に shaderFloat64 が無ければ skipped で緑）
- build: amd64・arm64・riscv64 の libGL・libGLESv2・libEGL・glxtest が warning 0。i915 実機は受け入れに含めない（2026-10-29 の決定）。
