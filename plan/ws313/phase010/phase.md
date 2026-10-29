<!-- awesome-plan project=zedbsd record=ws313-p010 -->

# ws313-p010: Mali-G310（Valhall v10）の shader compiler

Status: in-progress（実装済み・実機未試験）
Queue: q1675-i02（P7 の配下 C）、q1675-i05 の一部（P7 と配下 P7-A: textureGrad・SampleId・stencil の書き込み・共通の front end の OpFunctionCall の inline）
WS: [WS313](../ws.md)、設計 [gpu.md](../gpu.md) §11・§12

## 目的

Keiland の desktop が描く shader の全部（compute・vertex・fragment）を Valhall の program にする。共通の front end
（`src/drivers/gpu/compiler/`）の IR から、backend `src/drivers/gpu/mali/compiler/` で encode・register の割当・wait
の scheduling・message 命令・IDVS・ATEST と BLEND まで。口と shader の ABI は `compiler/shader.h`（配下 D が使う）。

## ユーザーの指示（原文、2026-10-29、Q1 経由）

「Maliのドライバは、このGPUでとりあえずデスクトップを動かせるように、シェーダーコンパイラ、コマンド投入、シェーダ実行などを、一通り実装して、UATとデバッグに備えてください。デスクトップが動かせる範囲の実装でよいですが、余裕があればきちんと実装の対応範囲を他のGPUと同様に広げてください。」
「実機テストなしでコードだけ書いて揃えておいてください。今日の夜、まとめてテストします。」

## 実装（2026-10-29）

| file | 中身 |
| --- | --- |
| `compiler/shader.h` | 口（`mali_shader_compile`・`_compile_ir`・`_program_free`、`struct mali_shader_program`）と shader の ABI |
| `compiler/va-isa.h` | 命令の field の位置、使う命令の opcode、enum の値（ISA.xml から block ごとの出典） |
| `compiler/va-private.h`・`va-encode.c` | 命令の list、source・destination・staging の encode、定数の表 |
| `compiler/va-schedule.c` | slot の割当、wait（scoreboard）、ATEST の前の wait0126・BLEND の前の wait、branch の offset と reconverge、end、padding |
| `compiler/va-compile.c` | IR からの lowering と register の割当（下記） |

lowering が持つ物: 浮動小数の四則・FMA・min/max・比較、rcp・rsq・sqrt・exp2・log2・floor・trunc・round・fract・
sin・cos（範囲を 1/4 回転に畳んだ Taylor の多項式）、整数の四則（除算は float の逆数の推定と補正）・shift・論理・比較、
変換、half の pack・unpack、SELECT（MUX）、KILL（DISCARD）、loop（後ろへの BRANCHZ）、push constant（FAU）、
uniform buffer（FAU の address からの LOAD）、storage buffer と shared memory の load・store・atomic（SHADDX で address、
predicate は前への branch で迂回）、barrier、compute の local id・workgroup id、vertex attribute（LD_ATTR_IMM）、
IDVS の position program（viewport の変換、1/w、point size）と varying program（LEA_BUF_IMM と STORE）、
fragment の varying（LD_VAR_BUF_IMM、flat、NoPerspective）、FragCoord・FrontFacing・PointCoord・PrimitiveID、
texture（TEX_SINGLE・TEX_FETCH、2D・3D・array、bias・explicit lod、定数の texel offset、compute の sampled image）、
texture の大きさと level 数・storage buffer の長さ（driver が FAU に書く語から）、微分（CLPER、coarse・fine）、
ATEST と各 render target への BLEND。

## 実装の追加（q1675-i03、P7 の配下 A2、2026-10-29、実機未試験）

範囲: compiler の未対応を他の GPU（amdrdna: cube・gather・compare）の範囲へ、と storage image・spill。

| 機能 | compiler | driver |
| --- | --- | --- |
| cube・cube array の texture | 済み: CUBEFACE1・CUBEFACE2_V9・CUBE_SSEL・CUBE_TSEL、FRCP・FMA.clamp_0_1 で s・t、MUX.bit で face を s の bit 31:29 に、dimension cube | 済み: arrayLayers > 1・CUBE_COMPATIBLE の image（layer を続けて置く）、view の 2D・2D_ARRAY・CUBE・CUBE_ARRAY、texture descriptor（dimension cube、array size = cube の数、plane を layer ごと）、textureSize の cube 数 |
| gather・compare | 済み: TEX_GATHER（channel bits 15:14）、shadow bit 12 と reference の staging（S T R ARRAY SHADOW OFFSET LOD の順）、compare の TEX_SINGLE は R だけ書く、compare gather | 済み: sampler の compareEnable → compare function（word 1 bits 13..15）、D16_UNORM・D32_SFLOAT を sample だけの format に（depth attachment は無い） |
| storage image | 済み: LD_TEX_IMM（format の型で f32・s32・u32）、LEA_TEX_IMM と ST_CVT（format の成分数、AUTO）、image の atomic（LEA_TEX_IMM の address に ATOM_RETURN）、imageSize、table 4 | 済み: set layout の STORAGE_IMAGE、table 4 に storage 用の texture descriptor（identity swizzle、LOD 0、plane を layer ごと）、format の STORAGE_IMAGE（・R32_UINT/R32_SINT の ATOMIC）、usage STORAGE、limit |
| spill | 済み: register が尽きた時、最も遠くまで生きる値を TLS へ STORE.i32（force）、読む所で LOAD.i32。loop の頭で足りなければ loop を通り抜ける値を先に spill（再試行の reserve 0・16・32・64）。program の tls_bytes | compute: 済み（TSD に TLS、`mali_vk_build_thread_storage`）。graphics: 済み（P7 が vk-draw.c に当て、vk-pipeline.c の拒否を外した。fragment の TLS の読み先は未確認） |
| FragDepth・SampleMask・UBO の vector load | FragDepth・SampleMask は q1675-i04 で済み（下）。UBO の vector load も q1675-i04 で済み（既定は無効、option の 1 bit で有効） | — |

