<!-- awesome-plan project=zedbsd record=ws039-p007 -->

# ws039-p007: shader の compiler（SPIR-V → GFX10、RDNA1、Navi10 = gfx1010）

Status: in-progress（2026-10-15 P5 着手・一時停止の後、同日夕に q1203 / q1203-i01 で P5 が再開。2026-10-16 q1214 / q1214-i01 で P5 が段 D の続き・TEXTURE の残り・特殊な入出力。実機前）
Disposition: normal
Parent: [WS039](../ws.md)
Queue: Q1 の割当（2026-10-15、P5: 「WS039 の shader の compiler（ws039-p007）」。ユーザーの決定 2026-10-15「担当を 2 人（driver と compiler）で並行してよいです。」）
依存: なし（host で作って確かめる）。GPU で走らせる確かめは driver（P3）の compute・draw の投入の口に依存する。
実行者: P5（phase-runner）

## 1. 判断

- 置き場所: `src/drivers/gpu/amdrdna/compiler/`（kernel の中、Guardrail の「SPIR-V の compile は kernel 空間」）。front・optimizer・IR の型は共有の `src/drivers/gpu/compiler/`（i915・V3D・nvrtx と共有）をそのまま使い、変えない。amdrdna は backend だけを持つ。
- 形は nvrtx の SM75（ws037-p008）に倣う: 命令の選択 → register の割り当て → 待ち（waitcnt）と hazard の印 → encode。ただし AMD は命令を 1 度 machine の命令の列（struct の配列）に作ってから、待ち・hazard・分岐の pass を通して最後に encode する（SM75 の「語から読み戻す」形より単純）。
- 出典の扱い（Guardrail「MIT の出典のデータの引用」、2026-10-15 ユーザー）: Mesa の ACO・RADV・`src/amd/registers` の encode・opcode・hazard の表・register の欄は**データ**として使い、ブロックごとに出典（path、Mesa の版、関数か表の名前、分かれば行）を comment に書く。copyright・MIT の notice は付けない。code の構造は写さない（ACO は C++ の SSA の大きな compiler で、構造は zedBSD の scalar IR に合わない）。encode の正しさは host の `llvm-mc-19`（LLVM の AMDGPU の assembler、gfx1010 を encode・disassemble できる）で照らす。LLVM の code は取り込まない（試験の道具として呼ぶだけ）。

### 出典（Mesa 25.0.7、tarball の SHA-256 `592272df3cf01e85e7db300c449df5061092574d099da275d19e97ef0510f8a6`。P1 の `build/mesa-tools/` から P5 の `build/mesa/` へ写して展開、commit しない）

| file | SPDX | SHA-256 | 取るデータ |
| --- | --- | --- | --- |
| `src/amd/compiler/aco_opcodes.py` | MIT | `27278b88f096658321a2631e873aed9b28b8355e9db04b87798f483194765b49` | GFX10 の opcode の番号（SOP1/2/C/K/P、SMEM、VOP1/2/3、VINTRP、MUBUF、MIMG、DS） |
| `src/amd/compiler/aco_assembler.cpp` | MIT | `9faca4439b09aa0587c300e1667e9e515c90f4ba3b5ccceb357584e921a63c04` | 各形式の欄の bit の位置（GFX10）、VOP1/VOP2 を VOP3 で書く時の opcode の足し算、分岐の offset 0x3f の不具合（`fix_branches_gfx10`） |
| `src/amd/compiler/aco_insert_NOPs.cpp` | MIT | `f25ed0cf369737118e4873472af7cbe3628da057851e07f7b82918f2b1e79409` | GFX10 の hazard（`handle_instruction_gfx10`: VMEMtoScalarWrite、SMEMtoVectorWrite、VcmpxExecWAR、LdsBranchVmemWAR、NSA）と緩和の命令 |
| `src/amd/compiler/aco_insert_waitcnt.cpp` | MIT | `5171bd9899f00a6fc3e8f811dd36f9fc211296a4fc197b59c2f0c78f4766a749` | 待ちの counter の種類（vm・exp・lgkm・vs）と GFX10 の s_waitcnt の欄 |
| `src/amd/compiler/README-ISA.md` | （Mesa の既定 MIT、`docs/license.rst`） | `6698da9ade24f469cb2b1046a529d8663fece6ea334ee31cf57dc857a6ca5251` | GFX10 の hazard の説明、DS は m0 を使わない、DLC/GLC |
| `src/compiler/nir/nir_lower_idiv.c` | MIT（file の notice） | `5882ef003f429104ce25a3c6ddb8607b9330fe6d02b4be5197d90e90dc448a1c` | 整数の除算の手順（`emit_udiv`: 浮動小数の逆数 × 4294966784.0 → Newton 1 回 → 2 回の補正）と定数 0x4f7ffffe（2026-10-15 q1203 で追加） |
| `src/amd/compiler/aco_lower_to_hw_instr.cpp` | MIT | 監査の表 | discard の早い終わり（exec 0 で null の export done vm、s_endpgm） |
| `src/amd/common/nir/ac_nir_prerast_utils.c` | MIT | 監査の表 | 頂点の export の順（位置が先、最後の位置に done、Navi1x は POS0 に valid mask） |
| `src/amd/vulkan/radv_shader_args.c` | MIT | `a21869c49a640b9d3a4cd6ef0e036faf7dfcbc6137348b0cb7d1c78872a2ecc2` | 入力の register の並び（CS: user SGPR → TGID x,y,z、VGPR v0..v2 = local id。legacy VS: v0 = vertex id、v3 = instance id。PS: user SGPR → prim mask、VGPR は SPI_PS_INPUT_ENA の bit の順） |
| `src/amd/common/ac_descriptors.c` | MIT | 監査の表 | 生の buffer の descriptor（V#）の 4 語 |
| `src/amd/registers/gfx10.json`・`gfx10-rsrc.json` | （Mesa の既定 MIT） | `f3435695…7d48`（gfx10.json） | PGM_RSRC1/2、SPI_PS_INPUT_ENA、COL_FORMAT、SPI_PS_INPUT_CNTL、V# の欄 |

