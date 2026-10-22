# WS283 の設計: bcm2711 の Vulkan の compute（V3D 4.2・7.1）

- 2026-10-23 P2（ws283-p001、第 1 版、design-reviewer の review 前）。実装はこの文書の判定の後。
- 出典の記号: **M** = Mesa 25.3.6（MIT、`06f9e28304d5`、作業の写しは `plan/ws141/temp/mesa/`、commit しない）の `src/broadcom/`。**Z** = zedBSD の今の tree（main `b86842b82`）。行の番号は M の 25.3.6 の物。
- license: M は MIT。register の値・bit の位置・packet と命令の形は M から取る（WS141・WS279 と同じ）。この文書の文は自分で書いた（写しは無い）。GPL の Linux の v3d は今回は読んでいない（CSD の register の offset は WS279 の design §2.5 で既に調べた値を使う）。

## 0. まとめ

- **前段（SPIR-V → IR）は compute を既に持つ**（Z `src/drivers/gpu/compiler/spirv-compute.inc`、i915・PowerVR・nvrtx・amdrdna が使う）。LocalInvocationId・WorkgroupId・NumWorkgroups・GlobalInvocationId、storage buffer の load・store・atomic、Workgroup の共有 memory、barrier・fence、loop は IR に出てくる。**前段は変えない**。
- **欠けているのは bcm2711 の後段（IR → QPU）と実行器（Vulkan の object → CSD の job）**。kernel の CSD の job（Z `v3d-job.c`・`v3d71-job.c` の `run_compute`）は 4.2・7.1 とも既にあり、7.1 は実機の self-test（V8 の store）で動いた。
- 後段の欠けは compute だけでなく一般の物が多い: **分岐（branch）が全く無い**（loop が通らない）、整数の乗算・比較・除算が無い、TMU の一般の読み書き（memory への load・store）が無い。scheduler と register の割り当ては 1 つの直線の block だけを扱う。これが一番大きな仕事（§1）。
- 実行器: queue の flag に COMPUTE、storage buffer の descriptor、compute pipeline、compute の bind point、`vkCmdDispatch` → CSD の 1 step（§2）。userland の `libvulkan` と wire の opcode（`GPU_OP_CREATE_COMPUTE_PIPELINES = 66`、`GPU_OP_CMD_DISPATCH = 110`、`_INDIRECT = 111`）と試験の `userland/tests/vkcompute` は既にある。
- 見込み: Phase p002〜p010 で 51〜77 LW（§4）。YOLOX-Nano 416×416 fp32 は Pi 5 で、最初の素直な kernel で 3〜8 fps、詰めて 10〜20 fps の見込み（§3.3、根拠の薄い所を明記）。

## 1. compiler に足りない物

### 1.1 今の形（Z）

共有の前段 → `shader-analyze.c`（検査・生存区間）→ `shader-lower.c`（4.2）/`shader71-lower.c`（7.1、board の source の一覧で選ぶ写し）→ `shader.c`（記録・register の割り当て）→ `shader-schedule.c`/`shader71-schedule.c` → `qpu.c`/`qpu71.c`（encoder）。stage は coordinate・vertex・fragment の 3 つ（`enum bcm2711_shader_stage`）、`shader.c` の preflight は GLCompute の entry と Workgroup の storage class を断る（`shader.c` 860・872 行）。

後段が扱う IR の op（`shader-analyze.c` の `instruction_shape`）に無い物と、compute で要る物:

