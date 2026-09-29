<!-- awesome-plan project=zedbsd record=ws101-design -->

# WS101 の設計: GPU の compute（i915 の Vulkan の compute、GLES 3.1 の compute、Noct の自動並列化）

Parent: [WS101](ws.md)。作成: ws101-p001（2026-09-30、サブエージェント）。見直しと敵対的レビュー（design-reviewer）の結果は
[phase001](phase001/phase.md)。第 2 版（レビューの指摘 H1〜H4・M1〜M14・L1〜L9 を反映）。

この文書は設計であり、実行の許可ではない。各 Phase は Queue に入ってから実行する。値（bit の位置・message の型）は Mesa 25.0.7
（`/home/awe/p014-c/mesa`、既存の `intel/eu-encoding-gen12.h` が引く版と同じ）と genxml（gen120 → gen110 → gen90 → gen80 → gen75 → gen70 →
gen60 の import の連鎖）で読んだものである。実装の Phase は値を encoding の header に出典付きで写し、Mesa の assembler・disassembler で
照合してから GPU に出す。

## 0. 今の状態（2026-09-30 の読み取り）

| 部分 | 状態 | 所在 |
| --- | --- | --- |
| SPIR-V の parser | Vertex・Fragment だけ（`i915_spirv_declare_entry_point` が他の execution model を断る）。OpExecutionMode は読み捨て。storage class は UniformConstant・Input・Uniform・Output・Function・PushConstant・StorageBuffer。Workgroup は無い。atomic・barrier の opcode は無い。選択の中の OpReturn は扱う（loop の中は断る）。storage の access chain の動的な index は 1 つまで | `compiler/spirv.c` |
| IR | scalar の SSA、構造化の制御は if 変換、loop は LOOP_BEGIN〜LOOP_END。`LOAD_STORAGE`・`STORE_STORAGE`（A64、store だけ predicate を持つ）。`UDIV`・`UMOD` はある | `compiler/ir.h` |
| code generator | SIMD8 だけ。SSBO は push data に置いた 64 bit の address に channel ごとの byte offset を足して A64 untyped read/write（SFID 12、BTI 253）。spill は OWord block（SFID 10）。thread の終わりは stage ごとの URB・RT write。uniform block と SSBO の address は合わせて 8 まで（`I915_SHADER_MAX_BLOCKS`）。stage の分岐に `!= I915_STAGE_VERTEX` を fragment とみなす所がある | `compiler/compile.c`、`compiler/eu.c`、`intel/eu-encoding-gen12.h` |
| 実行器 | 記録した op を vkQueueSubmit で 1 本の batch にし同期で走らせる。op ごとに flush・`PIPELINE_SELECT(3D)`・`STATE_BASE_ADDRESS` を出す自己完結の形で、op の後ろに flush があるので `vkCmdPipelineBarrier` は何もしない。opcode 110・111（Dispatch・DispatchIndirect）と 66（CreateComputePipelines）は ENOTSUP。`vkCmdBindPipeline`・`vkCmdBindDescriptorSets` の bind point は読み捨て。SSBO を書く draw は `transfer_pending` を立てない（rectangle だけが立てる） | `render/command.c`、`render/draw.c`、`render/state.c`、`render/heap.h`、`render/blit.c` |
| device の情報 | queue に COMPUTE_BIT。上限は ES 3.1 の最小値と同じ（shared 16384、invocation 128、size 128・128・64、count 65535）。stage あたりの storage buffer は 4 | `render/instance.c` |
| kernel の compute の試験 | `eu-test.c`: 3D で SBA → GPGPU の選択 → VFE（最大 thread 112×DSS−1、URB 2・2）→ MSF → IDL（1 thread）→ GPGPU_WALKER（SIMD8、1 group、right mask 1）→ MSF。kernel は r0 を r127 へ写し SFID 7（thread spawner）へ EOT。5330 の実機で動いた | `tests/execution/eu-test.c` |
| libvulkan | `vkCreateComputePipelines`・`vkCmdDispatch`・`vkCmdDispatchIndirect` の encode は既にある（Venus ではそのまま host に行く） | `userland/desktop/libvulkan/` |
| libglesv2 | GLSL ES 3.00 まで。compute の stage・buffer block（SSBO）・`shared`・atomic・barrier は無い。`GL_VERSION` は「OpenGL ES 3.0 Kei」。GL の buffer は CPU の bytes を持ち、device が書いた buffer（transform feedback）は CPU の読みの前に読み戻す。`glFlush`・`glFinish` は何もしない（submit は swap の frame の終わりか、`glReadPixels`・fence・読み戻しの `query_finish`）。source は libGL にも組み込まれる | `userland/desktop/libglesv2/`、`userland/retro/libGL/` |
| libegl | `eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA)`、pbuffer、`EGL_OPENGL_ES3_BIT` は既にある。`eglMakeCurrent` の release は pass を閉じるだけで submit しない | `userland/desktop/libegl/egl.c` |
| header | `include/libc/GLES3/gl3.h` はある。`gl31.h` は無い（Noct の `accel_opengles.c` は `<GLES3/gl31.h>` を include する） | `include/libc/GLES3/` |
| Noct | snapshot `fcf5759`。`NOCT_ENABLE_ACCEL` は OFF。OpenGL ES の backend は CMake の `CMAKE_SYSTEM_NAME` が Linux・FreeBSD のときだけ選ばれ、zedBSD の toolchain file は `zedBSD` を名乗るので、ON にしても backend が入らない。**GLES の backend は float を使う program を断り（CPU に戻る）、dispatch の group の数が device の上限を越えると error、shader の compile・link の失敗は CPU への fallback ではなく error** | `build/NoctLang/`（読み取りのみ） |

## 1. compiler（SPIR-V の GLCompute → IR → Gen12 の EU）

### 1.1 Mesa の brw のどこに従い、どこを変えるか

| 事項 | Mesa（Gen12.0、verx10 120） | zedBSD の設計 | 理由 |
| --- | --- | --- | --- |
| payload の r0 | header。WorkGroupID は r0.1・r0.6・r0.7（`emit_work_group_id_setup`）、barrier ID は r0.2 の bit 30:24（`emit_barrier`、mask 0x7f000000） | 同じ | hardware の事実 |
| push data | r1 から CURBE（`cs_thread_payload` は r0 だけ、`assign_curb_setup`）。cross-thread の後に per-thread | 同じ | 同上 |
| LocalInvocationID・Index | per-thread の push は subgroup ID 1 word。ID は shader が `subgroup_id * 8 + channel` から udiv・umod で作る（`brw_nir_lower_cs_intrinsics.c`） | **per-thread の push を 4 register**（x・y・z・index を channel ごとに）とし、実行器が CPU で埋める | IR に `UDIV`・`UMOD` はあるが、Gen12.0 の整数の割り算は math box で invocation ごとに重い。値は local size だけで決まるので pipeline の作成で 1 回作れる。費用は 1 thread 128 byte |
| NumWorkGroups | anv は push constant。indirect は address を push し shader が読む（sentinel で分岐） | **常に 3 word の置き場所の address を push し、shader が A64 で読む**。direct は実行器が slot に 3 word を書きその address、indirect は indirect buffer の address（§2.5） | direct と indirect で shader が同じ。CS の中の memory の順序に頼らない |
| SIMD 幅 | 8・16・32 を選ぶ | **SIMD8 だけ** | 既存の compiler が SIMD8 だけ。SIMD16 は Future Work（性能） |
| SSBO | A64（`brw_dp_a64_untyped_surface_rw_desc`） | 既存の A64 のまま | 既にある |
| SLM | HDC の untyped surface read・write、BTI 254（`GFX7_BTI_SLM`）、SFID 12 | 同じ | hardware の事実 |
| atomic | SSBO は A64 untyped atomic（SFID 12、型 0x12）、SLM は untyped atomic（型 2、BTI 254）。応答の bit（control の bit 5）は `has_dest` | 同じ。応答の bit と rlen は同じ flag から作る | 同上 |
| barrier | gateway（SFID 3）の barrier の message（subfunction 4）＋ `sync.bar`。group が 1 thread に収まれば出さない | 同じ | 同上 |
| fence | Gen11+ の LSC 以前: SFID 10 の MEMORY_FENCE（型 7、commit、BTI 0 = global、254 = SLM）、exec size 1。acquire の前に `sync.allwr`、fence の後に完了待ち | 同じ | 同上 |
| EOT | Gen12.0 は thread spawner（SFID 7）へ r0 の写しを送る（`emit_cs_terminate`）。g112〜127 から | 同じ（eu-test の kernel で確かめた、§1.6） | 同上 |
| scratch の thread ID | Gen12: `scratch_ids_per_subslice = 16 * 8`、subslices = GT2 なら fuse に依らず 6（`init_max_scratch_ids`） | 同じ（compute の scratch は 128 × 6 = 768 の thread 分） | FFTID の範囲 |

license の境界（[設計方針](../master-design-policy.md) §2.1）: i915 の compiler と実行器は base system で、Mesa の code は取り込まない。写すのは
hardware の事実（field の位置と値、message の型、register の番号）だけで、既存の `intel/eu-encoding-gen12.h`・`intel/genxml.h` の書式
（MIT の notice、Mesa の file と sha256）に出典を足す。算法（ID の作り方、CURBE の配置、barrier の lowering）は新たに書く。

### 1.2 SPIR-V の受け入れ

- **execution model** GLCompute（5）を `I915_STAGE_COMPUTE`（新しい値 2、`I915_STAGE_COUNT` は 3）にする。compiler と parser の
  `!= I915_STAGE_VERTEX` を fragment とみなす分岐（`compile.c`・`spirv.c` の各所）は p002 で全数を見直し、compute を明示に扱う。
