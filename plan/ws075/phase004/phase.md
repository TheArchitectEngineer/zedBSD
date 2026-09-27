<!-- awesome-plan project=zedbsd record=ws075p004 -->

# ws075-p004: i915 の compiler（GLES 2 の核）

Phase ID: `ws075-p004`
Parent: [WS075](../ws.md)
Status: in-progress（2026-09-27 着手）
Phase disposition: normal
承認: 2026-09-27 ユーザー「…i915の高度化に進んでください。」、WS075 の計画（main の登録）。

## 範囲

p001 の survey（`plan/ws075/tests/shader-survey/run.sh`）で libGLESv2 の生成する shader の不足のうち、GLES 2 の shader の核に当たるもの:

1. 補間: Flat（18 module）、Centroid、NoPerspective（vertex の output は効果なし、fragment の input は線形の barycentric が要る）。
2. 整数の入力（vertex の属性と varying、Flat と組）: `load of an input that is not a float scalar or vector`（ws031-p038）。
3. input builtin: FragCoord・FrontFacing・PointCoord（fragment）、VertexIndex・InstanceIndex（vertex）。output builtin PointSize。
4. texture() の bias・offset、textureLod（OpImageSampleExplicitLod）。
5. local の配列・struct と配列の定数（ws031-p040）、OpFwidth、Determinant・MatrixInverse・PackHalf2x16・UnpackHalf2x16。

範囲外: texelFetch・textureSize・shadow・整数や cube・配列・3D の sampler（p005）、MRT（p006）、geometry（p007）。

## 設計

### 増分 1: Flat と Centroid（2026-09-27、2756b254）

- compiler（`compiler/spirv.c`）: `OpDecorate Flat` を受けて変数に `flat` を記録し、fragment の input の IR（`i915_shader_ir_io.flat`）へ。
  Centroid は効果なし（実行器は 1 pixel 1 sample、centroid は pixel の中心）。NoPerspective は vertex の output だけ効果なし
  （fragment の input は拒むまま。線形の barycentric は後の増分）。判定は `i915_spirv_decoration_ignored()`。member の decoration は
  拒むまま（user の interface block の出力は、もともと Position の block 以外を受けない）。
- `compiler/compile.c`: `binary->input_flat_mask`（payload の順の n 番目の input が Flat なら bit n）。
- 実行器（`render/pipeline-prepare.c`・`state.c`）: `kernels->ps_flat_mask` を 3DSTATE_SBE の dword 3（Constant Interpolation
  Enable、Mesa gen90.xml の 3DSTATE_SBE）へ。setup が provoking vertex の値を定数の平面にするので、kernel の補間の命令はそのまま。
- 試験: `plan/ws031/tests/i915-vk-lower-test.c` の拒否の例を Flat から Component に替え、Flat・Centroid が受けられることを足した。
  survey の規則も同じに（Flat の不足が消え、その後ろに隠れていた「整数の入力」が 8 module に出た。compiler との最初の拒否の一致は保つ）。

### 増分 2: 整数の入力（2026-09-27、c2c87fa6）

- compiler: 整数の入力の load を受ける: vertex の属性（bit のまま）と、Flat の fragment の input。`compile.c` は Flat の fragment の
  input を補間せず、setup の平面の origin（provoking vertex の値、4 float の 4 番目）を MOV する（整数も float も bit のまま。
  Mesa の brw と同じ読み方）。
- 実行器: vertex の属性の 32 bit 整数の format（R32〜R32G32B32A32 の SINT・UINT、値は Mesa 25.0.7 の isl.h の enum isl_format）、
  整数の format の欠けた w は整数の 1（VFCOMP_STORE_1_INT、gen80.xml）。
- survey: 整数の入力の規則を compiler に合わせた（不足のある module 38、compiler と全一致）。

残り（この Phase）: input builtin（VertexIndex・InstanceIndex・FragCoord・FrontFacing・PointCoord）、PointSize、NoPerspective の
fragment の input、texture() の bias・offset と textureLod、local の配列・struct と配列の定数、OpFwidth、Determinant 等。
8 bit・16 bit の属性（normalized を含む）の format も GL の app が使う（p001 の静的な検査は作成時の parameter を数えないので
ここで足した）。

## 判断が要る点（既定を選んで進める）

（なし）

## 検証（増分 1）

| 確認 | 結果 |
| --- | --- |
| build（`make vmunix`、-Werror、test build） | PASS、warning 0 |
| host: `plan/ws031/tests/run-vk-host-tests.sh "lower spirv compile"` | PASS |
| host: 同じ script の cmd・res・sync・cmdbuf | FAIL（この変更の前から。ws068-p006 で実装した line width・vkResetDescriptorPool 等を「未実装」と期待する古い assert。ws031-p049 の類） |
| survey | 122 module、不足 41（43 から）、compiler の最初の拒否と全一致 |
| style-check | 変えた file の数は前と同じ |
| 実機 | 未実施（Flat の画素の確認は GL の client の run で） |
| 増分 2: build・host（lower・spirv・compile）・survey | PASS、PASS、38 module（compiler と一致） |
