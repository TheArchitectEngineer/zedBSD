<!-- awesome-plan project=zedbsd record=ws068p065 -->

# ws068-p065: GL 4.5 の残りと 4.5 の名乗り

Phase ID: `ws068-p065`
Parent: [WS068](../ws.md)
Status: planned
Phase disposition: normal
依存: p042、p063、p064
大きさの見積もり: 10 LW
出どころ: p036 j・l（2026-10-29 に p035・p036 を分けた。2026-10-29 ユーザー「WS068ですが、i915の試験を受け入れ条件から外して、VenusのみでOKとします。また、残りが大きい件は了解しましたので、phaseに分解して、目標を管理しながら、空いた時間に細々と取り組んで、WS完了に近づいていけるように、準備してください。」）

## 目的と範囲

- DSA の後半（vertex array・query・sampler・renderbuffer・transform feedback）、GL_ARB_conditional_render_inverted、GL_ARB_cull_distance、GL_ARB_derivative_control、GL_ARB_get_texture_sub_image、GL_KHR_robustness、GL_ARB_shader_texture_image_samples、GL_ARB_texture_barrier。4.5 の context と glx-p065.sh（新）。

## 受け入れ

- Venus で glx-p065（新）が PASS。回帰 PASS
- build: amd64・arm64・riscv64 の libGL・libGLESv2・libEGL・glxtest が warning 0。i915 実機は受け入れに含めない（2026-10-29 の決定）。