| IR の op | 今 | compute で | 備考 |
| --- | --- | --- | --- |
| `LOAD_SYSTEM`（local id・index、group id） | 無い | 必須 | §1.2 |
| `LOAD_STORAGE`・`STORE_STORAGE`・`STORAGE_SIZE` | 無い | 必須 | TMU の一般の access、§1.3 |
| `LOAD_SHARED`・`STORE_SHARED` | 無い | reduction・conv の tile に要る | §1.4 |
| `BARRIER`・`FENCE` | 無い | 共有 memory と組で | §1.4 |
| `ATOMIC` | 無い | YOLOX・debayer は要らない、後回し（D4） | TMU の atomic の op（M `nir_to_vir.c` 73〜226 行） |
| `LOOP_BEGIN`・`LOOP_END` | 無い | 必須（channel・tap の loop） | **分岐が要る**、§1.5 |
| `SKIP_BEGIN`・`SKIP_END` | 何もしない（両側を実行） | あると速い | 分岐ができれば本物の skip にできる |
| `IMUL` | 無い | 必須（index × stride） | QPU は `MULTOP`＋`UMUL24` で 32 bit の下位（M `v3d_compiler.h` 1541 行 `vir_UMUL`）。7.1 にもある（M `qpu_pack.c` 755 行） |
| `ILT`・`IGE`・`ULT`・`UGE`・`IEQ`・`INE` | 無い | 必須（範囲の判定、loop の条件） | 減算・XOR の flag の push（N・C・Z）と条件付きの move |
| `UDIV`・`UMOD`・`IDIV`・`IREM` | 無い | 定数の除数だけで足りる（x・y の分解） | 定数は乗算と shift。一般の除数は float の逆数＋補正（M は NIR で lower） |
| `PACK_HALF`・`UNPACK_HALF` | 無い | fp16 の時（D3） | encoder に `HALF_LOW`・`HALF_HIGH`・`PACK_HALVES` は既にある |
| `KILL`・`SIN`・`COS`・`TEXTURE` | 無い | 要らない | 範囲の外 |
| `LOAD_PUSH`・`LOAD_UBO`（定数の offset） | ある | そのまま使う | uniform の流れに入る |

QPU の encoder（`qpu.h` の `enum bcm2711_qpu_operation`）に無い命令: **branch**（4.2・7.1 の形、M `qpu_pack.c` の branch の pack）、`MULTOP`・`UMUL24`、`BARRIERID`（SYNCB へ書く）、`TMUWT`、`TIDX`（`EIDX` は `ELEMENT_INDEX` としてある）。TMU の書き込みの口（waddr）は peripheral の番号で書けるので encoder の変更は要らない: TMUD 11、TMUA 12、TMUAU 13、SYNC 16、SYNCU 17、SYNCB 18（M `qpu/qpu_instr.h` 106〜115 行、4.2・7.1 で同じ番号）。

### 1.2 compute の入口（payload と組み込みの値）

M `nir_to_vir.c` 4783〜4832 行・3357・3725〜3766 行:

- payload: 4.2 は rf0（payload 0）と rf2（payload 1）、7.1 は **rf3** と rf2（WS279 design §3.4 と同じ）。program の最初に別の register へ移す。
- WorkgroupId: x = payload0 & 0xffff、y = payload0 >> 16、z = payload1 & 0xffff（各 16 bit、だから dispatch は各次元 65535 まで）。
- LocalInvocationIndex = payload1 >> (32 − b)、b = log2(next_pow2(max(wg_size, 64)))（≤ 8、だから workgroup は 256 invocation まで）。
- LocalInvocationId の x・y・z は index から作る（V3D は index しか渡さない）: index < 256 なので float の除算で正確に割れる（y = floor((i + 0.5) / sx) 等）、size が 2 の冪なら shift と and。
- NumWorkgroups: 前段は「system set の storage buffer の 3 語」として出す（Z `ir.h` の `DRV_GPU_IR_SYSTEM_SET`）。後段は定数の offset のこの load を **uniform に置き換える**（M の `QUNIFORM_NUM_WORK_GROUPS` と同じ、TMU を使わない）。
- 共有 memory の base: base = uniform（arena の address）＋ wg_in_mem × shared_bytes。wg_in_mem = (payload1 >> 16) & ((1 << (16 − b)) − 1)（size が 1×1×1 の時は mask しない）。
- uniform の流れの address は CFG6。push constant・storage の address と大きさ・NumWorkgroups・shared の base は全部この流れに入る（graphics と同じく kernel が dispatch ごとに作る）。

### 1.3 storage buffer（TMU の一般の access）

M `nir_to_vir.c` 43〜52 行（config）、354〜430 行（TMUD の書き込み）、252〜294 行（FIFO と flush）:

