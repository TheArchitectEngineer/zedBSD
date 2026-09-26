<!-- awesome-plan project=zedbsd record=ws068p020 -->

# ws068-p020: GLSL 1.40〜3.30・ES 3.00 の言語

Phase ID: `ws068-p020`
Parent: [WS068](../ws.md)（p012 を分けたもの）
Status: planned
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
