<!-- awesome-plan project=zedbsd record=ws269-p010-compiler -->

# ws269-p010-compiler SPIR-V から Rogue の USC への compiler

- 親: [ws269](../ws.md)、設計: [design.md](../design.md) §5（D6 A: 自前の USC の backend）、境: [design-p010.md](../design-p010.md) §5（P1、版 1）と下の「境（合意の案）」
- Queue: q1363 / q1363-i01（P2、2026-10-20）
- Status: in-progress（2026-10-20 P2）
- 起点（2026-10-20 07:24 ユーザー）: 「PowerVRのコードは完了を目指して作業をお願いします。あとでUATにおいて、どこまで実行できたかわかるログが出れば、デバッグしやすいと思います。」
- 前提: p008（USC・PDS の encoder）、p009（spirv-fill の固定の subset。これを置き換える）。HAL・toolchain は変えない。

## 目的と受け入れの条件

1. Keiland の compositor・vkdemo・venus-frame・vkcompute が使う SPIR-V を compute・vertex・fragment で受ける（下の一覧）。範囲外は ENOTSUP で拒む（推測で code を出さない）。
2. 型・算術・load/store・制御（分岐・loop・break・continue）・関数の inline・descriptor（storage・uniform・push constant・combined image sampler）。
3. register の割り当て、shared register の要求の表、vertex の入力・varying・fragment の出力の表（driver が PDS を組む材料）。
4. host 試験: glslc の SPIR-V（実物と小さい試験の shader）を compile し、(a) 中間形を interpreter で流して C の同じ計算と比べる、(b) 機械の命令の列を simulator で流して interpreter と比べる、(c) 命令 group の bytes を Mesa の field の表から手で組んだ期待と比べる。
5. build（VF2・riscv64-virt・amd64）warning 0。QEMU は起動しない（VF2 の GPU は QEMU に無い）。

## SPIR-V の op の一覧（2026-10-20、glslc 2023.8 `-O`、`--target-env=vulkan1.0`）

対象: `userland/desktop/wayland/shaders/`（quad.vert・quad.frag・panel.vert・panel.frag（mode 無しと 0〜7 の 8 変種）・panel-batch.vert/.frag・turn.vert）、`userland/tests/vkdemo/shaders/cuboid.vert/.frag`、`userland/tests/vkcompute/fill.comp`。venus-frame（`userland/tests/venus-frame/`）は shader module を作らない（grep で確認）。

| 種類 | op（全 shader の合計の出現数） |
| --- | --- |
| 宣言 | OpConstant 162、OpTypePointer 101、OpDecorate 94、OpMemberDecorate 92、OpVariable 76、OpTypeVector 40、OpTypeInt 31、OpTypeStruct 20、OpConstantComposite 20、OpTypeFloat 17、OpTypeImage・OpTypeSampledImage 8、OpTypeArray 5、OpTypeRuntimeArray 1、OpTypeBool 1 |
| memory | OpLoad 128、OpAccessChain 96、OpStore 27 |
| 組み立て | OpCompositeExtract 73、OpCompositeConstruct 42、OpVectorShuffle 41 |
| 算術 | OpFSub 61、OpVectorTimesScalar 58、OpFAdd 54、OpFMul 30、OpFDiv 8、OpDot 5、OpFNegate 1、OpIMul 1、OpIAdd 1 |
| GLSL.std.450 | FMax 25、FClamp 20、FMix 15、FAbs 11、FMin 8、Length 8、Sin 2、Cos 2、SmoothStep 2 |
| 比較・制御 | OpFOrdLessThan 6、OpPhi 6、OpSelectionMerge 6、OpBranchConditional 6、OpBranch 12（panel.frag の mode 無しだけ） |
| texture | OpImageSampleImplicitLod 19（全部 fragment、sampler2D、combined） |
| 変数 | Input 27、Output 26、PushConstant 14、UniformConstant 8、Uniform（BufferBlock）1 |
| built-in | Position 5（gl_PerVertex の block）、PointSize・ClipDistance・CullDistance 各 5（捨てる）、GlobalInvocationId 1、WorkgroupSize 1 |

