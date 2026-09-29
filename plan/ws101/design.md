<!-- awesome-plan project=zedbsd record=ws101-design -->

# WS101 の設計: GPU の compute（i915 の Vulkan の compute、GLES 3.1 の compute、Noct の自動並列化）

Parent: [WS101](ws.md)。作成: ws101-p001（2026-09-30、サブエージェント）。見直しの結果は [phase001](phase001/phase.md)。

この文書は設計であり、実行の許可ではない。各 Phase は Queue に入ってから実行する。値（bit の位置・message の型）は Mesa 25.0.7
（`/home/awe/p014-c/mesa`、既存の `intel/eu-encoding-gen12.h` が引く版と同じ）と genxml（gen120 が取り込む gen110・gen80・gen60）で
読んだものである。実装の Phase は値を encoding の header に出典付きで写し、Mesa の assembler・disassembler で照合してから GPU に出す。

## 0. 今の状態（2026-09-30 の読み取り）

| 部分 | 状態 | 所在 |
| --- | --- | --- |
| SPIR-V の parser | Vertex・Fragment だけ（`i915_spirv_declare_entry_point` が他の execution model を断る）。OpExecutionMode は読み捨て。storage class は UniformConstant・Input・Uniform・Output・Function・PushConstant・StorageBuffer。Workgroup は無い。atomic・barrier の opcode は無い。選択の中の OpReturn は扱う（loop の中は断る） | `compiler/spirv.c` |
| IR | scalar の SSA、構造化の制御は if 変換、loop は LOOP_BEGIN〜LOOP_END。`LOAD_STORAGE`・`STORE_STORAGE`（A64、store だけ predicate を持つ） | `compiler/ir.h` |
| code generator | SIMD8 だけ。SSBO は push data に置いた 64 bit の address に channel ごとの byte offset を足して A64 untyped read/write（SFID 12、BTI 253）。spill は OWord block（SFID 10）。thread の終わりは stage ごとの URB・RT write | `compiler/compile.c`、`compiler/eu.c`、`intel/eu-encoding-gen12.h` |
| 実行器 | 記録した op を vkQueueSubmit で 1 本の batch にし同期で走らせる。op ごとに flush・`PIPELINE_SELECT(3D)`・`STATE_BASE_ADDRESS` を出す自己完結の形で、op の後ろに flush があるので `vkCmdPipelineBarrier` は何もしない。opcode 110・111（Dispatch・DispatchIndirect）と 66（CreateComputePipelines）は ENOTSUP。`vkCmdBindPipeline`・`vkCmdBindDescriptorSets` の bind point は読み捨て | `render/command.c`、`render/draw.c`、`render/state.c`、`render/heap.h` |
| device の情報 | queue に COMPUTE_BIT。上限は ES 3.1 の最小値と同じ（shared 16384、invocation 128、size 128・128・64、count 65535） | `render/instance.c` |
| kernel の compute の試験 | `eu-test.c`: 3D で SBA → GPGPU の選択 → VFE（最大 thread 112×DSS−1、URB 2・2）→ MSF → IDL（1 thread）→ GPGPU_WALKER（SIMD8、1 group、right mask 1）→ MSF。kernel は r0 を r127 へ写し SFID 7（thread spawner）へ EOT。5330 の実機で動いた | `tests/execution/eu-test.c` |
| libvulkan | `vkCreateComputePipelines`・`vkCmdDispatch`・`vkCmdDispatchIndirect` の encode は既にある（Venus ではそのまま host に行く） | `userland/desktop/libvulkan/` |
| libglesv2 | GLSL ES 3.00 まで。compute の stage・buffer block（SSBO）・`shared`・atomic・barrier は無い。`GL_VERSION` は「OpenGL ES 3.0 Kei」、`GL_RENDERER` は「Kei OpenGL ES on Vulkan」。GL の buffer は CPU の bytes を持ち、device が書いた buffer（transform feedback）は CPU の読みの前に読み戻す仕組みがある | `userland/desktop/libglesv2/` |
| libegl | `eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA)`、pbuffer、`EGL_OPENGL_ES3_BIT` は既にある | `userland/desktop/libegl/egl.c` |
| header | `include/libc/GLES3/gl3.h` はある。`gl31.h` は無い（Noct の `accel_opengles.c` は `<GLES3/gl31.h>` を include する） | `include/libc/GLES3/` |
| Noct | snapshot `fcf5759`。`NOCT_ENABLE_ACCEL` は OFF。OpenGL ES の backend は CMake の `CMAKE_SYSTEM_NAME` が Linux・FreeBSD のときだけ選ばれ、zedBSD の toolchain file は `zedBSD` を名乗るので、ON にしても backend が入らない。link は pkg-config の egl・glesv2 | `build/NoctLang/CMakeLists.txt`（読み取りのみ） |

## 1. compiler（SPIR-V の GLCompute → IR → Gen12 の EU）

### 1.1 Mesa の brw のどこに従い、どこを変えるか

| 事項 | Mesa（Gen12.0、verx10 120） | zedBSD の設計 | 理由 |
| --- | --- | --- | --- |
| payload の r0 | header。WorkGroupID は r0.1・r0.6・r0.7（`emit_work_group_id_setup`）、barrier ID は r0.2 の bit 30:24（`emit_barrier`、mask 0x7f000000） | 同じ | hardware の事実 |
| push data | r1 から CURBE（`cs_thread_payload` は r0 だけ、`assign_curb_setup`）。cross-thread の後に per-thread | 同じ | 同上 |
| LocalInvocationID・Index | per-thread の push は subgroup ID 1 word。ID は shader が `subgroup_id * 8 + channel` から udiv・umod で作る（`brw_nir_lower_cs_intrinsics.c`） | **per-thread の push を 4 register**（x・y・z・index を channel ごとに）とし、実行器が CPU で埋める | baseline の code generator に割り算を足さない。値は local size だけで決まるので pipeline の作成で 1 回作れる。費用は 1 thread 128 byte |
| NumWorkGroups | anv は push constant。indirect は address を push し shader が読む | cross-thread の push の「system」register の dword 0〜2。indirect は CS が indirect buffer から CURBE の memory へ写す（§2.5） | shader に分岐を足さない |
| SIMD 幅 | 8・16・32 を選ぶ | **SIMD8 だけ** | 既存の compiler が SIMD8 だけ。SIMD16 は Future Work（性能） |
| SSBO | A64（`brw_dp_a64_untyped_surface_rw_desc`） | 既存の A64 のまま | 既にある |
| SLM | HDC の untyped surface read・write、BTI 254（`GFX7_BTI_SLM`）、SFID 12 | 同じ | hardware の事実 |
| atomic | SSBO は A64 untyped atomic（SFID 12、型 0x12）、SLM は untyped atomic（型 2、BTI 254） | 同じ | 同上 |
| barrier | gateway（SFID 3）の barrier の message（subfunction 4）＋ `sync.bar`。group が 1 thread に収まれば出さない | 同じ | 同上 |
| fence | Gen11+ の LSC 以前: SFID 10 の MEMORY_FENCE（型 7、commit、BTI 0 = global、254 = SLM）。acquire の前に `sync.allwr`、fence の後に完了待ち | 同じ | 同上 |
| EOT | Gen12.0 は thread spawner（SFID 7）へ r0 の写しを送る（`emit_cs_terminate`）。g112〜127 から | 同じ。`eu-test.c` の実機で動いた kernel の最後の 2 命令と語を比べる | 同上 |

license の境界（[設計方針](../master-design-policy.md) §2.1）: i915 の compiler と実行器は base system で、Mesa の code は取り込まない。写すのは
hardware の事実（field の位置と値、message の型、register の番号）だけで、既存の `intel/eu-encoding-gen12.h`・`intel/genxml.h` の書式
（MIT の notice、Mesa の file と sha256）に出典を足す。算法（ID の作り方、CURBE の配置、barrier の lowering）は新たに書く。