- **execution mode** `LocalSize`（17）を読む。`LocalSizeId`（38、`OpExecutionModeId` 331）は operand の id が `OpConstant` のときだけ受ける。
  spec constant（`OpSpecConstant*`、`SpecId` の decoration）は断る（refuse の diagnostic）。各次元が 128・128・64 を越えるもの、積が 128 を
  越えるものは断る。
- **built-in**（Input の変数の BuiltIn の decoration）: `LocalInvocationId`（27）、`LocalInvocationIndex`（29）、`WorkgroupId`（26）、
  `NumWorkgroups`（24）、`GlobalInvocationId`（28）。`WorkgroupSize`（25）は glslang が BuiltIn の付いた `OpConstantComposite` で出すので、
  定数として読み、decoration は効果なしとして受ける。`SubgroupSize` などは断る。
- **storage class** `Workgroup`（4）の変数を SLM に置く（§1.4）。合計が 16 KiB（device の報告値）を越えれば断る。
- **decoration** `Coherent`（23）・`Volatile`（21）・`Restrict`（19）・`Aliased`（20）を「効果なし」の一覧に足す。根拠: code generator は
  命令を並べ替えず、SSBO の send は stateless の MOCS（uncached）で memory へ行き、順序は fence が担う。
- **命令**: `OpControlBarrier`（224）、`OpMemoryBarrier`（225）、`OpAtomicLoad`（227）・`Store`（228）・`Exchange`（229）・
  `CompareExchange`（230）・`IIncrement`（232）・`IDecrement`（233）・`IAdd`（234）・`ISub`（235）・`SMin`（236）・`UMin`（237）・`SMax`（238）・
  `UMax`（239）・`And`（240）・`Or`（241）・`Xor`（242）、`OpArrayLength`（68）。float の atomic、`OpAtomicFlag*`、image の atomic は断る。
- compute の stage で `OpImageSample*` と Input・Output の location の変数は断る（範囲の外）。
- `OpReturn` は今の扱いのまま（選択の中は channel を落とす、loop の中は断る）。Noct の `if (lane >= trip) return;` はこれで通る。
- **barrier の一様性**: if 変換では選択の中の barrier も全 thread が実行するので、それ自体は hang しない。危ないのは、ある thread の全
  channel が return した後、その thread だけ loop を早く抜け、loop の中の barrier の数が thread ごとに違う場合（GPU が固まる）。
  parser は、leaky な return（`constructs[].leaky`）の後の `BARRIER` と、入口の predicate が ALWAYS でない loop の中の `BARRIER` を断る
  （hang を compile の error に変える）。GLSL ES 3.10 の規則（制御の中と return の後の `barrier()` の禁止）は GLSL の front-end でも検査する（§3.2）。

### 1.3 IR の追加（`compiler/ir.h`）

| 新しい op | 意味 |
| --- | --- |
| `I915_IR_LOAD_SYSTEM` | dst = `component` の built-in: 0〜2 LocalInvocationID.xyz、3 LocalInvocationIndex、4〜6 WorkgroupID.xyz、7〜9 NumWorkgroups.xyz |
| `I915_IR_LOAD_SHARED` | dst = SLM の byte offset src[0] の word。`component` 1 なら src[2] の Boolean の channel だけ |
| `I915_IR_STORE_SHARED` | SLM の byte offset src[0] = src[1]。`component` 1 なら src[2] の channel だけ |
| `I915_IR_ATOMIC` | dst = 前の値。`immediate` = 演算（ADD・SUB・SMIN・UMIN・SMAX・UMAX・AND・OR・XOR・XCHG・CMPXCHG。INC・DEC は ADD・SUB の 1）。`location` = storage buffer の uniform の番号、または `I915_IR_LOCATION_SHARED`（0xFFFFFFFD）で SLM。src[0] offset、src[1] 値、src[2] 比べる値（CMPXCHG）、src[3] predicate（`component` の bit 0 が 1 のとき）。`component` の bit 1 は結果を使うか（使わなければ応答を求めない） |
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
  この配置を compiler が決めてよい。合計を binary の `shared_bytes` にする。
- **動的な index**: Workgroup と storage buffer の access chain は、**任意の数の動的な index** を stride を掛けて byte offset に足し込む形に
  する（memory なので、uniform block の「1 つの動的な index を全要素の load と select にする」形は要らない）。今の
  `i915_spirv_storage_offset` の元の access chain は動的な index を 1 つで断る（`spirv.c` の "access chain with more than one dynamic index"）
  ので、ここを変える。2 次元の shared の tile（`tile[ly][lx]`）のため。
- Workgroup の load・store は **forwarding をしない**（Function の変数の store-to-load forwarding は他の invocation の書きを見ないため）。
  毎回 `LOAD_SHARED`・`STORE_SHARED` を出し、block の predicate を付ける。
- **atomic**: pointer の storage class で A64（StorageBuffer、Uniform＋BufferBlock）か SLM を選ぶ。memory semantics が Relaxed（0）以外の
  ときは、release なら前に、acquire なら後ろに該当の `FENCE` を出す（GLSL の `atomicAdd` は glslang が Relaxed で出す）。
  `OpAtomicLoad`・`OpAtomicStore` は predicate 付きの普通の load・store と fence にする。`OpAtomicCompareExchange` は SPIR-V では
  Value が Comparator の前だが、Mesa の message は DATA0 = 比べる値、DATA1 = 新しい値なので、parser が入れ替えて src[1]・src[2] に置く
  （p006 で brw_asm と実機で確かめる）。
- **barrier**: `OpControlBarrier(exec, mem, sem)` は exec が Workgroup なら `BARRIER`（immediate は sem の WorkgroupMemory → SLM、
  UniformMemory → global）。exec が Subgroup 以下なら fence だけ。`OpMemoryBarrier(scope, sem)` は `FENCE`。scope・sem は定数の id
  でなければ断る。一様性の検査は §1.2。
- **`OpArrayLength`**: storage buffer の address の register（push の 1 register、今は dword 0〜1 だけ使う）の dword 2 に実行器が range の
  byte 数を置く（draw と compute の両方、§2.3）。長さ = (range − member の offset) / ArrayStride を `UDIV` で作る（stride が 2 の冪なら `SHR`）。

### 1.5 register の約束（SIMD8 の compute）

```
r0            header（WorkgroupID: r0.1 x、r0.6 y、r0.7 z。barrier ID: r0.2[30:24]。scratch: r0.3・r0.5）
r1 .. rC      cross-thread の push data（C register）:
                push constants（push_constant_bytes、register 単位）、
                uniform block の写しと storage buffer の address（1 buffer 1 register: dword 0〜1 address、dword 2 range）、
                gl_NumWorkGroups を読む shader では、set が I915_IR_SYSTEM_SET の「system の storage buffer」の address の register
                （p002 で確定: 独立の system の register は作らず、storage buffer の 1 つとして block の並びに入る）
rC+1 .. rC+4  per-thread の push（4 register）: LocalInvocationID.x、.y、.z、LocalInvocationIndex（8 channel の dword）
rC+5 ..       値（今の約束どおり r16 から、payload が r15 を越えるならその後ろから、r95 まで）
r127          EOT の message（r0 の写し）
```

- binary（`struct i915_shader_binary`）に足すもの: `local_size[3]`、`shared_bytes`、`uses_barrier`、`uses_num_workgroups`、
  `cross_thread_regs`（C）、`per_thread_regs`（4）、`system_offset`（cross-thread の中の system の byte の位置、未使用は 0xFFFFFFFF）。
  push の配置（`push_regs`、`push_constant_bytes`、`blocks`）は今の `i915_compile_blocks` をそのまま使う（uniform block と SSBO の address は
  合わせて `I915_SHADER_MAX_BLOCKS` = 8 まで）。
- `LOAD_SYSTEM` の NumWorkgroups は system register の address から A64 で 3 word を読む（1 回、全 channel に同じ値。uniform な A64 read）。
- per-thread の 4 register は、使わない ID があっても常に置く（IDD の読みの長さを固定にし、実行器を簡単にする）。
- scratch（spill）は今の vertex・fragment と同じ仕組み（r0.3・r0.5 の header、BTI 253 の OWord block）。

### 1.6 EU の lowering と encoding（Gen12、Xe-LP）

message の descriptor は `brw_message_desc`（mlen 28:25、rlen 24:20、header 19）と `brw_dp_desc`（BTI 7:0、control 13:8、type 18:14）の形。

| IR | EU |
| --- | --- |
| `LOAD_SYSTEM` | ID: per-thread の register から `mov`。WorkgroupID: r0 の scalar（`<0;1,0>:ud`）から `mov`。NumWorkgroups: §1.5 |
| `LOAD_SHARED` | `send(8)` SFID 12、untyped surface read（型 1）、control 0x2e（channel mask 0xE、SIMD8 = 2<<4）、BTI 254、mlen 1（offset の register がそのまま address の payload）、rlen 1。predicate は f0.0 |
| `STORE_SHARED` | 同じく write（型 9）、mlen 1、ex_mlen 1（値）、rlen 0、predicate |
| `ATOMIC`（SLM） | SFID 12、untyped atomic（型 2）、control = aop（3:0）\| SIMD8（bit 4）\| 応答（bit 5）、BTI 254、mlen 1、ex_mlen 1（CMPXCHG は 2）、rlen は応答のとき 1、無いとき 0 |
| `ATOMIC`（SSBO） | SFID 12、A64 untyped atomic（型 0x12）、control = aop \| 応答（bit 5）、BTI 253、mlen 2（A64 の address の組。今の `i915_compile_storage` の組み立てを使う）、ex_mlen 1・2、rlen は応答のとき 1 |
| `FENCE` | acquire なら `sync.allwr`。global: SFID 10、MEMORY_FENCE（型 7）、control bit 5（commit）、BTI 0、mlen 1（r0）、rlen 1、header。SLM: 同じく BTI 254。exec size 1、NoMask（Mesa の `brw_memory_fence`）。その後 fence の dst の完了を待つ（`sync.nop` に SBID の dst の wait） |
| `BARRIER` | 前に immediate の fence。`mov(8)` tmp 0（NoMask）、`and(1)` tmp.2 r0.2 0x7f000000（NoMask）、`send(8)` null tmp SFID 3（gateway）、descriptor の bit 2:0 に subfunction 4（barrier。Mesa の `brw_eu_inst.h` の `gateway_subfuncid` = MD12(2..0)）、mlen 1、rlen 0、header なし、NoMask。続けて `sync.bar`。group の invocation が 8 以下なら何も出さない |
| thread の終わり | `mov(8)` r127 r0（NoMask）、`send(8)` null r127 SFID 7（thread spawner）、descriptor 0x02000000（mlen 1）、ex_desc 0、NoMask、EOT |