- load: address を TMUA に書く（既定の config = 32 bit・per-pixel・regular）か、config が要る時は TMUAU に書いて次の uniform に config（op と型）を置く。結果は `ldtmu` の signal で受ける（今の texture と同じ signal）。
- store: data を TMUD に 1〜4 回書いてから TMUA に address（連続の 1〜4 語を書く）。完了を待つのは `TMUWT`（barrier の前と program の終わりの前に要る時だけ）。GFXH-1625: 最後の命令に TMUWT を置かない（M `qpu_validate.c` 376〜379 行）。
- vector: config の型 VEC2・VEC3・VEC4（M 47〜49 行）で連続の 2〜4 語を 1 回で読む。IR は scalar なので、後段で「同じ base で offset が +0・+4・+8・+12 の LOAD_STORAGE」を 1 つの lookup にまとめる（p008、性能）。
- 述語（IR の `component` 1 と predicate の src）: TMUA の書き込みを条件付きにすると、条件の外の lane は access しない（M は execute の mask で `IFA` の条件を付ける）。load も store も同じ。
- FIFO: 出力の FIFO は thread あたり 16 / threads 語（4 thread で 4 語、M 258 行）。それを越える前に `ldtmu` で受ける。thread の切り替え（thrsw）で待ちを隠す（今の texture の sample と同じ仕組み）。
- 範囲: address = buffer の VA＋descriptor の offset＋IR の offset。IR の offset は shader が計算した値なので、**descriptor の範囲に clamp する**（load は範囲の外で 0、store は落とす。Vulkan の robustBufferAccess と同じ振る舞い、1 access あたり 2〜3 命令）。`STORAGE_SIZE` は descriptor の range を uniform で渡す。
- uniform の offset（全 lane が同じ address）の load は `ldunifa`（unifa に address を書いて、以後 1 命令 1 語、全 lane に配る）が速い（M `nir_to_vir.c` 3093〜3200 行、SSBO にも使う）。encoder の signal は既にある（`BCM2711_QPU_SIGNAL_UNIFORM_ADDRESS`）。後段に簡単な divergence の解析（定数・push・uniform・group id・uniform な loop の counter だけから作った値）を足して使う（p008）。**conv の重みはこれで読む**（lane＝出力の pixel、重みは全 lane で同じ）。

### 1.4 共有 memory と barrier

- V3D に on-chip の共有 memory は無い。共有の変数は DRAM の arena に置き、**TMU で読み書きする**（M は `cs_shared_offset` を base に storage と同じ access、587 行）。だから共有 memory は storage と同じ速さ（L1T・L2T の cache だけが効く）。conv の tile の再利用の効果は GPU の他の機種より小さい。
- arena の大きさ: M は shared_bytes × workgroup の数を確保する（`v3dv_cmd_buffer.c` 4363〜4370 行）。workgroup が多いと大きい（例 4 KiB × 10 000 = 40 MiB）。zedBSD は session ごとの arena を使い回し、上限を越える dispatch は workgroup の base の offset（CFG0〜2 の下位 16 bit、M `v3d_csd.h` WG_OFFSET）で複数の CSD job に分ける。
- barrier（M `nir_to_vir.c` 3410〜3424・3697〜3712 行）: 前の TMU の操作を flush（未受領の `ldtmu` を受け、store があれば `TMUWT`）→ `BARRIERID` で SYNCB に書く → **thrsw**（block は次の thread の切り替えで起きる。その時に texture・TMU の待ちが残っていてはいけない）。barrier のある shader は single segment にしない。
- supergroup（M `common/v3d_util.c` `v3d_csd_choose_workgroups_per_supergroup`）: wg_size が 16 の倍数でない時、最大 16 個の workgroup を 1 つの supergroup に詰めて lane を埋める。barrier がある時は supergroup の batch を QPU の thread の総数の半分までに抑える（4.2: 8 QPU × threads、7.1: 12 QPU × threads）。barrier は supergroup の単位で待つ（M 3418〜3421 行のコメント）。
- 7.1 は 12 QPU が 3 つの slice に分かれる。同じ supergroup の中で、ある slice の TMU が書いた共有 memory を別の slice が barrier の後に正しく読めるかは、M が何もしない（hardware に任せている）事しか分からない → **実機の reduction の試験で確かめる（R1）**。

