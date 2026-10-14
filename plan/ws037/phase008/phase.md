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
| B | loop・除算・atomic・shared・barrier | 実装・host 済み（下の 9 節） |
| C | 最初の compute の投入（段 P9c、`nvrtx.compute=1`）: GR の channel・compute の object・QMD 2.2・cbuf・code・launch・fence・結果の確かめ | 実装・host 済み、実機（R7c）は T 待ち（下の 8 節） |
| D | latency の表による並べ替え、operand の再利用 | 未 |
| E | graphics の stage（SPH、attribute、texture） | 頂点・fragment の最小（compositor の quad）を実装・host 済み（下の 10 節）。3D の投入（p007）は未 |

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

## 8. 段 C: 最初の compute の投入（2026-10-15 P3、Q1 の指示「段 C を先に。投入の口も P3 が作る」）

### 8.1 流れ（nouveau の利用者の GR の channel と NVK の dispatch と同じ順、どちらも MIT）

`nvrtx.gsp=1 nvrtx.gr=1 nvrtx.compute=1` の時だけ、P5g（golden context と scrubber）の後、P8 の前に段 **P9c** を流す（`nvrtx.stop=P9c` で飛ばせる）。

1. **arena**: 最後の使える region の上端から 168 MiB 以内、2 MiB 揃え、32 MiB。GR の arena（P5g）より下。先頭に page table（`nvrtx_mmu_gr_tables` の形、VA 0x20000000〜0x28200000 の 130 MiB、PT 65 枚）、0x80000 に instance block、0x81000 に USERD、0x100000 から channel 自分の context buffer（global でない物: MAIN・PATCH。global は golden の物を共有）。
2. **table**: context buffer は golden と同じ VA（`gr->virtual_address[]`）に privileged で map（PRIV_ACCESS_MAP は read-only、UNRESTRICTED は外す: nouveau の非 golden の promote と同じ）。0x28000000 から system memory の 10 page（ring・push 4・semaphore・program・QMD・cbuf 0・result）を coherent・volatile・非 privileged で map（`mmu.c` に `nvrtx_mmu_gr_map_system` を足した）。
3. **vaspace**: 外部所有（P4 と同じ引数）`0x90f10003` → device に `SET_PAGE_DIRECTORY`（根 = arena の先頭）→ TLB の invalidate。nouveau の利用者の vmm も外部所有（`r535_mmu_vaspace_new(…, true)`）。
4. **channel**: instance block に根を書く（`nvrtx_mmu_instance`）→ channel（0xc46f、`0xf1f00003`、chid 3、engine GR0、非 privileged、ring は 0x28000000、method buffer は system memory）→ **promote**（`nvrtx_gr_promote` を gr.c から公開: scrubber と同じ形で global は共有、MAIN・PATCH は新しく）→ **compute の object**（TURING_COMPUTE_A 0xc5c0、`0xc5c00003`、引数なし）→ BIND（engine GR0）→ SCHEDULE。
5. **program**: `nvrtx_compute_first_ir()`（SPIR-V を使わず、const.comp と同じ意味の IR を手で組む: `o[gl_GlobalInvocationID.x] = 42`、local 32）→ `nvrtx_sm75_compile_ir()` → code を program の page へ。
6. **cbuf 0**: 0x200 に result の VA（low・high）と大きさ 128（3.2 節の約束）。QMD 2.2（`nvrtx_qmd_build`、NAK の fill_qmd と同じ欄: version 2.2・global caching・call limit なし・grid 1×1×1・workgroup 32×1×1・register 数・shared 0 と carve-out 9/9/17・cbuf 0（0x300 byte）・program の 64 bit の address）。
7. **push**: subchannel 1 に SET_OBJECT 0xc5c0、SHARED_MEMORY_WINDOW 0xfe000000・LOCAL_MEMORY_WINDOW 0xff000000（NVK の dispatch state）、INVALIDATE_SHADER_CACHES（instruction・data・constant）、SEND_PCAS_A = QMD >> 8、SEND_SIGNALING_PCAS_B = invalidate | schedule（Turing）。その後に P5 と同じ host の fence（semaphore 0x4242、release + WFI、membar、non-stall）。GPFIFO の entry 0、USERD の GP_PUT = 1、doorbell = (GR の runlist << 16) | 3。semaphore を 2 s poll。
8. **確かめ**: result の page を 0xdeadbeef で埋めておき、0〜31 語が 42、32 語目が 0xdeadbeef のまま。
9. **停止**（finish、P6 の前、GR の停止の前）: SCHEDULE 0 → compute の object・channel を FREE → UNSET_PAGE_DIRECTORY → vaspace を FREE。system memory は GSP の停止の後に解放。

