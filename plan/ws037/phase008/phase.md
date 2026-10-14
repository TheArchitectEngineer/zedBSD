<!-- awesome-plan project=zedbsd record=ws037-p008 -->

# ws037-p008: shader の compiler（SPIR-V → SM75、Turing）

Status: in-progress（2026-10-15 P3: 段 A を実装し、build と host の試験まで。GPU で走らせる確かめは p007 の投入の口の後）
Disposition: normal
Parent: [WS037](../ws.md)
Queue: Q1 の割当（2026-10-14 夜、P3: 「WS237 を止めて WS037 の shader の compiler に移る」。ユーザー「RTX対応を2エージェントで実施してOKです。displayとcompilerは平行にできそうです。」、ベータ2 に既定 off で入れる範囲は ws.md の末尾）
依存: なし（host で作って確かめる）。GPU で走らせる確かめは p007 の compute の投入の口（P1）と、p005 の channel に依存する。
実行者: P3（phase-runner）

## 1. 判断（design の判断の項目 8・16 への答え）

- **WS037 の Phase にする**（別の WS にしない）。i915 の WS031 と違い、SPIR-V の front と IR はもう共有の物（`src/drivers/gpu/compiler/`: `spirv.c`・`ir.h`・`optimize.c`、i915 と V3D（WS141 の bcm2711）が使う）があり、nvrtx は backend だけを持つ。
- 置き場所: `src/drivers/gpu/nvrtx/compiler/`（kernel の中、Guardrail の「SPIR-V の compile は kernel 空間」）。build は `CONFIG_DRIVER_PCI_NVRTX` の時だけ（既定 n）。
- 共有: front（`drv_gpu_shader_parse`）、optimizer（`drv_gpu_shader_optimize`）、IR の型。backend の形（命令の選択 → register の割り当て → 依存の印 → encode、と診断の形）は V3D の `bcm2711/shader*.c` と i915 の `compiler/compile.c` に合わせる。
- 参考: Mesa 25.3.6 の NAK（`src/nouveau/compiler/nak/`、MIT）。**事実だけ**（opcode・field の bit の位置・値の表・latency の分類・system value の番号）を取り、code は自分で C で書く。NAK は Rust で構成（SSA の IR、legalize、RA、calc_instr_deps）も大きいので、構成は真似ず、zedBSD の scalar IR に合う小さい backend にする。

### 出典（MIT、file ごと。sha256 は P1 の `plan/ws037/temp/mesa/` の物を P3 が 2026-10-15 に計った値、監査の表 [nvrtx-license-audit.md](../nvrtx-license-audit.md) の行と一致）

| file | sha256 | 取る事実 |
| --- | --- | --- |
| `src/nouveau/compiler/nak/sm70_encode.rs`（SPDX MIT、© 2022 Collabora） | `111b841c…6695` | 128 bit の命令の field（opcode 0..12、述語 12..16、dst 16..24、src 24..32・32..64・64..72、ALU の form 9..12、cbuf の位置、制御の bit 105..126）、各命令の opcode と修飾の bit |
| `src/nouveau/compiler/nak/sm75_instr_latencies.rs`（SPDX MIT） | `419b8b1d…8764` | Turing の latency の分類（coupled ALU・FMA・IMAD、decoupled（scoreboard が要る）） |
| `src/nouveau/compiler/nak/sm70.rs`（SPDX MIT） | 監査の表 | 汎用 register 253（RZ = 255、hw の予約 2）、述語 7（PT = 7）、最大の delay 15 |
| `src/nouveau/compiler/nak/calc_instr_deps.rs`（SPDX MIT） | `8c92e065…8695` | 直列の依存の付け方（書き込み scoreboard 0、読み scoreboard 1、次の命令が両方を待つ。分岐は全部を待つ） |
| `src/nouveau/compiler/nak_private.h`（SPDX MIT か p009 で全文を確かめる） | — | system register の番号（TID.X 0x21〜Z 0x23、CTAID.X 0x25〜Z 0x27） |
| `src/nouveau/compiler/nak/qmd.rs`（SPDX MIT） | `73f4daf7…6698` | compute の QMD 2.2 の field（段 C、P1 とすり合わせ） |

