# WS039 amdrdna の shader compiler の出典の監査の表（ws039-p007）

対象: `src/drivers/gpu/amdrdna/compiler/`（`gfx10-isa.h`・`gfx10-encode.[ch]`・`gfx10-compile.[ch]`・`gfx10-wait.[ch]`・`gfx10-program.[ch]`）。
規則: [Guardrail「MIT・GPL の出典のデータの引用」](../guardrail.md)（WS039 の項）と [design 13 節](design.md)。値は block ごとの「Values referenced:」、手順は「Procedure referenced:」で示し、file の先頭には出典を書かない。code・comment・構造・symbol の名前は写さない（AMD の公開の RDNA1 ISA の命令名は使ってよい、2026-10-15 ユーザー）。copyright・MIT の notice は付けない。

出典の版: Mesa 25.0.7（tarball `mesa-25.0.7.tar.xz`、SHA-256 `592272df3cf01e85e7db300c449df5061092574d099da275d19e97ef0510f8a6`。署名は公開鍵が無く未検証）。下の表は compiler の source が引用している file の全部（2026-10-15 に `grep` で数えた 20 file）。

| file | license | SHA-256 | 取ったもの（値 V / 手順 P） | 引用する compiler の file |
| --- | --- | --- | --- | --- |
| `src/amd/compiler/aco_opcodes.py` | MIT（SPDX） | `27278b88f096658321a2631e873aed9b28b8355e9db04b87798f483194765b49` | V: SOP2・SOPK・SOP1・SOPC・SOPP・SMEM・VOP2・VOP1・VOPC・VOP3・VINTRP・DS・MUBUF・MIMG の GFX10 の opcode | `gfx10-isa.h` |
| `src/amd/compiler/aco_assembler.cpp` | MIT（SPDX） | `9faca4439b09aa0587c300e1667e9e515c90f4ba3b5ccceb357584e921a63c04` | V: 各形式の印と欄の bit の位置、VOP3 の opcode の足し算、分岐の距離 0x3f（`fix_branches_gfx10`） | `gfx10-isa.h`・`gfx10-encode.c` |
| `src/amd/compiler/aco_insert_NOPs.cpp` | MIT（SPDX） | `f25ed0cf369737118e4873472af7cbe3628da057851e07f7b82918f2b1e79409` | V: s_waitcnt_depctr の 0xffe3・0xfffe と欄。P: GFX10 の 4 つの hazard（`handle_instruction_gfx10`） | `gfx10-isa.h`・`gfx10-wait.c` |
| `src/amd/compiler/aco_ir.h` | MIT（SPDX） | `2908a52a0fc4401ce0bf9fa48f5e2b2afbacf3d1f33291e9585e39e05429310d` | V: operand の番号（vcc・m0・null・exec）、inline の整数・浮動小数の番号 | `gfx10-isa.h` |
| `src/amd/compiler/aco_ir.cpp` | MIT（SPDX） | `b1665561dabb95c481991627436722121cd04699eba48c73bc1d45773f7d5083` | V: s_waitcnt の欄（`wait_imm::pack` の GFX10） | `gfx10-isa.h` |
| `src/amd/compiler/aco_instruction_selection.cpp` | MIT（SPDX） | `b96cc3b28e5b571ff47f3bc5eb8bdbc6bf6c583b3893caafdc0dc0f8eb36de8c` | V: DS の cmpst と buffer の cmpswap の data・compare の順、offset の 6 bit × 8 bit 間隔。P: image の address の順（`visit_tex`） | `gfx10-isa.h`・`gfx10-compile.c` |
| `src/amd/compiler/aco_lower_to_hw_instr.cpp` | MIT（SPDX） | `b53d1bb3aa0dbe63f07741655d63730e2126458e92beaddc65deffe3a253a6a2` | P: discard の早い終わり（exec 0 の null export done vm、s_endpgm） | `gfx10-compile.c` |
| `src/amd/compiler/README-ISA.md` | MIT（Mesa の既定、`docs/license.rst`） | `6698da9ade24f469cb2b1046a529d8663fece6ea334ee31cf57dc857a6ca5251` | V: VOP1 を VOP3 で書く足し算。P: GFX10 の hazard、L0/L1 と GLC・DLC、LDS で M0 を使わない | `gfx10-isa.h`・`gfx10-wait.c`・`gfx10-compile.c` |
| `src/compiler/nir/nir_lower_idiv.c` | MIT（file の notice） | `5882ef003f429104ce25a3c6ddb8607b9330fe6d02b4be5197d90e90dc448a1c` | V: 4294966784.0（0x4f7ffffe）。P: 符号なし除算（`emit_udiv`） | `gfx10-compile.c` |
| `src/amd/vulkan/radv_shader_args.c` | MIT（SPDX） | `a21869c49a640b9d3a4cd6ef0e036faf7dfcbc6137348b0cb7d1c78872a2ecc2` | V: legacy VS の v0・v3、PS の入力 VGPR の順、prim mask の SGPR | `gfx10-compile.c` |
| `src/amd/vulkan/radv_shader.c` | MIT（SPDX） | `9b9683c2652db9b17da90d6391ef342eefdb43ed0b45526feac79e41aad9ba6c` | V: wave32 の VGPR の 8 の block、legacy VS の VS_OUT_CONFIG・POS_FORMAT・PA_CL_VS_OUT_CNTL | `gfx10-program.c` |
| `src/amd/vulkan/radv_pipeline_graphics.c` | MIT（SPDX） | `b3157fbdb02d090f9c036fa62a738477a419686c8ddf73e253401373c648e1fb` | P: gl_FragCoord.w = 1 / w（`nir_lower_fragcoord_wtrans`） | `gfx10-compile.c` |
| `src/amd/common/nir/ac_nir_lower_intrinsics_to_args.c` | MIT（SPDX） | `391bbc70d648aa83fc25ba6a1ed925c7bcd093ec979256b22df04215041bf0ec` | P: front face = 引数 > 0 | `gfx10-compile.c` |
| `src/amd/common/nir/ac_nir_prerast_utils.c` | MIT（SPDX） | `d93c893b089188ac3c499e69548058ee222f85f4a46429fcacd72e255b404628` | P: 位置の export を先に、GFX10 の POS0 に valid mask | `gfx10-compile.c` |
| `src/amd/common/ac_gpu_info.c` | MIT（SPDX） | `f56e288b5024d51ec7ad5921d85fc15cd6a1df698a01b922543d2a62cf1a17c2` | V: LDS の encode の単位 128 × 4 byte | `gfx10-program.c` |
| `src/amd/common/ac_descriptors.c` | MIT（SPDX） | `d66a4c2761e8b13bd32c3a223176c533570bd7c825369f2ae62fb9d008fe41cf` | V: 生の buffer の V#（R32_FLOAT・XYZW・OOB_SELECT_RAW・RESOURCE_LEVEL 1） | `gfx10-program.c` |
| `src/amd/common/ac_binary.c` | MIT（SPDX） | `c7c8552a924f961eb148dd2e8108446cbef8d47d0516f539f3683d725677b619` | V: GFX10 の prefetch の 3 line | `gfx10-isa.h` |
| `src/amd/registers/gfx10.json` | MIT（Mesa の既定） | `f3435695c47c3c3b1d8329a96b098452d5909944ca2c99995520c22614617d48` | V: RSRC1/2・SPI_PS_INPUT_ENA・SPI_VS_OUT_CONFIG・SPI_SHADER_POS_FORMAT・COL_FORMAT・PS_IN_CONTROL・PS_INPUT_CNTL・PA_CL_VS_OUT_CNTL の欄、FLOAT_MODE・SPI_SHADER_FORMAT・SPI_SHADER_EX_FORMAT の enum | `gfx10-compile.c`・`gfx10-program.c` |
| `src/amd/registers/gfx10-rsrc.json` | MIT（Mesa の既定） | `ab6deb6cea2daf6ef546b8ae5eff105d5939c9d8cd76a710f9f8064e6a2cf820` | V: export 先、GFX10_FORMAT、SQ_BUF_RSRC_WORD1/3、OOB_SELECT | `gfx10-isa.h`・`gfx10-program.[ch]` |
| `src/amd/registers/gfx9.json` | MIT（Mesa の既定） | `081fbfd1251fb5c6f4b7b6a218ba217e21346e824a04ec8146ab0d1d389ddb06` | V: DST_SEL の enum（SQ_SEL_XYZW01、GFX10 の記述子も同じ値） | `gfx10-program.c` |

## 確かめ（2026-10-15、P5）

- file の先頭 20 行: 9 file とも出典の語（referenced・Mesa・SHA-256）が無い。
- `0x` の値を持つ `#define AMDRDNA_*` の全 block: 直前の（空行で区切られた）comment に「Values referenced:」がある（script で全数）。zedBSD 自身の値（flag の bit など）は `0x` を使わず `(1U << n)` で書き、自分の値と comment に書いた。
- 識別子: compiler の code（comment と文字列を除く）の識別子と、上の 20 file の識別子・json の名前の交差は一般語（`address`・`count`・`offset`・`stage` など）だけ。`has_bias`・`linear_vgpr`・`vertex_inputs`・`storage_count`・`read_count`/`write_count` は改名した。
- 構造: ACO は SSA の C++ の多段の compiler、zedBSD は scalar IR から直に命令の列を作り、待ちと hazard を 1 本の straight line の pass で入れる。pass の分け方・関数の切り方・comment の文は ACO と別。
- 人の review（design 13.3 の 3）は p010 で。
