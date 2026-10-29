<!-- awesome-plan project=zedbsd record=ws068p059 -->

# ws068-p059: GL 4.2 の残りと 4.2 の名乗り

Phase ID: `ws068-p059`
Parent: [WS068](../ws.md)
Status: planned
Phase disposition: normal
依存: p055、p057、p058
大きさの見積もり: 10 LW
出どころ: p036 l（2026-10-29 に p035・p036 を分けた。2026-10-29 ユーザー「WS068ですが、i915の試験を受け入れ条件から外して、VenusのみでOKとします。また、残りが大きい件は了解しましたので、phaseに分解して、目標を管理しながら、空いた時間に細々と取り組んで、WS完了に近づいていけるように、準備してください。」）

## 目的と範囲

- GL_ARB_base_instance（Vulkan の firstInstance）、GL_ARB_shading_language_420pack（layout(binding)、初期化子の list、.length() 等）、GL_ARB_transform_feedback_instanced、GL_ARB_conservative_depth、GL_ARB_internalformat_query、GL_ARB_map_buffer_alignment、GL_ARB_compressed_texture_pixel_storage、GL_ARB_texture_storage の確認。4.2 の context（GLSL 4.20）と glxtest `--gl42`・glx-p059.sh（新）。

## 受け入れ

- Venus で glx-p059（新）が PASS。回帰 PASS
- build: amd64・arm64・riscv64 の libGL・libGLESv2・libEGL・glxtest が warning 0。i915 実機は受け入れに含めない（2026-10-29 の決定）。
