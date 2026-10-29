<!-- awesome-plan project=zedbsd record=ws068p050 -->

# ws068-p050: GL 4.0 の gpu_shader5（2）: gather の残り・動的な index・invocations・interpolateAt

Phase ID: `ws068-p050`
Parent: [WS068](../ws.md)
Status: planned
Phase disposition: normal
依存: p049、p043
大きさの見積もり: 8 LW
出どころ: p036 e（2026-10-29 に p035・p036 を分けた。2026-10-29 ユーザー「WS068ですが、i915の試験を受け入れ条件から外して、VenusのみでOKとします。また、残りが大きい件は了解しましたので、phaseに分解して、目標を管理しながら、空いた時間に細々と取り組んで、WS完了に近づいていけるように、準備してください。」）

## 目的と範囲

- textureGather の comp（定数の検査）・shadow（refZ）・2DRect、textureGatherOffsets（ConstOffsets）、sampler 配列と UBO 配列の動的に一様な index、geometry shader の `layout(invocations = N)` と gl_InvocationID、interpolateAtCentroid・AtSample・AtOffset、`precise`、暗黙の変換の追加、GL_ARB_gpu_shader5 の名乗り。

## 受け入れ

- Venus で `sh plan/ws068/tests/venus-all.sh OUTDIR` の glx-p037（glxtest `--gl33`）が PASS し、その中の outcome「shader5」が緑（`GLXTEST GL33 check` の行に FAILED が無い）。回帰（venus-all の他の試験）も PASS。glsl-host が PASS
- build: amd64・arm64・riscv64 の libGL・libGLESv2・libEGL・glxtest が warning 0。i915 実機は受け入れに含めない（2026-10-29 の決定）。