shader.h の ABI（先頭の comment が本体）に足した物: `MALI_SHADER_TABLE_IMAGES`（4）と `images[]`・`image_count`
（set・binding・index・format・size_fau_word）、`tls_bytes`、`MALI_SHADER_SPIRV_FEATURES`（COMPUTE_IMAGES |
STORAGE_IMAGE、compiler と模型が同じ parse をする）、texture descriptor の dimension の約束、storage image は 2D の
layer の image（3D は slice を layer に）。

vk-desc に足した関数（既存の signature は変えていない）: `mali_desc_texture_view`・`mali_desc_thread_storage`・
`mali_desc_tls_size`・`mali_desc_sampler_compare`。vk-build.h に `mali_vk_build_thread_storage`。
copy（buffer↔image・image↔image）と clear は layer を扱う（clear は range ごと）。device: `imageCubeArray` を公表し、
create_device は公表した feature だけを受ける。maxImageDimensionCube 8192、maxImageArrayLayers 256、
storage image 8。

## 実装の追加（q1675-i04、P7 の配下 A3、2026-10-29、実機未試験）

範囲: 拒否していた物を compile できるように（ユーザー「余裕があればきちんと実装の対応範囲を他のGPUと同様に広げてください。」）。
今まで compile できた program の出力は変えない。driver は直していない（下の「driver への案」）。

| 機能 | compiler | 状態 |
| --- | --- | --- |
| FragDepth（gl_FragDepth） | parse の feature に `DRV_GPU_SPIRV_FEATURE_FRAG_DEPTH`。STORE_OUTPUT の `DRV_GPU_IR_LOCATION_FRAG_DEPTH` を持ち、ATEST の後・BLEND の前に `ZS_EMIT`（z の bit 25、depth を source 0、stencil の source は定数 0、coverage r60 を読み staging r60 に書く）。slot 0・前に wait0126（ATEST と同じ扱い、`VA_INST_ATEST` を流用）。program の `writes_depth` | 済み（host のみ） |
| SampleMask の書き込み（gl_SampleMask[0]） | feature `DRV_GPU_SPIRV_FEATURE_SAMPLE_MASK`。ATEST の前に `r60 = mask & r60`（LSHIFT_AND）。program の `writes_sample_mask` | 済み（host のみ） |
| SampleMaskIn の読み（gl_SampleMaskIn[0]） | `U16_TO_U32` で r61 の bits 15:0（rasterizer の coverage）。preload の bit r61。program の `reads_sample_mask` | 済み（host のみ） |
| SampleId | q1675-i05 で対応（下の i05 の節） | 済み（host のみ） |
| UBO の vector load | `MALI_SHADER_OPTION_VECTOR_UBO`（shader.h、`struct mali_shader_options` の flags）の時だけ、IR で隣り合う同じ buffer の次の語の LOAD_UBO を最大 4 語まで 1 つの LOAD.i64・i96・i128 に（`ubo_run()`）。3・4 語は offset が 16 の倍数、2 語は 8 の倍数の時だけ（足りなければ縮める）。結果は揃えた block（2 は偶数、3・4 は 4 の倍数の register）に置き、値に割り当てる。遠い offset（0x8000 以上）も SHADDX の address から同じに。**既定（option 無し・NULL）は従来どおり 1 語ずつ**で、driver（vk-pipeline.c・vk-preload.c）は NULL を渡すので今夜の desktop の出力は不変 | 済み（host のみ、既定は無効） |

shader.h に足した物（既存の field の位置・意味は不変、struct の末尾に追加）: `writes_depth`・`writes_sample_mask`・
`reads_sample_mask`、`MALI_SHADER_SPIRV_FEATURES` に FRAG_DEPTH と SAMPLE_MASK、ABI の comment（fragment の depth・sample mask）。
va-isa.h: `VA_OP_ZS_EMIT`・`VA_OP_U16_TO_U32`・`VA_SHIFT_ZS_EMIT_DEPTH`・`VA_PRELOAD_RASTER_COVERAGE`（ISA.xml・compiler.h から出典つき）。