**確かめた descriptor**（2026-09-30、p001 の中で。scratchpad の host の program が既存の `eu.c` の `drv_i915_eu_send` で send を作り、
Mesa 25.0.7 の `brw_disasm --gen=adl` が意図どおりに読み、`brw_asm` で組み直して同じ bytes になった。repo には置いていない。p002・p006 は
この値を encoding の header に写し、試験に同じ照合を入れる）:

| message | SFID | descriptor | ex_desc | brw_disasm の読み |
| --- | --- | --- | --- | --- |
| SLM の read | 12 | 0x02106efe | 0 | untyped surface read, Surface = 254, SIMD8, Mask = 0xe, mlen 1 rlen 1 |
| SLM の write | 12 | 0x02026efe | 0x40 | DC untyped surface write, Surface = 254, SIMD8, Mask = 0xe, mlen 1 ex_mlen 1 |
| SLM の atomic add（応答あり・なし） | 12 | 0x0210b7fe・0x020097fe | 0x40 | DC untyped atomic op, Surface = 254, SIMD8, add, rlen 1・0 |
| A64 の atomic add（応答あり・なし） | 12 | 0x0414a7fd・0x040487fd | 0x40 | DC A64 untyped atomic op, Surface = 253, add, mlen 2, rlen 1・0 |
| fence（global・SLM） | 10 | 0x0219e000・0x0219e0fe | 0 | DC mfence, bti 0・254, commit, mlen 1 rlen 1 |
| gateway の barrier | 3 | 0x02000004 | 0 | gateway barrier msg, mlen 1 |
| thread の終わり | 7 | 0x02000000 | 0 | thread_spawner, mlen 1, EOT（eu-test の実機で動いた kernel の最後の 2 命令を読んだ: `mov(8) g127<1>UD g0<8,8,1>UD {WE_all}`、`send(8) nullUD g127UD nullUD 0x02000000 0x00000000 thread_spawner mlen 1 {WE_all EOT}`） |

**注意（レビュー H4 で見つかった誤り）**: 初版は A64 の atomic の応答なしを 0x0404a7fd（応答の bit 5 が立ったまま rlen 0）と書いた。
`brw_disasm` は応答の bit を表示しないので、disassembler の照合では見つからなかった。試験は descriptor の数値で「control の bit 5 ⇔ rlen ≠ 0」
も確かめる。encoder は応答の bit と rlen を同じ flag から作る。

Mesa は fence を exec size 1 で出す。上の照合は exec size 8 の NoMask で作ったので、exec size 1 の形は p006 で足して同じく照合する。

aop（`BRW_AOP_*`）: AND 1、OR 2、XOR 3、MOV 4、INC 5、DEC 6、ADD 7、SUB 8、IMAX 10、IMIN 11、UMAX 12、UMIN 13、CMPWR 14。
`sync` の function（Mesa の `brw_eu_defines.h` の `tgl_sync_function`: NOP 0、ALLRD 2、ALLWR 3、BAR 0xe。確認済み）も写す。
新しい命令はすべて `eu.c` の encoder に足し、encoding の header に Mesa の file と sha256 を出典として足す。scoreboard（SWSB）は既存の
`scoreboard-check.h` で健全性を確かめる（send の dst を読む前の wait、fence の完了待ち）。

### 1.6a p002 の実装で確定したこと（2026-09-30）

- **NumWorkgroups**: parser は set `I915_IR_SYSTEM_SET`（0xFFFFFFFF）・binding 0 の storage buffer を uniform の並びに足し、3 word を
  `LOAD_STORAGE`（offset 0・4・8、predicate なし）で読む。binary の `blocks[]` に `set == I915_IR_SYSTEM_SET` の address の block として
  出るので、実行器（p004）はその block に 3 word の置き場所の address を入れる（direct は slot の中、indirect は indirect buffer + offset）。
- **`LOAD_STORAGE` の predicate** は src[1]（src[0] が offset で、source の番号が連続するため。STORE は src[2] のまま）。compute の stage だけに
  付ける（graphics の shader の生成は変えない）。
- **`ATOMIC`** の応答の有無の flag は IR に持たない。code generator が liveness（dst を読む命令が後にあるか）で決め、応答の bit と rlen を同じ
  判断から作る。predicate は値の後ろの source（src[2]、CMPXCHG は src[3]）。
- **`STORAGE_SIZE`**（`OpArrayLength` の range）は storage の address の register の dword 2 を読む。実行器（p004）がそこに range を置く。
- **WS075 の skip の囲い**（ws075-p023）: `ATOMIC` は `STORE_STORAGE` と同じく「囲いの中では P の下の predicate が要る」「囲いの後で
  garbage を受けてはならない」の検査に入れた。`BARRIER`（p006）は囲いの中にあれば囲いを断る（飛ぶと GPU が固まる）形で足す。
- **compute の変更の置き場所**: WS075 と同じ file での衝突を減らすため、`compiler/spirv-compute.inc`・`compiler/compile-compute.inc` に置き、
  spirv.c・compile.c の末尾から include する（host の試験は .c を include するので、そのまま通る）。
- **拒否**: Workgroup の変数（p006）、barrier（p006）、image・sampler、spec constant、各軸 128・128・64 と総数 128 を越える workgroup、
  SubgroupSize などの built-in、順序の semantics を持つ atomic（fence は p006）。

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
  作成の時に compile する。空でない specialization は、compile の refuse と同じく pipeline の作成の失敗にし、kernel の message で理由を言う
  （黙って無視しない）。compute の kernel は instruction window の全体（`I915_GFX_INSTRUCTION_BYTES`、48 KiB。graphics の vertex 16 KiB・
  pixel 32 KiB の分け方を使わない）に置いてよく、越えたら作成の失敗。
- **bind point の分離**: `vkCmdBindPipeline` と `vkCmdBindDescriptorSets` の bind point を op に記録し、実行の state に
  `compute_pipeline`・`compute_dset[4]`・compute の dynamic offset を graphics と別に持つ（Vulkan の規則）。push constants は共通。
- **opcode 110**（`vkCmdDispatch`: [cmdbuf][x][y][z]）と **111**（`vkCmdDispatchIndirect`: [cmdbuf][buffer][offset]）を
  `i915_record_command` に足し、op `I915_GFX_OP_DISPATCH`・`I915_GFX_OP_DISPATCH_INDIRECT` にする。x・y・z のどれかが 0 の direct は何もしない。
  上限（65535）を越えるものは EINVAL。

### 2.2 dispatch の実行（新しい file `render/compute.c`。`dispatch.c` は wire の router なので名前を分ける）

1. pipeline の kernel を instruction window に置く（`drv_i915_gfx_window` に compute の code を vertex の位置で渡す。大きさは §2.1）。
2. `transfer_pending` なら（前の GPU の op が buffer を書いたかもしれない）、CPU で uniform block を写す前に batch を走らせる（draw と同じ規則）。
3. slot を取り、state を書く:
   - dynamic heap（slot + 0x1000）の offset 0 に **INTERFACE_DESCRIPTOR_DATA**（8 dword、64 byte 境界）。
   - dynamic heap の offset 0x40 に NumWorkgroups の 3 word（direct のとき。shader が読むときだけ）。
   - slot + 0x2000（dynamic の offset 0x1000、64 byte 境界）から **CURBE**: cross-thread C register、続けて thread ごとに per-thread 4 register。
     合計を 64 byte の倍数に丸める。最大は C 32 と thread 16 で (32 + 64) register = 3 KiB。**slot の 0x2000〜0x2fff（4 KiB）に限る**
     （0x3000 は post-sync の書きの `I915_GFX_SCRATCH`）。
   - surface heap は使わない（SLM は BTI 254、SSBO と scratch は 253 で binding table を引かない）。IDD の binding table は 0、数 0。
4. batch に書く（§2.3）。
5. 記録した後、SSBO を 1 つでも持つ dispatch は `transfer_pending = 1`（後の op が CPU で写す uniform の元を書いたかもしれない）。
   **SSBO を書く draw（`vertexPipelineStoresAndAtomics`）も同じく立てる**（今は rectangle だけが立てる既存の不足。draw が書いた SSBO を compute が
   uniform として CPU で写すと古い値になる）。

### 2.3 batch（1 dispatch）