出典（MIT、Mesa 25.3.6、sha256）: `headers/nvidia/classes/clc3c0qmd.h`（QMD 2.2 の欄の位置、761286640c93…482b）、`classes/clc5c0.h`（TURING_COMPUTE_A の method、c716205f0786…1f2e）、`compiler/nak/qmd.rs`（fill_qmd、73f4daf7…6698）、`vulkan/nvk_cmd_dispatch.c`（dispatch state と launch、833d88ad7fec…53a2）、`winsys/nouveau_device.c`（Turing の shared の carve-out 32・64 KiB）。nouveau の `rm/r535/gr.c`（promote の形、MIT の notice）・`r535/vmm.c`（利用者の vmm は外部所有）。

### 8.2 file

| file | 中身 |
| --- | --- |
| `src/drivers/gpu/nvrtx/nvrtx-compute.h`・`compute-launch.c`（新） | hardware に触れない部分: `nvrtx_qmd_build`・`nvrtx_push_compute_launch`・`nvrtx_compute_first_ir` |
| `src/drivers/gpu/nvrtx/compute.c`（新） | 段 P9c（`nvrtx_compute_start`・`_stop`・`_release`） |
| `mmu.c`・`nvrtx-gsp.h` | `nvrtx_mmu_gr_map_system`（足しただけ） |
| `gr.c`・`nvrtx-gpu.h` | `gr_promote` を `nvrtx_gr_promote` として公開（中身は同じ）、`struct nvrtx_compute_state`、handle 3 つ |
| `stage.c`・`nvrtx-private.h` | `nvrtx.compute=1`（`nvrtx_stage_compute_wanted`） |
| `gsp.c` | P5g の後に P9c、finish で `nvrtx_compute_stop`（display・gr の停止の前）、release で `nvrtx_compute_release` |
| `platform/amd64/vmunix.mk` | NVRTX=y の時だけ 2 file |
| `plan/ws037/tests/config-amd64-nvrtx.mk` | `NVRTX_RUN=R7c`（`nvrtx.gsp=1 nvrtx.gr=1 nvrtx.compute=1`） |

### 8.3 確かめ（host と build）

| コマンド | 結果 |
| --- | --- |
| `sh plan/ws037/tests/nvrtx-sm75-host-test.sh` | `60 checks, 0 failures`（段 A の 39 に加えて: QMD の欄を clc3c0qmd.h の位置で読む 11、launch の push の 14 語、最初の program を IR から compile して interpreter で 32 語が 42・33 語目は元のまま、system page の entry と範囲の拒否） |
| `sh plan/ws037/tests/nvrtx-gsp-host-test.sh` | `PASS nvrtx gsp: 180 checks`（mmu・gr の変更の後も同じ） |
| `make -j16 ZEDBSD_CONFIG=plan/ws037/tests/config-amd64-nvrtx.mk BUILD=build/ws037-p008 build/ws037-p008/vmunix` | rc 0、`warning:` 0 行、include check・vmunix check PASS。既定 n の build は `vmunix.mk` の `ifeq` の中だけの変更なので変わらない |

未実施: 実機。R7g（P5g）がまだ実機で通っていないので、R7c はその後に流す。

### 8.4 T への手順（R7c、方式 B、R7g の image の系列に足す）

前提: R7g が合格（`P5g ok: golden context`・`P5g ok: scrubber`）。合格していなければ R7c は流さない（P9c は P5g の後でしか動かない）。

image: `plan/tools/guest/test-image.sh --no-harness plan/ws037/tests/config-amd64-nvrtx.mk build/ws037-nvrtx/r7c NVRTX_RUN=R7c`（main の merge の commit で）。各 run の前に host の reboot と `prepare.sh --check`。判定は今までどおり画面の PNG（`dmesg | grep nvrtx`）。

合格の印（R7g の印に加えて、P5g の後・P8 の前に）:

1. `P9c begin`
2. `P9c arena at …, own buffers … bytes`
3. `P9c page tables at …`
4. `P9c ok: vaspace`
5. `P9c ok: compute channel 3 on runlist N`
6. `P9c program: 16 instructions, 10 registers`
7. `P9c ok: fence 0x4242 reached after N us`
8. **`P9c ok: compute wrote 32 words of 42`**
9. その後は R7g と同じ（P8a の行、`P8b is not in this kernel …`、`P6 ok: channel and vaspace freed`（`P6: N compute frees failed` が無い）、`P7 ok: region torn down`、login prompt）

