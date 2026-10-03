<!-- awesome-plan project=zedbsd record=ws075p004 -->

# ws075-p004: i915 の compiler（GLES 2 の核）

Phase ID: `ws075-p004`
Parent: [WS075](../ws.md)
Status: cleared（2026-09-27。実機の vke2 17/17・vke1 4/4・vkx 9/9・vkc 9/9、zdesktop 6/6・zdesktop-x11 6/6）
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

### 増分 3: gl_VertexIndex・gl_InstanceIndex（2026-09-27、080c7e05）

- compiler: vertex shader の BuiltIn VertexIndex・InstanceIndex の input を、属性の後ろの location
  （`I915_SHADER_LOCATION_VERTEX_INDEX` 64・`_INSTANCE_INDEX` 65）の input にする（payload の順で最後、整数のまま読む）。
- 実行器: その input の vertex element は全 component が 0 の element、3DSTATE_VF_SGVS で fetcher が VertexID・InstanceID を
  その element の x に書く（gen80.xml の 3DSTATE_VF_SGVS）。vertex buffer の binding が無い pipeline（index だけで描く shader）も受ける。
- 制限: gl_InstanceIndex は firstInstance を足さない（HW の InstanceID。libGLESv2 は firstInstance 0 で描く）。

### 増分 4: gl_FrontFacing（2026-09-27、bd5a5f42）

- compiler: fragment shader の BuiltIn FrontFacing を location `I915_SHADER_LOCATION_FRONT_FACING`（66）の input にし、補間の
  input には数えない。`compile.c` は payload の r1.0 の bit 31（r1.1 の word の bit 15、裏面で 1）を ASR 31 して NOT する
  （Mesa brw の Gen12 の読み方）。

### 増分 5: 集合体の local と interface、PointSize（2026-09-27、3b55b6db）

- compiler（spirv.c）: local の配列・struct（flat な scalar の並び、`i915_spirv_type_size()`）を parser の slot の store に置く
  （`parser->slots`、変数ごとに `first_slot`・`slot_count`。以前の `comp[16]` の制限を外す）。output も slot（location ごとに 4 つ）。
  input・output の配列と interface block（member の Flat・NoPerspective・Centroid）を location ごとの IR の io に展開
  （`i915_spirv_add_io_run()`）。access chain は local は scalar、I/O は slot（location * 4 + component）で数える
  （`i915_spirv_type_step()`・`i915_spirv_io_map()`）。配列・struct の定数、OpCompositeConstruct・Extract（入れ子）・Insert・
  OpCopyObject。builtin PointSize（output block の member と単独の変数）は IR の location `I915_IR_LOCATION_POINT_SIZE`。
- compile.c: PointSize は VUE header の dword 3（staged も gathered も）。binary の `writes_point_size`。
- 実行器: 3DSTATE_SF の Point Width Source を vertex に（gen120.xml）。

### 増分 6: FragCoord・PointCoord・NoPerspective の input（2026-09-27、a8b82ea3・f7c74bb6）

- EU encoder: V の即値、SIMD16 NoMask の二項、region を明示した operand、stride 2 の destination（Mesa 25.0.7 の brw_reg_type.c・
  brw_compile_fs.cpp を eu-encoding-gen12.h に出典付きで）。
- compile.c: fragment の payload を可変に（r2・r3 の perspective barycentric の後に、使うときだけ linear barycentric 2 register、
  source depth、source w。brw_fs_thread_payload.cpp の順）。FragCoord.xy は Mesa の Gen12.0 の方法（r1.4 の subspan の x・y を
  add(16) で V 0x11001010 と足し、<8;4,1>:uw で x・y を取り出す）に anv の +0.5、z は source depth、w は source w の逆数。
  NoPerspective の input は linear barycentric で補間。PointCoord（location 68）は普通の補間の input。
- 実行器: 3DSTATE_WM の barycentric mode（linear pixel）、PS_EXTRA の source depth・w、SBE の point sprite の enable（入力の bit）と
  原点（左上）、**3DSTATE_CLIP の Non-Perspective Barycentric Enable**（gen80.xml、anv の uses_nonperspective_interp_modes。
  これが無いと linear の barycentric が壊れる。実機の NOPERSP で分かり f7c74bb6 で直した）。PointCoord は VS の varying を要らない。

### 増分 7: texture() の bias・offset と textureLod（2026-09-27、2a8989b2）

- IR: `I915_IR_SAMPLE_BIAS`・`_LOD`（src[2]）、SAMPLE の `component` に定数の texel offset（u は bit 11:8、v は 7:4）。
- compile.c: header（0、dword 2 に offset、dword 3 に r0.3 の sampler state pointer の下位 5 bit を消したもの）、bias か lod、u、v
  を連続の temporary に置いて一つの run で送る（sample_b・sample_l。lower_sampler_logical_send()）。offset・bias・lod の無い
  sample は従来の split send のまま。