### 1.2 SPIR-V の受け入れ

- **execution model** GLCompute（5）を `I915_STAGE_COMPUTE`（新しい値 2、`I915_STAGE_COUNT` は 3）にする。
- **execution mode** `LocalSize`（17）を読む。`LocalSizeId`（38、`OpExecutionModeId` 331）は operand の id が `OpConstant` のときだけ受ける。
  spec constant（`OpSpecConstant*`、`SpecId` の decoration）は断る（refuse の diagnostic）。local size の積が 128（device の上限）を超えるものは断る。
- **built-in**（Input の変数の BuiltIn の decoration）: `LocalInvocationId`（27）、`LocalInvocationIndex`（29）、`WorkgroupId`（26）、
  `NumWorkgroups`（24）、`GlobalInvocationId`（28）。`WorkgroupSize`（25）は glslang が BuiltIn の付いた `OpConstantComposite` で出すので、
  定数として読み、decoration は効果なしとして受ける。`SubgroupSize` などは断る。
- **storage class** `Workgroup`（4）の変数を SLM に置く（§1.4）。
- **decoration** `Coherent`（23）・`Volatile`（21）・`Restrict`（19）・`Aliased`（20）を「効果なし」の一覧に足す。根拠: code generator は
  命令を並べ替えず、SSBO の send は MOCS の uncached（stateless の MOCS）で memory へ行き、fence が順序を担う。
- **命令**: `OpControlBarrier`（224）、`OpMemoryBarrier`（225）、`OpAtomicLoad`（227）・`Store`（228）・`Exchange`（229）・
  `CompareExchange`（230）・`IIncrement`（232）・`IDecrement`（233）・`IAdd`（234）・`ISub`（235）・`SMin`（236）・`UMin`（237）・`SMax`（238）・
  `UMax`（239）・`And`（240）・`Or`（241）・`Xor`（242）、`OpArrayLength`（68）。float の atomic、`OpAtomicFlag*`、image の atomic は断る。
- compute の stage で `OpImageSample*` と Input・Output の location の変数は断る（範囲の外。要れば後の Phase）。
- `OpReturn` は今の扱いのまま（選択の中は channel を落とす、loop の中は断る）。Noct の `if (lane >= trip) return;` はこれで通る。

### 1.3 IR の追加（`compiler/ir.h`）

| 新しい op | 意味 |
| --- | --- |
| `I915_IR_LOAD_SYSTEM` | dst = `component` の built-in: 0〜2 LocalInvocationID.xyz、3 LocalInvocationIndex、4〜6 WorkgroupID.xyz、7〜9 NumWorkgroups.xyz |
| `I915_IR_LOAD_SHARED` | dst = SLM の byte offset src[0] の word。`component` 1 なら src[2] の Boolean の channel だけ |
| `I915_IR_STORE_SHARED` | SLM の byte offset src[0] = src[1]。`component` 1 なら src[2] の channel だけ |
| `I915_IR_ATOMIC` | dst = 前の値。`immediate` = 演算（ADD・SUB・SMIN・UMIN・SMAX・UMAX・AND・OR・XOR・XCHG・CMPXCHG、INC・DEC は ADD・SUB の 1）。`location` = storage buffer の uniform の番号、または `I915_IR_LOCATION_SHARED`（0xFFFFFFFD）で SLM。src[0] offset、src[1] 値、src[2] 比べる値（CMPXCHG）、src[3] predicate（`component` の bit 0 が 1 のとき）。`component` の bit 1 は結果を使うか（使わなければ応答を求めない） |
| `I915_IR_BARRIER` | workgroup の実行の barrier。`immediate` は前に置く fence（bit 0 global、bit 1 SLM） |
| `I915_IR_FENCE` | memory の fence。`immediate` の bit 0 global、bit 1 SLM、bit 2 acquire（前に `sync.allwr`） |

既存の変更: `I915_IR_LOAD_STORAGE` に STORE と同じ predicate（`component` 1 → src[2]）を足す。predicate の外の channel の読みは、
PPGTT が未使用の範囲を scratch page に向けるので fault にはならない（`ppgtt.c` の scratch tower）が、別の object を読む・帯域を使うので止める。
**STORE・ATOMIC は predicate が必須**（外の channel が他の buffer を書き換えないため）。

### 1.4 parser の lowering（`compiler/spirv.c`）

- **GlobalInvocationID** は parser が `WorkgroupID[c] * LocalSize[c] + LocalInvocationID[c]` に展開する（`IMUL` と `ICONST`、LocalSize が 1 なら
  掛けない）。
- **Workgroup の変数**: 宣言の順に SLM の byte offset を割り当てる。配置は自前の自然な形（scalar 4 byte、vecN は 4N、配列の stride は要素の
  大きさ、struct は宣言の順に 4 byte 境界）。Workgroup の変数には Offset・ArrayStride の decoration が無い（Vulkan 1.0 の GLSL）ので、
  この配置を compiler が決めてよい。合計を binary の `shared_bytes` にする。access chain の動的な index は storage buffer と同じ
  `i915_spirv_storage_offset` の形で byte offset にする。
- Workgroup の load・store は **forwarding をしない**（Function の変数の store-to-load forwarding は他の invocation の書きを見ないため）。
  毎回 `LOAD_SHARED`・`STORE_SHARED` を出し、block の predicate を付ける。
- **atomic**: pointer の storage class で A64（StorageBuffer、Uniform＋BufferBlock）か SLM を選ぶ。memory semantics が Relaxed（0）以外の
  ときは、release なら前に、acquire なら後ろに該当の `FENCE` を出す（GLSL の `atomicAdd` は glslang が Relaxed で出す）。
  `OpAtomicLoad`・`OpAtomicStore` は predicate 付きの普通の load・store と fence にする。
- **barrier**: `OpControlBarrier(exec, mem, sem)` は exec が Workgroup なら `BARRIER`（immediate は sem の WorkgroupMemory → SLM、
  UniformMemory → global）。exec が Subgroup 以下なら fence だけ。`OpMemoryBarrier(scope, sem)` は `FENCE`。scope・sem は定数の id
  でなければ断る。
- **`OpArrayLength`**: storage buffer の address の register（push の 1 register、今は dword 0〜1 だけ使う）の dword 2 に実行器が range の
  byte 数を置く。長さ = (range − member の offset) / ArrayStride を `UDIV` で作る（stride が 2 の冪なら `SHR`）。
- loop の中の `BARRIER` は受ける（GLSL は一様な制御の中の barrier を求める。IR の loop は active な channel が残る間まわるので、
  workgroup の全 thread の回る数が一様であることは shader の責任）。

### 1.5 register の約束（SIMD8 の compute）

```
r0            header（WorkgroupID: r0.1 x、r0.6 y、r0.7 z。barrier ID: r0.2[30:24]。scratch: r0.3・r0.5）
r1 .. rC      cross-thread の push data（C register）:
                push constants（push_constant_bytes、register 単位）、
                uniform block の写しと storage buffer の address（1 buffer 1 register: dword 0〜1 address、dword 2 range）、
                最後に system の 1 register（NumWorkgroups x・y・z を dword 0〜2。使うときだけ）
rC+1 .. rC+4  per-thread の push（4 register）: LocalInvocationID.x、.y、.z、LocalInvocationIndex（8 channel の dword）
rC+5 ..       値（今の約束どおり r16 から、payload が r15 を越えるならその後ろから、r95 まで）
r127          EOT の message（r0 の写し）
```

- binary（`struct i915_shader_binary`）に足すもの: `local_size[3]`、`shared_bytes`、`uses_barrier`、`uses_num_workgroups`、
  `cross_thread_regs`（C）、`per_thread_regs`（4）、`system_offset`（cross-thread の中の system の byte の位置、未使用は 0xFFFFFFFF）。
  push の配置（`push_regs`、`push_constant_bytes`、`blocks`）は今の `i915_compile_blocks` をそのまま使う。
