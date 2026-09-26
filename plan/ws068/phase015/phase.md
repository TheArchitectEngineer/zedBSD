<!-- awesome-plan project=zedbsd record=ws068p015 -->

# ws068-p015: GLSL compiler の設計と p003 の分割

Phase ID: `ws068-p015`
Parent: [WS068](../ws.md)
Status: cleared（2026-09-27）
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

## 結果（2026-09-27）

cleared。[glsl-design.md](../glsl-design.md) に §1〜§10（範囲、source の構成、流れ、データ構造、i915 の制約からの SPIR-V の出し方、
型検査の規則、libGLESv2 への接続、試験、Phase の分け方、既知の制限）を書き、ws.md に p015〜p019 を足した。i915 の制約は
`src/drivers/gpu/i915/compiler/spirv.c` の opcode・decoration の表と冒頭の説明から取った（OpFunctionCall・Private・struct と配列の
local・OpSwitch・OpCompositeInsert・OpAny/OpAll・投影の sample・Flat を持たない、一度も store していない成分の load を拒む、等）。
