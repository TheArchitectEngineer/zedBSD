<!-- awesome-plan project=zedbsd record=ws068p063 -->

# ws068-p063: GL 4.4 の残りと 4.4 の名乗り

Phase ID: `ws068-p063`
Parent: [WS068](../ws.md)
Status: planned
Phase disposition: normal
依存: p062
大きさの見積もり: 10 LW
出どころ: p036 l（2026-10-29 に p035・p036 を分けた。2026-10-29 ユーザー「WS068ですが、i915の試験を受け入れ条件から外して、VenusのみでOKとします。また、残りが大きい件は了解しましたので、phaseに分解して、目標を管理しながら、空いた時間に細々と取り組んで、WS完了に近づいていけるように、準備してください。」）

## 目的と範囲

- GL_ARB_buffer_storage、GL_ARB_clear_texture、GL_ARB_enhanced_layouts、GL_ARB_multi_bind、GL_ARB_query_buffer_object、GL_ARB_texture_mirror_clamp_to_edge、GL_ARB_texture_stencil8、GL_ARB_vertex_type_10f_11f_11f_rev。4.4 の context と glx-p063.sh（新）。

## 受け入れ

- Venus で glx-p063（新）が PASS。回帰 PASS
- build: amd64・arm64・riscv64 の libGL・libGLESv2・libEGL・glxtest が warning 0。i915 実機は受け入れに含めない（2026-10-29 の決定）。