```
drv_i915_gfx_emit_context_setup()   既存: flush（CS stall・RT・depth・DC、RT に HDC の pipeline flush）、PIPELINE_SELECT(3D)、
                                    STATE_BASE_ADDRESS（general = compute の scratch、surface・dynamic = slot、instruction = window）、
                                    invalidate。SBA を 3D で出すのは Wa_1607854226（eu-test と同じ）
PIPE_CONTROL  CS stall | RT flush | depth flush（→ HDC pipeline flush）   TGL の PRM: 3D → GPGPU の前
PIPELINE_SELECT(GPGPU)              0x69041312
PIPE_CONTROL  CS stall | stall at scoreboard                             eu-test と同じ。VFE の前の stalling PIPE_CONTROL
MEDIA_VFE_STATE (9 dw, 0x70000007)  scratch（Per Thread Scratch Space 35:32、Scratch Space Base Pointer 79:42）、
                                    Maximum Number of Threads 127:112 = 112 × DSS − 1（eu-test・anv）、Number of URB Entries 111:104 = 2、
                                    URB Entry Allocation Size 191:176 = 2、CURBE Allocation Size 175:160 = ALIGN(C + 4T, 2)
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
  Number of Threads in GPGPU Thread Group 201:192 = T、Shared Local Memory Size 212:208、Barrier Enable bit 213 = `uses_barrier`、
  Cross-Thread Constant Data Read Length 231:224 = C。
- **SLM の大きさ**: `shared_bytes` を 2 の冪に切り上げ（最小 1 KiB、`intel_compute_slm_calculate_size`）、Gen9+ の encode（0 = なし、
  1 = 1 KiB、2 = 2 KiB、3 = 4 KiB、4 = 8 KiB、5 = 16 KiB）。16 KiB を越えるものは compiler が断る（§1.2）。
- T = ⌈group の invocation / 8⌉（最大 16）。Right Execution Mask = invocation が 8 の倍数なら 0xff、でなければ (1 << (n mod 8)) − 1。
- Gen12 では SLM は L3 の外（Mesa の `intel_l3_config.c`: SLM の重みは ver < 11 だけ）。L3 の設定は変えない。
- **per-thread の ID の表**: thread t の channel c は linear = 8t + c。linear < n なら x = linear mod sx、y = (linear / sx) mod sy、
  z = linear / (sx·sy)、index = linear。n 以上は 0（right mask で走らない）。pipeline の作成時に作り、dispatch ごとに CURBE へ写す。
- **cross-thread**: push constants（state の 128 byte）、uniform block の写し（draw と同じ関数で CPU が写す）、SSBO の address（descriptor の
  buffer の memory の VA + buffer の offset + descriptor の offset + dynamic offset）と range、system の NumWorkgroups の address。
- **storage の register の dword 2（range）**: `OpArrayLength` のため、draw の push data の storage の register にも range を置く（今は dword 0〜1 だけ）。
- **scratch**: `i915_draw_scratch` の stage の添字に compute を足し、per-thread の大きさ × **128 × 6（= 768）** の部分を割り当てる
  （Mesa の Gen12 の `init_max_scratch_ids`: `scratch_ids_per_subslice = 16 * 8`、GT2 は fuse に依らず 6 subslice。レビュー H3。
  112 × DSS では FFTID が確保の外を指し、spill する kernel が隣の memory を壊しうる）。
- **stateless の MOCS**: 今の context setup は `STATE_BASE_ADDRESS` の stateless の data port の MOCS を uncached（index 3）にする。SSBO の
  load・store はこれで正しい。A64 の atomic が uncached の MOCS で正しく動くかは PRM で確かめていない。p005 の実機の ATOMIC-SSBO の step が
  違う値を出したら、compute の op だけ stateless の MOCS を write-back（index 2、instruction heap と同じ）にし、op の終わりの DC flush（既にある）で
  CPU と次の op に見せる。

### 2.4 graphics と compute の切り替え

各 op が 3D で始まり 3D で終わるので、draw・rectangle の既存の前提は変わらない。compute の op は自分の中で 3D → GPGPU → 3D を行う。
anv の Gen12.0 の「3D から compute へ切り替えたら IDL を出し直す」は、dispatch ごとに IDL を出すので満たす。flush の中身は anv の
`flush_pipeline_select`（TGL の PRM の引用）に従う。

- Gen12.0 には untyped の DC の flush の独立の bit が無い（gen120 の PIPE_CONTROL）ので、DC flush と HDC pipeline flush で代える（anv も 12.5 未満では同じ）。
- TGL の PRM は GPGPU → 3D の前に「Generic Media State Clear」も求めるが、anv は hang を理由に省いている（`flush_pipeline_select` の注）。
  WS101 も省く。
- `PIPE_CONTROL` の helper（`drv_i915_batch_pipe_control`）は HDC の flush を RT の flush に付けて出すだけなので、HDC だけを立てる形を足す
  （GPGPU の中では RT の flush を立てない。anv の注意: 「GPGPU の pipeline から tile cache を flush できない」）。
- depth の flush に depth stall を付ける Wa_1409600907（ADL に該当）は既存の helper も満たしていない。既存の不足として記録し、WS101 では
  compute の op の中に depth の flush を足さない（3D の側の不足は WS075 の範囲）。

### 2.5 `vkCmdDispatchIndirect`

```
MI_LOAD_REGISTER_MEM  GPGPU_DISPATCHDIMX (0x2500) ← buffer + offset + 0     （anv の compute_load_indirect_params）
MI_LOAD_REGISTER_MEM  GPGPU_DISPATCHDIMY (0x2504) ← + 4
MI_LOAD_REGISTER_MEM  GPGPU_DISPATCHDIMZ (0x2508) ← + 8
... MEDIA_CURBE_LOAD、IDL、GPGPU_WALKER（Indirect Parameter Enable）
```

- NumWorkgroups は §1.1・§1.5 の address の方式なので、indirect では system register に indirect buffer の address + offset を置くだけ。
  CS が CURBE の memory を書き換える手（`MI_COPY_MEM_MEM`）は使わない（レビュー M14。anv も iris も使っておらず、順序の保証が未確認）。
- 0x2500〜0x2508 は RCS の MMIO（0x2000〜0x27ff）の中で、anv は非特権の user の batch からこの LRM を使う。zedBSD の実行器の batch の
  実行の形態でも通るかは未確認で、p007 の実機で確かめる。
- indirect の数が 0 の場合: anv は特別な扱いをしない。p007 で 0 の group の walker が何もしないことを実機で確かめる。固まるなら
  `MI_PREDICATE` で飛ばす形に変える（p007 の中で直す）。
- indirect buffer が前の dispatch の結果（GPU が書いた数）でもよい: op の間の flush で書きは見える。これを試験の 1 step にする。

### 2.6 `eu-test.c` の実機で動いた設定の使い方

eu-test の batch（3D で SBA → flush → GPGPU → CS stall＋stall at scoreboard → VFE → MSF → IDL → walker → MSF）と VFE の値
（最大 thread 112 × DSS − 1、URB 2・2）を**そのまま骨格**にする。差は CURBE（eu-test は push が無い）、IDD の thread 数・SLM・barrier、
walker の group と mask、最後の 3D への戻り、compute の scratch。bring-up（p005）はまず eu-test と同じ 1 group・1 thread・1 channel の設定で
compiler の出した kernel を走らせ、次に 1 group の複数 thread、複数 group、端数の group の順に広げる。barrier は p006。

## 3. GLES 3.1 の compute（libglesv2・libegl、ws068-p035 との関係）

### 3.1 範囲の分け方

- WS101 が持つ: **GLES 3.1 の compute の部分集合**（GLSL ES 3.10 の compute の stage と SSBO・shared・atomic・barrier、
  `glDispatchCompute`・`glDispatchComputeIndirect`、`GL_SHADER_STORAGE_BUFFER`、`glMemoryBarrier`・`glMemoryBarrierByRegion`、
  compute の上限の query、`glGetProgramiv(GL_COMPUTE_WORK_GROUP_SIZE)`）。
- ws068-p035 に残す: desktop GL の `GL_ARB_compute_shader`（libGL）、image load/store、atomic counter、program interface query の全体。
  ws068-p035 の「p037 に依存」は desktop GL の順の話で、GLES 3.1 の compute の部分集合はそれに依らない。
  **ws068 の ws.md の p035 の行に「GLES 3.1 の compute の部分集合は WS101 へ移した」と書くのは main の作業**（WS101 の範囲の外）。
- libglesv2 の source は libGL にも組み込まれる（`userland/retro/libGL/Makefile`）。ES 3.1 の stub・export・版の名乗りが desktop GL に漏れない
  分け方（libGL の `gles_fixed` の有無で分ける既存の形）を p010 で決める。

### 3.2 GLSL ES 3.10 の compute を自前の compiler から SPIR-V へ（`userland/desktop/libglesv2/glsl/`、[glsl-design](../ws068/glsl-design.md) の延長）

| 項目 | 内容 |
| --- | --- |
| 版 | `#version 310 es` を受ける（今は 100・300 es）。3.10 の予約語のうち使うもの（`buffer`、`shared`、`coherent`、`volatile`、`restrict`、`readonly`、`writeonly`）を keyword に |
| stage | compute。`layout(local_size_x = N, local_size_y, local_size_z) in;` を読み `OpExecutionMode LocalSize` に。`gl_WorkGroupSize` は定数 |
| built-in | `gl_GlobalInvocationID`・`gl_LocalInvocationID`・`gl_WorkGroupID`・`gl_NumWorkGroups`（uvec3）、`gl_LocalInvocationIndex`（uint） |
| SSBO | `layout(std430, binding = n) buffer Name { ... } inst;`。std430 の offset（配列の stride は要素の base alignment、vec3 は 16 境界）、最後の member の実行時長の配列（`OpTypeRuntimeArray`）、`.length()`（`OpArrayLength`）。SPIR-V 1.0 に合わせ Uniform ＋ `BufferBlock`（Vulkan 1.0 の libvulkan と Venus の host のため）。memory の qualifier は `NonWritable`・`NonReadable`・`Coherent`・`Volatile`・`Restrict` |
| shared | `shared T name[N];` を Workgroup の変数に。初期化子は断る（GLSL の規則） |
| 関数 | `barrier()` → `OpControlBarrier(Workgroup, Workgroup, AcquireRelease\|WorkgroupMemory)`、`memoryBarrier()`・`memoryBarrierBuffer()`・`memoryBarrierShared()`・`groupMemoryBarrier()` → `OpMemoryBarrier`、`atomicAdd/Min/Max/And/Or/Xor/Exchange/CompSwap`（int・uint、buffer と shared の変数だけ） |
| barrier の規則 | GLSL ES 3.10 の「`barrier()` は `main` の制御の外（選択・loop の中でない）で、return の後でない所だけ」を意味解析で検査し、違反は compile の error |
| 早期の return | `main` の中の `return`（選択の中）。i915 の parser は扱える（loop の中は断る）。今の emitter（`emit.c`）は loop の中の `return` にも OpReturn を直接出すので、loop の中の return を持つ compute の shader は i915 で断られる（未確認、p008 で確かめる。Noct は使わない） |