- per-thread の 4 register は、使わない ID があっても常に置く（IDD の読みの長さを固定にし、実行器を簡単にする）。
- scratch（spill）は今の vertex・fragment と同じ仕組み（r0.3・r0.5 の header、BTI 253 の OWord block）。compute の scratch の thread ID の
  数は 112 × DSS（Mesa の `scratch_ids_per_subslice = max_cs_threads`）。

### 1.6 EU の lowering と encoding（Gen12、Xe-LP）

message の descriptor は `brw_message_desc`（mlen 28:25、rlen 24:20、header 19）と `brw_dp_desc`（BTI 7:0、control 13:8、type 18:14）の形。

| IR | EU |
| --- | --- |
| `LOAD_SYSTEM` | ID: per-thread の register から `mov`。WorkgroupID: r0 の scalar（`<0;1,0>:ud`）から `mov`。NumWorkgroups: system register の scalar から `mov` |
| `LOAD_SHARED` | `send(8)` SFID 12、untyped surface read（型 1）、control 0x2e（channel mask 0xE、SIMD8 = 2<<4）、BTI 254、mlen 1（offset の register がそのまま address の payload）、rlen 1。predicate は f0.0 |
| `STORE_SHARED` | 同じく write（型 9）、mlen 1、ex_mlen 1（値）、rlen 0、predicate |
| `ATOMIC`（SLM） | SFID 12、untyped atomic（型 2）、control = aop（3:0）\| SIMD8（bit 4）\| 応答（bit 5）、BTI 254、mlen 1、ex_mlen 1（CMPXCHG は 2）、rlen 1 か 0 |
| `ATOMIC`（SSBO） | SFID 12、A64 untyped atomic（型 0x12）、control = aop \| 応答（bit 5）、BTI 253、mlen 2（A64 の address の組。今の `i915_compile_storage` の組み立てを使う）、ex_mlen 1・2、rlen 1 か 0 |
| `FENCE` | acquire なら `sync.allwr`。global: SFID 10、MEMORY_FENCE（型 7）、control bit 5（commit）、BTI 0、mlen 1（r0）、rlen 1、header。SLM: 同じく BTI 254。exec size 1、NoMask。その後 fence の dst の完了を待つ（`sync.nop` に SBID の dst の wait） |
| `BARRIER` | 前に immediate の fence。`mov(8)` tmp 0（NoMask）、`and(1)` tmp.2 r0.2 0x7f000000（NoMask）、`send(8)` null tmp SFID 3（gateway）、descriptor の bit 2:0 に subfunction 4（barrier。Mesa の `brw_eu_inst.h` の `gateway_subfuncid` = MD12(2..0)）、mlen 1、rlen 0、header なし、NoMask。続けて `sync.bar`。group の invocation が 8 以下なら何も出さない |
| thread の終わり | `mov(8)` r127 r0（NoMask）、`send(8)` null r127 SFID 7、mlen 1、EOT |

aop（`BRW_AOP_*`）: AND 1、OR 2、XOR 3、MOV 4、INC 5、DEC 6、ADD 7、SUB 8、IMAX 10、IMIN 11、UMAX 12、UMIN 13、CMPWR 14。
CMPXCHG の 2 つの値の順（Mesa の `MEMORY_LOGICAL_DATA0`・`DATA1` と `atomic_comp_swap` の割り当て）は p005 で Mesa を読み、brw_asm で確かめる。
`sync` の function（Mesa の `brw_eu_defines.h` の `tgl_sync_function`: NOP 0、ALLRD 2、ALLWR 3、BAR 0xe。確認済み）も同じく写す。
新しい命令はすべて `eu.c` の encoder に足し、encoding の header に Mesa の file と sha256 を出典として足す。scoreboard（SWSB）は既存の
`scoreboard-check.h` で健全性を確かめる（send の dst を読む前の wait、fence の完了待ち）。

### 1.7 Gen12 で気をつけること

- EOT の send は g112〜g127 から（既存の `COMPILE_EOT_GRF`）。
- barrier の message は NoMask（全 channel が dispatch されない最後の thread でも thread として 1 回）。
- SLM の fence は group が 1 thread に収まるなら省いてよい（Mesa と同じ）が、最初は常に出して正しさを先に確かめる。
- `LOAD_SHARED` の predicate の外の channel の register は古い値のまま残る。値は SELECT でしか観測されないので問題ない（texture の guard と同じ理屈、ws075-p021）。

## 2. 実行器（`src/drivers/gpu/i915/render/`）

### 2.1 object と記録

- **compute pipeline**: `struct i915_gfx_pipeline` に `bind_point`、`compute`（shader module）、`cs_binary`、per-thread の ID の表
  （4 register × thread 数、作成時に作る）を足す。object の種類は同じ `I915_VK_OBJ_PIPELINE`（`vkDestroyPipeline` は 1 つ）。
  opcode 66（`vkCreateComputePipelines`）を `pipeline.c` が decode し（`i915_vkc_dec_VkComputePipelineCreateInfo` は codec にある）、
  作成の時に compile する。specialization info は空だけ受ける（空でない specialization は、compile の refuse と同じく pipeline の作成の失敗にし、
  kernel の message で理由を言う。黙って無視しない）。compute の kernel は instruction window の全体（`I915_GFX_INSTRUCTION_BYTES`、48 KiB。
  graphics の vertex 16 KiB・pixel 32 KiB の分け方を使わない）に置いてよく、越えたら作成の失敗。
- **bind point の分離**: `vkCmdBindPipeline` と `vkCmdBindDescriptorSets` の bind point を op に記録し、実行の state に
  `compute_pipeline`・`compute_dset[4]`・compute の dynamic offset を graphics と別に持つ（Vulkan の規則）。push constants は共通。
- **opcode 110**（`vkCmdDispatch`: [cmdbuf][x][y][z]）と **111**（`vkCmdDispatchIndirect`: [cmdbuf][buffer][offset]）を
  `i915_record_command` に足し、op `I915_GFX_OP_DISPATCH`・`I915_GFX_OP_DISPATCH_INDIRECT` にする。x・y・z のどれかが 0 の direct は何もしない。
  上限（65535）を越えるものは EINVAL。

### 2.2 dispatch の実行（新しい file `render/compute.c`。`dispatch.c` は wire の router なので名前を分ける）

1. pipeline の kernel を instruction window に置く（`drv_i915_gfx_window` に compute の code を vertex の位置で渡す）。
2. `transfer_pending` なら（前の GPU の op が buffer を書いたかもしれない）、CPU で uniform block を写す前に batch を走らせる（draw と同じ規則）。
3. slot を取り、state を書く:
   - dynamic heap（slot + 0x1000）の offset 0 に **INTERFACE_DESCRIPTOR_DATA**（8 dword）。
   - slot + 0x2000（dynamic の offset 0x1000）から **CURBE**: cross-thread C register、続けて thread ごとに per-thread 4 register。
     合計を 64 byte の倍数に丸める。C の上限 32 register と thread 16 で 1.5 KiB、slot の 0x2000〜0x3fff（8 KiB）に収まる。
   - surface heap は使わない（SLM は BTI 254、SSBO と scratch は 253 で binding table を引かない）。IDD の binding table は 0、数 0。
4. batch に書く（§2.3）。
5. 記録した後、SSBO を 1 つでも持つ dispatch は `transfer_pending = 1`（後の op が CPU で写す uniform の元を書いたかもしれない）。

### 2.3 batch（1 dispatch）

