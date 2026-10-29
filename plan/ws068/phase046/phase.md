<!-- awesome-plan project=zedbsd record=ws068p046 -->

# ws068-p046: GL 4.0 の cube map 配列（GL_ARB_texture_cube_map_array）

Phase ID: `ws068-p046`
Parent: [WS068](../ws.md)
Status: cleared（2026-10-29 q1686-i01 P2。Venus の venus-all（build/tmp/q1686-v5）で受け入れを確かめた）
Phase disposition: normal
依存: p037
大きさの見積もり: 4 LW
出どころ: p036 d（2026-10-29 に p035・p036 を分けた。2026-10-29 ユーザー「WS068ですが、i915の試験を受け入れ条件から外して、VenusのみでOKとします。また、残りが大きい件は了解しましたので、phaseに分解して、目標を管理しながら、空いた時間に細々と取り組んで、WS完了に近づいていけるように、準備してください。」）

## 目的と範囲

- GLSL: samplerCubeArray・isampler・usampler・samplerCubeArrayShadow（4.00、1.30〜3.30 は extension）、texture・textureLod・textureGrad・textureSize・textureGather、shadow の比較値は別の引数、SPIR-V の Cube Arrayed と SampledCubeArray。
- libGLESv2・libGL: GL_TEXTURE_CUBE_MAP_ARRAY（device の imageCubeArray の時）、glTexImage3D・glTexStorage3D（正方形・6 の倍数の layer-face）、VK_IMAGE_VIEW_TYPE_CUBE_ARRAY、黒の cube 配列、FBO の layer の取り付け、GL_TEXTURE_BINDING_CUBE_MAP_ARRAY。

## 受け入れ

- Venus で `sh plan/ws068/tests/venus-all.sh OUTDIR` の glx-p037（glxtest `--gl33`）が PASS し、その中の outcome「cube-map-array」が緑（`GLXTEST GL33 check` の行に FAILED が無い）。回帰（venus-all の他の試験）も PASS。glsl-host の `pass/cubearray330ext.*` と `fail/cubearray-330.frag` が PASS
- build: amd64・arm64・riscv64 の libGL・libGLESv2・libEGL・glxtest が warning 0。i915 実機は受け入れに含めない（2026-10-29 の決定）。

## 検証（2026-10-29 q1686-i01 P2）

- build: amd64・arm64・riscv64 の libGL・libGLESv2・libEGL・glxtest → warning 0。
- host: `sh plan/ws068/tests/glsl-host/run.sh build/tmp/q1686-glsl-host4` → PASS（`pass/cubearray330ext.*` の compile・link・spirv-val、`fail/cubearray-330.frag`）。
- Venus: `sh plan/ws068/tests/venus-all.sh build/tmp/q1686-v5` → `venus-all: PASS`。glx-p037 の cube-map-array は実行して緑（cube-array-binding、
  6 の倍数でない depth の GL_INVALID_VALUE、2 つの cube の +X と -Z の面と textureSize の ivec3(1, 1, 2)）。
- 未確かめ: shadow の cube map 配列と gather の cube map 配列は host の compile だけ（Venus の描画は無い）、layered の FBO の取り付け。