判断（推し）:
- coverage は r60 に置いたまま（Mesa の bi_ra.c が ATEST・ZS_EMIT の coverage を cumulative coverage の register に固定するのに倣う）。
  SampleMask の AND は Mesa の pan_nir_lower_fs_outputs.c と同じく preload の coverage（r60）と取る。SampleMaskIn は Mesa の
  load_sample_mask_in と同じく r61 の bits 15:0。
- 順は ATEST → ZS_EMIT → BLEND（Mesa の順）。EarlyFragmentTests の時に depth の書き込みを捨てる Mesa の扱いは無い
  （共通の front end が ExecutionMode EarlyFragmentTests を IR に残さない）。stencil の書き込み（FragStencilRef）も front end に無い。
- ZS_EMIT の depth は常に register（定数でも temp に作る）。
- 模型（tests/mali-compiler-model.c）の推定: fragment の preload の r60 を「rasterizer の coverage から最も低い sample を除いた物
  （2 個以上の時）」、r61 を rasterizer の coverage と sample 番号（bits 23:16）にした（r60 と r61 の取り違えが見える様に）。
  ZS_EMIT は source 0 の depth を書き coverage を素通しする（ATEST と同じ、推定）。模型は ATEST が 1 回、ZS_EMIT と BLEND が
  ATEST の後（BLEND は depth を書く program では ZS_EMIT の後）で ATEST の書いた coverage を読む事、ZS_EMIT の前の wait0126、
  stencil の bit が無く z の bit がある事を検査し、ATEST の検査した coverage と depth を IR の参照と比べる。新しい乱数は引かない
  （coverage は画素の位置から作るので既存の試行の入力は変わらない）。

### 確かめ（q1675-i04、2026-10-29、host だけ。GPU の証拠ではない）

- `timeout 1800 sh plan/ws313/tests/mali-compiler-host-test.sh 200`: PASS、24 module（新しく `mali-compiler-shaders/` に
  depth.frag（discard の後に depth と colour）・depth-only.frag（colour 無し、ZS_EMIT で end）・sample-mask.frag）。
  既存の 21 module の listing は変更前（`build/pc/mali-compiler-host.pwQwe5`）と byte で一致（`build/pc/mali-compiler-host.ifep0J`）。
- `timeout 1800 sh plan/ws313/tests/mali-compiler-desktop-test.sh 200`: PASS、60 module。60 の listing は変更前
  （`build/pc/mali-compiler-desktop.pJVHqW`）と byte で一致（`build/pc/mali-compiler-desktop.6jfgn3`）。
- 突然変異（手で、`build/a3-mut/` の複写の va-compile.c で、script には無い）: SampleMask の AND を OR に・SampleMaskIn を r60 から・
  ZS_EMIT の z の bit の削除・ZS_EMIT の wait0126 の削除・ZS_EMIT の depth の source を r60 に、の 5 つが全部 FAIL になった。
- `make -j6 ZEDBSD_CONFIG=config/ci/config-odroid-c5-keiland.mk BUILD=build/a3-k build/a3-k/vmunix`: exit 0、warning 0
  （D3 の作業中の vk の変更を含む tree）。
- `tests/mali-preload-host-test.c`（c5-build-check.sh の cc の行と同じ flag）: all checks passed (15)。
- 実機・QEMU: 未実施。

### UBO の vector load（q1675-i04 の続き、P7 の判断「既定は無効、option の 1 bit で有効、有効の経路を host 試験で照合」）

- 判断（推し）: alignment は Mesa 26.2.6 の bifrost_nir.c（mem_access_size_align_cb: 16 byte まで、大きさの 2 の冪に揃える）。
  buffer の base は 16 byte 揃いを前提にした（driver の minUniformBufferOffsetAlignment は 64、buffer の memory は 256 揃い。
  shader.h の「Uniform buffers」に ABI として書いた）。束ねるのは IR で隣り合う load だけ（並べ替えはしない）。
  load lane は Mesa の va_pack_load の identity（64・128 bit は 7、96 bit は 6、assembler-cases の語と一致）。
- 模型: LOAD.iN の N/32 と staging の数の一致、load lane の identity（i32 は w0）、2 語は 8・3 と 4 語は 16 の address の揃いを検査する
  （`check_load()`）。host 試験（`tests/mali-compiler-host-test.c`）は uniform buffer を持つ module を、既定の後に option を有効にして
  もう一度 compile・decode・模型で照合し、listing を `NAME.vector-ubo.txt` に書き、`PASS NAME vector-ubo` を出す（既定の listing の
  file と `PASS NAME.spv` の数は変わらない）。encoder の既知の語に LOAD.i64・i96・i128 の 3 語を足した（27 語）。
- 新しい試験の shader: `mali-compiler-shaders/ubo.vert`（offset 4・8・16・32 の run、vec3、mat4、0x8000 を越える遠い offset）。

確かめ（2026-10-29、host だけ）:
- `timeout 1800 sh plan/ws313/tests/mali-compiler-host-test.sh 200`: PASS、25 module、`vector-ubo` の照合 2 本（special.vert・ubo.vert）も
  PASS、encoder 27 語。既存の 24 本の既定の listing は変更前と byte で一致。
