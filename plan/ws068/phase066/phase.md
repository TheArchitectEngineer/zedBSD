<!-- awesome-plan project=zedbsd record=ws068p066 -->

# ws068-p066: GL 4.6 の SPIR-V の shader（1）: glSpecializeShader と Vulkan 形の module

Phase ID: `ws068-p066`
Parent: [WS068](../ws.md)
Status: planned
Phase disposition: normal
依存: p037
大きさの見積もり: 6 LW
出どころ: p036 k（2026-10-29 に p035・p036 を分けた。2026-10-29 ユーザー「WS068ですが、i915の試験を受け入れ条件から外して、VenusのみでOKとします。また、残りが大きい件は了解しましたので、phaseに分解して、目標を管理しながら、空いた時間に細々と取り組んで、WS完了に近づいていけるように、準備してください。」）

## 目的と範囲

- libGL の glShaderBinary の GL_SHADER_BINARY_FORMAT_SPIR_V_ARB、glSpecializeShader（entry point、特殊化の定数を VkSpecializationInfo に）、COMPILE_STATUS は特殊化の後、GL_SPIR_V_BINARY・GL_SPIR_V_EXTENSIONS の問い合わせ。uniform block に入った（Vulkan 形の）module まで。

## 受け入れ

- Venus で `sh plan/ws068/tests/venus-all.sh OUTDIR` の glx-p037（glxtest `--gl33`）が PASS し、その中の outcome「spirv-shader」が緑（`GLXTEST GL33 check` の行に FAILED が無い）。回帰（venus-all の他の試験）も PASS（glslang の SPIR-V の代わりに自前の compiler の SPIR-V を読ませた描画）
- build: amd64・arm64・riscv64 の libGL・libGLESv2・libEGL・glxtest が warning 0。i915 実機は受け入れに含めない（2026-10-29 の決定）。
