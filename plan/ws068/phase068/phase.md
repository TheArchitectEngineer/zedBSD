<!-- awesome-plan project=zedbsd record=ws068p068 -->

# ws068-p068: GL 4.6 の残りと 4.6 の名乗り

Phase ID: `ws068-p068`
Parent: [WS068](../ws.md)
Status: planned
Phase disposition: normal
依存: p065、p067
大きさの見積もり: 10 LW
出どころ: p036 l（2026-10-29 に p035・p036 を分けた。2026-10-29 ユーザー「WS068ですが、i915の試験を受け入れ条件から外して、VenusのみでOKとします。また、残りが大きい件は了解しましたので、phaseに分解して、目標を管理しながら、空いた時間に細々と取り組んで、WS完了に近づいていけるように、準備してください。」）

## 目的と範囲

- GL_ARB_indirect_parameters、GL_ARB_pipeline_statistics_query、GL_ARB_polygon_offset_clamp、GL_ARB_shader_atomic_counter_ops、GL_ARB_shader_draw_parameters、GL_ARB_shader_group_vote、GL_ARB_texture_filter_anisotropic、GL_ARB_transform_feedback_overflow_query。4.6 の context と glx-p068.sh（新）。

## 受け入れ

- Venus で glx-p068（新）が PASS。回帰 PASS
- build: amd64・arm64・riscv64 の libGL・libGLESv2・libEGL・glxtest が warning 0。i915 実機は受け入れに含めない（2026-10-29 の決定）。
