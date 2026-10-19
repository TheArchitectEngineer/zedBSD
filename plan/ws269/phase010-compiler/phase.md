<!-- awesome-plan project=zedbsd record=ws269-p010-compiler -->

# ws269-p010-compiler SPIR-V から Rogue の USC への compiler

- 親: [ws269](../ws.md)、設計: [design.md](../design.md) §5（D6 A: 自前の USC の backend）、境: [design-p010.md](../design-p010.md) §5（P1、版 1）と下の「境（合意の案）」
- Queue: q1363 / q1363-i01（P2、2026-10-20）。design-p010.md 版 2 の分割で、この試行は [p013](../phase013/phase.md)（encoder の広げ、O-ENC）と [p014](../phase014/phase.md)（compiler、O-SHADER-ABI・O-VSFS）の両方の最初の試行として記録した（q1369）。この phase.md は詳細の記録として残す
- Status: in-progress（2026-10-20 P2: 読み・back end・key（format・pack・blend）・encoder の広げ・compute の executor への差し替え、host 試験 PASS、VF2・riscv64-virt の build warning 0。vertex・fragment を driver が使うのは p010h・p010i）
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

## 境（合意の案、design-p010.md 版 2 §5 への返答）

2026-10-20 の Q1 の連絡（design-p010-review-1 の B4: D-P3 は (b)、encoder の広げは P2）と design-p010.md 版 2 §5 に合わせた。design-p010.md は直していない。型は `src/drivers/gpu/powervr/shader-compile.h` にある（§5.1 の言う `shader.h` の候補。名前を `shader.h` にするかは Q1・P1 の判断。file の改名は削除を伴うので Q1 に頼む）。

| 項目 | 実装した形 | design-p010.md 版 2 との関係 |
| --- | --- | --- |
| 入力 | `powervr_shader_read/compile/compile_inline(words, count, stage, key, ...)`。`struct powervr_shader_key`: location ごとの vertex の属性の format、color の location ごとの attachment の format（0 は出力なし）、blend（enable・factor・op・write mask、Vulkan の値）、sample の数（1 だけ、2 以上は ENOTSUP）。key が NULL なら fragment の色は float のまま temp に置く（host 試験と compute 用） | §5.1 の入力の表と一致 |
| 属性の format | R32・R32G32・R32G32B32・R32G32B32A32_SFLOAT（compositor・vkdemo が使う全部）。shader の型より語の少ない format は 0・0・0・1 で埋める（shader の中）。他の format（8 bit の正規化など）は ENOTSUP（Mesa `pco_nir_pvfio.c` の unpack は未実装、使う shader が無い） | §5.1「format の変換は shader の中」の一部。残りは要る時に |
| color の format・pack | R8G8B8A8_UNORM と B8G8R8A8_UNORM を 8 bit の正規化の 4 channel に pack（B8G8R8A8 も R が下位の byte、Mesa `to_pbe_format`）、pixel output register（pixout0〜3）に overlap check 付きの move で置く。出力の表の `reg` は pixel output register の番号、`format` は attachment の format | §5.1「pack と blend の済んだ raw の値の register」と一致。EOT の program（driver、§5.4）が pixout を PBE に渡す |
| blend | Vulkan の factor 0〜14（dual source は ENOTSUP）と op（add・subtract・reverse subtract・min・max）、色と alpha の別、write mask（書かない channel は元の色）、unorm の [0,1] の clamp。元の色は pixout を overlap check 付きで読み unpack。blend 定数は `BLEND_CONSTANTS`（4 語）の要求 | §5.2 の 6 と一致 |
| 要求の表 | SET_BASE・DESCRIPTOR（part: BUFFER_ADDRESS 2 語、IMAGE_STATE 4 語、SAMPLER_STATE 4 語、array_index）・PUSH_CONSTANTS（使った語まで）・LITERAL（今は出さない）・WORKGROUP_ID（3 語）・NUM_WORKGROUPS（3 語）・BLEND_CONSTANTS（4 語）。shared register の番号は表の順に 0 から詰める | §5.2 と一致。descriptor の memory の並び（16・32・48・80 byte）は driver が持ち、compiler は part で頼む。buffer は compiler が address の語から `ld`/`st` で読み書き |
| vertex の入力・出力 | 入力の表（location、語数、vertex input register）。出力は position が vertex output の語 0〜3、varying は location の順に 4 から（`uvsw.write` の即値）、最後に `uvsw.emit.endtask`（end） | §5.1 と一致（format の欄は driver が key から持つので表には置いていない。要れば足す） |
| fragment の入力 | varying ごとの coefficient register の番号（1 語 4 register: A・B・C・pad）、perspective の varying があれば w を 0〜3。smooth は `ditrp`（BXE は usc_itrsmp_enhanced、Mesa と同じ。1 語ずつ、count 1）、flat は C の係数の move | §5.1 と一致 |
| 性質 | workgroup の大きさ。discard・depth の書き・shared memory は未実装（使う shader が無い、ENOTSUP） | 残り |