FAIL の時に PNG に残す物: `rm: class … status …`・`rm: control … status …` の行（どこで断られたか: 0x90f1・0x801813・0xc46f・0x2080012b（promote）・0xc5c0・0xa06f0104（BIND）・0xa06f0103（SCHEDULE））、`P9c fail: …` の行、`P6 channel error: …`（RC の event: engine・chid・exceptType）。fence が来ない時は `P9c fail: fence … result word 0 …` の値。`P7 fail` なら host を reboot。

### 8.5 危険と未確認

1. 1 つの device の下に外部所有の vaspace を 2 つ（P4 と P9c）作って、それぞれに SET_PAGE_DIRECTORY を送ってよいか（nouveau は vmm ごとに client と device を作る）。断られたら nouveau に合わせて client・device を分ける。
2. program・QMD・cbuf を system memory（volatile）に置いた。NVK は VRAM に置く。SM が system memory から命令を読めない・遅い時は VRAM（arena）に移す。
3. SLM（local memory）を設定していない（program は local memory を使わない。NVK も slm 0 なら設定しない）。
4. 直列の依存の印（段 A）は遅いが正しいはず。命令の encode の正しさは既知の語 6 つと field の試験だけが見ている。実機で値が違えば、encode と制御の bit から疑う。

## 9. 段 B: loop・除算・atomic・shared memory・barrier（2026-10-15 P3、host と build だけ）

| IR | SM75 | 出典の事実（NAK、MIT） |
| --- | --- | --- |
| LOOP_BEGIN・LOOP_END | `BSSY Bn, →BSYNC`（入れ子の深さ n、16 まで）… 本体 … `ISETP.NE P0, cond, RZ`、`@P0 BRA 先頭`（分岐の条件の欄 87..90）、`BSYNC Bn`。warp の thread が別々の回で抜けても BSYNC で揃う | BSSY 0x945（barrier 16..19、offset 34..63）、BSYNC 0x941、BRA の条件 87..90。直列の依存では分岐・BSSY・BSYNC は全部の scoreboard を待ち yield（calc_instr_deps の serial） |
| 値の生存 | loop の前に作られ loop の中で読まれる値は loop の終わりまで register を保つ（次の回がまた読む） | — |
| UDIV・UMOD | `u2f`→`MUFU.RCP`→`×(2^32−512)`→`f2u` で約 2^32/d、`r += hi(r × (−d × r))` で 1 回の精度の改善、`q = hi(n × r)`、`m = n − q × d`、`m ≥ d` なら `q+1`・`m−d` を 2 回（hi は IMAD.WIDE.U32 の上の register） | 一般的な固定小数点の逆数の除算（NIR の idiv の lowering と同じ考え）。RCP の誤差 ±1 ulp を interpreter で入れても正しいことを確かめた |
| IDIV・IREM | 絶対値で UDIV/UMOD、商は符号の xor が負なら、余りは被除数が負なら `@P0 IADD3 −x` | GLSL の `%`（OpSMod）は front が IREM と除数の符号への直しに分ける |
| ATOMIC（storage） | `ATOMG.<op>.STRONG.GPU`（0x3a8、op 87..90: ADD 0・MIN 1・MAX 2・AND 5・OR 6・XOR 7・EXCH 8、型 73..75: U32 0・S32 1）、compare-and-exchange は `ATOMG.CAS`（0x3a9、比べる値 32..39・書く値 64..71）。SUB は値を負にして ADD。述語付きは `@P0` | sm70_encode.rs の OpAtom |
| ATOMIC（shared）・LOAD_SHARED・STORE_SHARED | `ATOMS`（0x38c）・`ATOMS.CAS`（0x38d）・`LDS`（0x984）・`STS`（0x388）、offset の register そのまま | 同 OpAtom・OpLd・OpSt の shared |
| BARRIER | （global の fence を伴う時だけ `MEMBAR.GPU`）`BAR.SYNC 0`（0xb1d）、その後に NOP（delay 2） | OpBar、OpMemBar（0x992、scope 76..78）。NOP は calc_instr_deps の「実行の latency のある命令の後」 |
| FENCE | `MEMBAR.GPU`（global）か `MEMBAR.CTA` | — |

- 結果の記述に `barrier_count`（barrier を使えば 1、QMD の BARRIER_COUNT）を足した。shared の大きさは `shared_bytes`。
- 1 つの IR の命令の最大の展開を 40 に（除算が約 25）。
- 試験（`nvrtx-sm75-host-test.sh` → **76 checks, 0 failures**）: interpreter を workgroup の invocation を barrier ごとに回す形に変えた（全員が barrier に来たら次の回、barrier で待つ間に終わる invocation は失敗）。新しい module: `plan/ws037/tests/sm75/loop.comp`（invocation ごとに回数の違う入れ子の loop）・`division.comp`（固定の端の値と疑似乱数の 32 組、符号付きは OpSMod の意味）・`shared.comp`（barrier を挟んだ shared と shared の atomic）、ws101 の `atomic.comp`（全部の storage の atomic、述語付き、古い値の返り）。BSSY・BSYNC・条件付き BRA・BAR・MEMBAR・ATOMG・LDS の field の試験。拒む物の試験は texture（SAMPLE）に変えた。
- nvrtx=y の kernel: rc 0、warning 0。