host の試験（ws068-p018 の方法を使う）: `spirv-val --target-env vulkan1.0`、lavapipe での実行（host の Vulkan の小さな harness）、
i915 の host の compile と disassembler の照合（§5.2）。**Noct が実際に生成する shader の形**（§5.2）を fixture に入れる。

### 3.3 API（`userland/desktop/libglesv2/`）

- **shader と program**: `glCreateShader(GL_COMPUTE_SHADER)`（`program.c` の種別の検査）、`glAttachShader` の slot、compute だけの program の
  link（今の「a program needs a vertex and a fragment shader」を compute の program では外し、他の stage と混ぜたら link の失敗）、
  descriptor set layout の配列の大きさ（`GLES_UNITS + 2 + GLES_NAMED_BLOCKS` に SSBO の分）、`stageFlags`（今は VERTEX|FRAGMENT に固定。
  compute の program は COMPUTE）、compute の VkPipeline（`vkCreateComputePipelines`）。
- **dispatch**: `glUseProgram` の後の `glDispatchCompute(x, y, z)`・`glDispatchComputeIndirect(offset)`（`GL_DISPATCH_INDIRECT_BUFFER` の target）。
  dispatch の前に開いている render pass を閉じる（`gles_target_close`）。
- **buffer**: `buffer_slot` に `GL_SHADER_STORAGE_BUFFER`・`GL_DISPATCH_INDIRECT_BUFFER` の target を足す。device の写しの `VkBuffer` の usage に
  `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT`・`INDIRECT_BUFFER_BIT` を足す（今は無い。Venus の host の validation と descriptor の書きで誤りになる）。
- **SSBO の binding**: `glBindBufferBase/Range(GL_SHADER_STORAGE_BUFFER, n, …)`。GL の binding n を Vulkan の set 0 の binding **56 + n** にする
  （既存: 0 = 既定の uniform、1〜16 = sampler、32〜55 = uniform block、48 = transform feedback の capture。64 が上限）。
  `GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS` と `GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS` は **device の `maxPerStageDescriptorStorageBuffers`
  （i915 は 4）以下**で、ES 3.1 の最小の 4 にする。i915 の compiler の block の上限（uniform block と SSBO の address で 8）にも収まる
  （Noct は data の buffer の数 + scalar + 結果 ≤ 4 で動く。data の buffer 2 つまで）。
- **device が書いた buffer と submission**（レビュー M1 で実装に合わせて書き直した）:
  - `glFlush`・`glFinish` は今は何もしない。submit・待ち・garbage の回収が起きるのは、swap の frame の終わり（`frame_done`）か、
    `glReadPixels`・fence・device が書いた buffer の読み戻し（`gles_buffer_fetch` → `query_finish`）。Noct では `glMapBufferRange(READ)` の
    読み戻しがその点になる。
  - dispatch の後、書ける SSBO として bind された buffer を「device が書いた」にし、`buffer->used = state->frame` を立てる（transform feedback と
    同じ。`feedback.c`）。読み戻しは used の frame を待つ。
  - Noct は操作ごとに `eglMakeCurrent(ctx)` と `eglMakeCurrent(NO_CONTEXT)` を繰り返す。libEGL の release は pass を閉じるだけで submit しない。
    記録した dispatch が release と再取得をまたいで残り、次の読み戻しで submit されることを p009 の試験に入れる。
  - `glFinish` を「記録を submit して待つ」にするか（ES の意味どおり）を p009 で決める（Noct の dispatch ごとの `glFlush` は害が無い）。
  - **garbage の回収**: Noct は `eglSwapBuffers` をしない pbuffer の context で、call ごとに `glBufferData` をする。読み戻しの待ち（`query_finish`）の
    時点で garbage が回収されることを p009 で確かめ、無ければ足す。回収されないと 16 MiB の device の memory（物理的に連続の GEM の object）が
    call ごとに積もる。回収されても、call ごとの 16 MiB の連続の確保と解放が断片化で失敗しうる（未確認。p011 の繰り返しの run で見る）。
- **`glMemoryBarrier(bits)`**: `vkCmdPipelineBarrier`（src COMPUTE_SHADER、dst は bits に応じて COMPUTE・VERTEX_INPUT・DRAW_INDIRECT・
  TRANSFER・HOST）。i915 の実行器では何もしない（§2.1）が、Venus には要る。
- **query**: `GL_MAX_COMPUTE_WORK_GROUP_COUNT/SIZE`（`glGetIntegeri_v`）、`GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS`、
  `GL_MAX_COMPUTE_SHARED_MEMORY_SIZE`、`GL_MAX_SHADER_STORAGE_BLOCK_SIZE`（`glGetInteger64v`、device の `maxStorageBufferRange`）、
  `GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT`（device の値）。device の limits から出す。
- **header と export**: `include/libc/GLES3/gl31.h`（Khronos、MIT、`gl3.h` と同じ registry の版）を足し、`include/libc/EGL/API-PROVENANCE.md`
  の表も更新する。sysroot は `include/libc` を写す（`toolchain/llvm/sysroot.mk` の規則は変えない。header の追加は source の変更）。libGLESv2 の
  `exports.map` に ES 3.1 の entry point を足す。compute 以外の 3.1 の関数（image load/store、indirect の draw、program pipeline、vertex attrib
  binding など）は `GL_INVALID_OPERATION` を記録する stub にする（黙って受けない。`-z now` の link で未定義の symbol を残さない）。
- **版の名乗り**: Noct は `GL_MAJOR_VERSION`・`GL_MINOR_VERSION` が 3.1 以上でなければ backend を使わない。ES 3.1 の全体
  （image load/store、atomic counter ほか）は実装しないので、「OpenGL ES 3.1」を名乗るかは**ユーザーの判断 D1**（§7）。
  既定の案: compute を持つ device の ES 3 の context は「OpenGL ES 3.1 Kei」・「OpenGL ES GLSL ES 3.10」を名乗り、未実装の 3.1 の機能は
  stub の error で断る。compute を持つ device とは、今の 3.0 の条件（`vertexPipelineStoresAndAtomics`）に加え、queue に COMPUTE_BIT があり、
  limits が ES 3.1 の最小以上（invocation 128、size 128・128・64、count 65535、shared 16384、stage あたりの storage buffer 4）のもの。
  i915 の実行器の報告値はちょうど最小。危険: 3.1 を見て 3.1 の経路を選ぶ他の ES3 の app が stub の error に当たる（egltest・既存の app の回帰で見る）。
- `GL_RENDERER`: 今の固定の文字列のまま（Venus の host が lavapipe でも Noct は受ける。性能は Venus では測らない）。

### 3.4 EGL と Noct の `accel_opengles.c` の使う API

| API | 今の状態 | 要ること |
| --- | --- | --- |
| `eglGetProcAddress("eglGetPlatformDisplayEXT")`、`EGL_PLATFORM_SURFACELESS_MESA`（`include/libc/EGL/eglext.h` に定義がある）、`eglGetDisplay(EGL_DEFAULT_DISPLAY)` | ある | surfaceless が先に選ばれる。surfaceless が失敗・不適（版が 3.1 未満など）なら Noct は `EGL_DEFAULT_DISPLAY` を試し、zedBSD ではそれが画面を直接使う display の platform になる。pbuffer だけなら画面を取らないことを p009 で確かめる（Wayland の session の中で Noct を走らせても desktop が消えないこと） |
| `eglInitialize`・`eglBindAPI(EGL_OPENGL_ES_API)`・`eglChooseConfig(PBUFFER, ES3)`・`eglCreateContext(CLIENT_VERSION 3)`・`eglCreatePbufferSurface(1×1)`・`eglMakeCurrent`（acquire と release の繰り返し）・`eglDestroy*`・`eglTerminate` | ある | p009 で surfaceless の pbuffer の context で compute が動くこと、release と再取得をまたぐこと（§3.3）を確かめる |
| `glGetIntegerv(GL_MAJOR/MINOR_VERSION, GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS, GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS)`、`glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT/SIZE)`、`glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE)`、`glGetString(GL_RENDERER)`、`glGetError` | 版は 3.0、compute の上限は無い | §3.3 |
| `glCreateShader(GL_COMPUTE_SHADER)`・`glShaderSource`・`glCompileShader`・`glGetShaderiv`・`glCreateProgram`・`glAttachShader`・`glLinkProgram`・`glGetProgramiv`・`glDetachShader`・`glDeleteShader`・`glDeleteProgram`・`glUseProgram` | compute が無い | §3.2・§3.3 |
| `glGenBuffers`・`glBindBuffer(GL_SHADER_STORAGE_BUFFER)`・`glBufferData(GL_DYNAMIC_COPY)`・`glBindBufferBase`・`glMapBufferRange(GL_MAP_READ_BIT)`・`glUnmapBuffer`・`glDeleteBuffers` | target の SHADER_STORAGE_BUFFER が無い | §3.3 |
| `glDispatchCompute`・`glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT \| GL_BUFFER_UPDATE_BARRIER_BIT)`・`glFlush`・`glFinish` | dispatch・barrier が無い | §3.3 |