- `timeout 1800 sh plan/ws313/tests/mali-compiler-desktop-test.sh 200`: PASS、60 module。**既定の 60 の listing は変更前
  （`build/pc/mali-compiler-desktop.pJVHqW`）と byte で一致**（`build/pc/mali-compiler-desktop.S27kGY`）。`vector-ubo` の照合 7 本も PASS。
- 突然変異（手で、`build/a3-mut/run2.sh`）: 16 揃いの規則の削除・8 揃いの規則の削除・i96 の lane を 64 bit の物に・block の値の
  割り当ての入れ替え・staging の数の 1 減・遠い load を i32 に・offset を見ずに束ねる、の 7 つが全部 FAIL。
- kernel（odroid-c5-keiland、`BUILD=build/a3-k`）exit 0・warning 0、`tests/mali-preload-host-test.c` all checks passed (15)。

**有効にした時に desktop で変わる物（夜の後に決める材料）**: 60 module のうち 5 本（libGL の vertex。fragment の 2 本は uniform
buffer を読むが隣り合う語が無く不変、他の 53 本は uniform buffer を読まない）。命令の語の数（position と varying の program の合計、
NOP を含む）と LOAD の数:

| module | 語（既定 → 有効） | LOAD（既定 → 有効） | 有効の時の内訳 |
| --- | --- | --- | --- |
| libGL-fixed_vert_1 | 384 → 323 | 91 → 30 | i128 11・i96 14・i32 5 |
| libGL-fixed_vert_2 | 498 → 425 | 108 → 35 | i128 13・i96 17・i32 5 |
| libGL-fixed_vert_4 | 726 → 629 | 142 → 45 | i128 17・i96 23・i32 5 |
| libGL-fixed_vert_8 | 1182 → 1037 | 210 → 65 | i128 25・i96 35・i32 5 |
| libGL-fixed_flat_vert | 1182 → 1037 | 210 → 65 | i128 25・i96 35・i32 5 |

listing の registers（position の program の work_registers。preload の r60・r61 があるので 62）は 5 本とも不変、varying の program の register 数は listing に出ないので未確認。spill（TLS）は既定・有効のどちらにも無い（listing に thread local storage の行が無い）。有効にするには
`vk-pipeline.c` の `mali_shader_compile` に flags = `MALI_SHADER_OPTION_VECTOR_UBO` の options を渡す（1 か所。preload の program は
uniform buffer を持たないので関係無い）。実機での LOAD.i96・i128 の動作と、buffer の base が 16 揃いである事は未確認。

### driver（D3）への案（q1675-i04、直していない）

この driver には depth attachment が無いので、FragDepth は今は効果が無い（ZS_EMIT は走るが書く先が無い）。depth を付ける時:
- `writes_depth` の時: Depth/stencil descriptor の word 4 の「Depth source」（bits 26:25）を Shader（3）に（今は vk-frame.c・vk-draw.c が
  Fixed function 2）。DCD Flags 0 の「Pixel Kill Operation」（bits 3:2）と「ZS Update Operation」（bits 5:4）を Force Late（3）に
  （Mesa の pan_earlyzs.c: shader が z を書けば late kill・late update）。値は Mesa 26.2.6 src/panfrost/genxml/v10.xml
  （struct "Depth/stencil"・"DCD Flags 0"、enum "Depth Source"・"Pixel Kill"）と lib/pan_shader.h（pan_depth_source）。
- `writes_sample_mask` か `discards` の時: DCD Flags 0 の「Shader Modifies Coverage」（bit 20）を立てる（Mesa の panvk の
  shader_modifies_coverage = writes_coverage || can_discard || alpha_to_coverage）。今の vk-draw.c は discard で Force Late に
  するだけで bit 20 を立てていない（2026-10-29 の vk-draw.c の FLAG_* に無い事を読んで確かめた。立てるべきかは Mesa に倣った推定で、実機未確認）。
- `reads_sample_mask`: driver の追加は無い（r61 は preload の bit で program の `preload` に入っている）。
- multisample が無い今は coverage は 1 sample で、SampleMask の効果は bit 0 だけ。

## 実装の追加（q1675-i05、P7 と配下 P7-A、2026-10-29、実機未試験）