```
drv_i915_gfx_emit_context_setup()   既存: flush（CS stall・RT・depth・DC、RT に HDC の pipeline flush）、PIPELINE_SELECT(3D)、
                                    STATE_BASE_ADDRESS（general = compute の scratch、surface・dynamic = slot、instruction = window）、
                                    invalidate。SBA を 3D で出すのは Wa_1607854226（eu-test と同じ）
PIPE_CONTROL  CS stall | RT flush | depth flush（→ HDC pipeline flush）   TGL の PRM: 3D → GPGPU の前
PIPELINE_SELECT(GPGPU)              0x69041312
PIPE_CONTROL  CS stall | stall at scoreboard                             eu-test と同じ。VFE の前の stalling PIPE_CONTROL
MEDIA_VFE_STATE (9 dw, 0x70000007)  scratch（Per Thread Scratch Space 3:0、Scratch Space Base Pointer 79:42）、
                                    Maximum Number of Threads 127:112 = 112 × DSS − 1、Number of URB Entries 111:104 = 2、
                                    URB Entry Allocation Size 191:176 = 2、CURBE Allocation Size 175:160 = 偶数に丸めた C + 4T
MEDIA_STATE_FLUSH (0x70040000)
[indirect だけ §2.5]
MEDIA_CURBE_LOAD (0x70010002)       CURBE Total Data Length 80:64、CURBE Data Start Address 127:96（dynamic からの offset）
MEDIA_INTERFACE_DESCRIPTOR_LOAD (0x70020002)  32 byte、dynamic の offset 0
GPGPU_WALKER (15 dw, 0x7105000d)    SIMD Size 159:158 = 0（SIMD8）、Thread Width Counter Maximum 133:128 = T − 1、
                                    Thread Group ID X/Y/Z Dimension、Right Execution Mask = 最後の thread の有効な channel、
                                    Bottom Execution Mask = 0xffffffff、indirect なら Indirect Parameter Enable（bit 10）
MEDIA_STATE_FLUSH
PIPE_CONTROL  CS stall | DC flush | HDC pipeline flush | pipe control flush   GPGPU → 3D の前（TGL の PRM）
PIPELINE_SELECT(3D)                 op は 3D で終わる（次の op の前提を保つ）
```

- **IDD**（gen120 の INTERFACE_DESCRIPTOR_DATA）: Kernel Start Pointer 47:6、Thread Preemption Disable bit 84 = 1（anv と同じ）、
  Denorm Mode bit 83 = 0、Sampler Count 0、Binding Table 0、Constant URB Entry Read Length 191:176 = 4、Read Offset 0、
  Number of Threads in GPGPU Thread Group 201:192 = T、Shared Local Memory Size 212:208（Gen9+ の encode: 0 = なし、1 = 1 KiB … 7 = 64 KiB）、
  Barrier Enable bit 213 = `uses_barrier`、Cross-Thread Constant Data Read Length 231:224 = C。
- T = ⌈group の invocation / 8⌉（最大 16）。Right Execution Mask = invocation が 8 の倍数なら 0xff、でなければ (1 << (n mod 8)) − 1。
- Gen12 では SLM は L3 の外（Mesa の `intel_l3_config.c`: SLM の重みは ver < 11 だけ）。L3 の設定は変えない。
- **per-thread の ID の表**: thread t の channel c は linear = 8t + c。linear < n なら x = linear mod sx、y = (linear / sx) mod sy、
  z = linear / (sx·sy)、index = linear。n 以上は 0（right mask で走らない）。pipeline の作成時に作り、dispatch ごとに CURBE へ写す。
- **cross-thread**: push constants（state の 128 byte）、uniform block の写し（draw と同じ関数で CPU が写す）、SSBO の address（descriptor の
  buffer の memory の VA + buffer の offset + descriptor の offset + dynamic offset）と range、system の NumWorkgroups。
- **scratch**: `i915_draw_scratch` の stage の添字に compute を足し、per-thread の大きさ × 112 × DSS の部分を割り当てる。
- **stateless の MOCS**: 今の context setup は `STATE_BASE_ADDRESS` の stateless の data port の MOCS を uncached（index 3）にする。SSBO の
  load・store はこれで正しい（op の後の DC flush も要らないほど保守的）。A64 の atomic が L3 を通らない uncached の MOCS で正しく
  動くかは PRM で確かめていない。p005 の実機の ATOMIC の step が違う値を出したら、compute の op だけ stateless の MOCS を write-back
  （index 2、instruction heap と同じ）にし、op の終わりの DC flush（既にある）で CPU と次の op に見せる。
- **storage の register の dword 2（range）**: `OpArrayLength` のため、compute の CURBE だけでなく draw の push data の storage の register にも
  range を置く（今は dword 0〜1 だけ）。

### 2.4 graphics と compute の切り替え

各 op が 3D で始まり 3D で終わるので、draw・rectangle の既存の前提は変わらない。compute の op は自分の中で 3D → GPGPU → 3D を行う。
anv の Gen12.0 の「3D から compute へ切り替えたら IDL を出し直す」は、dispatch ごとに IDL を出すので満たす。flush の中身は anv の
`flush_pipeline_select`（TGL の PRM の引用）に従う。Gen12.0 には untyped の DC の flush の独立の bit が無い（gen120 の PIPE_CONTROL）ので、
DC flush と HDC pipeline flush で代える（anv も 12.5 未満では同じ）。`PIPE_CONTROL` の helper（`drv_i915_batch_pipe_control`）は
HDC の flush を RT の flush に付けて出すだけなので、HDC だけを立てる形を足す（GPGPU の中では RT の flush を立てない。anv の注意:
「GPGPU の pipeline から tile cache を flush できない」）。

### 2.5 `vkCmdDispatchIndirect`

```
MI_LOAD_REGISTER_MEM  GPGPU_DISPATCHDIMX (0x2500) ← buffer + offset + 0     （anv の compute_load_indirect_params）
MI_LOAD_REGISTER_MEM  GPGPU_DISPATCHDIMY (0x2504) ← + 4
MI_LOAD_REGISTER_MEM  GPGPU_DISPATCHDIMZ (0x2508) ← + 8
MI_COPY_MEM_MEM × 3   CURBE の system register の dword 0〜2 ← 同じ 3 word（shader が NumWorkgroups を読むときだけ）
PIPE_CONTROL          CS stall（CURBE の memory の写しを MEDIA_CURBE_LOAD の前に確定させる。保守的な選択）
... MEDIA_CURBE_LOAD、IDL、GPGPU_WALKER（Indirect Parameter Enable）
```

- CURBE の memory への `MI_COPY_MEM_MEM` の書きが `MEDIA_CURBE_LOAD` の読みより前に見える保証は PRM で確かめていない（CS の中の
  順序と CS stall に頼る）。p006 の実機の試験（NumWorkgroups を読む shader の indirect）で違う値が出たら、anv の Gen12.0 の方式
  （push に indirect buffer の address を置き、shader が A64 で 3 word を読む）に切り替える。これは compiler の `LOAD_SYSTEM` の
  lowering の差し替えで済み、p006 の中で直す。
- indirect の数が 0 の場合: anv は特別な扱いをしない。p006 で 0 の group の walker が何もしないことを実機で確かめる。固まるなら
  `MI_PREDICATE` で飛ばす形に変える（そのときは p006 の中で直す）。
- indirect buffer が前の dispatch の結果（GPU が書いた数）でもよい: op の間の flush で書きは見える。これを試験の 1 step にする。

### 2.6 `eu-test.c` の実機で動いた設定の使い方

eu-test の batch（3D で SBA → flush → GPGPU → CS stall＋stall at scoreboard → VFE → MSF → IDL → walker → MSF）と VFE の値
（最大 thread 112 × DSS − 1、URB 2・2）を**そのまま骨格**にする。差は CURBE（eu-test は push が無い）、IDD の thread 数・SLM・barrier、
walker の group と mask、最後の 3D への戻り。bring-up（p004）はまず eu-test と同じ 1 group・1 thread・1 channel の設定で compiler の出した
kernel を走らせ、次に 1 group の複数 thread、複数 group、barrier の順に広げる。

## 3. GLES 3.1 の compute（libglesv2・libegl、ws068-p035 との関係）

### 3.1 範囲の分け方