この表の file 以外（NVIDIA の GPL の物、nouveau の GPL の物）は使わない。作業の文書は作らない（MIT の事実だけなので temp も要らない）。

## 2. 形

```
SPIR-V ──drv_gpu_shader_parse──▶ scalar IR ──drv_gpu_shader_optimize──▶ IR
   ──nvrtx_sm75_select──▶ 機械の命令の列（仮想 register、64 bit の組を含む）
   ──nvrtx_sm75_assign──▶ 物理 register（線形の走査、spill 無し、足りなければ拒む）
   ──nvrtx_sm75_schedule──▶ 制御の bit（delay・scoreboard・待ち・yield）
   ──nvrtx_sm75_encode──▶ 128 bit × N の code ＋ 結果の記述（register の数・shared・local size・cbuf の表）
```

| file | 中身 | host で試すか |
| --- | --- | --- |
| `nvrtx/compiler/sm75-isa.h` | 事実の値（opcode、field の位置、system register、cbuf の割り当て）。MIT の出典と sha256 を header に | — |
| `nvrtx/compiler/sm75-encode.[ch]` | 1 命令の 128 bit を作る（field の書き込み、ALU の operand の form、各命令の encode 関数、制御の bit） | はい |
| `nvrtx/compiler/sm75-compile.[ch]` | IR → 命令の選択、register の割り当て、依存の印、code の出力、公開の API `nvrtx_sm75_compile()`・`nvrtx_sm75_binary_free()` | はい |
| `plan/ws037/tests/nvrtx-sm75-host-test.[c,sh]` | 下の 4 節 | — |

## 3. 決めた設計

### 3.1 実行の模型（Turing、SM75）

- 命令は 128 bit。制御の bit（各命令の上位）: delay 105..108（次の命令を出すまでの cycle、1〜15）、yield 109、書き込みの scoreboard 110..112（7 = 無し）、読みの scoreboard 113..115（7 = 無し）、待つ scoreboard の mask 116..121、operand の再利用 122..125（使わない＝0）。
- **段 A の依存の付け方は直列（正しさが先、速さは後）**: 固定 latency の命令（coupled の ALU・FMA・IMAD・MOV・SEL・ISETP・FSETP など）は delay = 結果の種類の最悪の値（汎用 register 6、述語 13、Turing の表の最大）。可変 latency の命令（S2R・LDG・STG・LDC・MUFU・I2F・F2I・FRND）は書き込み scoreboard 0（結果がある時）と読み scoreboard 1（register を読む時）を立て、**次の命令が mask 0b11 を待つ**。分岐と EXIT の前は全部を待つ。NAK の直列の道（debug 用）と同じ考え。段 D で表の latency を使う並べ替えに進む。
- register: R0〜R252 を割り当てに使い、RZ（255）は 0 の読み・捨てる書き込み。述語は P0 だけを使い（比較の直後に読む）、PT（7）は常に真。
- 64 bit の address は偶数の register の組（R2n・R2n+1）。

### 3.2 cbuf の割り当て（dispatch との約束、P1 とすり合わせる）

| 場所 | 中身 |
| --- | --- |
| cbuf 0、0x000〜0x0ff | push constants（256 byte まで） |
| cbuf 0、0x100〜0x10f | 予約（段 B で workgroup の数など） |
| cbuf 0、0x200 + 16 × k | storage buffer の表の k 番目: GPU の仮想 address の low・high（8 byte）、大きさ（4 byte）、0 |
| cbuf 1〜7 | uniform buffer（LOAD_UBO の uniform の順に 1 から） |

結果の記述（`struct nvrtx_sm75_binary`）に、storage の表の k ↔ (set, binding)、cbuf の番号 ↔ (set, binding)、push の byte 数、汎用 register の数（使った最大 + 1 + hw の予約 2、QMD に入れる値）、shared の byte、local size を入れる。dispatch（p007、P1）はこれで cbuf 0 を作り、QMD に code の address・register の数・shared・local size・cbuf 0〜7 を書いて投入する。

### 3.3 IR の命令の選択（段 A の範囲、compute）