| 機能 | 中身 | 状態 |
| --- | --- | --- |
| 共通の front end の OpFunctionCall の inline | `src/drivers/gpu/compiler/spirv-inline.inc`（spirv.c が include）。feature `DRV_GPU_SPIRV_FEATURE_INLINE_CALLS`（0x200）の時だけ、関数が 2 つ以上の module を parse の前に書き直す: entry の call を callee の body に置き換え（新しい id、parameter は引数、Function の variable は entry の最初の block へ、戻り値は OpCopyObject）、他の関数と、その id の OpName・OpDecorate を落とす。拒否（ENOTSUP）: 再帰、途中の return、body の無い宣言、loop の header・continue からの複数 block の call。Mali だけが feature を立てる（`MALI_SHADER_SPIRV_FEATURES`）。他の GPU は parse が前と同じ（関数の分け方の変更だけ） | 済み（host のみ） |
| SampleId | front end: fragment の BuiltIn SampleId（18）を feature `DRV_GPU_SPIRV_FEATURE_SAMPLE_ID` の時に `DRV_GPU_SHADER_LOCATION_SAMPLE_ID`（71、flat）。backend: r61 の bits 20:16 を `RSHIFT_AND.i32`（Mesa の bi_load_sample_id_to の 5 bit）。program の `reads_sample_id` | 済み（host のみ） |
| stencil の書き込み（gl_FragStencilRefARB） | front end: BuiltIn FragStencilRefEXT（5014）を feature `DRV_GPU_SPIRV_FEATURE_STENCIL_EXPORT` の時に `DRV_GPU_IR_LOCATION_FRAG_STENCIL`。backend: ATEST の後の ZS_EMIT に stencil の bit 24、source 1 に値（depth と一緒でも一つだけでも）。program の `writes_stencil`（driver は ZSD の Stencil from shader と kill/update の Force Late） | 済み（host のみ） |
| textureGrad | 2D・3D・array・shadow: `TEX_GRADIENT`（derivative enable・LOD bias と clamp 無し、RG の 2 語を書く）で gradient descriptor を作り、sample の staging の最後の 2 語に置いて LOD mode grdesc（6）。cube: Mesa の lower_gradient_cube_map の式で level を ALU で出し（major 軸の選択・商の微分・log2・[-16, 16]）explicit で sample。cube の幅の FAU 語を `lay_out()` が割り当てる（P7-A が見つけた誤り: 割り当てが無く `MOV flow 15` で program が終わっていた。直した） | 済み（host のみ） |

確かめ（host だけ。GPU の証拠ではない）:
- `sh plan/ws313/tests/spirv-inline-host-test.sh 100` → PASS（3 pair が -O の版と模型で一致、早い return の拒否、inline 後の 3 本が spirv-val を通る、1 関数の 72 本は feature の有無で同じ IR）。直した所: 落とした関数の OpName・OpDecorate が残り spirv-val が落ちていた。試験の期待（-O 無しの module の拒否の理由は「not lowered」の時がある）。
- `sh plan/ws313/tests/mali-compiler-host-test.sh 50` → 31 module PASS（新しい grad・grad-cube・sample-id・stencil・stencil-depth・stencil-only の 6 本を含む）、`mali-compiler-desktop-test.sh 50` → 60/60 PASS。既存の listing は `mali-listing-compare.sh` で host 27・desktop 67 とも SAME。
- P7-A が compiler をわざと壊した 5 つの写し（微分の順、cube の -1、sample id の mask、ZS_EMIT の source、cube の MUX）で、どれも試験が FAIL になる事を確かめた。
- 模型の仮定（実機の証拠ではない）: TEX_GRADIENT の descriptor の 2 語は模型の独自の token（hardware の形ではない）、texture の値は入力の決まった関数、r61 の bits 23:21 は不定、ZS_EMIT の source 1 が stencil の値。

未確認・気になる点:
- explicit の LOD の詰め方が Mesa と違う（Mesa は [-16, 16] に clamp して 16 bit を 0 拡張、ここは lod×256 の F32_TO_S32 で負なら上位 16 bit が 0xFFFF）。hardware が上位 16 bit を読むかは不明。既存の listing が変わるので触っていない。負の LOD の textureLod・cube の textureGrad が夜に変なら最初に疑う。
- textureGradOffset は試験していない。

## 判断（推しで進めた所、夜の試験で確かめる）

- **shader の ABI**（`shader.h` の先頭の comment が本体）: FAU は push constants → vertex は viewport の 6 float → buffer の
  64 bit の address。resource table は 0 = texture、1 = sampler、2 = vertex attribute、3 = vertex buffer（compiler が番号を
  決め、driver が descriptor を書く。amdrdna の形に倣った）。panvk は set ごとの table だが、compiler が pipeline layout を
  知らずに済むよう番号を compiler が決める形にした。
- **IDVS は 2 program**（position と varying）を 1 つの code に（Mesa の bi_compile_variant の 2 回 compile と同じ形）。
  v12 の shader_output の 1 program の形は使わない。
- **varying の配置は location L が byte 16L**（vertex と fragment が location だけで一致する。LD_VAR_BUF_IMM の index は
  v10 で 8 bit の byte offset なので L < 16）。
- **vertex の position は screen 空間**（Mesa の nir_lower_viewport_transform と同じ: x/w・y/w・z/w に scale と offset、
  w は 1/w を ±2^15 に clamp）。viewport は FAU。
- **NoPerspective** は vertex で w を掛け、fragment で LD_VAR_SPECIAL の w（1/w）を掛ける（Mesa の
  pan_nir_lower_noperspective と同じ）。
- **fixed function の blend だけ**（blend shader は無い）。BLEND は f32 の v4、最後の BLEND が end。color の無い fragment は
  ATEST と end だけ。
- **texture の大きさ・level 数、storage buffer の長さ**は descriptor を shader で読まず、driver が FAU に書く
  （`size_fau_word`、読む物だけに語を割り当てる）。Mesa は descriptor を読むが、口を簡単にするため。
