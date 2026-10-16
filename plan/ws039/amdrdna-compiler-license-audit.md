# WS039 amdrdna の shader compiler の出典の監査の表（ws039-p007）

対象: `src/drivers/gpu/amdrdna/compiler/`（`gfx10-isa.h`・`gfx10-encode.[ch]`・`gfx10-compile.[ch]`・`gfx10-wait.[ch]`・`gfx10-program.[ch]`）。
規則: [Guardrail「MIT・GPL の出典のデータの引用」](../guardrail.md)（WS039 の項）と [design 13 節](design.md)。値は block ごとの「Values referenced:」、手順は「Procedure referenced:」で示し、file の先頭には出典を書かない。code・comment・構造・symbol の名前は写さない（AMD の公開の RDNA1 ISA の命令名は使ってよい、2026-10-15 ユーザー）。copyright・MIT の notice は付けない。

出典の版: Mesa 25.0.7（tarball `mesa-25.0.7.tar.xz`、SHA-256 `592272df3cf01e85e7db300c449df5061092574d099da275d19e97ef0510f8a6`。署名は公開鍵が無く未検証）。下の表は compiler の source が引用している file の全部（2026-10-15 に `grep` で数えた 21 file）。

| file | license | SHA-256 | 取ったもの（値 V / 手順 P） | 引用する compiler の file |
| --- | --- | --- | --- | --- |
| `src/amd/compiler/aco_opcodes.py` | MIT（SPDX） | `27278b88f096658321a2631e873aed9b28b8355e9db04b87798f483194765b49` | V: SOP2・SOPK・SOP1・SOPC・SOPP・SMEM・VOP2・VOP1・VOPC・VOP3・VINTRP・DS・MUBUF・MIMG の GFX10 の opcode | `gfx10-isa.h` |
| `src/amd/compiler/aco_assembler.cpp` | MIT（SPDX） | `9faca4439b09aa0587c300e1667e9e515c90f4ba3b5ccceb357584e921a63c04` | V: 各形式の印と欄の bit の位置、VOP3 の opcode の足し算、分岐の距離 0x3f（`fix_branches_gfx10`） | `gfx10-isa.h`・`gfx10-encode.c` |
| `src/amd/compiler/aco_insert_NOPs.cpp` | MIT（SPDX） | `f25ed0cf369737118e4873472af7cbe3628da057851e07f7b82918f2b1e79409` | V: s_waitcnt_depctr の 0xffe3・0xfffe と欄。P: GFX10 の 4 つの hazard（`handle_instruction_gfx10`） | `gfx10-isa.h`・`gfx10-wait.c` |
| `src/amd/compiler/aco_ir.h` | MIT（SPDX） | `2908a52a0fc4401ce0bf9fa48f5e2b2afbacf3d1f33291e9585e39e05429310d` | V: operand の番号（vcc・m0・null・exec）、inline の整数・浮動小数の番号 | `gfx10-isa.h` |
| `src/amd/compiler/aco_ir.cpp` | MIT（SPDX） | `b1665561dabb95c481991627436722121cd04699eba48c73bc1d45773f7d5083` | V: s_waitcnt の欄（`wait_imm::pack` の GFX10） | `gfx10-isa.h` |
| `src/amd/compiler/aco_validate.cpp` | MIT（SPDX） | `7961dc5336a3ae034961986087e8ff7ae599090b84d22d21670cbe8702c2b1b2` | V: VALU の constant bus の上限 2（GFX10、64 bit の shift を除く） | `gfx10-compile.c` |
| `src/amd/compiler/aco_instruction_selection.cpp` | MIT（SPDX） | `b96cc3b28e5b571ff47f3bc5eb8bdbc6bf6c583b3893caafdc0dc0f8eb36de8c` | V: DS の cmpst と buffer の cmpswap の data・compare の順、offset の 6 bit × 8 bit 間隔。P: image の address の順（`visit_tex`） | `gfx10-isa.h`・`gfx10-compile.c` |
| `src/amd/compiler/aco_lower_to_hw_instr.cpp` | MIT（SPDX） | `b53d1bb3aa0dbe63f07741655d63730e2126458e92beaddc65deffe3a253a6a2` | P: discard の早い終わり（exec 0 の null export done vm、s_endpgm） | `gfx10-compile.c` |
| `src/amd/compiler/README-ISA.md` | MIT（Mesa の既定、`docs/license.rst`） | `6698da9ade24f469cb2b1046a529d8663fece6ea334ee31cf57dc857a6ca5251` | V: VOP1 を VOP3 で書く足し算。P: GFX10 の hazard、L0/L1 と GLC・DLC、LDS で M0 を使わない | `gfx10-isa.h`・`gfx10-wait.c`・`gfx10-compile.c` |
| `src/compiler/nir/nir_lower_idiv.c` | MIT（file の notice） | `5882ef003f429104ce25a3c6ddb8607b9330fe6d02b4be5197d90e90dc448a1c` | V: 4294966784.0（0x4f7ffffe）。P: 符号なし除算（`emit_udiv`） | `gfx10-compile.c` |
| `src/amd/vulkan/radv_shader_args.c` | MIT（SPDX） | `a21869c49a640b9d3a4cd6ef0e036faf7dfcbc6137348b0cb7d1c78872a2ecc2` | V: legacy VS の v0・v3、PS の入力 VGPR の順、prim mask の SGPR | `gfx10-compile.c` |
| `src/amd/vulkan/radv_shader.c` | MIT（SPDX） | `9b9683c2652db9b17da90d6391ef342eefdb43ed0b45526feac79e41aad9ba6c` | V: wave32 の VGPR の 8 の block、legacy VS の VS_OUT_CONFIG・POS_FORMAT・PA_CL_VS_OUT_CNTL、GFX10・GFX11 の MEM_ORDERED（`radv_mem_ordered`、2026-10-16 追加） | `gfx10-program.c` |
| `src/amd/vulkan/radv_pipeline_graphics.c` | MIT（SPDX） | `b3157fbdb02d090f9c036fa62a738477a419686c8ddf73e253401373c648e1fb` | P: gl_FragCoord.w = 1 / w（`nir_lower_fragcoord_wtrans`） | `gfx10-compile.c` |
| `src/amd/common/nir/ac_nir_lower_intrinsics_to_args.c` | MIT（SPDX） | `391bbc70d648aa83fc25ba6a1ed925c7bcd093ec979256b22df04215041bf0ec` | P: front face = 引数 > 0 | `gfx10-compile.c` |
| `src/amd/common/nir/ac_nir_prerast_utils.c` | MIT（SPDX） | `d93c893b089188ac3c499e69548058ee222f85f4a46429fcacd72e255b404628` | P: 位置の export を先に、GFX10 の POS0 に valid mask | `gfx10-compile.c` |
| `src/amd/common/ac_gpu_info.c` | MIT（SPDX） | `f56e288b5024d51ec7ad5921d85fc15cd6a1df698a01b922543d2a62cf1a17c2` | V: LDS の encode の単位 128 × 4 byte | `gfx10-program.c` |
| `src/amd/common/ac_descriptors.c` | MIT（SPDX） | `d66a4c2761e8b13bd32c3a223176c533570bd7c825369f2ae62fb9d008fe41cf` | V: 生の buffer の V#（R32_FLOAT・XYZW・OOB_SELECT_RAW・RESOURCE_LEVEL 1） | `gfx10-program.c` |
| `src/amd/common/ac_binary.c` | MIT（SPDX） | `c7c8552a924f961eb148dd2e8108446cbef8d47d0516f539f3683d725677b619` | V: GFX10 の prefetch の 3 line | `gfx10-isa.h` |
| `src/amd/registers/gfx10.json` | MIT（Mesa の既定） | `f3435695c47c3c3b1d8329a96b098452d5909944ca2c99995520c22614617d48` | V: RSRC1/2・SPI_PS_INPUT_ENA・SPI_VS_OUT_CONFIG・SPI_SHADER_POS_FORMAT・COL_FORMAT・PS_IN_CONTROL・PS_INPUT_CNTL・PA_CL_VS_OUT_CNTL の欄、RSRC1 の MEM_ORDERED（VS 27・PS 25・CS 30、2026-10-16 追加）、FLOAT_MODE・SPI_SHADER_FORMAT・SPI_SHADER_EX_FORMAT の enum | `gfx10-compile.c`・`gfx10-program.c` |
| `src/amd/registers/gfx10-rsrc.json` | MIT（Mesa の既定） | `ab6deb6cea2daf6ef546b8ae5eff105d5939c9d8cd76a710f9f8064e6a2cf820` | V: export 先、GFX10_FORMAT、SQ_BUF_RSRC_WORD1/3、OOB_SELECT | `gfx10-isa.h`・`gfx10-program.[ch]` |
| `src/amd/registers/gfx9.json` | MIT（Mesa の既定） | `081fbfd1251fb5c6f4b7b6a218ba217e21346e824a04ec8146ab0d1d389ddb06` | V: DST_SEL の enum（SQ_SEL_XYZW01、GFX10 の記述子も同じ値） | `gfx10-program.c` |
| `src/amd/compiler/aco_insert_waitcnt.cpp` | MIT（SPDX） | `5171bd9899f00a6fc3e8f811dd36f9fc211296a4fc197b59c2f0c78f4766a749` | P: 順に終わる event（vector memory・shared）と順の無い event（scalar load）、counter の数の待ち（`update_counters`・`target_info`）。2026-10-16 追加 | `gfx10-wait.c` |
| `src/amd/vulkan/radv_cmd_buffer.c` | MIT（SPDX） | `3ecb5f4b2e3d151316450b835ac64dd700b91edb97ac95bc9c06c08f1654179e` | V: point sprite の attribute（PT_SPRITE_TEX、OFFSET 0x20、`radv_emit_ps_inputs`）。2026-10-16 追加 | `gfx10-program.c` |
| `src/amd/vulkan/radv_queue.c` | MIT（SPDX） | `7b29329a5bd3ef645cb3f3c544fd037d356cd526fa9f287e25f59e1075316f8c` | V: preamble の SPI_INTERP_CONTROL_0（flat・sprite の S・T・0・1）。2026-10-16 追加 | `gfx10-program.c` |

