<!-- awesome-plan project=zedbsd record=ws068p020 -->

# ws068-p020: GLSL 1.40〜3.30・ES 3.00 の言語

Phase ID: `ws068-p020`
Parent: [WS068](../ws.md)（p012 を分けたもの）
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: —（2026-09-27 ユーザーの指示でサブエージェントが実行。「p003 の後、時間があれば p012」）
設計: [glsl-design.md](../glsl-design.md) §11

## 範囲

1. 前処理と keyword: `#version 140`・`150`・`330`（`core`・`compatibility`）・`300 es`。`layout`。ES 3.00 で予約語になった
   `attribute`・`varying`、消えた `gl_FragColor`・`gl_FragData`・`texture2D` 等は誤り。
2. `layout(location = N)`: vertex の入力（`glBindAttribLocation` より優先）と fragment の出力。複数の fragment 出力は location の順に。
3. `in`/`out` の interface block（1.50 以上、desktop）: block の名前で vertex の出力と fragment の入力を照合する。
4. 整数の varying（fragment の入力は flat）、`gl_InstanceID`。
5. sampler: `sampler2DArray`・`isampler2DArray`・`usampler2DArray`・`sampler2DArrayShadow`・`samplerCubeShadow`。texture: `textureGrad`、
   `textureOffset`、`textureLodOffset`、`textureProjLod`。built-in: `determinant`、`inverse`、`floatBitsToInt`・`floatBitsToUint`・
   `intBitsToFloat`・`uintBitsToFloat`、`packSnorm2x16`・`unpackSnorm2x16`・`packUnorm2x16`・`unpackUnorm2x16`・`packHalf2x16`・
   `unpackHalf2x16`。
6. libGLESv2: `#version 300 es` は ES 3 の context（`EGL_CONTEXT_CLIENT_VERSION` 3）でだけ受ける。
7. 試験: glsl-host に 3.30・ES 3.00 の pass/・fail/・exec/ を足す。

## 受け入れ

1. glsl-host の試験が PASS（新しい shader を含め、spirv-val と lavapipe の実行）。p016〜p019 の試験が変わらず PASS。
2. 新しい C の style-check 0、warning 0。
3. Venus の egltest で ES 3 の context の `#version 300 es` の場面、または未実施の理由（API の不足）を書く。i915 実機は未実施。

## 結果（2026-09-27）

cleared。受け入れ 1〜3 を満たした。

### 実装

- 版: `#version 140`・`150`・`330`（`core`・`compatibility`、1.50 以上は `GL_core_profile`・`GL_compatibility_profile`）・`300 es`。
  版の判定は `glsl_version_mask`・`glsl_since`（types.c）に一本化。ES 3.00 では `attribute`・`varying`・`noperspective` 等は予約語、
  `gl_FragColor`・`gl_FragData`・`texture2D` 等は無い。
- `layout(location = N)`（vertex の入力は `glBindAttribLocation` より優先、fragment の出力。ES 3.00 で出力が複数なら全部に要る）、
  `layout(...)` の std140・shared・packed・row_major・column_major の解析。
- `in`/`out` の interface block（desktop 1.50 以上）: block 名で照合、SPIR-V では Block の struct の入出力（Vulkan は member の Flat を
  Block の中でだけ受ける。lavapipe で Block 無しの struct では flat の整数が壊れたのを確かめて直した）。
- `gl_InstanceID`、`sampler2DArray`・`isampler2DArray`・`usampler2DArray`・`sampler2DArrayShadow`・`samplerCubeShadow`、
  `textureGrad`・`textureOffset`・`textureLodOffset`・`textureProjLod`、`determinant`・`inverse`、`floatBitsToInt` 等、
  `pack*`/`unpack*`（ES 3.00）。
- libGLESv2: `#version 300 es` は ES 3 の context（`EGL_CONTEXT_CLIENT_VERSION` 3）でだけ受ける（`glsl_shader_version`）。
- egltest `--scene=glsl3`（ES 3 の context、GLSL ES 3.00 の source、layout の location・in/out・`texture()`）、`egl-p020.sh`。

### 検証（host。QEMU・Venus は下。i915 実機は未実施）

- `plan/ws068/tests/glsl-host/run.sh` PASS: pass/ に es300・glsl330（in/out の block）・glsl150（compatibility）・scene300、fail/ に
  ES 3.00 の予約語・消えた built-in・layout の誤用・block の版・入力の配列・精度の 8 例、exec/ に `30-es300`（bit cast、pack、
  inverse・determinant、uint、texture・textureLod・textureGrad・textureOffset・texelFetch・textureSize、located な 2 出力、
  gl_InstanceID）と `31-glsl330-blocks`（in/out の block の flat の int・uint）。すべて spirv-val と lavapipe で期待どおり。
- i915 の host の検査: `scene300`（ES 3.00 の場面）が vertex 1600 byte、fragment 1056 byte で accepted。

### 検証（QEMU・Venus）

- `plan/ws068/tests/egl-p020.sh` PASS（build/ws068-p020.log、再確認 build/ws068-p021-egl-p020.log）: ES 3 の context で GLSL ES 3.00 の
  場面が display 直接・窓・docked の画面の 10 点と glReadPixels の 11 点で一致。
- 回帰: egl-p019・egl-p008 PASS。

### 制限

- `modf`（out 引数を持つ built-in）、`texelFetchOffset`・`textureProjOffset` 等、sampler の配列、`gl_FragCoord` の向き、
  in/out の block の配列、ES 3.00 の API（`glBindFragDataLocation` 等）は範囲外。