この一覧に加えて、最適化なし（`-O` なし）の形（Function の変数・OpFunctionCall・out 引数）と、整数の比較・bit 演算・shift・select・loop の break/continue・変換・GLSL の Floor/Ceil/Fract/Round/Sign/Pow/Exp2/Log2/Sqrt/InverseSqrt/U・S の Min/Max/Clamp/Step/Fma/Distance/Normalize、OpMatrixTimesVector も受ける（試験の shader で確かめる）。libkeiland の UI・files・monitor など Keiland の app の shader は p011 で数える。

## 設計

### 置き場所（kernel の中、bcm2711 の shader*.c と同じ作り方: 読み → 中間形 → 選択・割り当て → 出力）

| file | 中身 |
| --- | --- |
| `src/drivers/gpu/powervr/shader-compile.h` | driver が見る唯一の面: stage、shared register の要求の表、vertex の入力・varying・fragment の出力の表、register の数、`powervr_shader_read`・`powervr_shader_compile`。p010h で P1 と合わせて `shader.h` にする時はこの型を移す |
| `src/drivers/gpu/powervr/shader-ir.h` | 中間形: scalar の命令の線形の列。構造（IF・ELSE・END_IF・LOOP・LOOP_CONTINUE・END_LOOP・BREAK・CONTINUE）は命令のまま残す。値は書き換え可能な slot（phi・局所変数は move） |
| `src/drivers/gpu/powervr/shader-private.h` | 作業の memory（caller が渡す、compiler は allocate しない） |
| `src/drivers/gpu/powervr/shader-spirv.c` | SPIR-V の読み（2 pass: 宣言 → entry の構造化 CFG の walk、関数の inline、vector の scalar 化、GLSL.std.450 の展開） |
| `src/drivers/gpu/powervr/shader-backend.c` | 命令の選択・register の割り当て・制御の lowering（execution mask counter）・group への encode |
| `plan/ws269/tests/p010-compiler/` | host 試験（interpreter `ir-run.c`、machine の simulator `machine-run.c`、`compile-host-test.c`、試験の shader、`run.sh`） |

### 中間形の決め事

- bool は語（真 = 全 bit 1、偽 = 0）。float の neg・abs は operand の modifier（back end が hardware の modifier か mbyp で付ける）。
- 定数は operand（back end が movi32 で使う所に作る）。shared register の語は `SHARED`（要求の番号、語）。
- 入力は entry の前に全部読む（vertex: vertex input register、fragment: iterate）。出力は値に溜め、最後の block の後に `OUTPUT` で渡す。
- sin・cos は角度を 1 回転に縮めて Taylor の y^15 までの奇数項（誤差 1e-6 未満、新しい hardware の op を使わない）。sqrt は rcp(rsq(x))（0 で正しい）。
- loop の continue は SPIR-V の continue construct に戻る（`LOOP_CONTINUE` で continue した lane が戻り、continue construct を流してから header へ）。

### 制御（Mesa 26.2.5 `pco_cf.c` の手順、Procedure referenced）

execution mask counter（emc、temp 1 つ）。if は `tstz` で p0、`cndst`（P0_FALSE、adjust 1）、全 lane が off なら else/endif へ `br`（ALLINST）、else は `cndef`、endif は `cndend 1`。loop は prologue `cndst 2` と epilogue への `br`、interlogue `cndend 1`・`cndst 1`、epilogue `cndlt 2` と body の頭への `br`（E1_Z1、exec_cond）。break・continue は `cndsm` に loop の中の if の深さ + 2 / + 1。interlogue は `LOOP_CONTINUE` の所（continue construct の前）に置く（Mesa の NIR は continue construct を body の末尾に含めるので、同じ位置）。予測実行（pred_exec）は使わない。