### 1.5 分岐と loop（一番大きい）

IR の loop: `LOOP_BEGIN`〜`LOOP_END` を、`LOOP_END` の Boolean が真の channel が残る限り繰り返す。loop の変数は `MOVE` で loop の前と終わりに定義される（Z `ir.h` 先頭のコメント）。QPU には channel ごとの実行の mask が無い（fragment の MSF は compute で使わない）ので、M と同じく **execute の mask を register に持つ**:

1. loop の前: active = 全 lane 真。
2. loop の中: loop の変数への `MOVE` と、memory の store・load（述語）は active を条件にする（条件付きの書き込み）。他の値は SSA で loop の中だけで使われるので、抜けた lane の値は捨ててよい。
3. `LOOP_END`: active = active AND cond、flag を push、**どれかの lane が active なら先頭へ branch**（branch の条件 ANYA 等、M の branch の cond）。branch の後の 3 語は遅延の slot。
4. 入れ子は mask を重ねる。barrier を含む loop は前段が「全 invocation が入る loop」に限る。

付随して要る物:

- **uniform の流れの巻き戻し**: loop の本体が uniform を読むと、繰り返すたびに流れが進む。M は branch の命令の `ub`（uniform の branch）で流れの address を相対に戻す（branch が 1 つ uniform を消費する）。zedBSD も同じにする（loop の外へ定数を全部出す案は register が足りない）。
- **scheduler**: 今は 1 block だけ。branch と loop の先頭を block の境にし、block ごとに詰める。最初は遅延の slot を NOP で埋め、後で詰める。4.2 は thrsw で accumulator が消える決まりが block の境でも要る。7.1 は accumulator が無い。
- **生存区間と register の割り当て**: loop の外で定義され中で読まれる値、loop の変数は loop の終わりまで生かす（back edge）。今の割り当ては「最後の読み」で解放するので、loop の中の読みは loop の終わりに延ばす。
- **spill は無い**（今の compiler に無い、M は TMU に spill）。4 thread は 1 thread あたり rf 28 個、2 thread は 56 個の expression の register。足りない shader は 2 thread に落とし、それでも足りなければ断る。WS284 の kernel はこの範囲に収まる形で書く（R3）。
- **thread の数と CFG5**: M は 4 thread の時だけ THREADING の bit を立てる（`v3dv_cmd_buffer.c` 4357〜4358 行）。2 thread の compute の variant は bit を立てずに動く事を実機で確かめる。thrsw の無い program は SINGLE_SEG。

### 1.6 その他

- push constant（`LOAD_PUSH`）: 今の graphics と同じく uniform の流れ。compute の stage の bit を pipeline layout で受ける。
- specialization constant: 前段が処理する（`OpSpecConstant*` は既定値で畳む。今の graphics と同じ扱いかは p003 で確かめる。YOLOX の kernel は push constant で足りる）。
- 4.2 と 7.1 の違い（compute に効く物）: payload の register（§1.2）、small immediate の決まり（7.1 は 1 語に 1 つ）、accumulator の有無、CSD の register の offset と CFG7（§2.4）、CFG4 の「− 1」（< 7.1.6）、CFG5 の PROPAGATE_NANS（4.2 だけ）、QPU の数（8 と 12）。user の好み（WS279 p004「無理に共通化しないで rpi4のコピーでrpi5を作って、共通部分をまとめる」）に従い、compute の lowering は 4.2 の `shader-compute.c` を作り、違いのある所だけ 7.1 の写し `shader71-compute.c` にする。版に依らない部分（IR の解析、loop の構造、divergence の解析）は共有の file にする。

## 2. 実行器（Vulkan → CSD）

### 2.1 今の形（Z `src/drivers/gpu/bcm2711/vk/`）