license の監査の表は p007 の終わりに `plan/ws039/amdrdna-compiler-license-audit.md` に作る（file ごとの SHA-256・SPDX・取ったデータの範囲）。

## 2. 形

```
SPIR-V ──drv_gpu_shader_parse──▶ scalar IR ──drv_gpu_shader_optimize──▶ IR
   ──gfx10 lowering──▶ machine の命令の列（VGPR・SGPR は物理、label で分岐）
   ──gfx10 wait──▶ s_waitcnt・hazard の緩和・分岐の offset（0x3f の回避）
   ──gfx10 encode──▶ 32/64 bit の語の列 ＋ s_code_end の詰め物 ＋ 結果の記述（register の数・入力・出力・資源の表の配置）
```

| file | 中身 |
| --- | --- |
| `amdrdna/compiler/gfx10-isa.h` | データ: opcode、operand の番号、欄の位置、export の先、ABI の定数（出典の comment つき） |
| `amdrdna/compiler/gfx10-encode.[ch]` | machine の命令の struct と 1 命令の encode、読み書きする register の範囲 |
| `amdrdna/compiler/gfx10-compile.[ch]` | IR → 命令の選択、register の割り当て、公開の API |
| `amdrdna/compiler/gfx10-wait.[ch]` | 待ちの挿入、hazard の緩和、分岐の解決 |
| `amdrdna/compiler/gfx10-program.c` | driver 向け: PGM_RSRC1/2 など register の値、生の buffer の V# |

## 3. program の形（driver との約束、P3 とすり合わせる）

すべて **wave32**（CS: DISPATCH_INITIATOR の CS_W32_EN、VS: VGT_SHADER_STAGES_EN の VS_W32_EN、PS: SPI_PS_IN_CONTROL の PS_W32_EN を driver が立てる）。頂点は **legacy の VS**（NGG にしない。Navi10 は両方使える。legacy は primitive の export が無く単純）。scratch（spill）は使わない（register が足りなければ拒む）。

### 3.1 user SGPR と入力の register

| stage | SGPR | VGPR |
| --- | --- | --- |
| すべて | s[0:1] = この program の**資源の表**の GPU の仮想 address（64 bit、16 byte 揃え） | — |
| CS | user SGPR 2 個。s2・s3・s4 = workgroup の id x・y・z（RSRC2 の TGID_X/Y/Z_EN を立てる）。TG_SIZE は使わない | v0・v1・v2 = workgroup の中の id x・y・z（TIDIG_COMP_CNT = 2） |
| VS | user SGPR 5 個。s2 = base vertex（indexed は vertexOffset、そうでなければ firstVertex）、s3 = first instance、s4 = instance rate の mask（bit L: location L を instance の番号で読む） | v0 = vertex id（base を含まない）、v3 = instance id（base を含まない）（VGPR_COMP_CNT = 3） |
| PS | user SGPR 2 個。s2 = prim mask（hardware が user SGPR の後に置く、v_interp の前に m0 へ） | SPI_PS_INPUT_ENA = ADDR の bit の順に詰める: PERSP_CENTER（i, j）は常に、LINEAR_CENTER は noperspective の入力がある時、POS_X/Y/Z/W_FLOAT は gl_FragCoord を読む時、FRONT_FACE は gl_FrontFacing を読む時 |

gl_VertexIndex = v0 + s2、gl_InstanceIndex = v3 + s3（driver は VGT_INDX_OFFSET を 0 にする）。gl_FragCoord.w は 1 / POS_W（RADV と同じ）。pixel の中心は x.5（SPI_BARYC_CNTL の既定）。

### 3.2 資源の表（s[0:1] が指す、driver が draw・dispatch ごとに書く）

compiler が program ごとに配置を決め、結果の記述に byte の offset と (set, binding) を入れる。

| 順 | 中身 | 大きさ |
| --- | --- | --- |
| 1 | push constants（0 から push_bytes、16 byte に切り上げ） | push_bytes |
| 2 | storage buffer k の V#（生、`amdrdna_gfx10_raw_buffer_descriptor`: base、stride 0、num_records = byte 数、32_FLOAT、OOB_SELECT = RAW） | 16 × storage_count |
| 3 | uniform buffer j の V#（生、同じ形） | 16 × uniform_count |
| 4 | sampled image k の T#（32 byte）と S#（16 byte） | 48 × texture_count |
| 5 | VS だけ: 頂点の location L の V#（typed: base = buffer + binding の offset + attribute の offset、stride = binding の stride、num_records = 頂点の数、FORMAT は VkFormat から、DST_SEL で無い成分は 0・0・0・1、OOB_SELECT = STRUCTURED） | 16 × (最大の location + 1)（使う location だけ書けばよい） |

- gl_NumWorkGroups は共有の front の約束どおり set `0xFFFFFFFF` binding 0 の storage（3 語）として表に入る。
- 頂点の fetch は shader の中で `buffer_load_format_xyzw … idxen`（index は location ごとに vertex か instance）。format の変換は V# が持つので、program は pipeline の vertex input に依らない。

### 3.3 出力

- VS: `exp pos0`（x y z w、Navi1x の約束で valid mask を立てる）、point size があれば `exp pos1`（x = size、PA_CL_VS_OUT_CNTL の USE_VTX_POINT_SIZE・VS_OUT_MISC_VEC_ENA）、最後の位置の export に done。その後 location L の varying を `exp param L`（param の番号 = location、疎でよい）。driver: VS_EXPORT_COUNT = 最大の location（無ければ NO_PC_EXPORT）、POS_FORMAT は 4COMP。
- PS: 入力 location L は attribute L（SPI_PS_INPUT_CNTL_L の OFFSET = L、flat は FLAT_SHADE）。色 location L は `exp mrt L`（32_ABGR、4 成分の float の bit そのまま）。最後の export に done・vm。何も書かない program は `exp null` done vm。discard で全 lane が消えたら途中で `exp null off off off off done vm`・`s_endpgm`。
- 結果の記述に: stage、code と語の数、VGPR・SGPR の数、user SGPR の数、LDS の byte、local size、PS の input ena と flat・noperspective の mask、色の target の mask、VS の param の mask と point size、資源の表の配置。`amdrdna_gfx10_program_registers()` が RSRC1/RSRC2・SPI_PS_INPUT_ENA/ADDR・SPI_SHADER_COL_FORMAT・Z_FORMAT・SPI_VS_OUT_CONFIG・POS_FORMAT・PA_CL_VS_OUT_CNTL の該当の bit・SPI_PS_INPUT_CNTL_0..31 の値を作る。
- RSRC1: FLOAT_MODE = 0xf0（f32・f16/64 の denormal を保つ、IR の参照と同じ IEEE の f32）、IEEE_MODE 0、DX10_CLAMP 1。VGPRS = ceil(n / 8) − 1（wave32）。
- code の後に `s_code_end` を 3 cache line（48 語）詰める（GFX10 の命令の prefetch が 3 line 先まで読む、`ac_align_shader_binary_for_prefetch`）。