- TEX の register type の field（bits 27:26）は float（1）にした。Mesa 26.2.6 の va_pack.c は書かず 0 のまま、
  assembler-cases（DDK の出力）は 1。DDK の形に合わせた（夜に TEX が壊れたら最初に疑う所）。
- **定数**は register を持たず、ISA の定数表にある値は表から、他は読む命令ごとに IADD_IMM で作る（register の圧迫を避ける）。
- register は stage の preload の下（compute r0〜r54、fragment r0〜r56、vertex r0〜r58）。r48（blend shader の link）は
  blend shader を使わないので使う。spill は無い（足りなければ ENOSPC）。
- wait は Mesa の va_insert_flow の規則（message の slot の結果を読む・書く前に wait、memory と varying の load は直列、
  ATEST の前 wait0126、BLEND の前 wait、barrier の後 wait）を、前の命令の flow に載せる（載らなければ NOP）。
- log2・exp2 は Mesa の bi_flog2_32・bi_fexp_32 の形（FREXPE・FADD_LSCALE・FLOGD・FEXP）。この 4 命令の意味は文書が無く、
  host の模型の意味は推定（下の「未確認」）。

- **cube の s の上 3 bit に face**（Mesa の build_cube_desc の形）。ハードが bit 31:29 を 001 に戻すことは Mesa の
  comment からの推定。CUBE_SSEL・CUBE_TSEL・CUBEFACE2 の意味（符号を含む face の表、face の番号 +X 0 … -Z 5、
  bit 29 から）は文書が無く、模型の意味は Vulkan の face 選択の表からの推定。夜に cube が壊れたら最初に疑う所。
- **compare の TEX_SINGLE の write mask は R だけ**（write count 0）。Mesa は読む成分の mask。gather は RGBA。
- **storage image の 3 番目の座標は常に layer（W、上位 16 bit）**。IR の image atomic は image の dimension を言わない
  ので、3D の image も slice の 2D array として descriptor を書く約束にした（driver は 3D の image を作らない）。
  共通の front end が texel atomic の image_shape に kind を足せば分けられる（下の「他への案」）。
- **image の座標は 16 bit に切るだけ**（Mesa の上位 bit の範囲外の検査はしない）。範囲外の座標の robustness は無い。
- **image の store の register format は AUTO**（Mesa と同じ）、load は format の型（f32・s32・u32）。
- **spill の値の選び方**: 最も遠い last use。pin するのは 2 回以上 MOVE される値（loop 変数）と述語付きの load の結果
  だけ（front end は loop の前に変数を全部 MOVE で写すので、MOVE 1 回の値は spill してよい）。loop の中では、その
  loop の body で作られた値だけを spill する（loop の前の値は次の周回で register から読まれるため）。述語の branch の
  中では spill も読み直しもしない（その時は ENOSPC）。
- **TLS の大きさ**: 1 thread は 2 の冪で 16 byte 以上、全体は × 2048 thread/core × core 番号の範囲。2048 は Valhall の
  core の thread 数より大きいはずという推定（GPU の THREAD_MAX_THREADS は読まない。読めば節約できる）。base は
  4096 揃え、descriptor は Mesa（v9 以降）に倣い base >> 8 と packed の address mode。stage から取り、clear しない。
- **depth の format**（D16_UNORM → Z16 UNORM 75、D32_SFLOAT → Z32 76、RGBA の順、raw の clump）は Mesa の v9 以降の表
  から。linear filter は公表しない。

## 確かめ（host だけ。GPU の証拠ではない）

- `sh plan/ws313/tests/mali-compiler-host-test.sh 200`（2026-10-29）: PASS。ISA.xml から生成した表が tree の物と一致、
  encoder の既知の語 24 個が Mesa の `valhall/test/assembler-cases.txt` と一致、assembler-cases の 246 語を自前の decoder が
  全部名前どおりに decode、16 module（`plan/ws313/tests/mali-compiler-shaders/` の 3、nvrtx の sm75 の 8、gpu-compute の
  3、compositor の quad の 2）が compile・decode・1 lane の模型と IR の参照で 200 試行ずつ一致（2026-10-29 の最後の回は
  `plan/ws313/tests/mali-compiler-shaders/` が 4 本で 17 module）。
- `sh plan/ws313/tests/mali-compiler-desktop-test.sh 200`（2026-10-29）: PASS、desktop の全 60 module（vert 19・frag 41）。
- 模型が誤りを捕まえる事の確かめ: FSUB の符号・SELECT の入れ替え・wait の削除・varying の offset・viewport の word の
  5 つの突然変異が全部 FAIL になった（2026-10-29、手で流した。script には入れていない）。
- kernel の object としての compile: odroid-c5-keiland の kernel の flag（`build/llvm/bin/clang --target=aarch64-unknown-zedbsd
  … -mgeneral-regs-only -ffreestanding -Os -Wall -Wextra -Werror -fno-builtin -flto`、`make -n` から取った）で
  `va-compile.c`・`va-encode.c`・`va-schedule.c` と共通の `spirv.c`・`optimize.c` が warning 0。link は未実施（mali.mk に未登録）。