kernel の中の Vulkan の実行器。wire の opcode を `vulkan-dispatch.c` の route の表で受け、command buffer を記録（`vulkan-record*.c`）、submit で descriptor を写して prepared の graph（`vulkan-prepared.c`）、worker が step ごとに native の pass を実行（`vulkan-native-job.c`、1 step = 1 native pass、session が順に 1 step ずつ回る、`render-worker.c` 1282〜1292 行）。V3D の job は 1 つずつ完了まで待つ（`bcm2711_v3d_job_run`）。

### 2.2 足す物

| 部分 | 中身 | file（予定） |
| --- | --- | --- |
| queue | 1 つの family に `VK_QUEUE_COMPUTE_BIT` を足す（graphics・compute・transfer、M v3dv と同じ 1 family） | `vulkan-query.c` 272 行 |
| 限界 | maxComputeWorkGroupCount 65535×3、Invocations 256、Size は前段の 128・128・64、SharedMemorySize 16384、storage buffer の数（per stage 12 程度）、maxStorageBufferRange（memory の budget 512 MiB 以下） | `vulkan-query.c` |
| descriptor | `STORAGE_BUFFER`（と `_DYNAMIC` は後）を pool・layout・update・prepared の写しに足す。中身は buffer の VA・offset・range | `vulkan-descriptor-*.c`・`vulkan-layout.c`・`vulkan-prepared.c` |
| pipeline | `GPU_OP_CREATE_COMPUTE_PIPELINES`: 作る時に compile し（`BCM2711_SHADER_COMPUTE` の stage）、code を GPU の memory に置く（graphics の shader の置き方 `native-shader.c`/`native71-shader.c` と同じ）。variant の情報: local size、threads、single seg、shared bytes、barrier の有無、uniform の一覧 | `vulkan-pipeline-*.c` |
| bind point | compute の pipeline・set・push を graphics と別に持つ（Vulkan の決まり） | `vulkan-record*.c`・`vulkan-draw.h` の写し |
| dispatch | `GPU_OP_CMD_DISPATCH` を記録 → prepared の event（descriptor の写し）→ 1 step: uniform の流れを作り、shared の arena を割り当て、CSD の構成（§2.4）を作って `BCM2711_V3D_JOB_COMPUTE` を走らせる | 新しい `vulkan-native-compute.c` |
| barrier | 実行器は job を 1 つずつ完了まで待ち、CSD の後に L2T を clean、次の job の前に cache を無効化する（Z `v3d-job.c` 502〜545 行）。だから compute と render・transfer の間の `vkCmdPipelineBarrier` は hardware の命令が要らない。記録の検査で COMPUTE_SHADER の stage と SHADER_READ・WRITE の access を受け付ける | `vulkan-barrier-validate.c` |
| indirect | `GPU_OP_CMD_DISPATCH_INDIRECT`: step の実行の時に CPU が indirect の buffer の 3 語を読む（前の job は完了済み、memory は coherent なので M の CPU job の仕組みは要らない）。p006 の後半で小さく足す | 同上 |

### 2.3 dispatch の step の中身

1. group の数のどれかが 0 なら何もしない（M 4400 行付近）。各次元 ≤ 65535。
2. uniform の流れ: compiler の uniform の一覧の順に、push の語・storage の address と range・NumWorkgroups・shared の base・TMU の config・loop の uniform の巻き戻しの値。
3. shared の arena: shared_bytes × workgroup の数（上限を越えれば workgroup の base で job を分ける、§1.4）。
4. CSD の構成（§2.4）→ `bcm2711_v3d_job_run`。CPU が読む memory の可視性は今の graphics と同じ（CSD の後の clean）。
5. 1 dispatch = 1 step。他の session（Keiland の compositor）は step の間に入れる。1 つの dispatch が長すぎると compositor を待たせるので、WS284 の kernel は 1 dispatch を数 ms に収める（大きい層は分ける）。hang の判定は CURRENT_CFG4（残りの batch）が減らない時（WS279 design §2.6）。

### 2.4 CSD の構成の値（M `common/v3d_csd.h`、`v3dv_cmd_buffer.c` 4273〜4380 行、WS279 design §2.5）

