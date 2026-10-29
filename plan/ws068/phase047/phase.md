<!-- awesome-plan project=zedbsd record=ws068p047 -->

# ws068-p047: GL 4.0 の fp64（1）: GLSL compiler の double

Phase ID: `ws068-p047`
Parent: [WS068](../ws.md)
Status: planned
Phase disposition: normal
依存: p037
大きさの見積もり: 8 LW
出どころ: p036 b（2026-10-29 に p035・p036 を分けた。2026-10-29 ユーザー「WS068ですが、i915の試験を受け入れ条件から外して、VenusのみでOKとします。また、残りが大きい件は了解しましたので、phaseに分解して、目標を管理しながら、空いた時間に細々と取り組んで、WS完了に近づいていけるように、準備してください。」）

## 目的と範囲

- double・dvec2〜4・dmat2〜4（dmatNxM）の型、定数（`lf` の接尾辞）、四則・比較・変換（float・int との間）、double の built-in（abs・floor・sqrt・dot・cross・inverse・packDouble2x32 等、GLSL 4.00 の表のもの）、SPIR-V の Float64 の capability と 64 bit の型、std140 の double の配置。
- host の試験: glsl-host の pass/fail と exec（lavapipe は shaderFloat64 を持つ）。

## 受け入れ

- glsl-host が PASS（double の pass/fail と exec を 1 本以上）。spirv-val が通る
- build: amd64・arm64・riscv64 の libGL・libGLESv2・libEGL・glxtest が warning 0。i915 実機は受け入れに含めない（2026-10-29 の決定）。

## 備考

- i915 の native compiler は対象外（Venus だけ）。
