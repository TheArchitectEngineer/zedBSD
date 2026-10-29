<!-- awesome-plan project=zedbsd record=ws068p062 -->

# ws068-p062: GL 4.3 の残り（2）と 4.3 の名乗り

Phase ID: `ws068-p062`
Parent: [WS068](../ws.md)
Status: planned
Phase disposition: normal
依存: p044、p041、p059、p061
大きさの見積もり: 10 LW
出どころ: p036 l（2026-10-29 に p035・p036 を分けた。2026-10-29 ユーザー「WS068ですが、i915の試験を受け入れ条件から外して、VenusのみでOKとします。また、残りが大きい件は了解しましたので、phaseに分解して、目標を管理しながら、空いた時間に細々と取り組んで、WS完了に近づいていけるように、準備してください。」）

## 目的と範囲

- GL_ARB_clear_buffer_object、GL_ARB_copy_image、GL_ARB_framebuffer_no_attachments、GL_ARB_invalidate_subdata、GL_ARB_multi_draw_indirect、GL_ARB_texture_buffer_range、GL_ARB_texture_storage_multisample、GL_ARB_texture_view、GL_ARB_internalformat_query2、GL_ARB_ES3_compatibility、GL_ARB_robust_buffer_access_behavior。4.3 の context（GLSL 4.30）と glxtest `--gl43`・glx-p062.sh（新）。

## 受け入れ

- Venus で glx-p062（新）が PASS。回帰 PASS
- build: amd64・arm64・riscv64 の libGL・libGLESv2・libEGL・glxtest が warning 0。i915 実機は受け入れに含めない（2026-10-29 の決定）。
