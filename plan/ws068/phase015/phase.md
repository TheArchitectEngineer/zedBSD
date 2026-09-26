<!-- awesome-plan project=zedbsd record=ws068p015 -->

# ws068-p015: GLSL compiler の設計と p003 の分割

Phase ID: `ws068-p015`
Parent: [WS068](../ws.md)
Status: planned
Phase disposition: normal
Queue: —（2026-09-27 ユーザー「GLSLコンパイラと…サブエージェントで実装を進めてもらえますか。」の指示で、サブエージェントが実行）
設計: [glsl-design.md](../glsl-design.md)、[design.md](../design.md) §4（方式 A）・§6

## 範囲

1. compiler の構成（file と責務）、データ構造、SPIR-V の出し方（i915 のネイティブ compiler の制約からの決定）、libGLESv2 への接続、
   試験の方針を [glsl-design.md](../glsl-design.md) に書く。
2. p003 を有限の Phase（p016〜p019）に分け、受け入れを決める。

## 受け入れ

1. glsl-design.md が §1〜§10 を持ち、i915 の制約（`src/drivers/gpu/i915/compiler/spirv.c`）と p008 の SPIR-V の約束に合っている。
2. ws.md の表に p015〜p019 があり、各 phase.md に範囲と受け入れがある。