- WS101 が持つ: **GLES 3.1 の compute の部分集合**（GLSL ES 3.10 の compute の stage と SSBO・shared・atomic・barrier、
  `glDispatchCompute`・`glDispatchComputeIndirect`、`GL_SHADER_STORAGE_BUFFER`、`glMemoryBarrier`・`glMemoryBarrierByRegion`、
  compute の上限の query、`glGetProgramiv(GL_COMPUTE_WORK_GROUP_SIZE)`）。
- ws068-p035 に残す: desktop GL の `GL_ARB_compute_shader`（libGL）、image load/store、atomic counter、program interface query の全体。
  ws068-p035 の「p037 に依存」は desktop GL の順の話で、GLES 3.1 の compute の部分集合はそれに依らない。
  **ws068 の ws.md の p035 の行に「GLES 3.1 の compute の部分集合は WS101 へ移した」と書くのは main の作業**（WS101 の範囲の外）。

### 3.2 GLSL ES 3.10 の compute を自前の compiler から SPIR-V へ（`userland/desktop/libglesv2/glsl/`、[glsl-design](../ws068/glsl-design.md) の延長）

| 項目 | 内容 |
| --- | --- |
| 版 | `#version 310 es` を受ける（今は 100・300 es）。3.10 の予約語のうち使うもの（`buffer`、`shared`、`coherent`、`volatile`、`restrict`、`readonly`、`writeonly`）を keyword に |
| stage | compute。`layout(local_size_x = N, local_size_y, local_size_z) in;` を読み `OpExecutionMode LocalSize` に。`gl_WorkGroupSize` は定数 |
| built-in | `gl_GlobalInvocationID`・`gl_LocalInvocationID`・`gl_WorkGroupID`・`gl_NumWorkGroups`（uvec3）、`gl_LocalInvocationIndex`（uint） |
| SSBO | `layout(std430, binding = n) buffer Name { ... } inst;`。std430 の offset（配列の stride は要素の base alignment、vec3 は 16 境界）、最後の member の実行時長の配列（`OpTypeRuntimeArray`）、`.length()`（`OpArrayLength`）。SPIR-V 1.0 に合わせ Uniform ＋ `BufferBlock`（Vulkan 1.0 の libvulkan と Venus の host のため）。memory の qualifier は `NonWritable`・`NonReadable`・`Coherent`・`Volatile`・`Restrict` |
| shared | `shared T name[N];` を Workgroup の変数に。初期化子は断る（GLSL の規則） |
| 関数 | `barrier()` → `OpControlBarrier(Workgroup, Workgroup, AcquireRelease\|WorkgroupMemory)`、`memoryBarrier()`・`memoryBarrierBuffer()`・`memoryBarrierShared()`・`groupMemoryBarrier()` → `OpMemoryBarrier`、`atomicAdd/Min/Max/And/Or/Xor/Exchange/CompSwap`（int・uint、buffer と shared の変数だけ） |
| 早期の return | `main` の中の `return`（選択の中）。i915 の parser は扱える（loop の中は断る） |

host の試験（ws068-p018 の方法を使う）: `spirv-val --target-env vulkan1.0`、lavapipe での実行（host の Vulkan の小さな harness）、
i915 の host の compile と disassembler の照合（§5.2）。

### 3.3 API（`userland/desktop/libglesv2/`）

- `glCreateShader(GL_COMPUTE_SHADER)`、compute だけの program の link（他の stage と混ぜたら link の失敗）、`glUseProgram` の後の
  `glDispatchCompute(x, y, z)`・`glDispatchComputeIndirect(offset)`（`GL_DISPATCH_INDIRECT_BUFFER` の target）。
- **SSBO の binding**: `glBindBufferBase/Range(GL_SHADER_STORAGE_BUFFER, n, …)`。GL の binding n（0〜7）を Vulkan の set 0 の binding
  **56 + n** にする（既存: 0 = 既定の uniform、1〜 = sampler、32〜 = uniform block、48 = transform feedback の capture。64 が上限）。
  `GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS` と `GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS` は 8（ES 3.1 の最小は 4。Noct は 2 以上を求める）。
- **device が書いた buffer**: dispatch の後、書ける SSBO として bind された buffer を「device が書いた」にする（transform feedback と同じ印）。
  CPU の読み（`glMapBufferRange` の READ、`glBufferSubData` の前の読み戻しなど）の前に読み戻す既存の経路を使う。次の draw・dispatch は
  device の写しをそのまま使う（CPU の bytes を上書きしない）。
- **submission**: dispatch は context の今の command buffer に記録する。`glFlush`・`glFinish`・fence・device の書いた buffer の CPU の読み・
  `eglSwapBuffers` で submit する（WS068 の既存の規則）。pbuffer の context（Noct）もこれで動く。
- **garbage の回収**: GL の buffer は「記録中の frame が使った device の写しは書き直さず、新しい写しを作り古いものは frame の終わりまで
  garbage に置く」。Noct は `eglSwapBuffers` をしない pbuffer の context で、call ごとに `glBufferData` をする。submit して待った時点
  （`glFinish`・読み戻し）で garbage を回収することを p008 で確かめ、無ければ足す。回収されないと 16 MiB の device の memory（物理的に
  連続の GEM の object）が call ごとに積もる。
- **descriptor set layout**: program の layout は使う binding だけを持つ（`program.c`）。i915 の実行器の上限は layout あたり 32 binding
  （`I915_GFX_MAX_LAYOUT_BINDINGS`）なので、compute の program も使う SSBO・uniform・sampler だけにする。
- **`glMemoryBarrier(bits)`**: `vkCmdPipelineBarrier`（src COMPUTE_SHADER、dst は bits に応じて COMPUTE・VERTEX_INPUT・DRAW_INDIRECT・
  TRANSFER・HOST）。i915 の実行器では何もしない（§2.1）が、Venus には要る。
- **query**: `GL_MAX_COMPUTE_WORK_GROUP_COUNT/SIZE`（`glGetIntegeri_v`）、`GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS`、
  `GL_MAX_COMPUTE_SHARED_MEMORY_SIZE`、`GL_MAX_SHADER_STORAGE_BLOCK_SIZE`（`glGetInteger64v`、device の `maxStorageBufferRange`）、
  `GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT`（device の値）。device の limits から出す。
- **header と export**: `include/libc/GLES3/gl31.h`（Khronos、MIT、`gl3.h` と同じ registry の版）を足す。sysroot は `include/libc` を
  写す（`toolchain/llvm/sysroot.mk` の規則は変えない。header の追加は source の変更）。libGLESv2 の `exports.map` に ES 3.1 の entry point を
  足す。compute 以外の 3.1 の関数（image load/store、indirect の draw、program pipeline、vertex attrib binding など）は
  `GL_INVALID_OPERATION` を記録する stub にする（黙って受けない。`-z now` の link で未定義の symbol を残さない）。
- **版の名乗り**: Noct は `GL_MAJOR_VERSION`・`GL_MINOR_VERSION` が 3.1 以上でなければ backend を使わない。ES 3.1 の全体
  （image load/store、atomic counter ほか）は実装しないので、「OpenGL ES 3.1」を名乗るかは**ユーザーの判断 D1**（§7）。
  既定の案: compute を持つ device の ES 3 の context は「OpenGL ES 3.1 Kei」・「OpenGL ES GLSL ES 3.10」を名乗り、未実装の 3.1 の機能は
  stub の error で断る。compute を持つ device とは、今の 3.0 の条件（`vertexPipelineStoresAndAtomics`）に加え、queue に COMPUTE_BIT があり、
  limits が ES 3.1 の最小以上（invocation 128、size 128・128・64、count 65535、shared 16384）のもの。i915 の実行器の報告値はちょうど最小。
- `GL_RENDERER`: 今の固定の文字列のまま（Venus の host が lavapipe でも Noct は受ける。性能は Venus では測らない）。

### 3.4 EGL と Noct の `accel_opengles.c` の使う API