## 4. lowering の要点（正しさが先）

- IR の値は全部 VGPR（lane ごと）。uniform の値（push・UBO の語・descriptor）は SGPR に読み、v_mov で VGPR へ。Boolean は全 1 か 0 の VGPR、比較は `v_cmp` → VCC → `v_cndmask_b32 v, 0, -1, vcc`。
- loop（IR の LOOP_BEGIN…LOOP_END）: 入る時の exec を SGPR に保存、終わりで `v_cmp_ne_u32 vcc, 0, cond`、`s_and_b32 exec, exec, vcc`、`s_cbranch_execnz 先頭`、`s_mov_b32 exec, 保存`。抜けた lane の register は書かれないので loop 変数はその lane の最後の値を保つ。loop の前に作られ中で読まれる値は loop の終わりまで register を保つ。
- KILL（fragment）: 生きている lane の mask（SGPR）から消し、exec を WQM（implicit LOD の sample がある時）か生きている mask にし、生きている lane が 0 なら早い終わりへ。storage への書き込みは生きている lane だけ（exec を一時的に絞る）。
- texture: T#・S# を prologue で SGPR に読む。座標は連続の VGPR に写して（NSA は使わない）`image_sample`（fragment）・`image_sample_lz`（頂点・compute）・`_l`・`_b`・`_o`・`image_load_mip`。結果は 4 つの連続の VGPR。
- 待ち: 読み込み（SMEM・VMEM・DS）の行き先の register、export が読む register を「保留」にし、それを読む・書く命令の前に該当の counter を 0 まで待つ。後ろへの分岐の前は全部を待つ。barrier・fence の前は vmcnt・lgkmcnt・vscnt を 0 に。
- hazard（GFX10.1、`handle_instruction_gfx10`）: VMEM/DS が読んだ SGPR（exec・m0 を含む）を SALU/SMEM が書く前に `s_waitcnt_depctr 0xffe3`、SMEM が読んだ SGPR を VALU が書く前に `s_mov_b32 null, 0`、VMEM と DS の間に分岐があれば `s_waitcnt_vscnt null, 0`、非 VALU が exec を読んだ後の VALU の exec の書き込み（v_cmpx）は使わない、m0 の書き込みの後の v_interp の前に `s_nop 0`、分岐の offset が 0x3f なら分岐の後に `s_nop 0`。label（loop の先頭）では全部の状態を「あり」として扱う。

## 5. 確かめ（host）

1. **既知の語**: compiler の encode 関数で作った命令を、同じ命令の文字列を `llvm-mc-19 -arch=amdgcn -mcpu=gfx1010 -show-encoding` で encode した語と bit まで比べる（試験の中で llvm-mc を呼ぶ）。
2. **disassemble**: compile した全 program の code を `llvm-mc-19 -disassemble` に通し、全ての語が gfx1010 の有効な命令になること、試験の decoder の命令の名前の列と llvm の名前の列が一致すること。
3. **interpreter**: 試験の中の GFX10 の wave32 の interpreter（必要な命令だけ、encode の語を自分で decode）で走らせ、IR の参照の interpreter と比べる。32 lane に別々の入力を入れ、loop の exec の扱いも見る。待ちの検査（保留の register を待たずに読む・書く）と hazard の検査も interpreter が行う。
4. **desktop の全 shader**: `userland/desktop/**/shaders.h` の全 module を compile し、200 入力ずつ IR の参照と一致すること（nvrtx の `nvrtx-sm75-desktop-test.sh` と同じ形）。
5. build: `config/ci/config-amd64.mk` の vmunix（P3 の `CONFIG_DRIVER_PCI_AMDRDNA` の行の下に compiler の file を足す、Q1 の指示 2026-10-15）、warning 0。P3 の行が main に入るまでは kernel の flag で単体の compile を確かめる。

## 6. 段

| 段 | 内容 | 状態 |
| --- | --- | --- |
| A | encode ＋ compute の最小（算術・比較・変換・特殊関数・storage・push・system value）＋ 1〜3 の試験 | 済（2026-10-15、host） |
| B | loop・除算・atomic・shared・barrier・fence | 済（2026-10-15、host） |
| E | 頂点・fragment（頂点の fetch、補間、export、sample、KILL、UBO） | 済（2026-10-15、host。TEXTURE は SAMPLE・BIAS・LOD・LD の 4 種） |
| F | desktop の全 shader の差分の試験（5 節の 4） | 済（2026-10-15、host、44 module × 200 回） |
| D | 速さ（待ちを register ごとの counter に、uniform の値を SGPR のまま使う） | 済（2026-10-16、host。7.9） |
| G | TEXTURE の残りの message（2D）、POINT_COORD・PRIMITIVE_ID・SECOND_COLOR、whole quad の storage 書き込み | 済（2026-10-16、host。7.9。3D・cube・array は IR に次元が無く拒む） |

## 7. 記録

### 7.1 2026-10-15 P5（一時停止のラップアップ）