Noct の GLSL（`accel_shader_source.c`）が使う言語: `#version 310 es`、`precision highp`、`layout(local_size_x = 64) in;`、
`layout(std430, binding = n) [readonly|coherent] buffer B { uint word[]; } b;`、`gl_GlobalInvocationID.x`、int・uint の算術
（`uint(int(a) / int(b))`、`%`、shift、bit 演算）と比較、`?:`、SSBO の `atomicAdd`（DOSUM と結果の word）、`main` の選択の中の `return`
（`if (…)` の次の行に `return;` だけの形）。shared・barrier・loop は使わない。**float（`uintBitsToFloat` など）は GLES の backend では使われない**
（`accel_opengles_program_uses_f32` が真なら `ACCEL_COMPILE_DECLINED` で CPU に戻る）。

**Noct の shader の compile と link の失敗は CPU への fallback ではなく program の error**（`accel_opengles.c` の `hir_error`）。DOSUM と結果の
word は SSBO の `atomicAdd` を使うので、**SSBO の A64 の atomic は G3 の経路に要る**（§6 で p002・p005 に入れた。レビュー M8）。

## 4. Noct（G3）

### 4.1 guest の build で accel を有効にする変更の案（**toolchain の変更。main とユーザーの許可まで適用しない。実装は main**）

subagent は toolchain（`userland/base/noct/` の patch と build の規則、共有の `build/NoctLang`）を変えられないので、p011 の toolchain の部分は
main が行う（Phase の表に明記）。

(1) Noct の patch を 1 つ足す（`userland/base/noct/patches/0004-accel-opengles-on-zedbsd.patch`）。CMakeLists.txt の OpenGL ES の backend の
**2 か所**（source の選択と定義）の条件に zedBSD を足す。link は zedBSD では Linux・FreeBSD の枝に入らないので変えない（hunk は 2 つ）:

```diff
--- a/CMakeLists.txt
+++ b/CMakeLists.txt
@@ (accel の source の選択)
-  if(   CMAKE_SYSTEM_NAME STREQUAL "Linux"
-     OR CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
+  if(   CMAKE_SYSTEM_NAME STREQUAL "Linux"
+     OR CMAKE_SYSTEM_NAME STREQUAL "FreeBSD"
+     OR NOCT_TARGET_ZEDBSD)
@@ (accel の定義)
-  if(   CMAKE_SYSTEM_NAME STREQUAL "Linux"
-     OR CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
+  if(   CMAKE_SYSTEM_NAME STREQUAL "Linux"
+     OR CMAKE_SYSTEM_NAME STREQUAL "FreeBSD"
+     OR NOCT_TARGET_ZEDBSD)
```

- 上は形の説明。patch は `patch --fuzz=0` で当たるので、実物の `CMakeLists.txt`（context の行に行末の空白がある。574・583・593 行付近）から
  `diff -u` で作る。
- `ZEDBSD_NOCT_PATCHES` の明示の一覧（`userland/base/noct/Makefile` の 16〜19 行）に足し、`version.mk` の `ZEDBSD_NOCT_PATCH_LEVEL` を上げる。
- **影響**: patch level は host の Noct（共有の `build/NoctLang`、toolchain の lock の対象、全ての worktree の build が使う）の
  `NOCT_HOST_SOURCE_STAMP` にも効くので、**host の Noct も展開と build のやり直しになる**。避けるなら、target だけの patch の一覧と level を分ける
  （これも build の規則の変更）。どちらにするかを D2 に含める。

(2) `userland/base/noct/zedbsd.cmake` の `noct_configure_zedbsd_target` で、accel のときだけ build の shared library を link し、`LINK_DEPENDS` にも足す
（library を更新したら relink する）:

```diff
+  # The OpenGL ES accelerator backend loads the build's libEGL and libGLESv2.
+  if(NOCT_ENABLE_ACCEL)
+    target_link_libraries("${target}" PRIVATE
+      "$ENV{ZEDBSD_DYNAMIC_DIR}/libEGL.so"
+      "$ENV{ZEDBSD_DYNAMIC_DIR}/libGLESv2.so"
+    )
+    set_property(TARGET "${target}" APPEND PROPERTY LINK_DEPENDS
+      "$ENV{ZEDBSD_DYNAMIC_DIR}/libEGL.so"
+      "$ENV{ZEDBSD_DYNAMIC_DIR}/libGLESv2.so"
+    )
+  endif()
```

(3) accel を有効にするかの決め方。`ZEDBSD_USER_PROGRAMS` は top の Makefile の後半（290〜335 行付近）で確定し、package の Makefile を読む時点では
確定していない。rule の target と依存は rule を読んだ時点で展開されるので、recipe の中の遅い展開では stamp の名と `libEGL.so` の依存を決められない。
そこで **構成の file（`config-*.mk`、package より前に読まれる）で明示の変数 `ZEDBSD_NOCT_ACCEL := y` を持つ**形にする:

```make
# userland/base/noct/Makefile（案）
NOCT_ZEDBSD_ACCEL := $(and $(filter amd64,$(NOCT_ZEDBSD_ARCH)),$(filter y,$(ZEDBSD_NOCT_ACCEL)))
NOCT_ZEDBSD_CMAKE_OPTIONS += -DNOCT_ENABLE_ACCEL=$(if $(NOCT_ZEDBSD_ACCEL),ON,OFF)   # CMake の cache に前の ON を残さない
NOCT_ZEDBSD_BUILD_STAMP := $(NOCT_ZEDBSD_BUILD_DIR)/.zedbsd-built-$(ZEDBSD_NOCT_VERSION)-$(ZEDBSD_NOCT_PATCH_LEVEL)$(if $(NOCT_ZEDBSD_ACCEL),-accel)
# accel の stamp の依存に $(BUILD)/dynamic/libEGL.so と libGLESv2.so を足す
```

`ZEDBSD_NOCT_ACCEL := y` を置く構成（demo・desktop の amd64 の config）は libegl・libglesv2 も選ぶ。置かない構成（i386・pc98・rpi4・最小の構成）の
`/bin/noct` は今のまま。最小の差分は p011 で Makefile の全体と config の読み込みの順を読んで作り、main に出す。

なぜ条件付きか: accel を有効にした `/bin/noct` は `libEGL.so`・`libGLESv2.so`（とその先の libvulkan・libwayland）を DT_NEEDED に持ち、
`-z now` で読み込む。desktop の library の無い image では `/bin/noct` が起動できなくなる。代わりの形（D2 の選択肢 B）: base の `/bin/noct` は
変えず、同じ検証済みの tarball から accel 付きの `/bin/noct-gpu` を別に作る。

(4) ヘッダ: `accel_opengles.c` は `<GLES3/gl31.h>` を要る（§3.3 で足す）。`eglplatform.h` は `__unix__` の枝（`include/libc/EGL/eglplatform.h`
の 122 行付近）で通る見込み（libegl が同じ header で build できている）。

### 4.2 G3 の見本と N・倍率の目標（案。**ユーザーの確認 D3・D5**）

**Noct の GLES の backend は float を使う program を GPU に出さない**（§3.4）。よって G3 の見本は整数だけで作る。float の見本（例: saxpy）は
GLES の backend では CPU で走るだけなので、見せ物にならない。float を GPU に出すには Noct に patch を当てる（Noct の意味の変更で、
toolchain の変更でもある）ことになり、判断 D5 とする。

- **見本 1（受け入れ）`mix.nct`**: 整数の hash の DOALL。結果は CPU と bit で一致する。1 要素あたり 32 の整数の演算（乗算・加算・xor・
  論理 shift を 8 回）を直線で書く。
  ```
  __accel func mix(data: rpackeduint32, count: int): void {
      for (i in 0..count) {
          var x = data[i];
          x = x * 1664525 + 1013904223;  x = x ^ (x >> 13);
          ... （8 回。p011 で Noct の offload の受ける形に合わせる）
          data[i] = x;
      }
  }
  ```
  型の名（Noct の試験は `rpackeduint32_in`・`_out` を使うが、`src/core/hir.c` の表には `rpackeduint32` がある）、`Accel.call` と直接の呼び出し、
  `>>` の符号（論理 shift か）、32 bit の wrap が GPU と同じかは p011 で Noct の source と試験で確かめる。
- **N = 4,000,000**（16 MB の buffer 1 つ、in-place）。Noct は group の数 ⌈N / 64⌉ を device の `GL_MAX_COMPUTE_WORK_GROUP_COUNT[0]`
  （i915 と lavapipe は 65535）と比べて越えれば error にするので、N ≤ 65535 × 64 = 4,194,240（2^22 は 1 越える。レビュー H1）。
  上限を上げる（walker の dim は 32 bit）なら instance.c の報告値の変更になり D3 に含める。
- **計り方**:
  - 同じ `noct`（最適化の水準 1 以上。GPU の書き換えの条件）で `--gpu` 有りと無しを比べる。
  - Noct の JIT は呼ばれた数（既定 5 回）で効き始めるので、CPU の側は十分な warm-up の後に計る（何回で安定するかは p011 で確かめる）。
    GPU の側は最初の 1 回（EGL の初期化と shader の compile）を除いた最良。
  - **offload が本当に起きた**こと（DECLINED で黙って CPU に戻っていないこと）を確かめる。例: libglesv2 の dispatch の counter を環境変数で
    stderr に出す、`--gpu-list` と実行の時間の差。方法は p010・p011 で決める。
  - 結果は全要素の checksum（CPU で計算）と、CPU の run の結果との一致を確かめる。
- **目標（案）**: 5330 の素の機械で GPU が CPU の 3 倍以上速い（伸び 10 倍）。**見込みには根拠がまだ無い**（CPU の 1 要素の時間は未測定）。
  p011 で最初に素の 5330（または passthrough）で CPU だけの時間を測り、D3 の倍率を決め直す。