| API | 今の状態 | 要ること |
| --- | --- | --- |
| `eglGetProcAddress("eglGetPlatformDisplayEXT")`、`EGL_PLATFORM_SURFACELESS_MESA`（`include/libc/EGL/eglext.h` に定義がある）、`eglGetDisplay(EGL_DEFAULT_DISPLAY)` | ある | surfaceless が先に選ばれる。surfaceless が失敗・不適（版が 3.1 未満など）なら Noct は `EGL_DEFAULT_DISPLAY` を試し、zedBSD ではそれが画面を直接使う display の platform になる。pbuffer だけなら画面を取らないことを p008 で確かめる（Wayland の session の中で Noct を走らせても desktop が消えないこと） |
| `eglInitialize`・`eglBindAPI(EGL_OPENGL_ES_API)`・`eglChooseConfig(PBUFFER, ES3)`・`eglCreateContext(CLIENT_VERSION 3)`・`eglCreatePbufferSurface(1×1)`・`eglMakeCurrent`・`eglDestroy*`・`eglTerminate` | ある | p008 で surfaceless の pbuffer の context で compute が動くことを確かめる |
| `glGetIntegerv(GL_MAJOR/MINOR_VERSION, GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS, GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS)`、`glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT/SIZE)`、`glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE)`、`glGetString(GL_RENDERER)`、`glGetError` | 版は 3.0、compute の上限は無い | §3.3 |
| `glCreateShader(GL_COMPUTE_SHADER)`・`glShaderSource`・`glCompileShader`・`glGetShaderiv`・`glCreateProgram`・`glAttachShader`・`glLinkProgram`・`glGetProgramiv`・`glDetachShader`・`glDeleteShader`・`glDeleteProgram`・`glUseProgram` | compute が無い | §3.2・§3.3 |
| `glGenBuffers`・`glBindBuffer(GL_SHADER_STORAGE_BUFFER)`・`glBufferData(GL_DYNAMIC_COPY)`・`glBindBufferBase`・`glMapBufferRange(GL_MAP_READ_BIT)`・`glUnmapBuffer`・`glDeleteBuffers` | target の SHADER_STORAGE_BUFFER が無い | §3.3 |
| `glDispatchCompute`・`glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT \| GL_BUFFER_UPDATE_BARRIER_BIT)`・`glFlush`・`glFinish` | dispatch・barrier が無い | §3.3 |

Noct の GLSL（`accel_shader_source.c`）が使う言語: `#version 310 es`、`precision highp`、`layout(local_size_x = 64) in;`、
`layout(std430, binding = n) [readonly|coherent] buffer B { uint word[]; } b;`、`gl_GlobalInvocationID.x`、`uintBitsToFloat`・
`floatBitsToUint`、int・uint・float の算術と比較、`?:`、SSBO の `atomicAdd`、`main` の選択の中の `return`。shared・barrier・loop は使わない。

## 4. Noct（G3）

### 4.1 guest の build で accel を有効にする変更の案（**toolchain の変更。main とユーザーの許可まで適用しない**）

(1) Noct の patch を 1 つ足す（`userland/base/noct/patches/0004-accel-opengles-on-zedbsd.patch`、`version.mk` の `ZEDBSD_NOCT_PATCH_LEVEL` を
`zedbsd13` へ）。CMakeLists.txt の OpenGL ES の backend の 2 か所の条件に zedBSD を足し、pkg-config の link を zedBSD では行わない:

```diff
--- a/CMakeLists.txt
+++ b/CMakeLists.txt
@@ (accel の source の選択)
-  if(   CMAKE_SYSTEM_NAME STREQUAL "Linux"
-     OR CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
+  if(   CMAKE_SYSTEM_NAME STREQUAL "Linux"
+     OR CMAKE_SYSTEM_NAME STREQUAL "FreeBSD"
+     OR NOCT_TARGET_ZEDBSD)
     list(
       APPEND
       NOCT_ACCEL_BACKEND_SOURCE
       src/accel/accel_opengles.c
     )
@@ (accel の定義)
-  if(   CMAKE_SYSTEM_NAME STREQUAL "Linux"
-     OR CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
+  if(   CMAKE_SYSTEM_NAME STREQUAL "Linux"
+     OR CMAKE_SYSTEM_NAME STREQUAL "FreeBSD"
+     OR NOCT_TARGET_ZEDBSD)
     list(
       APPEND
       NOCT_PUBLIC_CPPFLAGS
       NOCT_ACCEL_BACKEND_DEFINITIONS
       NOCT_ACCEL_BACKEND_OPENGLES
     )
@@ (accel の link)
-  elseif(   CMAKE_SYSTEM_NAME STREQUAL "Linux"
-         OR CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
+  elseif(NOCT_TARGET_ZEDBSD)
+    # zedBSD links libEGL and libGLESv2 in userland/base/noct/zedbsd.cmake.
+  elseif(   CMAKE_SYSTEM_NAME STREQUAL "Linux"
+         OR CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
     find_package(PkgConfig REQUIRED)
```

(2) `userland/base/noct/zedbsd.cmake` の `noct_configure_zedbsd_target` で、accel のときだけ build の shared library を link する:

```diff
+  # The OpenGL ES accelerator backend loads the build's libEGL and libGLESv2.
+  if(NOCT_ENABLE_ACCEL)
+    target_link_libraries("${target}" PRIVATE
+      "$ENV{ZEDBSD_DYNAMIC_DIR}/libEGL.so"
+      "$ENV{ZEDBSD_DYNAMIC_DIR}/libGLESv2.so"
+    )
+  endif()
```

(3) `userland/base/noct/Makefile` の target の cmake に、**amd64 で libegl と libglesv2 を選んだ構成のときだけ** `-DNOCT_ENABLE_ACCEL=ON`
を、それ以外は明示の `OFF` を渡す（CMake の cache は build の directory に残るので、構成を切り替えたとき前の ON が残らないよう毎回渡す）。
`ZEDBSD_USER_PROGRAMS` は package の Makefile を読む順では確定していないので、`:=` ではなく recipe の中で展開する。build の stamp の名に
accel の有無を入れ（構成の切り替えで作り直す）、accel のときは stamp の依存に `$(BUILD)/dynamic/libEGL.so`・`libGLESv2.so` を足す:

```diff
+# The GPU accelerator (WS101): amd64 builds that carry the OpenGL ES libraries. Expanded
+# late, because the selected programs are known only after every package is read.
+NOCT_ZEDBSD_ACCEL = $(and $(filter amd64,$(NOCT_ZEDBSD_ARCH)),$(filter libegl,$(ZEDBSD_USER_PROGRAMS)),$(filter libglesv2,$(ZEDBSD_USER_PROGRAMS)))
+NOCT_ZEDBSD_ACCEL_OPTION = -DNOCT_ENABLE_ACCEL=$(if $(NOCT_ZEDBSD_ACCEL),ON,OFF)
 ...
 	cd '$(NOCT_SOURCE_DIR)' && \
 		... \
 		cmake --preset zedbsd-amd64 -B build-zedbsd-$(NOCT_ZEDBSD_ARCH) $(NOCT_ZEDBSD_CMAKE_OPTIONS) \
+		$(NOCT_ZEDBSD_ACCEL_OPTION) \
 		-DNOCT_VERSION='$(ZEDBSD_NOCT_VERSION)'
```

stamp の名と依存の変更は、`NOCT_ZEDBSD_BUILD_STAMP` の定義が `:=` なので、同じく遅い展開の形に直す（p009 で Makefile の全体を読んで
最小の差分を作り、main に出す）。

なぜ条件付きか: accel を有効にした `/bin/noct` は `libEGL.so`・`libGLESv2.so`（とその先の libvulkan・libwayland）を DT_NEEDED に持ち、
`-z now` で読み込む。desktop の library の無い image（i386・pc98・rpi4・最小の構成）では `/bin/noct` が起動できなくなる。
代わりの形（D2 の選択肢 B）: base の `/bin/noct` は変えず、同じ検証済みの tarball から accel 付きの `/bin/noct-gpu` を別に作る。

