<!-- awesome-plan project=zedbsd record=ws101p002 -->

# ws101-p002: compiler の核（GLCompute・built-in・SSBO の atomic）

Phase ID: `ws101-p002`
Parent: [WS101](../ws.md)
Status: cleared（2026-09-30。host の試験 PASS、kernel の build（-Werror）PASS。GPU での実行は未実施で p005）
Phase disposition: normal
Queue: main の割り当て（2026-09-30、サブエージェント `wt/ws101`）

## 範囲（[design.md](../design.md) §1、§6 の p002）

GLCompute、LocalSize・LocalSizeId、built-in（`LOAD_SYSTEM`、GlobalInvocationID の展開、NumWorkgroups）、compute の register の約束と
binary の field、thread spawner への EOT、`LOAD_STORAGE` の predicate、効果なしの decoration、`OpArrayLength`、stage の分岐の見直し、
SSBO の A64 の atomic、storage の access chain の複数の動的な index、拒否。

## 変えた file

| file | 内容 |
| --- | --- |
| `src/drivers/gpu/i915/compiler/ir.h` | `I915_STAGE_COMPUTE`、`I915_IR_LOAD_SYSTEM`・`I915_IR_ATOMIC`・`I915_IR_STORAGE_SIZE`、`I915_IR_SYSTEM_*`・`I915_IR_ATOMIC_*`・`I915_IR_SYSTEM_SET`、`LOAD_STORAGE` の predicate、`struct i915_shader_ir` の `local_size[3]` |
| `src/drivers/gpu/i915/compiler/compiler.h` | binary の `local_size[3]`・`cross_thread_regs`・`per_thread_regs`、`I915_SHADER_PER_THREAD_REGS`・`I915_SHADER_MAX_GROUP_INVOCATIONS` |
| `src/drivers/gpu/i915/compiler/spirv-compute.inc`（新） | execution mode、workgroup の大きさの検査、built-in の変数と load、system の storage buffer、SSBO の atomic（AtomicLoad・Store を含む）、`OpArrayLength`、compute の拒否 |
| `src/drivers/gpu/i915/compiler/spirv.c` | 定数、parser の field、宣言・entry point・decoration（Coherent・Volatile・Restrict・Aliased を効果なしに、member の decoration も同じ関数で）、変数、load・access chain・命令の switch のつなぎ、SSBO の複数の動的な index、compute の load の predicate、末尾の include |
| `src/drivers/gpu/i915/compiler/compile-compute.inc`（新） | compute の interface の検査、`LOAD_SYSTEM`（per-thread の register と r0.1・r0.6・r0.7）、`STORAGE_SIZE`、A64 の atomic（応答は liveness で）、thread の終わり、binary の記述 |
| `src/drivers/gpu/i915/compiler/compile.c` | `COMPILE_CS_PUSH_GRF`、operand の数、命令の switch、payload の位置、interface・prologue・terminate・describe の compute の分岐、`LOAD_STORAGE` の predicate、WS075 の skip の検査に `ATOMIC`、末尾の include |
| `src/drivers/gpu/i915/compiler/eu.c`・`eu.h` | `drv_i915_eu_send_all_end()`（NoMask の EOT の send） |
| `src/drivers/gpu/i915/intel/eu-encoding-gen12.h` | thread spawner の SFID、DC1、A64 atomic の message の型、応答の bit、EOT の descriptor（Mesa 25.0.7 の出典と sha256） |
| `plan/ws101/tests/host/`（新） | `run.sh`、`compute-dump.c`、`compute-lower.c`、`shaders/*.comp`（6）と `shaders/refuse/*.comp`（7） |

設計からの変更（[design.md](../design.md) §1.6a に記録）: NumWorkgroups は独立の system の register ではなく「system の storage buffer」
（set `I915_IR_SYSTEM_SET`）として block の並びに入れた。`LOAD_STORAGE` の predicate は src[1]、compute だけ。`ATOMIC` の応答は IR の flag でなく
liveness で決める。

## 確認

| コマンド | 結果 |
| --- | --- |
| `TMPDIR=… plan/ws101/tests/host/run.sh`（glslc 2025、Mesa 25.0.7 の `brw_disasm`・`brw_asm`、`/home/awe/p014-c/mesa/build-asm`） | PASS。6 module が compile、scoreboard が健全、atomic の応答の bit と rlen が一致、A64 atomic は address 2 register、全 kernel が thread spawner への EOT で終わる、Mesa の disassembler が全命令を受け、組み直しが同じ bytes。拒否の 7 件（shared、barrier、image、spec constant、軸 256、総数 256、SubgroupSize）が断られる。IR の interpreter で add（1024 invocation のうち 1000、範囲外の 24 は読まず書かず）、ids（288 invocation の 13 word）、atomic（12 種、200/256、返る値が重ならない）、length（10 と 5）、dynamic（2 つの動的な index）、noct（Noct の kernel の形、300 lane と結果 word の和）が C の独立の計算と一致 |
| 変異の試験（scratchpad の複写で compute の load の predicate を外す） | add・noct が「load past the end」で FAIL（試験が predicate の誤りを捕える） |
| `sh plan/ws031/tests/run-vk-host-tests.sh "spirv lower eu compile"` | PASS（graphics の回帰。EU model、scoreboard 4271 kernel、ws075-p023 の囲い） |
| `sh plan/ws075/tests/guard/run.sh` | PASS |
| `make ZEDBSD_CONFIG=/home/awe/zedBSD-rpi4/config.mk BUILD=build/ws101-p002 -j16 vmunix`（worktree） | PASS（-Werror、kernel include check、amd64 vmunix check） |
| `git diff --check` | OK |
| 目視（`brw_disasm`） | add: WorkgroupID は `g0.1<0,1,0>`、LocalInvocationID.x は per-thread の `g5`、SSBO の load は `(+f0.0) send`、終わりは `mov(8) g127 g0 {WE_all}` と `send … thread_spawner … EOT`。atomic: compare-exchange は data 2 register（compare → value）、応答の無いものは rlen 0 で control の bit 5 も 0 |

## 未実施

- GPU での実行（実行器は p003・p004、実機は p005）。
- EU の水準の model（`plan/ws031/tests/i915-vk-compile-test.c` の EU model）を compute へ広げる試験。自分の理解との一致しか確かめられないので、
  p002 では行わなかった。実機の bring-up（p005）で固まる危険を減らしたいときは p004 で足す。
- shader-survey（`plan/ws075/tests/shader-survey/run.sh`）は ws068 の host の build を前提とするので走らせていない。graphics の生成は
  compute の stage でだけ分岐するので変わらない（ws031 の compile・lower の試験と guard で確認）。
- boot test（kernel の意味の変更は compute の stage の shader だけで、実行器はまだ compute を渡さない）。

## 残課題と次

- p003（実行器の object と記録）で、pipeline の作成が compute の module を graphics の stage に使う誤りを断る検査（`ir->stage` の一致）を入れる。
- Master の Tools 節への `plan/ws101/tests/host/run.sh` の登録は main に依頼する（WS101 の範囲の外の file）。