済み:
- 設計（1〜6 節）。program の形（3 節）は P3 とまだすり合わせていない。
- `src/drivers/gpu/amdrdna/compiler/gfx10-isa.h`: operand の番号、形式の固定の bit、使う opcode（SOP・SMEM・VOP1/2/3/C・VINTRP・DS・MUBUF・MIMG）、export の先、waitcnt の欄、hazard の値、ABI の定数。ブロックごとに出典（Mesa 25.0.7 の path・関数・行）。`gcc -std=c89 -Wall -Wextra -Werror -fsyntax-only` で通る。まだ誰も include しない。
- llvm-mc-19（host の `/usr/bin/llvm-mc-19`）で gfx1010 の encode を約 90 命令確かめた（VOPC の番号、VOP3 の VOPC・VOP2・VOP1 の足し算、VINTRP の prefix 0xc8、exp の target（null 9・pos 12・param 32）、DS・SMEM・MUBUF・MIMG の欄、s_waitcnt の欄、s_waitcnt_depctr 0xffe3、s_waitcnt_vscnt null）。wave32 の Boolean は `v_cndmask_b32_e64 v, 0, -1, vcc_lo`（e32 は src1 が VGPR だけ）。
- 調べの結果（Mesa の data）: ACO の GFX10.1 の hazard の一覧（`aco_insert_NOPs.cpp` の `handle_instruction_gfx10` と `resolve_all_gfx10`、GFX10.3 は無し）、分岐 offset 0x3f、legacy VS の export の順（位置が先、Navi1x は POS0 に valid mask、`ac_nir_prerast_utils.c`）、discard の早い終わり（`aco_lower_to_hw_instr.cpp`: exec 0 で `exp null off,off,off,off done vm`、s_endpgm）、RADV の入力の register（CS: TGID の SGPR、v0..v2。legacy VS: v0 vertex id、v3 instance id。PS: SPI_PS_INPUT_ENA の順）、prefetch 3 line、DS cmpst は data0 = 比べる値・data1 = 書く値、buffer cmpswap は vdata = 書く値・vdata+1 = 比べる値。
- desktop の shader の IR の使い方（44 module）: loop 無し、KILL は libGL の 2 つ、derivative 無し、TEXTURE の op 無し（SAMPLE だけ）、頂点 buffer の入力が多い（4 成分まで、location 0〜5）、UBO は libGL、push は 16〜96 byte。

### 7.2 再開点

1. 準備: Mesa 25.0.7 の tarball を `build/mesa/` に（P5 の worktree の物は P1 の `/home/awe/zedBSD-worktrees/p1/build/mesa-tools/mesa-25.0.7.tar.xz` の複写、SHA-256 `592272df3cf01e85e7db300c449df5061092574d099da275d19e97ef0510f8a6`、署名は公開鍵が無く未検証）。展開した範囲は `src/amd/{compiler,common,registers,vulkan}`・`docs/license.rst`。GFX10 の opcode の表は `aco_opcodes.py` を正規表現で読む script（scratchpad に作った、commit していない）で取った: 行の `op(...)` の gfx10 の値、無ければ前の世代の値を引き継ぐ。
2. 次: `gfx10-encode.[ch]`（2 節の machine の命令の struct、形式ごとの encode、読み書きする register の範囲）→ 試験 `plan/ws039/tests/` の既知の語（llvm-mc と比べる）→ `gfx10-compile.[ch]` の段 A（compute）→ `gfx10-wait.[ch]` → interpreter と IR の参照 → 段 B・E・F（6 節）。
3. 規則（2026-10-15 ユーザー、Guardrail）: MIT でも code・comment・構造は写さない。data（encode・opcode・hazard の表・register の値）だけを使い、ブロックごとに出典を書く。copyright・MIT の notice は付けない。
4. vmunix.mk: P3 が `CONFIG_DRIVER_PCI_AMDRDNA` の行を入れ、その下に P5 が compiler の file を別の commit で足す（Q1 の指示）。それまでは kernel の flag で単体の compile。
5. 未実施: encode・lowering・試験・build・P3 との program の形のすり合わせ、license の監査の表。

### 7.3 2026-10-15 P5（q1203 / q1203-i01、再開）

