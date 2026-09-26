<!-- awesome-plan project=zedbsd record=ws068p018 -->

# ws068-p018: GLSL compiler の SPIR-V の出力と link

Phase ID: `ws068-p018`
Parent: [WS068](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: —（2026-09-27 ユーザーの指示でサブエージェントが実行）
設計: [glsl-design.md](../glsl-design.md) §3・§5

## 範囲

1. `module.c`・`emit.c`・`emit-builtin.c`・`link.c`: SPIR-V 1.0 の module、式・文・制御（構造化した if・loop・switch の展開）、
   関数の inline 展開、uniform block（std140、使う uniform だけ）・sampler・attribute・varying の配置、定数。§5 の i915 の形。
2. host の試験: (a) shader の組を compile・link し、p008 の `gles_spirv_reflect`・`gles_spirv_position` を通して `spirv-val
   --target-env vulkan1.0`、(b) host の Vulkan（lavapipe）で描いて期待の色（算術・built-in・制御・inline・uniform・texture）、
   (c) i915 の compiler（host）で代表の shader（egltest の場面、固定機能の shader の GLSL 版）が受けられる。

## 受け入れ

1. (a) の全 shader が spirv-val を通る。(b) の全ての場合が期待の色。(c) の代表の shader が i915 で accepted。
2. 新しい C の style-check 0、warning 0。

## 結果（2026-09-27）

cleared。受け入れ 1・2 を満たした。

### 実装

- `module.c`（SPIR-V 1.0 の module、節ごとの word 列、型と定数の重複の除去）、`emit.c`（式・文・制御、path による load・store、
  関数の inline 展開、switch と早い return の once-loop、uniform block（その stage の uniform だけ、link の offset）、sampler・
  入出力・built-in の変数）、`emit-builtin.c`（GLSL.std.450、単一の命令、texture の各形）、`link.c`（uniform の併合と std140 の配置、
  sampler の binding、attribute（glBindAttribLocation の後に空き）・varying・fragment 出力の location、両 stage の emit、API 用の
  uniform の記述）。

### 検証（host）

- `plan/ws068/tests/glsl-host/run.sh` PASS（2026-09-27）:
  - 3: pass/ の 4 組が link し、両 stage と、libGLESv2 の反射と gl_Position の書き換えを通した後の SPIR-V が `spirv-val --target-env
    vulkan1.0` を通る。
  - 4: i915 の compiler（host）が scene（vertex 1600 byte、fragment 1168 byte）と固定機能の GLSL 版（vertex 23136 byte、fragment
    2576 byte）を accepted（glslc の固定機能の vertex は 22352 byte）。
  - 5: `vk-run`（host の Vulkan、lavapipe）で exec/ の 15 の shader を描き、全画素が期待の色: 算術、uniform（matrix、配列、struct、
    struct の配列、bool、int、動的な添字）、制御（loop、break・continue、入れ子）、関数（out・inout、overload、loop の中からの
    早い return）、built-in 37 種、matrix、local の struct と配列、texture（2D、Proj、bias、sampler の引数）、副作用のある `&&`・`?:`・
    `,`、discard、vertex から fragment への varying（配列、matrix）、定数式、GLSL 1.30（switch の fall-through、uint、bit 演算、shift、
    `%`、texture・textureSize・texelFetch、flat・noperspective、gl_VertexID、非正方の matrix、配列の constructor と length()）。
    失敗する shader を足すと FAIL になることも確かめた。
- style-check 0（compiler と試験の C）。

### 分かったこと

- i915 で拒まれる形（記録、F-023 の範囲）: 逆三角・双曲線・refract 等の GLSL.std.450（i915 の表に無い）、dFdx 等の微分、
  vertex shader の texture（ExplicitLod）、struct と配列の local、built-in の入力（gl_FragCoord 等）、Flat、16 を越える uniform。