（host 試験の道具 `plan/ws039/tests/gfx10-texture.c` は gfx10-rsrc.json・gfx9.json（SQ_TEX_DEPTH_COMPARE・SQ_TEX_XY_FILTER）の値を引用。試験の道具で driver の code ではない。）

## 確かめ（2026-10-15、P5）

- file の先頭 20 行: 9 file とも出典の語（referenced・Mesa・SHA-256）が無い。
- `0x` の値を持つ `#define AMDRDNA_*` の全 block: 直前の（空行で区切られた）comment に「Values referenced:」がある（script で全数）。zedBSD 自身の値（flag の bit など）は `0x` を使わず `(1U << n)` で書き、自分の値と comment に書いた。
- 識別子: compiler の code（comment と文字列を除く）の識別子と、上の 21 file の識別子・json の名前の交差は一般語（`address`・`count`・`offset`・`stage` など）だけ。`has_bias`・`linear_vgpr`・`vertex_inputs`・`storage_count`・`read_count`/`write_count`・`is_signed` は改名した。
- 構造: ACO は SSA の C++ の多段の compiler、zedBSD は scalar IR から直に命令の列を作り、待ちと hazard を 1 本の straight line の pass で入れる。pass の分け方・関数の切り方・comment の文は ACO と別。
- 人の review（design 13.3 の 3）は p010 で。