| IR | SM75 |
| --- | --- |
| CONST・ICONST・BOOL | MOV R, imm32 |
| LOAD_PUSH | MOV R, c[0][imm]（cbuf の operand、固定 latency） |
| LOAD_UBO | MOV R, c[1 + n][imm] |
| FADD・FSUB・FMUL・FNEG・FABS・FMIN・FMAX | FADD（FSUB は src1 の neg、FNEG は RZ − x）・FMUL・FMNMX（述語 PT/!PT で min/max） |
| IADD・ISUB・INEG・IMUL | IADD3（neg の修飾）・IMAD（x × y + RZ） |
| IAND・IOR・IXOR・INOT・SHL・SHR・ASR | LOP3（lut 0xc0・0xfc・0x3c・0x0f）・SHF |
| 比較（FLT・FGE・FEQ・FNEU・ILT・IGE・ULT・UGE・IEQ・INE）・AND・OR・NOT | FSETP・ISETP で P0、SEL R, ~0, RZ, P0 で Boolean（全 1 か 0）。AND・OR・NOT は LOP3 |
| SELECT | ISETP.NE P0, cond, RZ; SEL R, a, b, P0 |
| I2F・U2F・F2I・F2U・FTRUNC・FLOOR・FROUND_EVEN・FRACT | I2F・F2I・FRND（FRACT は FRND.FLOOR と FADD） |
| RCP・RSQ・SQRT・EXP2・LOG2・SIN・COS | MUFU（SIN・COS は 1/(2π) を掛けてから、NAK と同じ） |
| LOAD_SYSTEM | S2R（TID・CTAID）、LOCAL_INDEX は IMAD で組み立て |
| LOAD_STORAGE・STORE_STORAGE（述語付きを含む） | LDC.64 で表から base、IMAD.WIDE.U32 で base + offset、LDG.E・STG.E（.64 address、32 bit）。述語付きは ISETP.NE P0 と @P0 |
| STORAGE_SIZE | MOV R, c[0][0x208 + 16k] |
| MOVE・NOP | MOV、無し |

段 A で拒む物（診断の理由を返す）: LOOP_*、SKIP_*、ATOMIC、shared と BARRIER・FENCE、UDIV・UMOD・IDIV・IREM、texture と graphics の stage。段 B で足す（loop は VOTE.ANY と warp で揃った分岐、除算は NAK の列の事実）。

## 4. 確かめ（host）

1. **既知の命令の語**: CUDA の SM70 以降の kernel の dump で広く知られた語と bit まで同じこと。`MOV R1, c[0x0][0x28]` = `0x00000a0000017a02` `0x000fc40000000f00`（delay 2）、`S2R R2, SR_TID.X` = `0x0000000000027919` `0x000e220000002100`、`IMAD R0, R3, c[0x0][0x0], R0` = `0x0000000003007a24` `0x001fca00078e0200`、`EXIT` = `0x000000000000794d` `0x000fea0003800000`、`BRA` 自分へ = `0xfffffff000007947` `0x000fc0000383ffff`、`NOP` = `0x0000000000007918` `0x000fc00000000000`。どれも NAK の field の定義から出る値と一致する（field の位置と form の確かめ）。
2. **field ごとの試験**: 各 encode 関数が NAK の定義の位置に値を置くこと（opcode・form・述語・cbuf・修飾）。
3. **小さい emulator で走らせる**: 段 A の命令だけを解く SM75 の命令の interpreter（host の試験の中、encode の逆）で、`plan/ws101/tests/host/shaders/` の compute（add・ids など、glslc で SPIR-V に）を走らせ、buffer の結果を C の計算と比べる。命令の選択・register の割り当て・address・述語の誤りを見つける（encode そのものの正しさは 1 と 2 が見る）。
4. 拒む物の診断。

実機（RTX 2070、方式 B）は p007 の compute の投入の口ができてから T1/T2 に依頼する（buffer に定数を書く・足し算）。

## 5. 段

| 段 | 内容 | 状態 |
| --- | --- | --- |
| A | encoder ＋ compute の最小の lowering（3.3）＋ host の試験 1〜4 | 実装・host 済み（下の 6 節） |
| B | loop・除算・atomic・shared・barrier | 未 |
| C | QMD 2.2 の組み立て（P1 の投入の口に渡す、qmd.rs の事実） | 未（P1 と相談） |
| D | latency の表による並べ替え、operand の再利用 | 未 |
| E | graphics の stage（SPH、attribute、texture） | 未（p007 の 3D と一緒に） |

