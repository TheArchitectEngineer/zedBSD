<!-- awesome-plan project=zedbsd record=ws068p018 -->

# ws068-p018: GLSL compiler の SPIR-V の出力と link

Phase ID: `ws068-p018`
Parent: [WS068](../ws.md)
Status: planned
Phase disposition: normal
Queue: —（2026-09-27 ユーザーの指示でサブエージェントが実行）
設計: [glsl-design.md](../glsl-design.md) §3・§5

## 範囲

1. `spirv.c`・`emit.c`・`emit-builtin.c`・`link.c`: SPIR-V 1.0 の module、式・文・制御（構造化した if・loop・switch の展開）、
   関数の inline 展開、uniform block（std140、使う uniform だけ）・sampler・attribute・varying の配置、定数。§5 の i915 の形。
2. host の試験: (a) shader の組を compile・link し、p008 の `gles_spirv_reflect`・`gles_spirv_position` を通して `spirv-val
   --target-env vulkan1.0`、(b) host の Vulkan（lavapipe）で描いて期待の色（算術・built-in・制御・inline・uniform・texture）、
   (c) i915 の compiler（host）で代表の shader（egltest の場面、固定機能の shader の GLSL 版）が受けられる。

## 受け入れ

1. (a) の全 shader が spirv-val を通る。(b) の全ての場合が期待の色。(c) の代表の shader が i915 で accepted。
2. 新しい C の style-check 0、warning 0。
