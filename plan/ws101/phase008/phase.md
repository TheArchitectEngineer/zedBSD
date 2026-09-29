<!-- awesome-plan project=zedbsd record=ws101p008 -->

# ws101-p008: GLSL ES 3.10 の compute（自前の GLSL compiler → SPIR-V）

Phase ID: `ws101-p008`
Parent: [WS101](../ws.md)
Status: cleared（2026-09-30。host の試験 PASS: spirv-val・i915 の host の compile と Mesa の disasm/asm・lavapipe での実行・拒否の試験。image の build は下の表）
Phase disposition: normal
Queue: main の割り当て（2026-09-30、サブエージェント `wt/ws101`。G3 への最短の経路として p007 より先に。libglesv2 の変更は main の許可）

## 範囲（[design.md](../design.md) §3.2、§5.2、§6 の p008）

`userland/desktop/libglesv2/glsl/`（WS068 の GLSL compiler）に GLSL ES 3.10 の compute を足す: 版、stage、built-in、SSBO（std430）、
shared、barrier と規則、atomic、`.length()`、SPIR-V 1.0 の出力、Noct の shader の形の fixture。libglesv2 の API（p009）は範囲外。

## 変えた file（`userland/desktop/libglesv2/glsl/`）

| file | 内容 |
| --- | --- |
| `glsl.h` | `GLSL_STAGE_COMPUTE`（3）、`GLSL_STAGES`（4。`glsl_program.code` と `words` を 4 つに）、`GLSL_STORAGE_FIRST_BINDING`（56）・`GLSL_STORAGE_BINDINGS`（8）、`struct glsl_storage_info`（名前・GL の binding・run-time array の前の bytes・その stride・readonly）、`glsl_program` に `local_size[3]`・`storages`、`glsl_link_compute()`、`glsl_compute_layout()` |
| `internal.h` | `GLSL_VERSION_ES310`、keyword（buffer・shared・coherent・volatile・restrict・readonly・writeonly）、storage（BUFFER・SHARED）、memory の qualifier の bit、`GLSL_VAR_BUFFER`・`BUFFER_MEMBER`・`SHARED`、compute の built-in、`GLSL_IN_ES310`（3.10 の版の mask は ES300 と ES310 の両方）、atomic と barrier の special、layout（std430・binding・local_size）、node・symbol・shader の field、std430 の関数 |
| `preprocess.c` | `#version 310 es` |
| `parse.c` | 3.10 の keyword（3.00 の予約語より前に並べる）、qualifier、`layout(std430, binding = n, local_size_x/y/z = n)` |
| `types.c` | 3.10 の版の mask、std430 の alignment・size・array の stride・行列の列の stride（vec3 は 16 境界、配列・struct は 16 に丸めない、mat2 の列は 8） |
| `builtins.c` | stage の bit に compute（`BI_BOTH` は compute も含む）、`atomicAdd/Min/Max/And/Or/Xor/Exchange/CompSwap`（int・uint、compute）、`barrier`・`memoryBarrier`・`memoryBarrierBuffer`・`memoryBarrierShared`・`groupMemoryBarrier`、compute の built-in 変数（`gl_GlobalInvocationID`・`gl_LocalInvocationID`・`gl_WorkGroupID`・`gl_NumWorkGroups`・`gl_LocalInvocationIndex`）と const の `gl_WorkGroupSize`（layout が値を入れる） |
| `check.c` | buffer block（3.10・compute だけ、std430 だけ、binding < 8、配列の block と行優先を断る、run-time array は最後の member だけ、member の memory qualifier を断る）、`layout(local_size…) in;`（各軸 128・128・64、合計 128、二度目は同じ値だけ）、shared（compute だけ、初期化子を断る）、compute の in・out を断る、binding と memory の qualifier は buffer block だけ、**barrier() の規則**（main の中、選択・loop・switch の外、return の後でない。GLSL ES 3.10）、atomic の第 1 引数は buffer か shared の変数で書ける（readonly の buffer への書きと atomic を断る）、run-time array の `.length()` は定数でない int、compute の shader は local size が要る |
| `emit.h`・`emit.c` | GLCompute の entry と `LocalSize`、compute の built-in の decoration、std430 の型（`OpTypeRuntimeArray`、ArrayStride、Offset、列優先の MatrixStride、bool は uint）、buffer block は `BufferBlock` の Uniform 変数（set 0、binding 56 + n、memory の qualifier を member の NonWritable・NonReadable・Coherent・Volatile・Restrict に）、shared は Workgroup 変数、buffer の読み書き（葉、bool の変換、集合は部分ごと、**行列は列ごと**: i915 は memory の行列の load・store を断るため）、`OpArrayLength`（int へ bitcast）、atomic（buffer は Device の scope、shared は Workgroup、relaxed。`atomicCompSwap(mem, compare, data)` は Value = data、Comparator = compare）、`barrier()` は `OpControlBarrier(Workgroup, Workgroup, AcquireRelease|WorkgroupMemory)`（glslang と同じ）、memory barrier は `OpMemoryBarrier`。**loop の中に return を持つ compute の main** は一度だけ回る loop の中で走らせる（inline の関数の early return と同じ形。i915 は loop の中の OpReturn を断るため。design §3.2 の「未確認、p008 で確かめる」を確かめて直した） |
| `link.c`・`glsl.c` | `glsl_link_compute()`（既定の uniform block・uniform block は draw の program と同じ配置、buffer block は binding 56 + n、同じ binding の 2 つを断る、storage の情報と local size）、`glsl_compute_layout()`、program の解放に storage |