compute の今の道: p009 の executor（`vk-queue.c`）は DOUTD の shared の DMA を持たないので、`powervr_shader_compile_inline` で要求の値（buffer の address・workgroup の id・数）を定数に焼いて workgroup ごとに compile する（p009 と同じく 1 workgroup = 1 kernel、instance ≦ 32、y・z は 1）。p010h で PDS の data の program ができたら通常の `powervr_shader_compile` に替える。push constant の compute は executor の側が未対応（ENOTSUP、記録に push constant が無い）。

## 確かめ

| 確かめ | コマンド | 結果 |
| --- | --- | --- |
| compiler の host 試験 | `timeout 300 sh plan/ws269/tests/p010-compiler/run.sh build/ws269-p010c/host` | `powervr compiler host test: 298 passed, 0 failed`、`powervr compiler group host test: 20 passed, 0 failed`（2026-10-20） |
| p008・p009 の host 試験（encoder と executor） | `sh plan/ws269/tests/p008/run.sh build/ws269-p010c/p008`、`sh plan/ws269/tests/p009/run.sh build/ws269-p010c/p009` | encode 91/0、compute 70/0、executor 112/0（executor は compiler の出力と bytes で比べる形に直した） |
| VF2 の kernel | `timeout 3600 make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/ws269-p010c-vf2 -j16 vmunix`（log `build/ws269-p010c/vf2.log`、最後の版は `vf2b.log`） | rc=0、`warning:` 0（vf2.log・vf2b.log の両方） |
| amd64 の kernel（powervr は build しない、確かめのため） | `... config/ci/config-amd64.mk BUILD=build/ws269-p010c-amd64 ... vmunix`（log `amd64.log`） | rc=0、`warning:` 0 |
| riscv64-virt の kernel（共有の `platform/riscv64/vmunix.mk` を変えたため） | `... config-riscv64-virt.mk BUILD=build/ws269-p010c-virt ... vmunix`（log `virt.log`） | rc=0、`warning:` 0 |

host 試験の中身:

- (a) 読み: inventory の全部（compositor の 7 と panel.frag の 8 変種、vkdemo の 2、vkcompute、p009 の fill・offset）と試験の shader 4 本（`shaders/loop.comp`: 発散する loop と break・continue、`branch.comp`: 入れ子の if・else if・select・push constant・float 変換、`math.comp`: sin・cos・abs・clamp・mix・length・smoothstep・min・max・inversesqrt、`call.comp`: 関数・out 引数・局所の配列）を `-O` あり・なしで読み、中間形の interpreter（`ir-run.c`、32 lane の mask で core の task と同じく流す）の結果を C の同じ計算と比べる（fill・offset・loop・branch・math（誤差 2e-5）・call・quad.vert・vkdemo の vertex（回転、誤差 1e-4）・fragment（sample）・key 付きの vkdemo の fragment（pack）・quad.frag（premultiplied の blend と write mask））。
- (b) back end: 同じ shader を compile し、機械の命令の列を simulator（`machine-run.c`）で流して、memory・vertex の出力・pixel output が interpreter と一致する（全 shader、key 付きの panel.frag の 8 変種を含む）。register の割り当て・命令の選択・execution mask counter の手順を確かめる。
- (c) group: 種類ごとの group の field（unit、write enable、source の slot、is0〜is5、repeat、overlap check）と phase の bytes を、Mesa の `pco_map.py` の group_map と `pco_isa.py` の piece から手で組んだ期待と比べる（20 件）。operand の語と header は p008 の encoder（p008 で照合済み）。

## 仮定と危険（実機で確かめる所）