- 実機: 未実施。QEMU: 未実施（Mali の machine が無い）。

### 確かめ（q1675-i03、2026-10-29、host だけ。GPU の証拠ではない）

- `sh plan/ws313/tests/mali-compiler-host-test.sh 200`: PASS、21 module（新しく `mali-compiler-shaders/` に
  cube.frag・gather.frag・image.comp・spill.comp）。spill.comp は TLS 256 byte、spill の STORE 60・LOAD 60、loop の中の
  読み直しを含む。
- `sh plan/ws313/tests/mali-compiler-desktop-test.sh 200`: PASS、60 module。desktop の 60 の listing は変更前
  （`build/pc/mali-compiler-desktop.OUaVNH`）と byte で一致、host の既存 17 module の listing も一致（既存の出力は不変）。
- 模型に足した意味: CUBEFACE1・CUBEFACE2_V9・CUBE_SSEL・CUBE_TSEL（推定）、MUX.bit、FMA の clamp、TEX_SINGLE・
  TEX_FETCH・TEX_GATHER の staging の並びを field から読む（数が合わなければ FAIL）・3D の r・layer・face・compare
  （reference <= 値、sampler の比較を LEQUAL とみなす）・gather（推定の 4 texel）、LD_TEX_IMM・LEA_TEX_IMM・ST_CVT
  （image の memory は texel 4 語の raw、format の成分数、足りない成分は 0,0,0,1）、image の atomic、TLS の LOAD・STORE
  （force の hint と tls_bytes の範囲を検査）、FAU special page 1 の TLS pointer。SHADDX の .w0 の source を 1 語の
  read とした（以前は 2 語とみなして誤った wait の FAIL を出した）。
- 突然変異（手で流した、script には無い）: CUBE_SSEL の operand の入れ替え・shadow bit の削除・TLS の offset のずれ・
  image の layer を Z へ・loop の spill の規則の削除の 5 つが全部 FAIL になった。
- `cc -std=c11 -Wall -Wextra -Werror -Isrc -Iinclude tests/mali-vk-desc-host-test.c src/drivers/gpu/mali/vk/vk-desc.c`:
  all checks passed (67)（cube・storage の texture、TLS の descriptor、compare を足した）。
- kernel: `make -j6 ZEDBSD_CONFIG=config/ci/config-odroid-c5-keiland.mk BUILD=build/a2 build/a2/vmunix` exit 0、
  warning 0（compiler と vk の object を含む）。
- 実機・QEMU: 未実施。driver の側（descriptor の実物、copy の layer、TLS、storage image の descriptor）は host の
  試験が無く、build だけ。

## 未確認・やっていない事

- 実機で 1 語も走らせていない。encode は ISA.xml と assembler-cases に合うが、v10 の G310 の実際の挙動は未確認。
- 模型の意味の推定: FREXPE（sqrt の modifier）・FADD_LSCALE・FLOGD・FEXP、FRCP・FRSQ の精度（模型は正確な値）、
  TEX の結果（模型は決まった関数）、CLPER（模型は 1 lane なので微分は 0 でしか試していない）、DISCARD が lane を終える事。
- preload の register 番号（r55〜r61）は Mesa の bi_preload_reg（v9 以降）の値で、v10 の CSF の driver 側の設定（shader
  program descriptor の preload の mask）と合わせる必要がある（D）。
- 無い物（q1675-i03 の後）: buffer・multisample の texture、texture の gradient（textureGrad、TEX_GRADIENT）、
  texel buffer・input attachment、mip level が 2 以上の image、3D の image（driver）、geometry shader、dual source の
  第 2 color、SampleId・stencil の書き込み（front end に無い）、UBO の vector の load の既定での有効化（option の時だけ）、image の座標の範囲外の検査、
  graphics の spill（vk-draw.c の TSD 待ち）、compute の workgroup memory（vk-queue.c が dispatch を拒否する。
  WLS の descriptor が無い。範囲外なので触っていない）、helper の lane の discard の flow（最適化）、register の
  last-use の bit（hint）。
- 未確認（q1675-i03）: cube の face の bit と CUBE_* の意味、compare の write mask R、TEX_GATHER の channel、
  LD_TEX_IMM・LEA_TEX_IMM・ST_CVT の table 4 と storage 用 descriptor、TLS の base >> 8 と packed、thread 数の上限
  2048、D16・D32 の sample。

## 他への案

- **当てた（2026-10-29、P7）**: 下の vk-draw.c の差分（局所の `vertex`・`fragment` を使う形）と vk-pipeline.c の拒否の削除。graphics の spill も TSD に TLS を持つ。
  未確認: fragment の program の TLS を GPU が draw の TSD（IDVS の FRAGMENT_TSD）から読むか、FBD の Local Storage から読むか（今の FBD の Local Storage に TLS は無い）。
  spill する fragment の program が fault したら最初に疑う。
  **資料で確かめた（2026-10-29、q1675-i04、P7）**: v10 の FBD には Local Storage の section が無く、RUN_IDVS の fragment は `fragment_tsd_select` = 0 で TSD_0 から読む（panvk の v10 も TSD_0 だけを書く）。今の driver の形と一致。実機で見る手順は [gpu.md](../gpu.md) §14。desktop の 60 module は spill しない（listing が変わらない）ので今夜の desktop には響かない。