| 語 | 中身 |
| --- | --- |
| CFG0〜2 | group の数 << 16 \| group の base の offset（16 bit ずつ） |
| CFG3 | (wgs_per_sg & 0xf) << 8（0 は 16）\| (batches_per_sg − 1) << 12 \| (wg_size & 0xff) << 0。bit 26 OVERLAP_WITH_PREV、[25:20] max SG ID は使わない |
| CFG4 | batch の数。batches_per_sg = ceil(wgs_per_sg × wg_size / 16)、全体 = batches_per_sg × 完全な sg の数 ＋ ceil(余りの wg × wg_size / 16)。**4.2 と 7.1.6 より前は − 1 を書く** |
| CFG5 | code の address \| SINGLE_SEG(bit 1) \| THREADING(bit 0、4 thread) \| PROPAGATE_NANS(bit 2、**4.2 だけ**、7.x は reserved) |
| CFG6 | uniform の流れの address |
| CFG7 | 7.1 だけ、0 |

書く順: cache の無効化 → CFG1〜6 →（7.1）CFG7 → CFG0（ここで動く）。4.2 の queue は core＋0x904〜、7.1 は 0x930〜（Z の `run_compute` が既に正しく書く）。supergroup の選び方は M の関数の規則を独自に書き直す（wg_size が 16 の倍数なら 1、それ以外は空きの lane が最少になる数、barrier がある時の上限は §1.4）。

## 3. 試験の計画と性能の見込み

### 3.1 host（担当が流す、短い物）

| 試験 | 中身 | 元 |
| --- | --- | --- |
| H1 encoder | branch・MULTOP・UMUL24・BARRIERID・TMUWT・TIDX、TMUA・TMUAU・TMUD・SYNCB への書き込み、ldunifa の word を、4.2・7.1 とも M の packer・decoder と照合 | `plan/ws141/tests/qpu-format-host-test.c`・`plan/ws279/tests/qpu71-format-host-test.c` の形 |
| H2 compute の実行 | compute の SPIR-V（saxpy、reduction の共有 memory と barrier、3×3 conv、depthwise 3×3、SiLU、2 倍の upsample、concat、debayer の 2×2）を compile し、**lane の model** で走らせて C の参照と比べる（bit 一致、または fp の誤差の上限） | lane の model `qpu-machine.c`（4.2）・`qpu71-machine.c`（7.1）を広げる: 16 lane、workgroup の複数の thread、TMU の一般の memory（FIFO の上限の検査）、SYNCB の barrier、branch と遅延の slot、uniform の流れの巻き戻し、payload |
| H3 CSD の構成 | §2.4 の値（supergroup・batch・rev の規則）を表の場合で M の式と照合 | 新規 |
| H4 実行器 | descriptor・pipeline・record の検査の境界（storage の range、bind point の分離、group の数 0・65536） | 既存の vk の host の試験があればその形 |

lane の model は WS141・WS279 の試験の file。WS283 で広げる時の置き場所は Q1 の判断（D5）。

### 3.2 実機（Pi 5 D0、kei@10.0.30.5、T1 に依頼。Pi 4 は板がある時）

`vkquad-pi` と同じく、build した program を board に置いて走らせる（`plan/ws279/tests/vkquad-pi-build.sh` の形）。

| 試験 | 合格の条件 |
| --- | --- |
| R0 `vkcompute`（既存、fill、local size 32、128 group） | `PASS, 4096/4096 words` |
| R1 saxpy（1 M 要素、local size 64・256） | 全要素が CPU の参照と bit 一致 |
| R2 reduction（共有 memory、barrier、local size 256 と 48〈supergroup の経路〉、1 M float の和） | CPU の参照と誤差の上限の中。slice をまたぐ supergroup の可視性（R1 の risk）の確かめ |
| R3 3×3 conv（16→16 channel、52×52、重みは ldunifa、loop） | CPU の参照と相対誤差 1e-5 の中 |
| R4 compute と描画の混在（Keiland の desktop の上で R3 を 1000 回） | lockup・MMU の違反なし、desktop が止まらない |
| R5 計測 | 空の dispatch の 1 回の費用（step の往復）、1×1 conv の GFLOPS、TMU の load の帯域 |

