<!-- awesome-plan project=zedbsd record=ws068p058 -->

# ws068-p058: GL 4.2 の atomic counter（GL_ARB_shader_atomic_counters）

Phase ID: `ws068-p058`
Parent: [WS068](../ws.md)
Status: planned
Phase disposition: normal
依存: p057
大きさの見積もり: 6 LW
出どころ: p035 2（2026-10-29 に p035・p036 を分けた。2026-10-29 ユーザー「WS068ですが、i915の試験を受け入れ条件から外して、VenusのみでOKとします。また、残りが大きい件は了解しましたので、phaseに分解して、目標を管理しながら、空いた時間に細々と取り組んで、WS完了に近づいていけるように、準備してください。」）

## 目的と範囲

- atomic_uint（layout(binding, offset)）、atomicCounterIncrement・Decrement・atomicCounter を storage buffer の atomic に、GL_ATOMIC_COUNTER_BUFFER の indexed の binding point、glGetActiveAtomicCounterBufferiv、GL_MAX_*_ATOMIC_COUNTER*、名乗り。

## 受け入れ

- Venus で `sh plan/ws068/tests/venus-all.sh OUTDIR` の glx-p037（glxtest `--gl33`）が PASS し、その中の outcome「atomic-counter」が緑（`GLXTEST GL33 check` の行に FAILED が無い）。回帰（venus-all の他の試験）も PASS（fragment の数を数えて読み戻す）
- build: amd64・arm64・riscv64 の libGL・libGLESv2・libEGL・glxtest が warning 0。i915 実機は受け入れに含めない（2026-10-29 の決定）。