- 範囲外: Grad（textureGrad、sample_d）は p005 へ（es300.frag だけが使う）。

### 増分 8: 微分（2026-09-27、09a7bb9a）

- OpDPdx・OpDPdy・OpFwidth と Coarse は coarse、OpDPdxFine は fine（<2;2,0>）、fwidth は |ddx| + |ddy|（vtn_alu.c）。
  y の fine（OpDPdyFine・OpFwidthFine）は exec size 4 の命令が要るので拒む（follow-up）。

### 増分 9: Determinant・MatrixInverse・PackHalf2x16・UnpackHalf2x16（2026-09-27、01b1c633・f6024ee8）

- 行列式は第一行の余因子展開、逆行列は余因子の転置 × 行列式の逆数。half は HF の stride 2 の MOV（brw_lower_pack.cpp）。

### 増分 10: 8・16・10 bit の vertex format（2026-09-27、4add247f）

- state.c の vertex format の表（isl.h の enum isl_format の値、出典付き）: libGLESv2 の渡す R8〜R16G16B16A16 の UNORM・SNORM・
  USCALED・SSCALED・UINT・SINT・SFLOAT と A2B10G10R10。

### 増分 11: local の動的 index の一般化と試験（2026-09-27、01b1c633・a40fc700〜c2e396f8）

- local の配列・行列（列）・vector（成分）への動的 index、二つ目の動的 index は一つの scalar の offset に畳む（候補 64 まで）。
  一度も store していない候補は 0 と読む。
- 試験: vke2 に AGG・MATFN・COORD・DERIV・NOPERSP・PERSP・POINT・VFORMAT、vke1 に TEXOPS（2 level の texture）を足した
  （`generality-shaders/`・`feature-shaders/` の regenerate.py が期待値を計算）。host の IR の interpreter
  （i915-vk-lower-test.c）で agg・matfn・coord・vformat を 4096 画素ずつ照合。
- test build の kernel が 16 MiB の上限を 40 KiB 越えたので、規則で書ける期待値（COORD・DERIV・NOPERSP・POINT・VFORMAT）は
  kernel の試験が計算する（表を持たない）。**残りの余裕は約 40 KiB**。

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
| 増分 3・4: build・host（lower・spirv・compile・pipe）・survey | PASS、PASS、33 module（compiler と一致） |
| 実機（i915、5330）の回帰 vkx・vke2・vkc（増分 1〜4 の後。vke2・vkc は FrontFacing の後の tree、vkx はその直前か直後の tree） | 3 つとも **PASS 9/9**（`build/ws075-p004/`） |
| 増分 5〜11: build（test build、-Werror） | PASS、warning 0 |
| host: `run-vk-host-tests.sh "lower spirv compile pipe"`（lower に ws075-p004 の 4 shader × 4096 画素） | PASS |
| host: gentool（`BRW_TOOLS=/home/awe/p014-c/mesa/build-asm/src/intel/compiler plan/ws031/tests/run-vk-gentool-test.sh`、新しい shader を含む） | PASS（Mesa 25.0.7 の brw_asm・brw_disasm が全命令を受け、再 assemble が一致） |
| survey | 122 module、不足 29（33 から）。p004 の範囲の不足 0（残りは p005 以降と Grad）。compiler の拒否と全一致 |
| style-check | spirv.c 29 → 9、compile.c 14 → 13、generality.c 17 → 16、features.c 18 → 18、他は同じか 0 |
| 実機 vke2（`plan/ws075/tests/test-hw.sh vke2 build/ws075-p004/hw-vke2`） | 1 回目 NOPERSP FAIL（linear の値が壊れる）→ CLIP の bit を直して **PASS 17/17** |
| 実機 vke1・vkx・vkc | **PASS 4/4**（TEXOPS を含む）・**9/9**・**9/9** |
| 実機 capture: zdesktop、zdesktop-x11（GLX の zgears） | **6/6**・**6/6**（`build/ws075-p004/hw-zdesktop`・`hw-x11`、画像は `build/ws031-shots/ws075-p004-20260927-*.png`） |
| QEMU（Venus） | 未実施（i915 の実行器だけの変更） |
| egltest・glxtest の capture の scenario | 未実施（p005 で足す。p004 の機能の画素は vke2・vke1 の step で実機が確かめた） |

## 後回し（follow-up）

- textureGrad（sample_d）、OpDPdyFine・OpFwidthFine（exec size 4 の group の encoder）: p005。
- loop の中で初めて store した local を loop の後で読む shader は拒む（前からの制限。loop の前に store の先を前もって走査して
  carry すれば受けられる）。
- i915 の test build の kernel は 16 MiB の上限まで約 40 KiB。試験の表をさらに足すときは期待値を規則で計算する。
- gl_InstanceIndex の firstInstance（増分 3 の制限）。