(4) ヘッダ: `accel_opengles.c` は `<GLES3/gl31.h>` を要る（§3.3 で足す）。`-U__linux__` の下の `eglplatform.h` は libegl が既に使う形で通る見込み。

### 4.2 G3 の見本と N・倍率の目標（案。**ユーザーの確認 D3**）

- **見本 1（受け入れ）`mix.nct`**: 整数の hash の DOALL。結果が CPU と bit で一致する（float の丸めの差が無い）。1 要素あたり 32 の整数の
  演算（乗算・加算・xor・論理 shift を 8 回）を直線で書く。N = 4,194,304（2^22、16 MiB の buffer 1 つ、in-place）。
  ```
  __accel func mix(data: rpackeduint32_in, count: int): void {
      for (i in 0..count) {
          var x = data[i];
          x = x * 1664525 + 1013904223;  x = x ^ (x >> 13);
          ... （8 回。p009 で Noct の offload の受ける形に合わせる）
          data[i] = x;
      }
  }
  ```
  計り方: 同じ `noct` で `--gpu` 有りと無しを、最初の 1 回（EGL の初期化と shader の compile を含む）の後の 5 回の最良で比べる。
  結果は全要素の checksum（CPU で計算）と、CPU の run の結果との一致を確かめる。**目標: 5330 の素の機械で GPU が CPU の 3 倍以上速い**
  （見込み: CPU は 1 要素 100〜250 ns で 0.4〜1 秒、GPU は kernel 数 ms と 16 MiB の写し数回で 50〜100 ms。10 倍は伸びの目標）。
- **見本 2（見せ物）`saxpy.nct`**: float の `y[i] = a * x[i] + y[i]`。帯域で決まるので倍率は小さい見込み。相対誤差 1e-6 以内の一致を見るだけで、受け入れの倍率には使わない。
- **使わないもの**: DOSUM（Noct は lane ごとに 1 word へ `atomicAdd` するので、N が大きいと 1 address への atomic が直列になり遅い）。Noct の
  code 生成の改善は WS101 の範囲の外（Future Work の候補）。
- N を 2^24 にしない理由: GEM の object は物理的に連続の run（`struct i915_gem_object` の `kern_pmem run`）で、64 MiB の連続の確保は
  断片化で失敗しうる。2^22（16 MiB）なら余裕がある。

## 5. 試験

### 5.1 i915 の compute の試験の群（kernel の試験の build の場面 `vkcs`）

`src/drivers/gpu/i915/tests/render/compute.c` と `compute-shaders/`（GLSL と SPIR-V、`regenerate.py` が host の `glslc` で SPIR-V を作り、
期待値を Python で計算して `tests/fixtures/compute-shaders-gen.inc` に書く。`generality.c`（vke2）と同じ形）。wire（`drv_i915_render_execute`）
を通すので、`vkCreateComputePipelines`・`vkCmdDispatch`・`vkCmdDispatchIndirect` の decode まで試す。

| step | 内容 | 判定 |
| --- | --- | --- |
| ADD | `c[i] = a[i] + b[i]`、local size 64、N = 1000（64 の倍数でない。`if (i >= n) return;`） | 全要素が C の計算と一致、N の外は書かれていない |
| ID | local size (4, 2, 3)、group (3, 2, 2)。全 invocation が 5 つの built-in を書く | 全 word の一致 |
| PUSH | push constants と uniform block と 3 つの SSBO（dynamic offset 付き） | 一致 |
| MIXED | 1 つの command buffer で draw → `vkCmdCopyImageToBuffer` → dispatch（その buffer を SSBO として読み、別の buffer に頂点を書く）→ draw（その buffer を vertex buffer に） | 画素と word の一致（3D ↔ GPGPU の切り替えと op の間の順序） |
| SPILL | 値の多い compute の kernel（scratch） | 一致 |
| SHARED | shared の配列の転置（64×… の tile）と `barrier()` | 一致 |
| REDUCE | local size 128 の shared memory の木の reduction（barrier を 7 回）、group ごとの和 | 一致 |
| ATOMIC | SSBO の histogram（`atomicAdd`）、`atomicMin/Max/And/Or/Xor/Exchange/CompSwap`、shared の atomic | 一致（順に依らない結果だけを比べる） |
| LOOP | loop の中の一様な barrier | 一致 |
| INDIRECT | indirect buffer の (x, y, z)、前の dispatch が GPU で書いた数、0 の group | 一致、0 は何も書かない |
| LENGTH | `.length()` | 一致 |
| MANY | N = 4M の ADD の時間（参考の値、判定は一致だけ） | 一致 |

試験の build の kernel は 16 MiB の上限（AMD64_KERNEL_MAX_BYTES）の近く（WS075 の注意）。期待値の配列は小さく保ち（生成の式を C で持つ）、
大きな state は heap に置く。

### 5.2 host の試験（`plan/ws101/tests/host/`）

- **encoder**: 新しい命令（SLM の read・write、A64・SLM の atomic、fence、gateway の barrier、`sync.bar`・`sync.allwr`、TS への EOT）の語を
  Mesa 25.0.7 の `brw_disasm --gen=adl` で読み、期待の mnemonic と field（SFID、message の型、BTI、aop）を照合し、`brw_asm` で組み直して
  同じ bytes になることを確かめる（`plan/ws031/tests/run-vk-gentool-test.sh` の encoder の検査の形。tool は `BRW_TOOLS=/home/awe/p014-c/mesa/build-asm/src/intel/compiler`）。
- **compile**: compute の SPIR-V（vkcs と GLES の試験の shader）を `plan/ws075/tests/guard/shader-dump.c` と同じ形の dumper（compute の stage を
  足した WS101 の複写）で compile し、disassembler が全命令を受けること、組み直しが同じ bytes、`scoreboard-check.h` が健全と言うこと
  （`plan/ws075/tests/guard/run.sh` の形）。
- **lower**: IR の interpreter（`plan/ws031/tests/i915-vk-lower-test.c` の形の WS101 の複写）に compute を足す。1 workgroup の全 invocation を
  lockstep で走らせ（if 変換の IR なので lockstep は自然）、SLM・barrier・atomic を模し、C の独立の式と比べる。
- **batch**: dispatch と indirect の batch を host で作り（`plan/ws031/tests/i915-vk-cmd-test.c` の形）、packet を genxml（gen120）で読み、
  VFE・IDD・CURBE・walker の field（thread 数、right mask、SLM、barrier、CURBE の長さ）と flush と PIPELINE_SELECT の順を確かめる
  （`plan/ws031/tests/dump-gen-packets.py` を使う）。

### 5.3 実機（5330）

- passthrough: `plan/ws075/tests/test-hw.sh vkcs OUTDIR`（`flock /tmp/i915-hw.lock` を取る。WS075 のエージェントと共有の lock。BUILD は
  WS101 の worktree の build）。GLES と Noct の user の program は `plan/ws031/tests/vkloop-hw.sh` の service の鎖（vkprobe）の形の
  WS101 の複写（`plan/ws101/tests/hw/`）で、同じ lock の下で走らせる。
- 回帰: 同じ lock の下で `vkx`・`vke1`・`vke2`・`vkc`（実行器の変更の Phase ごと）。
- 素の機械（G3 の時間、G4）: ユーザー。
- GPU が固まったとき: serial の log を読んで判定しない。guest を gdbstub で止め（QEMU の `-S -gdb`）、i915 の error の register
  （EIR・IPEHR・ACTHD・INSTDONE）を monitor・gdb で読む。同じ条件の変更無しの再試行は 3 回まで。

### 5.4 Venus（QEMU）

- GLES の compute の試験の program（`plan/ws101/tests/` の小さな program、または egltest に `--scene=compute`。どちらにするかは p008 で決める。
  egltest は WS068 の program なので、足すなら main の了承を取る）を `--platform=pbuffer` で走らせ、結果の word を比べて PASS・FAIL を出す。
  guest の操作は `plan/tools/guest/serial.py` か `guest.sh`。