- `gfx10-isa.h` を出典の規則（Guardrail「MIT・GPL の出典のデータの引用」、design 13 節）に合わせた: file の先頭の出典（Mesa の版・SHA-256）を消し、block ごとの複数行の「Values referenced: Mesa 25.0.7 <path> (<関数・表の名前、行>)」にした。形式の印は `AMDRDNA_GFX10_FORM_*`（独自の語）、出力先・待ち・hazard の値も独自の名前にした。命令の opcode・register の名前（`AMDRDNA_GFX10_V_ADD_F32` など ISA の mnemonic から作った名前）は、ユーザーの判断（AMD の公開の ISA の mnemonic が symbol の写しに当たるか）を Q1 が確かめ中なので、決まるまで今の形（2026-10-15 Q1）。DPP の値は使わないので消した。
- `gfx10-encode.[ch]`: 機械の命令の record（`struct amdrdna_gfx10_inst`）、形式ごとの書き出し（scalar 2/1/比較/定数/control/分岐、scalar load、vector 1/2/比較/wide（短い形に収まらなければ wide へ自動で）、補間、shared、buffer、image、export）、読み書きする register の範囲と結果を待つ counter（`amdrdna_gfx10_describe`）。欄の位置は block ごとに `aco_assembler.cpp` の関数名を出典に書いた。
- 試験 `plan/ws039/tests/amdrdna-compiler-host-test.sh`（`gfx10-encode-test.c`・`gfx10-compare-words.py`）: 85 個の命令を encode し、同じ命令の文を `llvm-mc-19 -mcpu=gfx1010 -show-encoding` で encode した語と比べる → **85 一致、0 差**。register の記述の 5 例も確かめる。
- kernel の flag（clang `--target=x86_64-unknown-none`、amd64 の `AMD64_CFLAGS` と `-Wdeclaration-after-statement`、gcc `-std=c99 -fsyntax-only` も）で warning 0。
- `gfx10-compile.[ch]`（段 A・B、compute）: IR の値は全部 VGPR（最小の空きを定義で取り、最後の読みの後で返す。loop の前の値で loop の中で読むものは loop の終わりまで）。descriptor は prologue で SGPR に読む（storage・uniform の V#、4 個ずつ）。push と UBO の語は scalar の一時（8 個を順に）へ読んで v_mov。比較は VCC → v_cndmask で全 1/0。loop は exec を保存 → 終わりで `s_and_b32 exec, exec, vcc` と `s_cbranch_execnz`。述語つきの storage・shared・atomic は `s_and_saveexec_b32` で lane を絞る。除算は `nir_lower_idiv.c` の `emit_udiv` の手順（出典を Procedure referenced で書いた）、符号つきは絶対値で割って符号を xor と引き算で付ける。storage の load は GLC・DLC（README-ISA の L0/L1 の節）。atomic は使われる時だけ GLC で古い値を返す。shared は DS（M0 は使わない）。barrier・fence は s_waitcnt・s_waitcnt_vscnt の後に s_barrier。資源の表の配置は push → storage の V# → uniform の V#（`struct amdrdna_gfx10_program` に offset）。
- `gfx10-wait.[ch]`: 各命令の前に、読み書きする register が待つ counter を 0 まで待つ（export の読みは export の counter）。GFX10.1 の 4 つの hazard（VMEMtoScalarWrite・SMEMtoVectorWrite・VcmpxExecWAR・LdsBranchVmemWAR、`aco_insert_NOPs.cpp` の手順を Procedure referenced）を破る命令を置く。label と分岐の前では全部を待ち、開いている hazard を全部破る（どの道から label に来ても空の状態）。M0 の scalar 書きの直後の補間の前に s_nop（zedBSD の用心）。組み立て: 分岐の距離の解決、距離 0x3f の分岐の後に s_nop を入れて並べ直す、最後に s_code_end で prefetch の 3 line ＋ 16 語の境界まで詰める。
- 試験（host、`amdrdna-compiler-host-test.sh` に追加）: `gfx10-machine.[ch]`（GFX10 の wave32 の interpreter。語を自分で decode する。memory の命令の結果の register を counter の待ちの前に読み書きしたら FAIL、4 つの hazard と距離 0x3f の分岐も FAIL）、`gfx10-reference.[ch]`（IR を workgroup の全 invocation で lockstep に走らせる参照）、`gfx10-compute-test.c`（同じ乱数の入力で両方を走らせ storage を語ごとに比べる。sin・cos を含む program だけ相対 1e-5 の許容）、`shaders/*.comp` 10 個（ids・float・integer・loop・uniform・division・atomic・shared・trig・half）。各 program の code を `llvm-mc-19 -disassemble` に通し、全語が gfx1010 の命令として decode されること。
- 結果（2026-10-15、`plan/ws039/tests/amdrdna-compiler-host-test.sh`）: known words 86 一致 0 差、compute 10 module × 16 回 PASS（3D の workgroup・3 wave の shared と barrier・分岐の分かれる loop を含む）、10 program の disassemble に誤り無し。試験が効くことの確かめ: compiler の FMAX→FMIN・ASR→SHR の取り違え、待ちの省略をわざと入れると FAIL になった（戻した）。除算は逆数が 1 ulp 小さい時も PASS（1 ulp 大きいと 3 などで外れる。Mesa・LLVM と同じ前提で、hardware の `v_rcp_iflag_f32` がそれより大きくならないことに頼る。実機で確かめる項目）。
- kernel の flag（zedBSD の target clang `x86_64-unknown-zedbsd`、`AMD64_CFLAGS`＋`-Wdeclaration-after-statement`）で 3 file とも warning 0。
- ユーザーの判断（2026-10-15、Q1 経由のクリック「mnemonic は使ってよい」）: AMD の公開の RDNA1 ISA の命令名（s_mov_b32 など）は code の名前・comment に使ってよい。Mesa（ACO）の source の symbol（aco_opcode の enum 名・関数名・型名）は写さない。→ opcode・register の名前（`AMDRDNA_GFX10_V_ADD_F32`・`AMDRDNA_GFX10_VCC_LO` など）はこの規則で確定。

### 7.4 段 E（2026-10-15 q1203、続き）