## 確かめ（2026-10-16、P5、q1214）

- `plan/ws039/tests/audit-symbols.sh`（Mesa 25.0.7 の tree を `plan/ws039/temp/mesa` に置いて）: PASS（amdrdna の 24 file、8461 の引用元の識別子、許した一般語 69）。block の直前の「referenced」の検査も同じ script で PASS。
- 段 D・TEXTURE・入力の追加で新しく引用したのは上の 3 行（aco_insert_waitcnt.cpp・radv_cmd_buffer.c・radv_queue.c）。aco_validate.cpp の行は 2026-10-15 からある。

## executor の hardware の部分（ws039-p008、2026-10-16、P5）

対象: `src/drivers/gpu/amdrdna/vk/` のうち hardware の部分（`amdrdna-vk-gfx10.h`・`vk-pm4.c`・`vk-state.c`・`vk-descriptor.c`・`vk-execute.c`・`vk-pipeline.c`・`vk-quad.c`・`vk-float.c`）。nvrtx から写した hardware に依らない部分（`vk-object.c` など 13 file）は zedBSD 自身の code で、引用は無い。出典の版は compiler と同じ Mesa 25.0.7（design 9.2 は driver の側を 25.3.6 としているが、P5 の手元に 25.3.6 の tree が無いので、実際に読んだ 25.0.7 を使い、block ごとに版を書いた。2026-10-16 Q1 の決定: 25.0.7 でよく、design との違いを記録する）。上の表に既にある file（`radv_cmd_buffer.c`・`radv_queue.c`・`radv_shader.c`・`ac_gpu_info.c`・`ac_descriptors.c`・`gfx10.json`・`gfx10-rsrc.json`・`gfx9.json`）は executor からも引用する（取ったものは下の「追加」の欄）。