- Noct の見本の正しさ（倍率は測らない）。
- Venus の host の Vulkan の device（開発機の GPU か lavapipe）で結果が変わりうるので、正しさだけを見る。

## 6. Phase の分け方

1 Phase は数時間の 1 枠。コードの Phase は Gen12 の hardware で確かめる前に host の試験を通す。

| Phase | 内容 | 依存 | 確かめ方 |
| --- | --- | --- | --- |
| p002 | compiler の核: GLCompute、LocalSize・LocalSizeId、built-in（`LOAD_SYSTEM`、GlobalInvocationID の展開）、compute の register の約束と binary の field、TS への EOT、`LOAD_STORAGE` の predicate、効果なしの decoration の追加、`OpArrayLength` | p001 | host: encoder・compile・lower（§5.2） |
| p003 | 実行器の dispatch: compute pipeline の作成（opcode 66、window の全体に kernel）、bind point の分離、`vkCmdDispatch`（110）、`compute.c`（IDD の全 field（SLM・barrier は binary の値。p005 までは 0）・CURBE・per-thread の ID の表・VFE・walker・3D への戻り・compute の scratch）、`transfer_pending`、HDC だけの PIPE_CONTROL、storage の register の dword 2 の range（draw と compute） | p002 | host: batch の試験、既存の host の実行器の試験（`run-vk-host-tests.sh`） |
| p004 | 実機の bring-up と `vkcs` の場面（ADD・ID・PUSH・MIXED・SPILL）: eu-test と同じ 1 thread から段階的に | p003 | 5330 の passthrough の vkcs、vkx・vke1・vke2・vkc の回帰 |
| p005 | SLM・barrier・fence・atomic: IR の op、parser（Workgroup・atomic・barrier）、EU（§1.6）、IDD の SLM と barrier の field、`vkcs` の SHARED・REDUCE・ATOMIC・LOOP | p004 | host の 3 種と実機の vkcs |
| p006 | `vkCmdDispatchIndirect`（111、DISPATCHDIM の register、CURBE への写し）、0 の group、vkcs の INDIRECT・LENGTH・MANY、G1 の受け入れの run | p005 | 実機の vkcs の全 step、回帰 |
| p007 | GLSL ES 3.10 の compute（§3.2）: 版・stage・SSBO std430・shared・built-in・barrier・atomic・`.length()` → SPIR-V | p001（p002〜p006 と並行できる） | host: spirv-val、lavapipe、i915 の host の compile |
| p008 | libglesv2・libegl の ES 3.1 の compute の API（§3.3）、`gl31.h`、export と stub、版の名乗り（D1 に従う）、GLES の compute の試験の program。Venus で G2、i915 の passthrough で G2 | p007、p004（i915 の基本の分。shared・atomic・indirect の試験の i915 の分は p005・p006 の後）、D1 | Venus の試験、5330 の passthrough の試験、egltest の既存の場面の回帰 |
| p009 | Noct の accel の有効化（§4.1、**D2 の許可の後**）と G3 の見本（`plan/ws101/tests/noct/`）: Venus で正しさ、5330 の passthrough で正しさと時間 | p008、D2 | Venus・passthrough の run、時間の記録 |
| p010 | G4: fg010 の台本の場面（CPU と GPU の時間を並べて見せる script と手順）、素の 5330 でのユーザーの確認 | p009 | ユーザーの確認 |
| p011 | 規約の全文との照合（WS101 の全ての変更）、host の試験、vkx・vke1・vke2・vkc・vkcs・GLES の試験、boot test | p002〜p010 | 全文の review と回帰の記録 |

依存の図（前提 → 後）:

```
p001 → p002 → p003 → p004 → p005 → p006
                            p004 ─────────────→ p008（i915 の基本の分）
                                   p005・p006 ─→ p008（shared・atomic・indirect の i915 の分）
p001 → p007 ─────────────────────────────────→ p008 → p009（＋D2・D3）→ p010
p002〜p010 → p011
```

日程の目安（新規実装は 10/10 ごろまで）: p002・p007 を並行で始め、p003〜p006 と p008 を 10/6〜10/8、p009 を 10/9、p010 は 10/10 以後の実機の
確認。p005・p006 が遅れたら、G3（Noct の DOALL は shared・barrier・indirect を使わない）に要る p002〜p004・p007〜p009 を先にする。

## 7. ユーザーと main の判断が要る点

| # | 判断 | 選択肢 | 既定の案と理由 | 待つ Phase |
| --- | --- | --- | --- | --- |
| D1 | GLES の「OpenGL ES 3.1」の名乗り（ES 3.1 の全体は実装しない） | A: compute を持つ device で 3.1 を名乗り、未実装の 3.1 の関数は error の stub。B: ES 3.1 の残り（image load/store、atomic counter、indirect の draw、program pipeline ほか）も実装してから名乗る（OSC の後、数 Phase）。C: 環境変数などで 3.1 を opt-in | A（Noct は 3.1 未満だと GPU を使わない。OSC に間に合う）。WS068 の「実装した版を名乗る」方針（desktop GL）からの外れなのでユーザーの判断 | p008 |
| D2 | Noct の build の変更（toolchain の変更） | A: amd64 で libegl・libglesv2 を選んだ構成の `/bin/noct` だけ accel を ON（§4.1）。B: `/bin/noct` は変えず `/bin/noct-gpu` を別に作る | A（デモで `noct --gpu` と書ける。desktop の無い構成は今までどおり） | p009 |
| D3 | G3 の N と倍率 | N = 2^22、整数の hash、GPU が 3 倍以上（伸び 10 倍） | 物理的に連続の確保と、float の一致の問題を避ける | p009 |
| D4（main） | ws068-p035 の範囲の書き換え | GLES 3.1 の compute の部分集合を WS101 へ移したと ws068 の ws.md に記録 | WS101 の範囲の外の file なので main が行う | p007 の前 |

## 8. 危険と対策

| 危険 | 対策 |
| --- | --- |
| barrier・VFE・SLM の誤りで GPU が固まる | eu-test の設定から 1 段ずつ広げる（§2.6）。barrier は group が 2 thread 以上のときだけ。IDD の Barrier Enable と thread 数を host の batch の試験で確かめる。固まったら gdbstub と register で解析（§5.3） |
| 大きな buffer の物理的に連続の確保 | N = 2^22 を目標にする |
| CPU と GPU の float の丸め（denorm、mad の融合） | 受け入れの見本は整数。float は許容差で見る |
| Noct の DOSUM の per-lane の atomic が遅い | 倍率の目標に使わない |
| SIMD8 だけの kernel の速さ | 見込みで 3 倍は届く。SIMD16 は Future Work |
| 試験の build の kernel の大きさ（16 MiB の上限） | 期待値は式で持ち、大きな state は heap |
| Venus の host の device の違い | Venus では正しさだけ |
| libglesv2 の変更が WS068 の保留の作業とぶつかる | WS068 の GL 3.3 以降は保留中。ws068-p035 の範囲を D4 で分ける |
| 共有の 5330 の取り合い | `flock /tmp/i915-hw.lock`（WS075 と共有）の下でだけ使う |
| uncached の stateless の MOCS での A64 の atomic | p005 の ATOMIC で確かめ、違えば compute の op の stateless の MOCS を write-back に（§2.3） |
| indirect の CURBE の写しの順序 | p006 で確かめ、違えば anv の方式（shader が indirect buffer を読む）に（§2.5） |
| pbuffer の context で GL の garbage が回収されず device の memory が積もる | p008 で submit の待ちの時点の回収を確かめる（§3.3） |
| surfaceless が失敗すると Noct が画面を直接使う display の platform を試す | p008 で pbuffer だけでは画面を取らないことを確かめる（§3.4） |
| uncached の MOCS による compute の帯域 | 受け入れの見本は演算の多い整数の hash。帯域が足りなければ write-back の MOCS を性能の改善として試す（p009 の中の測定の後） |
