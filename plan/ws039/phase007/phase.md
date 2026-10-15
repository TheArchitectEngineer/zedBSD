<!-- awesome-plan project=zedbsd record=ws039-p007 -->

# ws039-p007: shader の compiler（SPIR-V → GFX10、RDNA1、Navi10 = gfx1010）

Status: in-progress（2026-10-15 P5 着手・一時停止の後、同日夕に q1203 / q1203-i01 で P5 が再開）
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
| A | encode ＋ compute の最小（算術・比較・変換・特殊関数・storage・push・system value）＋ 1〜3 の試験 | 未 |
| B | loop・除算・atomic・shared・barrier・fence | 未 |
| E | 頂点・fragment（頂点の fetch、補間、export、sample、KILL、UBO） | 未 |
| F | desktop の全 shader の差分の試験（5 節の 4） | 未 |
| D | 速さ（待ちを register ごとの counter に、uniform の値を SGPR のまま使う） | 未 |

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