## 境（合意の案、design-p010.md への返答）

2026-10-20 の Q1 の連絡（design-p010-review-1 の B4）で変わった点を含む。design-p010.md は P1 が版 2 で直す。

1. compiler の出力（`struct powervr_shader_program`）: stage、code の長さ、temps・shareds・vertex_inputs・vertex_outputs・coefficients の数、w の coefficient の位置、workgroup、shared register の要求の表（kind: SET_BASE・DESCRIPTOR・PUSH_CONSTANTS・LITERAL・WORKGROUP_ID・NUM_WORKGROUPS、DESCRIPTOR は part: BUFFER_ADDRESS（2 語）・IMAGE_STATE（4 語）・SAMPLER_STATE（4 語）で、descriptor の memory の並びは driver が持つ）、vertex の入力の表（location、語数、vertex input register）、varying の表（location、語数、flat、vertex は出力の語の番号（position は 0〜3）、fragment は coefficient register の番号（1 語 4 register: A・B・C・pad、perspective の varying があれば w を 0〜3））、fragment の出力の表。
2. buffer の中身は compiler が DESCRIPTOR の address の語から USC の `ld`/`st` で読み書きする。
3. **D-P3 は (b)**（2026-10-20 Q1 の判断、Mesa の pco と同じ形）: compiler が color の pack と blend を行う。compile の入力（key）に vertex の属性の format、color attachment の format、blend の設定、sample の数を足し、要求の表に `BLEND_CONSTANTS` を足す。vertex の format の変換も shader の中（Mesa `pco_nir_pvfio.c` の手順）。color の store は tile の終わりの PDS の pixel event と USC の EOT の program（`emitpix` と PBE の state の語）。encoder（usc-encode.c）に SMP・FITR/FITRP（BXE は Mesa と同じく DITR/DITRP）・EMITPIX・UVSW を足すのはこの Phase（P1 は触らない）。**未実装（この記録の時点）**: key と pack・blend・EOT は compute・vertex・fragment の本体の後に足す。
4. compute の今の道（p009 の executor、DOUTD の shared の DMA は実機で未確かめ）では、driver は要求の値を compile の後に定数として埋める形を使えるようにする（下の「compute の差し替え」）。

## 確かめ

| 確かめ | コマンド | 結果 |
| --- | --- | --- |
| 読みの host 試験（中間形の interpreter と C の計算の比較） | `plan/ws269/tests/p010-compiler/run.sh`（作成中） | 2026-10-20: inventory の全 shader（compositor 7 + panel.frag の 8 変種、vkdemo 2、vkcompute 1、p009 2）と試験の shader 4 本 × `-O` あり・なしが読め、fill・loop（break/continue）・branch・math（sin/cos/smoothstep など）・call（関数・out 引数・局所配列）・quad.vert・vkdemo の vertex（回転）・fragment（sample）が C の計算と一致 |

## 残り（作業中の順）

1. back end（選択・割り当て・制御・encode）と simulator、bytes の試験。
2. encoder に smp・ditr・uvsw の write/emit/endtask・emitpix を足す。
3. key（format・blend・sample 数）と pack・blend・EOT。
4. vk-layout・vk-queue の compute の道を spirv-fill から compiler に差し替える。
5. build（VF2・riscv64-virt・amd64）。

## 気付いた事（Q1 へ）

- p008 の `compute-program.c` の `pass_group` は special register の inst_num（51）を s0 から mbyp で読む。Mesa の `ref_src_map_valid`（`pco_internal.h`）では special register は s1・s2・s4 にしか置けず、Mesa は `movs1`（phase 2 の movwm、s1、is0 = s1、is4 = fte）に変える。p008 の実機の試験が FAILED なら最初に疑う所。compiler はこの形（movs1）を使う。