1. **execution mask counter の意味**: Mesa の `pco_cf.c` の手順（cndst・cndef・cndend・cndsm・cndlt・br の並び）をそのまま写した。simulator の cnd の意味は Mesa の使い方から推した model（instance は counter が 0 の間動く、start は取られない instance の counter を adjust だけ上げる、else は 0 と 1 を入れ替える、end は adjust だけ下げる、set は動いている instance に値を入れる、loop の test は動く instance があれば p0、無ければ全部を adjust だけ下げる）で、hardware の資料は無い。実機で分岐・loop が誤るなら最初に疑う。分岐の無い shader（compositor・vkdemo の全部、vkcompute）はこの手順を使わない。
2. **group の encoding の variant の選び方**: Mesa の生成した encoder は「既定（最初）の variant 以外を順に試し、条件が全部成り立つ最初」を選ぶと読んだ（`pco_map.py` の encode_map）。条件付き move の mask は ref の elem 0（全 element）が `== 0b1111` の条件に合わないので常に extended の形、と読んだ。違えば bytes が変わる（意味は同じはず）。
3. **pck・upck の repeat 4**: Mesa は 4 channel の pack を repeat 4 の 1 命令にする（`pco_trans_nir.c` の pack_pco）。unpack も同じく repeat 4 で element 0。repeat の register の進み方は simulator の model（source・destination が 1 つずつ進む）。
4. **special register の inst_num は movs1**（下の「気付いた事」）。
5. 一時 register を 8 個（emc、scratch 3、座標 2、address 2）取っておくので temps が多め（fill は 13）。占有率は p011 で見る。
6. 作業の memory は 1.1 MB（kern_malloc、compile の度に取って返す）。

## UAT のための log

- module の作成で compute の entry が読めない時: `powervr: vk: a shader module of <n> words has no compute entry the compiler takes (<errno>)`（graphics の module でも出る。p010i で stage を見て出し分ける）。
- dispatch の時: `powervr: vk: dispatch of <n> workgroups: the compute shader compiled to <bytes> bytes, <t> temps, <s> shared words`、compile の失敗は `powervr: vk: the compute shader did not compile (<errno>)`。

## 残り

1. vertex・fragment の compile を driver が使うのは p010h・p010i（P1）。O-VSFS の「gpu-selftest の shader」は gpu-selftest に shader がまだ無いので、できたら host 試験に足す。
2. 属性の 8・16 bit の format の変換、discard、FragCoord・VertexIndex・InstanceIndex、depth の書き、compute の shared memory・barrier・atomic、texture の LOD 指定・配列・cube、switch、早い return: どれも inventory の shader が使わないので未実装（ENOTSUP）。p011 で数える。
3. design-p010.md §5.4 の固定の program（EOT・SR/PT・transfer）が要る命令の一覧が P1 から届いたら encoder に足す（今は smp brief・ditr・emitpix・upck・uvsw を足した。fitr/fitrp（backend の形）は BXE では使わないので足していない）。
4. 規約の全文の見直し（p012）の前に: `shader-spirv.c` は長い（約 3,500 行）。関数の分け方は見直しの時に。
5. Q1 への片付けの依頼（rm は Q1）: 置き換えで使われなくなった `src/drivers/gpu/powervr/spirv-fill.c`・`spirv-fill.h`、p009 の `plan/ws269/tests/p009/spirv-host-test.c` と `shaders/constant.comp`・`index.comp`・`reject-branch.comp`・`reject-local.comp`・`reject-read.comp`（どの試験からも参照しない。`fill`・`offset`・`reject-size` は使う）、`build/ws269-p010c/`・`build/ws269-p010c-vf2/`・`build/ws269-p010c-virt/`・`build/ws269-p010c-amd64/`。p009 の phase.md の spirv-fill の記述は履歴として残す（この Phase が置き換えた）。

## 気付いた事（Q1 へ）

- p008 の `compute-program.c` の `pass_group` は special register の inst_num（51）を s0 から mbyp で読む。Mesa の `ref_src_map_valid`（`pco_internal.h`）では special register は s1・s2・s4 にしか置けず、Mesa は `movs1`（phase 2 の movwm、s1、is0 = s1、is4 = fte）に変える。p008 の実機の試験（compute kernel の行）が FAILED なら最初に疑う所。compiler はこの形（movs1）を使うので、vkcompute の道（p009 の差し替え後）はこの点では Mesa と同じ。p008 の試験の program を直すかは Q1 の判断（直せば p008 の host 試験の期待も変わる）。→ q1369 で直した（compute-program.c の `special_group`、p008 の compute 70/0）。
- design-p010.md 版 2 は compiler の Phase を p013（encoder）・p014（compiler）に分けている。この記録（q1363）はその両方の最初の試行にあたる（encoder の広げも含む）。ws.md の行の付け替えは Q1 へ。→ q1369 で p013・p014 の phase.md を作り、ws.md の行を合わせた。`shader.h` の名前は p014 で決めた（改名は Q1 に `git mv`）。