### 3.3 YOLOX-Nano 416×416 の見込み（荒い）

- 計算量: YOLOX-Nano 416 は 1.08 GFLOP（YOLOX の公表値、乗算と加算を別に数えた値）、params 0.91 M。層は depthwise 3×3＋pointwise 1×1 が主。
- Pi 5 の peak: 12 QPU × 16 lane / 4 cycle（1 命令 4 cycle）× 2 ALU（add と mul、FMA は無い）× clock。clock は WS279 p002 の記録の 1.15 GHz で **110 GFLOPS**（960 MHz なら 92）。Pi 4 は 8 QPU × 500 MHz で 32 GFLOPS。
- 効率: FMA が無いので 1 MAC に fmul と fadd の 2 命令（2 つの ALU を同時に使えれば 1 語）。conv の内側の loop は重みが ldunifa（signal、ALU を使わない）、入力が TMU の load（address の計算の add が TMUA に直に書けば ALU 1 つ）。1 lane が 4〜8 の出力 channel を register に持てば入力 1 つで 4〜8 MAC。この形で 1×1 conv は peak の 30〜50%、depthwise は memory で律速（1 入力に 1 MAC）で 5〜15%。全体で 15〜30%（**推定、R5 で測る**）。
- memory: activation の総量は fp32 で 1 推論あたり読み書き 40〜60 MB 程度（最大の tensor は 208×208×16 の 2.8 MB）。TMU 経由の実効の帯域は不明（5〜10 GB/s と置く）→ 5〜12 ms。
- dispatch の費用: 層の数から 80〜150 dispatch。1 dispatch の往復（job の完了待ち・L2T の clean・step の切り替え）を 30〜80 µs と置く → 3〜12 ms。
- 合計: 1.08 GFLOP / (110 × 0.15〜0.30) GFLOPS ＝ 33〜65 ms ＋ memory と dispatch で、**詰めた後 50〜100 ms（10〜20 fps）**。最初の素直な kernel（vector の load も ldunifa も無し）は 3〜8 fps。Pi 4 はその 1/3〜1/4（1〜5 fps）。
- 比べると、Pi 5 の CPU（Cortex-A76 × 4、2.4 GHz、NEON の FMA）の peak は約 150 GFLOPS で GPU より高い。GPU の利点は CPU を camera・表示・NMS に空ける事（D2）。
- 速さの余地（p008 以降）: 320×320 の入力（0.69 GFLOP 相当、約 0.6 倍）、fp16 の保存（帯域が半分、計算は fp32、D3）、連続の CSD の間の L2T の clean を省く・CSD の queue に次を積む（CFG3 の OVERLAP）。

## 4. Phase の案

1 LW ≈ 実時間 15〜25 分（Master の較正）。host の試験は担当、実機は T1。