- `gfx10-compile.c`: 頂点（user SGPR 5 個、v0・v3、`v_add_nc_u32` で base を足す、location ごとに V# を SGPR へ・`s_bitcmp1_b32`＋`s_cselect_b32`＋`v_cndmask` で instance の番号を選び `buffer_load_format_xyzw idxen`、出力は予約した VGPR に集めて最後に `exp pos0`（valid mask）・`exp pos1`（point size）・`exp param L`）、pixel（SPI_PS_INPUT_ENA の順に v0 から: perspective i/j は常に、linear・位置・face は使う時、M0 = s2、`v_interp_p1/p2`・flat は `v_interp_mov p0`、gl_FragCoord.w は `v_rcp_f32`、face は `0 < face`、色は予約した VGPR から最後に `exp mrt L`（最後に done・vm）、無ければ `exp null`）、KILL（生きている lane の SGPR、`s_andn2_b32`→`s_cbranch_scc0` で早い終わり（exec 0 で `exp null done vm`・`s_endpgm`）、exec を生きている lane に、loop の出口でも絞り直す）、texture（T#・S# を prologue で SGPR へ、address を連続の VGPR に offset・bias・u・v・level の順で写す、fragment は whole quad（`s_wqm_b32`）で `image_sample`(`_b`/`_o`)、他の stage は `_lz`、`_l`、texelFetch は `image_load_mip unorm`、結果は 4 つ連続の VGPR に）。whole quad の fragment での storage 書き込みはまだ拒む。
- `gfx10-program.[ch]`（新規）: `amdrdna_gfx10_program_registers()`（RSRC1 相当: VGPR の block・FLOAT_MODE 0xf0・DX10_CLAMP・VS の VGPR_COMP_CNT 3、RSRC2 相当: USER_SGPR・TGID_X/Y/Z・TIDIG_COMP_CNT 2・LDS_SIZE（512 byte 単位）、VS_OUT_CONFIG・POS_FORMAT・PA_CL_VS_OUT_CNTL の point size の bit、SPI_PS_INPUT_ENA（ADDR も同じ値を書く）・COL_FORMAT 32_ABGR・Z_FORMAT 0・PS_IN_CONTROL の NUM_INTERP と PS_W32_EN・SPI_PS_INPUT_CNTL の OFFSET と FLAT_SHADE、discard の有無（DB_SHADER_CONTROL の kill は driver が立てる））、`amdrdna_gfx10_raw_buffer_descriptor()`（word3 = 0x31016fac、試験で照合）、`amdrdna_gfx10_vertex_descriptor()`（stride・record 数・format・DST_SEL 0,0,0,1、OOB_SELECT STRUCTURED）。struct の field の名前は register の mnemonic を使わず役割の語（counts・inputs・pixel_input_enable など）。
- 試験: interpreter に export・補間（`fmaf(P10, i, P0)`・`fmaf(P20, j, D)`）・typed fetch（GFX10_FORMAT 20/22/56/62/64/72/74/75/77、DST_SEL）・image（`gfx10-texture.[ch]`: level 0 の linear 2D、RGBA8 unorm・float4、point・bilinear、wrap・clamp。implicit LOD の sample で exec が whole quad でなければ FAIL）。参照に graphics の入出力・KILL・sample（同じ texture の helper を使う）。`gfx10-graphics-test.c`: 頂点は 40 頂点 × 2 instance（2 wave、location ごとに乱数の format と stride、instance mask）、pixel は 1 wave（乱数の coverage・barycentric・位置・face・4 primitive の attribute）、export を語ごとに比べ、生きている lane の集合・最後の export の done/vm・position 0 の valid mask・register の値を確かめる。shader は `basic.vert`・`sparse.vert`・`textured.vert`・`basic.frag`・`discard.frag`・`textured.frag`。
- 結果（2026-10-15）: `amdrdna-compiler-host-test.sh` PASS（known words 86/86、compute 10、graphics 6 module × 16 回、16 program の disassemble に誤り無し）。試験が見つけた不具合: SAMPLE_LOD の u・v を連続の値と思っていた（`.yx` で外れた）→ 直した。わざとの誤り（i・j の取り違え、base vertex の省略、vertex と instance の取り違え、`s_wqm_b32` の省略）は FAIL になった。loop の出口の生きている mask の絞り直しを外しても色は変わらず PASS（副作用の無い試験では見えない。storage を書く fragment の試験は whole quad では拒むので今は無い）。
- 出典の監査（自分の確かめ）: file の先頭 20 行に出典の語が無いこと、`0x` の値を持つ `#define AMDRDNA_*` の全 block の直前の comment に「referenced」があること（SOP・VOP の表を形式ごとの block に分けた）、compiler の識別子と引用した Mesa の file（ACO・RADV・ac・nir_lower_idiv・sid.h）の識別子・gfx10*.json の名前の交差が一般語だけであること（`has_bias`・`linear_vgpr`・`vertex_inputs`・`storage_count` などを `biased`・`screen_linear_vgpr`・`fetched_locations`・`storage_used` に改名した後）。
- kernel の flag で 4 file（compile・encode・wait・program）とも warning 0。

### 7.5 出典の表への追加（2026-10-15、q1203 で引用した file）

| file | SPDX | SHA-256 | 取ったもの |
| --- | --- | --- | --- |
| `src/amd/compiler/aco_instruction_selection.cpp` | MIT | `b96cc3b28e5b571ff47f3bc5eb8bdbc6bf6c583b3893caafdc0dc0f8eb36de8c` | image の address の順（`visit_tex`）、offset の 6 bit × 8 bit 間隔、atomic の data と compare の順 |
| `src/amd/compiler/aco_ir.h`・`aco_ir.cpp` | MIT | `2908a52a…310d`・`b1665561…5083` | operand の番号・inline 定数、s_waitcnt の欄（`wait_imm::pack`） |
| `src/amd/vulkan/radv_shader.c` | MIT | `9b9683c2652db9b17da90d6391ef342eefdb43ed0b45526feac79e41aad9ba6c` | VGPR の 8 の block（wave32）、legacy VS の VS_OUT_CONFIG・POS_FORMAT・PA_CL_VS_OUT_CNTL |
| `src/amd/vulkan/radv_pipeline_graphics.c` | MIT | `b3157fbdb02d090f9c036fa62a738477a419686c8ddf73e253401373c648e1fb` | gl_FragCoord.w = 1/w（`nir_lower_fragcoord_wtrans`） |
| `src/amd/common/nir/ac_nir_lower_intrinsics_to_args.c` | MIT | `391bbc70d648aa83fc25ba6a1ed925c7bcd093ec979256b22df04215041bf0ec` | front face = 引数 > 0 |
| `src/amd/common/nir/ac_nir_prerast_utils.c` | MIT | `d93c893b089188ac3c499e69548058ee222f85f4a46429fcacd72e255b404628` | 位置の export の順、GFX10 の POS0 の valid mask |
| `src/amd/common/ac_gpu_info.c` | MIT | `f56e288b5024d51ec7ad5921d85fc15cd6a1df698a01b922543d2a62cf1a17c2` | LDS の encode の単位 512 byte |
| `src/amd/common/ac_descriptors.c` | MIT | `d66a4c2761e8b13bd32c3a223176c533570bd7c825369f2ae62fb9d008fe41cf` | 生の buffer の V#（R32_FLOAT・XYZW・OOB_SELECT_RAW・RESOURCE_LEVEL 1） |
| `src/amd/common/ac_binary.c` | MIT | `c7c8552a924f961eb148dd2e8108446cbef8d47d0516f539f3683d725677b619` | prefetch の 3 line |
| `src/amd/registers/gfx10-rsrc.json` | （Mesa の既定 MIT） | `ab6deb6cea2daf6ef546b8ae5eff105d5939c9d8cd76a710f9f8064e6a2cf820` | export 先、GFX10_FORMAT、SQ_BUF_RSRC_WORD1/3、OOB_SELECT |
| `src/amd/registers/gfx9.json` | （Mesa の既定 MIT） | `081fbfd1251fb5c6f4b7b6a218ba217e21346e824a04ec8146ab0d1d389ddb06` | DST_SEL の enum（SQ_SEL_XYZW01） |