- **使わないもの**: DOSUM（Noct は lane ごとに 1 word へ `atomicAdd` するので、N が大きいと 1 address への atomic が直列になり遅い）。Noct の
  code 生成の改善は WS101 の範囲の外（Future Work の候補）。
- N を大きくしない理由: GEM の object は物理的に連続の run（`struct i915_gem_object` の `kern_pmem run`）で、64 MiB の連続の確保は断片化で
  失敗しうる。

## 5. 試験

### 5.1 i915 の compute の試験の群（kernel の試験の build の場面 `vkcs`）

`src/drivers/gpu/i915/tests/render/compute.c` と `compute-shaders/`（GLSL と SPIR-V、`regenerate.py` が host の `glslc` で SPIR-V を作り、
期待値を Python で計算して `tests/fixtures/compute-shaders-gen.inc` に書く。`generality.c`（vke2）と同じ形）。wire（`drv_i915_render_execute`）
を通すので、`vkCreateComputePipelines`・`vkCmdDispatch`・`vkCmdDispatchIndirect` の decode まで試す。

| step | 内容 | Phase | 判定 |
| --- | --- | --- | --- |
| ADD | `c[i] = a[i] + b[i]`、local size 64、N = 1000（64 の倍数でない。`if (i >= n) return;`） | p005 | 全要素が C の計算と一致、N の外は書かれていない |
| ID | local size (4, 2, 3)、group (3, 2, 2)。全 invocation が 5 つの built-in を書く | p005 | 全 word の一致 |
| ODD | local size (5, 3, 1)（15 invocation、2 thread、right mask 0x7f）。ID と ADD | p005 | 一致。端数の channel は書かない |
| PUSH | push constants と uniform block と 3 つの SSBO（dynamic offset 付き） | p005 | 一致 |
| ATOMIC-SSBO | SSBO の histogram（`atomicAdd`）、結果の word への lane ごとの `atomicAdd`（Noct の DOSUM の形）、`atomicMin/Max/And/Or/Xor/Exchange/CompSwap`、応答の有無の両方 | p005 | 一致（順に依らない結果だけを比べる） |
| MIXED | 1 つの command buffer で draw → `vkCmdCopyImageToBuffer` → dispatch（その buffer を SSBO として読み、別の buffer に頂点を書く）→ draw（その buffer を vertex buffer に） | p005 | 画素と word の一致（3D ↔ GPGPU の切り替えと op の間の順序） |
| MANYOPS | 1 つの submit に 200 の dispatch（slot の 128・window の 32 を越えて途中で batch を走らせる） | p005 | 一致 |
| SPILL | 値の多い compute の kernel（scratch）を**全 thread が埋まる数の group**で | p005 | 一致（FFTID の範囲の誤りを見る） |
| SHARED | shared の 2 次元の tile の転置（`tile[ly][lx]`、動的な index 2 つ）と `barrier()` | p006 | 一致 |
| REDUCE | local size 128 の shared memory の木の reduction（barrier を 7 回）、group ごとの和 | p006 | 一致 |
| ODD-BARRIER | local size (5, 3, 1) で shared と barrier | p006 | 一致 |
| ATOMIC-SHARED | shared の atomic | p006 | 一致 |
| LOOP | loop の中の一様な barrier | p006 | 一致 |
| INDIRECT | indirect buffer の (x, y, z)、前の dispatch が GPU で書いた数、0 の group、NumWorkgroups を読む shader | p007 | 一致、0 は何も書かない |
| LENGTH | `.length()`（compute と fragment） | p007 | 一致 |
| MANY | N = 4,000,000 の ADD の時間（参考の値、判定は一致だけ） | p007 | 一致 |
| REFUSE | spec constant、shared > 16 KiB、`SubgroupSize`、return の後の loop の中の barrier を compile が断る（GPU に出さない） | p002・p006 | pipeline の作成が失敗し、kernel の message が理由を言う |

試験の build の kernel は 16 MiB の上限（AMD64_KERNEL_MAX_BYTES）の近く（WS075 の注意）。期待値の配列は小さく保ち（生成の式を C で持つ）、
大きな state は heap に置く。

### 5.2 host の試験（`plan/ws101/tests/host/`）

- **encoder**: 新しい命令（SLM の read・write、A64・SLM の atomic の応答の有無、fence（exec size 1）、gateway の barrier、`sync.bar`・`sync.allwr`、
  TS への EOT）の語を Mesa 25.0.7 の `brw_disasm --gen=adl` で読み、期待の mnemonic と field（SFID、message の型、BTI、aop）を照合し、
  `brw_asm` で組み直して同じ bytes になることを確かめる（`plan/ws031/tests/run-vk-gentool-test.sh` の encoder の検査の形。tool は
  `BRW_TOOLS=/home/awe/p014-c/mesa/build-asm/src/intel/compiler`）。加えて **descriptor の数値を decode して、control の bit 5（応答）と rlen の
  一致、mlen・ex_mlen を確かめる**（disassembler は応答の bit を表示しない。レビュー H4）。
- **compile**: compute の SPIR-V（vkcs・GLES の試験の shader、**Noct の生成する shader の形**）を `plan/ws075/tests/guard/shader-dump.c` と
  同じ形の dumper（compute の stage を足した WS101 の複写）で compile し、disassembler が全命令を受けること、組み直しが同じ bytes、
  `scoreboard-check.h` が健全と言うこと（`plan/ws075/tests/guard/run.sh` の形）。
- **Noct の shader の形の fixture**: Noct の `accel_shader_source.c` の GLSL の形（`if (…)` の次の行に `return;` だけ、`uint(int(a)/int(b))`、`%`、
  `coherent`・`readonly`、複数 kernel の region が結果 word を読む形、結果 word への `atomicAdd`）を fixture にし、libglesv2 の GLSL →
  `spirv-val` → i915 の host の compile に通す（p008・p010）。
- **lower**: IR の interpreter（`plan/ws031/tests/i915-vk-lower-test.c` の形の WS101 の複写）に compute を足す。1 workgroup の全 invocation を
  lockstep で走らせ（if 変換の IR なので lockstep は自然）、SLM・barrier・atomic を模し、C の独立の式と比べる。
- **batch**: dispatch と indirect の batch を host で作り（`plan/ws031/tests/i915-vk-cmd-test.c` の形）、packet を genxml で読み、
  VFE・IDD・CURBE・walker の field（thread 数、right mask、SLM、barrier、CURBE の長さ）と flush と PIPELINE_SELECT の順を確かめる。
  **`plan/ws031/tests/dump-gen-packets.py` は使えない**（`plan/ws031/mesa-refs` の symlink が別の checkout を指し、古い genxml の `dword`・`bits`
  の属性を読む）。WS101 の dumper を作り、`/home/awe/p014-c/mesa/src/intel/genxml` の gen120 から import の連鎖（gen110 → gen90 → gen80 →
  gen75 → gen70 → gen60）をたどり、`start`・`end` の属性を読む。

### 5.3 実機（5330）

- passthrough: `plan/ws075/tests/test-hw.sh vkcs OUTDIR`（`flock /tmp/i915-hw.lock` を取る。WS075 のエージェントと共有の lock。BUILD は
  WS101 の worktree の build）。GLES と Noct の user の program は `plan/ws031/tests/vkloop-hw.sh` の service の鎖（vkprobe）の形の
  WS101 の複写（`plan/ws101/tests/hw/`）で、同じ lock の下で走らせる。
- 回帰: 同じ lock の下で `vkx`・`vke1`・`vke2`・`vkc`（実行器の変更の Phase ごと）。
- 素の機械（G1 の受け入れの残り、G3 の時間、G4）: ユーザー（p012）。
- GPU が固まったとき: serial の log を読んで判定しない。guest を gdbstub で止め（QEMU の `-S -gdb`）、i915 の error の register
  （EIR・IPEHR・ACTHD・INSTDONE）を monitor・gdb で読む。同じ条件の変更無しの再試行は 3 回まで。

### 5.4 Venus（QEMU）

- GLES の compute の試験の program（`plan/ws101/tests/` の小さな program、または egltest に `--scene=compute`。どちらにするかは p009 で決める。
  egltest は WS068 の program なので、足すなら main の了承を取る）を `--platform=pbuffer` で走らせ、結果の word を比べて PASS・FAIL を出す。
  guest の操作は `plan/tools/guest/serial.py` か `guest.sh`。
- Noct の見本の正しさ（倍率は測らない）。
- Venus の host の Vulkan の device（開発機の GPU か lavapipe）で結果が変わりうるので、正しさだけを見る。

## 6. Phase の分け方

1 Phase は数時間の 1 枠。コードの Phase は Gen12 の hardware で確かめる前に host の試験を通す。第 2 版でレビュー M13 を受けて、
実行器を 2 つ、GLES の API を Venus と i915 に分け、素の機械の確認の Phase を足した。