| Phase | 内容 | 依存 | 見込み |
| --- | --- | --- | --- |
| p001 | この設計 | — | （済み） |
| p002 | encoder: branch（4.2・7.1）、MULTOP・UMUL24、BARRIERID、TMUWT、TIDX。H1 | p001 | 3〜4 LW |
| p003 | compiler の直線の compute: `BCM2711_SHADER_COMPUTE` の stage、preflight、payload と `LOAD_SYSTEM`、NumWorkgroups の uniform、整数の乗算・比較・定数の除算、storage の load・store（TMU、述語、clamp、FIFO）、push。lane の model の memory と payload。H2 の saxpy・SiLU・concat・upsample | p002 | 8〜12 LW |
| p004 | 分岐と loop: execute の mask、`LOOP_*` の branch、uniform の巻き戻し、block ごとの scheduler、back edge の生存区間、`SKIP_*` を本物の skip に（任意）。lane の model の branch。H2 の conv | p003 | 8〜12 LW |
| p005 | 共有 memory と barrier: arena の base、`LOAD_SHARED`・`STORE_SHARED`、`BARRIER`・`FENCE`（flush、SYNCB、thrsw）、multi segment。lane の model の workgroup と barrier。H2 の reduction | p004 | 5〜7 LW |
| p006 | 実行器: queue の flag と限界、STORAGE_BUFFER の descriptor、compute pipeline、bind point、push、`vkCmdDispatch` → CSD の step（uniform の流れ、arena、supergroup、CFG）、barrier の受け付け、indirect。H3・H4 | p003（p004・p005 と並べられる） | 10〜14 LW |
| p007 | Pi 5 の実機の確認（T1）と直し: R0〜R5 | p005・p006 | 4〜8 LW（直しの量で変わる） |
| p008 | 速さ: vector の TMU の load・store、divergence の解析と ldunifa、fp16 の pack・unpack（D3）、dispatch の費用の削減、R5 の再計測 | p007 | 8〜12 LW |
| p009 | Pi 4（4.2）: host の H1〜H3 の 4.2 の版、実機は板がある時（D1） | p007 | 2〜4 LW |
| p010 | 規約の全文の見直しと license の監査（M の file ごとの license、類似の検出） | p002〜p009 | 3〜4 LW |

合計 51〜77 LW（実時間 13〜32 時間）。WS284 は p006 の後（storage buffer の compute が Pi 5 で動けば）に層の kernel を書き始められ、p008 と並べられる。WS285 の debayer は p003・p006 だけで書ける（loop も共有 memory も要らない形にできる）。

## 5. 決めてほしい事（ユーザー）

- **D1 Pi 4 の実機**: WS の完了の条件は「Pi 4 は host で確かめ、実機は板がある時」。p009 の実機を WS283 の完了から外してよいか（推し: 外す。4.2 の host の試験は完了の条件に残す）。
- **D2 速さの目標**: 推定は Pi 5 で 10〜20 fps（詰めた後、416×416 fp32）、最初は 3〜8 fps。CPU の NEON の方が速い可能性がある。WS283・WS284 の完了の条件を「GPU で動き、結果が参照と一致する」とし、fps の数は目標にしない（計測して報告）でよいか。それとも数値の目標（例 10 fps）を置くか（推し: 数値の目標は置かず、p008 で測って報告。足りなければ 320×320 を選べるようにする）。
- **D3 fp16**: まず fp32 で正しく動かし、fp16 は保存だけ（packHalf2x16 で buffer の帯域を半分）を p008 で試す、でよいか（推し: そう。V3D は fp16 の計算は速くならない）。
- **D4 範囲の外にする物**: atomic、storage image（imageLoad・imageStore）、subgroup の操作は WS283 の最初の範囲から外す。debayer の出力は storage buffer に書いて buffer → image の copy で表示に渡す（推し: 外す。WS285 の設計で要ると分かれば Phase を足す）。

Q1 の判断:

- **D5 lane の model の置き場所**: `plan/ws141/tests/qpu-machine.c` と `plan/ws279/tests/qpu71-machine.c` は他の WS の試験。広げる版を `plan/ws283/tests/` に写すか、共有の回帰の道具として `plan/tools/` へ移して Master の Tools に登録するか（推し: 移す。3 つの WS が使う）。

## 6. risk

- R1 7.1 の slice をまたぐ共有 memory の可視性（§1.4）。R2 で確かめる。駄目なら barrier の前に TMU の cache の操作を足すか、supergroup を 1 slice に収まる大きさに抑える。
- R2 dispatch の 1 回の費用（R5 で測る）。大きければ連続の CSD をまとめる（p008）。
- R3 spill が無い: register が足りない kernel は断られる。WS284 の kernel の形（出力 channel の register の block の大きさ）で調整する。
- R4 compiler の分岐の導入は graphics の shader にも効く（`SKIP_*` を本物にする時）。graphics の既存の試験（`shader-host-test`・`shader71-host-test`）を p004 で流す。
- R5 前段の制約: IR は scalar（vector の load は後段でまとめる）、fp16 の型・16 bit の storage は前段に無い（pack・unpack で代える）。