## 6. 段 A の結果（2026-10-15 P3、host と build だけ）

| file | 内容 |
| --- | --- |
| `src/drivers/gpu/nvrtx/compiler/sm75-isa.h` | 事実の値（MIT の notice、出典と sha256）と cbuf の約束 |
| `src/drivers/gpu/nvrtx/compiler/sm75-encode.[ch]` | 1 命令の encode（ALU の form、MOV・FADD・FMUL・FFMA・FMNMX・FSETP・ISETP・IADD3・IMAD・IMAD.WIDE・LOP3・SHF・SEL・MUFU・I2F・F2I・FRND・S2R・LDC・LDG・STG・EXIT・NOP・BRA、制御の bit） |
| `src/drivers/gpu/nvrtx/compiler/sm75-compile.[ch]` | `nvrtx_sm75_compile()`（SPIR-V → 共有の front → optimizer → 3.3 の選択、1 pass の register の割り当て、直列の依存、EXIT・BRA・128 byte の padding）、`nvrtx_sm75_compile_ir()`、`nvrtx_sm75_binary_free()`。skip の印は全部の block を走らせるので無視 |
| `platform/amd64/vmunix.mk` | `CONFIG_DRIVER_PCI_NVRTX=y` の時に上の 2 つと共有の `spirv.c`・`optimize.c`（i915 と重ならない分）を build |
| `plan/ws037/tests/nvrtx-sm75-host-test.[c,sh]`・`plan/ws037/tests/sm75/*.comp` | 4 節の試験。module は const（定数を書く、最初の目標）・float・integer・loop（拒む）と ws101 の add・ids |

| コマンド | 結果 |
| --- | --- |
| `sh plan/ws037/tests/nvrtx-sm75-host-test.sh` | `nvrtx_sm75_host_test: 39 checks, 0 failures`（既知の語 6 つが bit まで一致、field 16、const・add・ids・float・integer が interpreter で C の計算と一致、loop は「not compiled yet (stage A)」で拒む）。ASan/UBSan |
| `make -j16 ZEDBSD_CONFIG=plan/ws037/tests/config-amd64-nvrtx.mk BUILD=build/ws037-p008 build/ws037-p008/vmunix` | rc 0、`warning:` 0 行、`kernel include check: PASS`、`amd64 vmunix check: PASS`（compiler はまだ誰も呼ばないので link で落ちる） |
| `git diff --check` | 出力なし |

生成の大きさ（参考）: const 16 命令・8 register、add 40・12、ids 184・16、float 104・14、integer 96・16（直列の依存なので遅い。段 D で詰める）。

未実施: 実機（RTX 2070）で走らせること。p007 の compute の投入（QMD 2.2、cbuf 0〜7 の bind、code の置き場所、channel への push）が要る。interpreter は encode の逆を同じ field の定義で読むので、encode の bit の正しさは既知の語（6 つ）と field の試験だけが見ている。

## 7. P1（投入の口）への約束の案

compute の dispatch がすること（3.2 の表のとおり）:

1. `nvrtx_sm75_compile()` の `code`（`code_words` × 4 byte）を GPU の memory に置く（128 byte 揃え）。QMD の program の address と register の数 `gpr_count`、shared `shared_bytes`、local size `local_size[]` を書く。
2. cbuf 0（最低 0x200 + 16 × `storage_count` byte）: push constants を 0 から、storage の k 番目の GPU の仮想 address（low・high）と大きさを 0x200 + 16k から。`storage[k]` の (set, binding) で descriptor を引く。gl_NumWorkGroups は set `0xFFFFFFFF`（`DRV_GPU_IR_SYSTEM_SET`）binding 0 の storage で、group の数 x・y・z を持つ buffer。
3. cbuf 1〜7: `uniforms[j]` を cbuf 1 + j に bind。
4. 最初の確かめ: `plan/ws037/tests/sm75/const.comp`（local 32、1 buffer に 42 を書く）を 1 group で投げ、buffer の 32 語が 42 になること。次に ws101 の add.comp（a + b、n = 100 の push）。