（`aco_opcodes.py`・`aco_assembler.cpp`・`aco_insert_NOPs.cpp`・`README-ISA.md`・`aco_lower_to_hw_instr.cpp`・`radv_shader_args.c`・`gfx10.json`・`nir_lower_idiv.c` は 1 節の表。compiler が引用する 20 file の全部と確かめの結果は監査の表 [amdrdna-compiler-license-audit.md](../amdrdna-compiler-license-audit.md)（2026-10-15 Q1 の許可で作成）。）

### 7.6 段 F（2026-10-15 q1203、続き）

- `plan/ws039/tests/gfx10-desktop-extract.py`（`userland/desktop/**/shaders.h` の SPIR-V を entry point の stage の拡張子で書き出す）と `amdrdna-compiler-desktop-test.sh`（全 module を compile、graphics-test で 200 回ずつ参照と比べ、llvm-mc で disassemble）。desktop は 44 module（vertex 18・fragment 26、compute は無い）。
- 結果: 最初の実行で libGL の `fixed_frag`・`fixed_flat_frag` が FAIL（全 lane を discard した wave の早い終わりで exec を 0 にせずに `exp null done vm` していた → valid mask が全 lane になる不具合）。早い終わりに `s_mov_b32 exec_lo, 0` を足して直し、host 試験にこれを確かめる `uniform_discard.frag` を加えた（直す前の code で FAIL することを確かめた）。直した後: **44 module 全部 compile、44 × 200 回 PASS、44 program の disassemble に誤り無し**（約 21 秒）。host 試験も PASS（graphics 7 module）。
- 監査の表 [amdrdna-compiler-license-audit.md](../amdrdna-compiler-license-audit.md) を作った（2026-10-15 Q1 の許可）。

### 7.7 段 D の一部（2026-10-15 q1203、続き）

物差し: desktop 44 program の命令数（`s_code_end` の詰め物を除く、llvm-mc の disassemble の行数）。

| 段階 | 命令 | s_waitcnt | v_mov |
| --- | --- | --- | --- |
| 段 F の後 | 8765 | 1074 | 1633 |
| uniform の語を prologue で 16 byte ずつ SGPR へ（`s_load_dwordx4`・`s_buffer_load_dwordx4`）、inline 定数を operand のまま、VALU の SGPR operand は constant bus 2 個まで（超えたら scratch の VGPR へ写す） | 7567 | 677 | 1112 |
| 出力にだけ使う値を出力の register に直接定義、sample の u・v が既に連続なら写さない | 7288 | 684 | 828 |
| SGPR に入らない uniform の語は 4 個の chunk の cache（loop の始まりと終わりで忘れる） | 6636 | 338 | 888 |

- 変更: `gfx10-compile.c`（`operand_code`・`moved`・`output_home` の配列、`plan_uniforms`・`has_operand`・`vgpr_operand`・`scratch_copy`・`legalize_scalars`・`plan_output_homes`、`lower_uniform` を chunk の cache に）、`gfx10-isa.h`（`s_buffer_load_dwordx4` 0x0a）、`gfx10-encode.c`（その記述）。constant bus の上限 2 は `aco_validate.cpp`（`const_bus_limit`、SHA-256 `7961dc5336a3ae034961986087e8ff7ae599090b84d22d21670cbe8702c2b1b2`、MIT）から引用（監査の表に足すこと）。
- 試験: interpreter に constant bus の検査（3 個目の異なる scalar の読みで FAIL）と `s_buffer_load_dwordx4`。`shaders/cache.comp`（40 個の vec4 の uniform、loop の前・中・後で読む）を足した。cache を loop で忘れる処理を外すと FAIL することを確かめた。host 試験 PASS（compute 11・graphics 7）、desktop 44 × 200 回 PASS、disassemble 誤り無し。kernel の flag で warning 0。

### 7.8 再開点（2026-10-15 q1203 の段 D の一部の後）

1. 段 D の続き: 待ちを counter の数で（今は全部 0 まで）、label・分岐の前の全部の待ちを減らす（loop の back edge で状態を合わせる）、比較 → Boolean → 再比較の重複（v_cmp・v_cndmask・v_cmp_ne）を減らす、VS の param・位置の予約の VGPR を減らす、libGL の固定機能の VS（1400 命令）の uniform の読みの並べ方。
2. whole quad の fragment での storage 書き込み、IR の TEXTURE の他の message、POINT_COORD・PRIMITIVE_ID・SECOND_COLOR。
3. P3 とのすり合わせ（`struct amdrdna_gfx10_program`・`struct amdrdna_gfx10_registers` の field、資源の表の配置、T#・S# は driver が作る、SPI_PS_INPUT_ADDR は ENA と同じ値）、vmunix.mk への compiler の 4 file の追加（P3 の `CONFIG_DRIVER_PCI_AMDRDNA` の行の後）、実機での確かめ（`v_rcp_iflag_f32` の精度に頼る除算、Navi1x の valid mask、早い終わり、constant bus）。
4. Master の Tools への登録（Q1 に依頼済み）: `amdrdna-compiler-host-test.sh`・`amdrdna-compiler-desktop-test.sh`。

### 7.9 段 D の続きと段 G（2026-10-16 q1214 / q1214-i01、P5、実機前）

承認: 2026-10-16 ユーザー「RDNAのコンパイラや、パイプラインやexecutorの組み立てなど、別なサブエージェントで先に進められることがあれば、進めてオーケーです」（Q1 経由）。