- vk-draw.c（D2）へ（graphics の spill の TLS、q1675-i03）: draw() の TSD の 2 行を次に替え、`uint64_t storage;` と
  `uint32_t tls_bytes;` を宣言に足す。当てたら vk-pipeline.c の「A draw's thread storage has no local storage」の
  拒否を外す（P7）。

  ```c
  	/* The draw's thread storage: the pass's, or one with the programs' thread local storage. */
  	storage = state->storage;
  	tls_bytes = graphics->vertex->tls_bytes;
  	if (graphics->fragment->tls_bytes > tls_bytes)
  		tls_bytes = graphics->fragment->tls_bytes;
  	if (tls_bytes != 0U) {
  		storage = mali_vk_build_thread_storage(build, tls_bytes);
  		if (storage == 0U)
  			return;
  	}
  	mali_cs_move64(&build->cs, MALI_CS_IDVS_TSD, storage);
  	mali_cs_move64(&build->cs, MALI_CS_IDVS_TSD + 4U, storage);
  ```
- 共通の front end（`spirv.c`）へ（q1675-i04、D3 の報告、案だけ）: OpFunctionCall を lower しない。front end は function を 1 つだけ
  受け（2 つ目で「more than one function (calls are not lowered)」の ENOTSUP）、`glslc` を `-O` 無しで通すと helper の関数が残るので
  拒否される。backend（Mali）からは直せない（IR に関数の概念が無い）。案: parse の前に、呼ばれる関数の body を呼ぶ側へ inline する
  段（OpFunctionParameter を引数の id に、OpReturnValue を結果の id に置き換え、再帰は拒否）を `spirv.c` に足す。他の GPU（GHCI・
  amdrdna・nvrtx）にも同じく効く。それまでは desktop の shader を `glslc -O` で作る（今の `shaders.h` は -O の物と見込むが未確認）。
- 共通の front end（`spirv.c`）へ（q1675-i04、案だけ）: fragment の入力の BuiltIn SampleId（18）を、SAMPLE_MASK の feature の時に
  新しい location（例 `DRV_GPU_SHADER_LOCATION_SAMPLE_ID` 71、flat）にすれば、backend は r61 の bits 23:16 を RSHIFT_AND で
  取れる（Mesa の bi_load_sample_id_to は 5 bit の mask）。他の GPU（GHCI）は feature の bit を見て決めるので影響は無い見込み（未確認）。
- 共通の front end（`spirv-compute.inc` の texel atomic、`inst->image_shape = DRV_GPU_IR_IMAGE_TEXEL;`）へ: texel pointer
  の image type の `drv_gpu_spirv_image_shape(type)` を OR すれば backend が 3D と array を分けられる（GHCI は TEXEL の
  bit だけを見るので影響しない見込み、未確認）。今は要らない（Mali は 3D を slice の array として扱う）。

- `platform/arm64/mali.mk` の `ARM64_MALI_SOURCES` に足す file（D か P7）: `src/drivers/gpu/mali/compiler/va-compile.c`・
  `va-encode.c`・`va-schedule.c`。共通の `src/drivers/gpu/compiler/spirv.c`・`optimize.c` も要る（odroid-c5 の build に
  既に入っていなければ）。
- 共通の front end（`src/drivers/gpu/compiler/`）への差分は要らなかった（q1675-i02 の時点。i03 の案は上）。

## 再開点

- 夜の実機の結果で直す（GPU の fault・CS の fault の値と、どの program か）。疑わしい順: wait の flow（message の結果の
  待ち）、preload の register、varying の offset、viewport の FAU の位置、TEX の handle の table と index。
- 余裕があれば（q1675-i03 の後）: graphics の spill（上の vk-draw.c の案）、textureGrad（TEX_GRADIENT）、mip level、
  UBO の vector load、FragDepth（depth attachment と一緒に）、compute の workgroup memory（WLS、範囲外）。
- q1675-i04 の UBO の vector load: 既定は無効。夜の後に有効にするか P7 が決める（上の表）。有効にして壊れたら LOAD.i96 の lane・
  buffer の base の揃いを疑う。
- q1675-i04 の物が夜に壊れたら（depth・sample mask を使う shader は desktop に無い）: ZS_EMIT の source の並び・z の bit、
  coverage の r60 の扱い。
- q1675-i03 の新しい機能が夜に壊れたら疑う順: cube の face の bit（CUBEFACE2 の出力と MUX.bit）、storage の
  texture descriptor（table 4、plane の数）、TLS の descriptor（base >> 8、packed、size の shift）。
- 規約: 明らかな違反（条件の中の関数呼び出し、まとめた確保の検査、三項演算子）は直した。全部の if の前の comment など
  規約の全文の見直しは WS の最後の Phase に残す（va-compile.c が大きい）。
