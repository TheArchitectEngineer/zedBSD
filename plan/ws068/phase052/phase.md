<!-- awesome-plan project=zedbsd record=ws068p052 -->

# ws068-p052: GL 4.0 の残りと 4.0 の名乗り

Phase ID: `ws068-p052`
Parent: [WS068](../ws.md)
Status: planned
Phase disposition: normal
依存: p034、p040、p043、p045、p046、p048、p050、p051
大きさの見積もり: 10 LW
出どころ: p036 l（2026-10-29 に p035・p036 を分けた。2026-10-29 ユーザー「WS068ですが、i915の試験を受け入れ条件から外して、VenusのみでOKとします。また、残りが大きい件は了解しましたので、phaseに分解して、目標を管理しながら、空いた時間に細々と取り組んで、WS完了に近づいていけるように、準備してください。」）

## 目的と範囲

- GL_ARB_shader_subroutine（subroutine uniform を switch に変換）、GL_ARB_texture_query_lod（textureQueryLod）、GL_ARB_texture_buffer_object_rgb32、GL_ARB_draw_buffers_blend の確認、GL 4.0 の glGet の残り。
- 4.0 の context（libGLX の attributes、fixed.c の GL_VERSION と GLSL 4.00 の上限、`zgl_glx_version_limit` の device の条件: tessellationShader・imageCubeArray・sampleRateShading・shaderFloat64）。glxtest に `--gl40` の場面と plan/ws068/tests/glx-p052.sh（新）。

## 受け入れ

- Venus で glx-p052（新）が PASS: 4.0 の context で GL_VERSION「4.0」・GLSL「4.00」、`#version 400` の tessellation・fp64・gather・subroutine の描画が緑。venus-all の回帰 PASS（glx-p037 は 3.3 の context のまま）
- build: amd64・arm64・riscv64 の libGL・libGLESv2・libEGL・glxtest が warning 0。i915 実機は受け入れに含めない（2026-10-29 の決定）。