変更（`src/drivers/gpu/amdrdna/compiler/`）:
- `gfx10-wait.c`: 待ちを counter の数で（vector memory の load・sample と shared は順に終わるので「後に出た同じ種類の数」まで、scalar load と export は 0 まで。`aco_insert_waitcnt.cpp` の `update_counters` を Procedure referenced）。label の状態を、そこへ落ちる道と分岐の道の状態の合流（register は小さい方の数、hazard は和）で求め、全 label が変わらなくなるまで歩き直す（16 回で収まらなければ従来の「label と分岐の前で全部を待つ」で 1 回）。export は VGPR だけを export の counter で保ち（EXEC は読んだ時点で済む）、読みは待たず書きだけが待つ。
- `gfx10-compile.c`:
  - 条件: VCC_LO が持つ Boolean を追い、同じ条件の v_cmp_ne を出さない。条件にだけ使う比較の結果は VGPR を持たず VCC_LO に残す（`plan_conditions`: 定義から最後の読みまでに VCC_LO を書く・lane を広げる命令が無い時）。guard の後は guard の条件が VCC_LO に残る。
  - 使われない命令の除去（`find_dead`: 結果を誰も読まない副作用の無い命令を後ろから、読みの数を減らしながら）。
  - operand のまま使う値: VS の fetch した channel と vertex・instance の番号、CS の local id・workgroup id、storage の大きさ、uniform の語（start の chunk、cache の entry は最後の読みまで保つ。全部保たれていれば予備の chunk に読んで写す）。
  - VS の v0..v3 は prologue の後、fetch の VGPR は最後の読みの後に返す。出力は予約しない: 1 度だけ書かれる channel は値の VGPR から export が直に読み（値は最後まで生きる）、他は最初の書き込みで VGPR を取る。parameter の export は書かれた channel だけ。
  - prologue の順: descriptor → vertex の fetch → uniform の chunk（fetch が uniform の load を待たない）。
  - TEXTURE: 2D の全 message（SAMPLE・BIAS・LOD・COMPARE・BIAS_COMPARE・LOD_COMPARE・DERIVS・DERIV_COMPARE・LD・RESINFO）。address の順は offset・bias・reference・勾配（du/dx dv/dx du/dy dv/dy）・u・v・level（`visit_tex`）、compare は dmask 1、level 0 の定数は `_lz`。parameter の数が 2D と違う message（3D・cube・array）は IR が image の次元を持たないので拒む（共有の front は変えない）。
  - PS の POINT_COORD（最後の attribute の次、PT_SPRITE_TEX・OFFSET 0x20）、PRIMITIVE_ID（その次、flat、parameter 31 を読む）、SECOND_COLOR（colour 1 として export、location 1 と同時は拒む、`dual_source`）。
  - compile の options（`struct amdrdna_gfx10_compile_options`、`export_primitive_id`: VS が v2 の primitive 番号を parameter 31 に export）。`amdrdna_gfx10_compile`・`_compile_ir` の引数に options（NULL 可）を足した。
  - whole quad の PS の storage 書き込み・atomic: 生きている lane（と述語）だけで（`guard_writes`、helper は書かない）。拒むのをやめた。
- `gfx10-program.[ch]`: registers に `primitive_id_enable`（VGT_PRIMITIVEID_EN）・`interpolation_control`（SPI_INTERP_CONTROL_0、RADV の preamble と同じ値）、sprite・primitive の attribute の control。
- `gfx10-isa.h`: image の opcode（resinfo・sample_d・c・c_d・c_l・c_b・c_lz と `_o`）、parameter 31 と NO_ATTRIBUTE。
- `platform/amd64/vmunix.mk`: `AMD64_AMDRDNA_COMPILER_SOURCES`（P3 の `AMD64_AMDRDNA_SOURCES` の行は変えない。共有の spirv.c・optimize.c は i915・nvrtx と重ならないよう filter-out）。

試験（`plan/ws039/tests/`）:
- interpreter: counter の数の待ちを最悪の順で確かめる（vector と shared は順、scalar load は 0 でだけ済む）。export の保持は書きだけ止める。image の全 opcode（offset・bias・compare・勾配・level・lz・fetch・resinfo、dmask の channel だけ書く）。VS の v2 に primitive 番号。
- texture の helper（`gfx10-texture.[ch]`）: request の struct（level・bias・勾配から LOD、LOD > 0 で縮小の filter、depth compare、size）。参照も同じ helper。
- 参照: TEXTURE の全 message、POINT_COORD・PRIMITIVE_ID の入力、SECOND_COLOR の slot。
- graphics 試験: VS は primitive の export の有無の 2 通り、PS の storage を参照の写しと語ごとに比べる、sprite・primitive の attribute の register の値。
- 新しい shader: `condition.comp`・`query.vert`・`query.frag`・`sprite.frag`・`helpers.frag`。known words に image の 6 命令。

結果（2026-10-16）:
- `amdrdna-compiler-host-test.sh` PASS: known words 92/92、compute 12 module × 16、graphics 11 module（VS 4 は 2 通り）× 16、27 program の disassemble に誤り無し。
- `amdrdna-compiler-desktop-test.sh` PASS: 44 module（VS は 2 通りで 62 program）× 200、disassemble 誤り無し。
- desktop 44 program（primitive の版を除く）: 命令 6636 → 5649、s_waitcnt 338 → 249（VS は待ちの数が増え 1 つずつは短い、PS の discard は減る）、v_mov 888 → 127、最大 VGPR 75 → 48（全部が wave32 の満員の占有 ≤ 48）。
- わざとの誤りで FAIL になることを確かめた: VCC の追いの忘れ（atomic で FAIL）、勾配の順の取り違え（query-frag）、helper の書き込み（helpers-frag）。
- `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk vmunix` exit 0、warning 0（kernel の include の検査・vmunix の検査 PASS）。kernel の flag と `-Wdeclaration-after-statement` で 4 file とも warning 0。
- `audit-symbols.sh`（Mesa の tree つき）PASS。監査の表に aco_insert_waitcnt.cpp・radv_cmd_buffer.c・radv_queue.c を足した。

実機で確かめる事（未実施）: counter の数の待ち（vector memory の順、exec 0 の VMEM も counter を数える前提）、v2 の primitive 番号（VGT_PRIMITIVEID_EN）、point sprite の attribute（SPI_INTERP_CONTROL_0 は driver の preamble）、dual-source の colour 1、sample_c の dmask 1、resinfo の z。7.8 の 3 の項目も残る。

### 7.10 再開点（2026-10-16）

1. 3D・cube・array の sample: 共有の IR が image の次元を持たない（共有の front の変更は p007 の範囲外、Q1 に相談）。
2. DDX・DDY（IR にある、desktop は使わない）、gather は未。
3. driver との口は p008（executor）で使う形に合わせる（`amdrdna_gfx10_compile` の options、registers の新しい field）。