| file | license | SHA-256 | 取ったもの（値 V / 手順 P） | 引用する executor の file |
| --- | --- | --- | --- | --- |
| `src/amd/common/sid.h` | MIT（SPDX） | `3aff0d7daa12c83abbf0ed2908b560ce46483d084dbd397b68596c30fc41122e` | V: RELEASE_MEM の欄（EOP の行き先・割り込み）、event の index の位置（2026-10-16 q1219: type-3 の header・opcode・register の空間の base・WAIT_REG_MEM・CONTEXT_CONTROL・NOP の詰め物は driver の `amdrdna-hw.h`・`command-format.c`（出典 Linux v6.19 `nvd.h`・`gfx_v10_0.c`）に寄せ、vk/ はそれを使う） | `amdrdna-vk-gfx10.h` |
| `src/amd/registers/pkt3.json` | MIT（Mesa の既定） | `36f5c2eba891df4ba1f85807241d536e8e4cc6787e34328971f071f4cddf0242` | V: RELEASE_MEM の cache の操作の bit、ACQUIRE_MEM の GCR_CNTL の bit | `amdrdna-vk-gfx10.h` |
| `src/amd/vulkan/radv_cs.c` | MIT（SPDX） | `8d37b923ea8b00d7f7eb544743b44495eae049e902dcab61865a9f3f58848818` | P: GFX10 の cache の flush（partial flush・metadata・timestamp の event と release の待ち・PFP_SYNC_ME・acquire）、end-of-pipe の書き込み | `vk-pm4.c`・`vk-state.c` |
| `src/amd/vulkan/radv_cs.h` | MIT（SPDX） | `6482e3ff534af74b35a3b070c7545030be62aae21ae0e620a866c79127d7c9e6` | P: uconfig の index 付きの setter、memory の値の待ち | `vk-pm4.c` |
| `src/amd/common/ac_cmdbuf.c` | MIT（SPDX） | `d59674e81abf5d1c250b2f468593bdae5f36dab977d2843c97b0e3faaa73c299` | V・P: GFX10 の graphics と compute の preamble（CU の mask、wave の上限、REQ_CTRL、cache の policy、binner の CNTL_1、NGG の deallocation、COHER_START_DELAY 0x20、STATIC_THREAD_MGMT）、generic scissor の 16384 | `amdrdna-vk-gfx10.h`・`vk-state.c` |
| `src/amd/vulkan/radv_sampler.c` | MIT（SPDX） | `da9ea55ef9d124a9ae4c4867b9ee7e387419ed7b47d6e80db0e33435a052768d` | P: VkSampler の値 → clamp・filter・mip filter・border・compare の対応、nearest の truncation | `vk-descriptor.c` |
| `src/amd/vulkan/radv_pipeline_graphics.h` | MIT（SPDX） | `b8870aaf94d91ebae5c7391591adfa9f5b1140d8c4e94d4814fd67a311e190a7` | P: blend の factor・function、topology → primitive の型の対応 | `vk-state.c` |
| 追加: `radv_cmd_buffer.c` | | （上の表） | P: color blend・raster・viewport・scissor・vgt の stage・GE_CNTL・DB_SHADER_CONTROL・PS の入力・hw VS・draw の packet・dispatch の packet、binner off の GFX10 の値、scan の walk | `vk-state.c`・`vk-pm4.c`・`amdrdna-vk-gfx10.h` |
| 追加: `radv_queue.c` | | （上の表） | P: graphics の preamble（CONTEXT_CONTROL・CLEAR_STATE、point size、VTX_CNTL、VTE、restart index、stipple scale） | `vk-state.c`・`amdrdna-vk-gfx10.h` |
| 追加: `radv_shader.c` | | （上の表） | P: DB_SHADER_CONTROL（memory を書く program の late Z・EXEC_ON_*、DUAL_QUAD_DISABLE） | `vk-state.c` |
| 追加: `ac_gpu_info.c` | | （上の表） | V: Navi10 の pc_lines 1024・pbb_max_alloc_count、rbplus_allowed、graphics の IB の 8 語の詰め物 | `vk-state.c`・`vk-execute.c` |
| 追加: `ac_descriptors.c` | | （上の表） | V・P: T#（RESOURCE_LEVEL・PERF_MOD 4・address の bit 8 から）、S# の LOD の 4.8 の clamp | `vk-descriptor.c` |
| 追加: `gfx10.json`・`gfx10-rsrc.json`・`gfx9.json` | | （上の表） | V: 使う register の offset と欄、SQ_IMG_RSRC/SQ_IMG_SAMP の欄、BinningMode・WritePolicy・ReadPolicy・VGT_EVENT_TYPE・VGT_DI_PRIM_TYPE・SQ_TEX_* の enum | 全部 |

自分の値（IEEE 754 の binary32 の bit、golden ratio の hash の乗数）は block に「Value(s) referenced:」で規格・文献を書いた（`audit-symbols.py` の規則 2）。

### 確かめ（2026-10-16、P5）

- `plan/ws039/tests/audit-vk.sh`（`audit-symbols.py` の規則を `vk/` に、許す一般語は `audit-allow.txt` と `audit-allow-vk.txt` を合わせて）: PASS（24 file、Mesa の引用元の識別子 8461、許した一般語 69）。Linux の tree は手元に無いので Mesa だけ。
- 注意: driver 全体の `audit-symbols.sh` は `compiler/` だけを除いて歩くので、`vk/` を merge すると `vk/` の一般語（`requested`・`objects`・`result` など 30 語、`audit-allow-vk.txt`）で FAIL になる。2026-10-16 Q1 の決定: driver の `audit-symbols.sh` は `vk/` と `compiler/` を歩かない形にする。merge の時に、Q1 が P3 に頼む。