## 試験（`plan/ws101/tests/glsl/`、新）

| file | 内容 |
| --- | --- |
| `glsl-compute.c` | host の driver: `link`（compile・link・SPIR-V・local size と storage の表示）、`expect`（失敗と log の `// expect:` の照合） |
| `vk-compute.c` | host の Vulkan（lavapipe）の compute の実行器: 各 shader を compile・link し、binding 56 + n の storage と binding 0 の既定の uniform block を置いて dispatch し、C の計算と比べる |
| `pass/` | add（早期の return、uniform、instance 名の有無、readonly・writeonly）、ids（全 built-in と `gl_WorkGroupSize`）、atomic（int・uint の全 atomic、応答の有無）、shared（shared の tile の転置と barrier）、reduce（shared の木の和、barrier 6 つ、shared の atomic）、length（run-time array と 12 byte の struct の配列、sized の配列）、layout（std430 の offset: vec3、float、vec2 の配列、mat2、struct の配列、bool、ivec4、局所の struct の写し）、noct（`plan/ws101/tests/host/shaders/noct.comp`: Noct の `accel_shader_source.c` の GLES の形）、noct-ops（Noct の他の形: 結果の word の読み、算術・shift・bit 演算・6 つの比較・選択）、loopret（loop の中の return） |
| `fail/` | 17: barrier が if・loop・return の後・main 以外、shared の初期化子、局所の変数への atomic、readonly への書きと atomic、途中の unsized、group の大きさ、local size 無し、3.00 の buffer、binding の範囲、std140 の buffer、同じ binding、compute の in、member の memory qualifier |
| `run.sh` | 1. pass の link → spirv-val（vulkan1.0）→ i915 の host の compile（`plan/ws101/tests/host/compute-dump.c`）→ Mesa の brw_disasm/brw_asm の往復、2. fail、3. lavapipe の実行 |

## 確認

| コマンド | 結果 |
| --- | --- |
| `plan/ws101/tests/glsl/run.sh build/ws101-p008/glsl` | PASS。pass の 10 shader が link・spirv-val・i915 の compile（scoreboard 健全、descriptor の検査）・disasm/asm の往復を通る。fail の 17 が期待の文言で失敗。lavapipe で 10 shader が C の計算と一致 |
| `plan/ws068/tests/glsl-host/run.sh`（WS068 の GLSL compiler の回帰。読むだけで実行） | compile・link・blocks・i915・run（21 の実行の試験）は全て PASS。**`fail/version.vert` だけ FAIL**: この試験は「`#version 310 es` は未対応」を期待していたが、p008 で 310 es を受けるようにしたため。WS068 の file なので変えていない（main への依頼: 期待を `#version 320 es` と「GLSL version 320 is not supported」に変える） |
| image の build（compute の組、`build/ws101-p008/vkcs`、`VKLOOP_BUILD_ONLY=1`。libglesv2 と libGL（GLSL の source を組み込む）を target の compiler で build） | PASS。GLSL の 13 の source が build され、新しい warning 0（log の warning 2 つは make の jobserver と `userland/base/noct` の既存のもの） |

## 決めたこと・制限

- SSBO は compute の stage だけ（GLSL ES 3.10 は他の stage にも許すが、G3 の範囲の外。他の stage では compile の error）。
- 配列の配列は未対応（compiler の既存の制限。shared の 2 次元の tile は 1 次元の index で書く。GLSL ES 3.10 の機能としては欠け）。
- member の memory qualifier、buffer の行優先の行列、atomic counter、image load/store、`gl_MaxCompute*` の built-in の定数は未対応（error か未定義の名前）。
- `%` は既存の emitter のとおり `OpSRem`（C の意味。glslang は `OpSMod`。負の値の結果は GLSL では未定義）。Noct の CPU の意味と一致する。
- binding の対応（GL の n → Vulkan の set 0 の 56 + n）は design §3.3 のとおり、compiler が SPIR-V に直接書く（ES 3.1 には `glShaderStorageBlockBinding` が無い）。
- 実機・Venus・libglesv2 の API（`glDispatchCompute` など）は p009 以降。

## 未実施

- GPU での実行（p009・p010 の API の後）。i915 は host の compile と disasm までで、実機では走らせていない。
- boot test。