段 B の残り: texture・image、graphics の stage（段 E）、spill、latency の表による並べ替え（段 D）。shared の atomic の float、64 bit の atomic は IR に無い。

## 10. 段 E: 頂点・fragment の program（2026-10-15 P3、host と build だけ）

目標（Q1）: Keiland の compositor が使う程度（全画面の quad の頂点、texture の sample、単色・blend）。最初の対象は `userland/desktop/wayland/shaders/quad.vert`・`quad.frag`。

| IR | SM75 | 出典の事実（MIT） |
| --- | --- | --- |
| 頂点の LOAD_INPUT（location L、成分 c） | `ALD R, a[0x80 + 16L + 4c]`（vertex・offset は RZ、1 成分）。vertex 番号・instance 番号は a[0x2fc]・a[0x2f8] | NAK の OpALd（0x321、address 40..49）、nak_private.h の NAK_ATTR_* |
| 頂点の STORE_OUTPUT | 位置 a[0x70 + 4c]、point size a[0x6c]、location L は a[0x80 + 16L + 4c] へ `AST` | OpASt（0x322、data 32..39、vertex 64..71） |
| fragment の LOAD_INPUT | `IPA`（frequency pass、flat は constant）。perspective の入力は program の始めに `IPA a[0x7c]`（w）と `MUFU.RCP` で 1/w を作り、毎回掛ける。gl_FragCoord は a[0x70 + 4c] | OpIpa（0x326、address/4 は 64..71、frequency 78..79）、nak_nir_lower_fs_inputs.c の SM70 の道 |
| fragment の STORE_OUTPUT（colour L） | 書く location を昇順に R0 から 4 つずつ割り当て（割り当てから外す）、`MOV` でそこへ。EXIT で hardware が読む | from_nir.rs の OpRegOut の順 |
| SAMPLE・SAMPLE_LOD・SAMPLE_BIAS（2D） | 座標を register の組へ、`TEX`（handle は cbuf 0 の `0x400 + 4k` の語、opcode 0xb60）、結果は 2 つの組（4 成分）。LOD は fragment は implicit、頂点は zero、明示・bias は src2 | OpTex の CBuf の形（handle の位置 40..58、2 つ目の dst 64..71、次元 61..63、channel 72..75、LOD mode 87..89）、nak_nir_lower_tex.c の src0/src1 の詰め方 |
| KILL | `ISETP P0`、`@P0 KILL`（0x95b） | OpKill |
| SPH（version 4、128 byte） | type・version 4・stage・SASS version 1・fragment の MRT_ENABLE・KILLS_PIXELS・global memory の bit・頂点の store request、input map（頂点は bit、fragment は 2 bit の補間の種類）、output map、colour target | sph.rs の encode_header、cla097sph.h の欄 |

- 公開の API: `nvrtx_sm75_compile_stage(words, count, stage, ...)`（compute・vertex・fragment）。結果に `graphics`・`sph[32]`・`textures[]`（sampled image の (set, binding)、handle の語の順）を足した。graphics の program は SPH の 128 byte の後に code を置き、program の address は SPH を指す（NVK と同じ形）。3D の投入の側（p007）がこの配置で upload する。
- cbuf 0 の約束に `0x400 + 4k`: k 番目の sampled image の texture handle（tic の index は 19:0、tsc の index は 31:20、Turing の NVK の descriptor と同じ）を足した（sm75-isa.h）。
- 試験（`nvrtx-sm75-host-test.sh` → **88 checks, 0 failures**）: quad.vert と quad.frag を glslc で SPIR-V にして compile し、SPH の欄（type・version・stage・input と output の map・store request・colour target・perspective の補間）を確かめ、interpreter で走らせる（ALD・AST・IPA（SPH の map が perspective なら w = 2 を掛けて返す）・TEX（handle と座標を返す）・KILL・EXIT の R0..R3）。頂点の位置と texture の座標、fragment の R0..R3 が C の計算と一致した。
- nvrtx=y の kernel: rc 0、warning 0。
- 残り: blend・単色は 3D の state（p007）で、shader の側は今のままで足りる。panel.vert/frag・cell など他の desktop の shader、derivative（DDX/DDY）、texel offset、array・cube、depth の出力、geometry、TEXTURE（一般の message）は未。