| Phase | 内容 | 依存 | 確かめ方 |
| --- | --- | --- | --- |
| p002 | compiler の核: GLCompute、LocalSize・LocalSizeId、built-in（`LOAD_SYSTEM`、GlobalInvocationID の展開、NumWorkgroups の A64 の読み）、compute の register の約束と binary の field、TS への EOT、`LOAD_STORAGE` の predicate、効果なしの decoration、`OpArrayLength`、`!= I915_STAGE_VERTEX` の分岐の見直し、**SSBO の A64 の atomic**（IR の `ATOMIC` の SSBO の分、応答の有無）、storage の access chain の複数の動的な index、拒否（spec constant など） | p001 | host: encoder・compile・lower（§5.2） |
| p003 | 実行器の object と記録: `vkCreateComputePipelines`（66、window の全体に kernel、per-thread の ID の表）、bind point の分離、`vkCmdDispatch`（110）の記録 | p002 | host: 既存の host の実行器の試験（`run-vk-host-tests.sh`）に decode の試験 |
| p004 | 実行器の batch: `render/compute.c`（IDD の全 field（SLM・barrier は binary の値）・CURBE・NumWorkgroups の置き場所・VFE・walker・3D への戻り・compute の scratch 768 thread 分）、`transfer_pending`（dispatch と SSBO を書く draw）、HDC だけの PIPE_CONTROL、storage の range（draw と compute）、WS101 の genxml の dumper | p003 | host: batch の試験（§5.2） |
| p005 | 実機の bring-up と `vkcs` の場面（ADD・ID・ODD・PUSH・ATOMIC-SSBO・MIXED・MANYOPS・SPILL）: eu-test の 1 thread の設定から段階的に | p004 | 5330 の passthrough の vkcs、vkx・vke1・vke2・vkc の回帰 |
| p006 | SLM・barrier・fence・SLM の atomic（IR・parser・EU、barrier の一様性の検査、CMPXCHG の順）、vkcs の SHARED・REDUCE・ODD-BARRIER・ATOMIC-SHARED・LOOP・REFUSE | p005 | host の 3 種と実機の vkcs |
| p007 | `vkCmdDispatchIndirect`（111、DISPATCHDIM の LRM）、0 の group、vkcs の INDIRECT・LENGTH・MANY、G1 の passthrough での受け入れの run | p006 | 実機の vkcs の全 step、回帰 |
| p008 | GLSL ES 3.10 の compute（§3.2、barrier の規則の検査、Noct の shader の形の fixture） | p001（p002〜p007 と並行できる）、D4（main の記録） | host: spirv-val、lavapipe、i915 の host の compile |
| p009 | libglesv2・libegl の ES 3.1 の compute の API（§3.3: shader・program・link・layout・stageFlags、dispatch、buffer の target と usage、SSBO の binding（4）、device が書いた印と `used`、garbage の回収、`glMemoryBarrier`、query）、`gl31.h`、export と stub、版の名乗り（D1）、libGL への漏れの分離、GLES の compute の試験の program。**Venus で G2** | p008、D1 | Venus の試験、egltest の既存の場面の回帰 |
| p010 | GLES の compute の **i915 での G2**（passthrough）: 基本は p005 の後、shared・barrier の試験は p006、indirect は p007 の後。Noct の shader の形の fixture を i915 で | p009、p005（p006・p007 の分は後） | 5330 の passthrough の試験 |
| p011 | Noct: **toolchain の部分（§4.1 の patch・Makefile・zedbsd.cmake、host の Noct の作り直し）は main が D2 の許可の後に行う**。WS101 の分: G3 の見本（`plan/ws101/tests/noct/`、N = 4,000,000）、型名と offload の確認、CPU の時間の先の測定、Venus で正しさ、5330 の passthrough で正しさと時間 | p010、D2、D3、D5 | Venus・passthrough の run、時間の記録 |
| p012 | 素の 5330: G1 の受け入れの残り（vkcs）、G3 の時間、G4 の fg010 の台本の場面（CPU と GPU の時間を並べる script と手順、GPU が固まったときの回復の手順）。ユーザーの確認 | p011 | ユーザーの確認 |
| p013 | 規約の全文との照合（WS101 の全ての変更）と回帰（host、vkx・vke1・vke2・vkc・vkcs・GLES、boot test） | p002〜p012 | 全文の review と回帰の記録 |

依存の図（前提 → 後）:

```
p001 → p002 → p003 → p004 → p005 → p006 → p007
                            p005 ─────────────→ p010（i915 の基本の分）
                                   p006・p007 ─→ p010（shared・barrier・indirect の i915 の分）
p001 → p008 → p009 ─────────────────────────→ p010 → p011（＋D2・D3・D5、toolchain は main）→ p012
p002〜p012 → p013
```

日程の目安（新規実装は 10/10 ごろまで）: p002・p008 を並行で始め（10/1〜2）、p003〜p005 と p009 を 10/3〜10/6、p006・p007・p010 を 10/7〜10/8、
p011 を 10/9、p012 は 10/10 以後の実機の確認。p006・p007 が遅れたら、G3（Noct の DOALL は shared・barrier・indirect を使わない。SSBO の atomic は
p002・p005 に入れた）に要る p002〜p005・p008〜p011 を先にする。

## 7. ユーザーと main の判断が要る点

| # | 判断 | 選択肢 | 既定の案と理由 | 待つ Phase |
| --- | --- | --- | --- | --- |
| D1 | GLES の「OpenGL ES 3.1」の名乗り（ES 3.1 の全体は実装しない） | A: compute を持つ device で 3.1 を名乗り、未実装の 3.1 の関数は error の stub。B: ES 3.1 の残り（image load/store、atomic counter、indirect の draw、program pipeline ほか）も実装してから名乗る（OSC の後、数 Phase）。C: 環境変数などで 3.1 を opt-in | A（Noct は 3.1 未満だと GPU を使わない。OSC に間に合う）。WS068 の「実装した版を名乗る」方針（desktop GL）からの外れで、3.1 の経路を選ぶ他の ES3 の app が stub の error に当たる危険があるので、ユーザーの判断 | p009 |
| D2 | Noct の build の変更（toolchain の変更） | A: 構成で `ZEDBSD_NOCT_ACCEL := y` を置いた amd64 の `/bin/noct` だけ accel を ON（§4.1）。B: `/bin/noct` は変えず `/bin/noct-gpu` を別に作る。どちらも patch level を上げると共有の host の Noct（`build/NoctLang`）も作り直しになる。避けるなら target だけの patch の一覧を分ける | A（デモで `noct --gpu` と書ける。desktop の無い構成は今までどおり）。実装は main | p011 |
| D3 | G3 の N と倍率 | N = 4,000,000（Noct の group の数の上限 65535 × 64 の内）、整数の hash、GPU が 3 倍以上（伸び 10 倍）。倍率は CPU の時間を測ってから決め直す | 物理的に連続の確保と、Noct の group の数の上限を避ける | p011 |
| D5 | デモの見本は整数だけでよいか（Noct の GLES の backend は float の program を GPU に出さない） | A: 整数の hash だけ。B: Noct に float を許す patch を当てる（Noct の意味の変更、toolchain の変更、CPU との一致が崩れる） | A | p011 |
| D4（main） | ws068-p035 の範囲の書き換え | GLES 3.1 の compute の部分集合を WS101 へ移したと ws068 の ws.md に記録 | WS101 の範囲の外の file なので main が行う | p008 の前 |

## 8. 危険と対策

| 危険 | 対策 |
| --- | --- |
| barrier・VFE・SLM の誤りで GPU が固まる | eu-test の設定から 1 段ずつ広げる（§2.6）。barrier は group が 2 thread 以上のときだけ。IDD の Barrier Enable と thread 数を host の batch の試験で確かめる。非一様な barrier は compile で断る（§1.2）。固まったら gdbstub と register で解析（§5.3） |
| descriptor の応答の bit と rlen の食い違い（disassembler では見えない） | encoder が同じ flag から作り、host の試験が数値で確かめる（§5.2） |
| compute の scratch の FFTID の範囲 | 128 × 6 の thread 分（§2.3）。SPILL を全 thread が埋まる数の group で |
| 大きな buffer の物理的に連続の確保、call ごとの 16 MB の確保と解放 | N = 4,000,000。繰り返しの run で見る（p011） |
| Noct の group の数の上限（65535） | N ≤ 4,194,240 |
| Noct の GLES の backend は float を出さない | 見本は整数（D5） |
| Noct の shader の compile・link の失敗は program の error | Noct の shader の形の fixture を host と i915 で通す。SSBO の atomic を G3 の経路（p002・p005）に |
| Noct の DOSUM の per-lane の atomic が遅い | 倍率の目標に使わない |
| SIMD8 だけの kernel の速さ | 3 倍の見込みの根拠はまだ無い。CPU の時間を先に測る（p011）。SIMD16 は Future Work |
| 試験の build の kernel の大きさ（16 MiB の上限） | 期待値は式で持ち、大きな state は heap |
| Venus の host の device の違い | Venus では正しさだけ |
| libglesv2 の変更が WS068 の保留の作業・libGL とぶつかる | WS068 の GL 3.3 以降は保留中。ws068-p035 の範囲を D4 で分ける。libGL への漏れは p009 で分ける |
| 3.1 を名乗ると他の ES3 の app が 3.1 の経路で stub に当たる | D1。egltest と既存の app の回帰で見る |
| 共有の 5330 の取り合い | `flock /tmp/i915-hw.lock`（WS075 と共有）の下でだけ使う |
| uncached の stateless の MOCS での A64 の atomic | p005 の ATOMIC-SSBO で確かめ、違えば compute の op の stateless の MOCS を write-back に（§2.3） |
| 非特権の batch からの GPGPU_DISPATCHDIM への LRM | p007 で確かめる（§2.5） |
| pbuffer の context の記録が release・再取得をまたぐか、garbage の回収 | p009 で確かめる（§3.3） |
| surfaceless が失敗すると Noct が画面を直接使う display の platform を試す | p009 で pbuffer だけでは画面を取らないことを確かめる（§3.4） |
| Noct の CMake の patch が `--fuzz=0` で当たらない | 実物の file から `diff -u` で作る（§4.1） |
| uncached の MOCS による compute の帯域 | 受け入れの見本は演算の多い整数の hash。帯域が足りなければ write-back の MOCS を性能の改善として試す（p011 の測定の後） |
| live のデモの中で GPU が固まる | p012 で回復の手順（reset、session の再起動、機械の再起動）を台本に書く。デモの前に同じ見本を繰り返し走らせる |
