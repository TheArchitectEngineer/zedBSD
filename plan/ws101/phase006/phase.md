<!-- awesome-plan project=zedbsd record=ws101p006 -->

# ws101-p006: SLM・barrier・fence・SLM の atomic（と試験の build の分割）

Phase ID: `ws101-p006`
Parent: [WS101](../ws.md)
Status: cleared（2026-09-30。5330 の passthrough で vkcs 15/15 PASS、実行器の回帰 PASS、host の試験 PASS）
Phase disposition: normal
Queue: main の割り当て（2026-09-30、サブエージェント `wt/ws101`）

## 範囲（[design.md](../design.md) §1.2〜§1.7、§2.3 の IDD、§5.1 の p006 の step、§6 の p006）

Workgroup の変数（SLM）、`OpControlBarrier`・`OpMemoryBarrier`（gateway の barrier と fence）、SLM の atomic、atomic の順序の
semantics の fence、barrier の一様性の検査、IDD の SLM の大きさと barrier の field。vkcs の SHARED・REDUCE・ODD-BARRIER・
ATOMIC-SHARED・LOOP・REFUSE。main の判断（2026-09-30）で、最初に**試験の build の分割**（試験の kernel が
`AMD64_KERNEL_MAX_BYTES` の直前のため）。

## 1. 試験の build の分割（main の判断: 上限と kernel 全体の bss は変えず、試験の build を分ける）

| file | 内容 |
| --- | --- |
| `platform/amd64/vmunix.mk` | `I915_TEST_SET`（既定 `all`）。`compute` は runner・`tests/render/compute.c`（vkcs）・`firmware-override.c`（何も入れなければ何もしない）だけを link する。`all` は今までの一覧から vkcs を除いたもの（vkcs は compute の組で走らせる）。組が変わったら relink する内容の stamp（`$(BUILD)/.i915-test-set`、`ZEDBSD_GRAPHICS_CONFIG_STAMP` と同じ形）を試験の build の vmunix の前提に足した。試験でない build は stamp も一覧も変わらない |
| `src/drivers/gpu/i915/tests/execution/runner.c` | execution・render の場面と `drv_i915_ktest_run` を weak で参照する（display の場面と同じ）。組に無い場面は「not linked」と FAIL の行を出す（display の場面の文言を共通にした） |
| `plan/ws101/tests/hw/run-hw.sh` | vkcs を `I915_TEST_SET=compute`、ほかを既定で build し、組を lock の中の make にも渡す（渡さないと lock の中で別の組に relink される）。lock の待ちを含む時間の上限を 1 時間にした（20 分では WS075 が lock を持つ間に vkc が打ち切られた） |

大きさ（text + data + bss、上限 16 MiB = 16,777,216）: 既定の組（vkx）16,729,584（余り 47,632 bytes）、compute の組（vkcs）
15,365,616（余り約 1.4 MB）。p005 の時点は vkcs を含む既定で 16,770,544（余り 6,672）だった。

## 2. compiler と実行器

| file | 内容 |
| --- | --- |
| `compiler/ir.h`・`compiler.h` | IR `LOAD_SHARED`（predicate は src[1]）・`STORE_SHARED`（src[2]）・`BARRIER`（immediate = 前の fence）・`FENCE`（bit 0 global、1 SLM、2 acquire）、`I915_IR_LOCATION_SHARED`、`I915_IR_MAX_SHARED_BYTES`（16 KiB）、IR と binary の `shared_bytes`・`uses_barrier` |
| `compiler/spirv.c`（差し込みだけ）・`spirv-compute.inc` | Workgroup（storage class 4）の変数を宣言の順に自然な配置（scalar 4、vector 4n、配列は要素の大きさ、struct は順に）で SLM に置き、合計 16 KiB を越えれば断る。access chain（任意の数の動的な index を byte offset に畳む）、load・store（forwarding をしない、block の predicate 付き）、SLM の atomic（`location` = SHARED）、`OpAtomicLoad/Store` の SLM。memory semantics の順序の bit は断らずに atomic の前後に FENCE（storage class の bit で global・SLM、無ければ両方）。`OpControlBarrier` の Workgroup の scope は fence の後に BARRIER、狭い scope と `OpMemoryBarrier` は FENCE。**barrier の一様性**: OpReturn の後の barrier と、入口の predicate が ALWAYS でない loop の中の barrier を断る。barrier の拒否を外した |
| `compiler/compile.c`（差し込みだけ）・`compile-compute.inc` | SLM の read・write（untyped surface、BTI 254、SIMD8、mlen 1）、SLM の atomic（untyped atomic、BTI 254、SIMD8 の bit、応答の bit と rlen は同じ判断から）、fence（SFID 10、型 7、commit、header、SIMD1 NoMask、応答の register を待つ）、barrier（fence の後、group が 2 thread 以上なら payload を 0 にして dword 2 に r0.2 & 0x7f000000、gateway の barrier の message を NoMask で、sync.bar）。binary の `uses_barrier` は message を出すときだけ 1 |
| **WS075 の囲い（p023）** | `BARRIER` と `FENCE` を含む囲いは飛ばさない（全 thread が barrier に来なければ GPU が固まる）。`STORE_SHARED` は囲いの中で P の下の predicate が要り、囲いの後に garbage を受けてはならない（`STORE_STORAGE` と同じ扱い）。design §1.6a の方針どおり |
| `compiler/eu.c`・`eu.h`（追加だけ） | `drv_i915_eu_send_scalar()`（SIMD1 NoMask の send）、`drv_i915_eu_sync_function()`（sync.bar・sync.allwr） |
| `intel/eu-encoding-gen12.h` | gateway・fence・untyped の型・BTI 254・sync の function などの定数（Mesa 25.0.7 の brw_eu_defines.h・brw_eu.h・brw_eu_emit.c・brw_fs_nir.cpp・brw_generator.cpp、sha256 を記載） |
| `render/compute.c` | IDD の word 6 に SLM の大きさ（1 KiB からの 2 の冪の encoding、Mesa の intel_compute_slm.c、sha256 記載）と barrier enable（binary の `uses_barrier`）。p004 の XXX を解消 |

## 3. 試験

| file | 内容 |
| --- | --- |
| `src/drivers/gpu/i915/tests/render/compute-shaders/` | shared（8×8 の tile の転置、動的な index 2 つ）・reduce（128 の木の和、loop の中に barrier）・scan（64 の prefix sum、loop の 6 回に barrier 2 つずつ）・oddbar（5×3、2 thread の端数）・atomsh（SLM の atomic）、`refuse/`（big: 16 KiB 超、retbar: return の後の barrier、loopbar: 一部だけが入る loop の中の barrier）。regenerate.py に足した |
| `src/drivers/gpu/i915/tests/render/compute.c` | step SHARED・REDUCE（64 group）・ODD-BARRIER（10 group）・ATOMIC-SHARED（16 group）・LOOP（32 group）・REFUSE（3 つの module が vkCreateComputePipelines で作られないこと） |
| `plan/ws101/tests/host/compute-lower.c` | 解釈器を group の lockstep に変えた（各 invocation を次の BARRIER まで走らせ、全員が来たら先へ。一部が終わり一部が barrier で待てば FAIL）、group ごとの SLM（0xCDCDCDCD で埋める）、`LOAD/STORE_SHARED`・SLM の atomic・FENCE。5 つの shader の試験 |
| `plan/ws101/tests/host/compute-dump.c` | fence（SIMD1 NoMask・header・commit・rlen 1・BTI 0/254）、barrier（gateway 0x02000004 NoMask の直後に sync.bar）、SLM の atomic（mlen 1、SIMD8 の bit、BTI 254）の検査、binary の shared・barrier の表示 |
| `plan/ws101/tests/host/compute-batch-test.c` | reduce の pipeline: binary の shared 512・barrier 1、IDD の word 6 = 16 thread | SLM 1 | barrier。genxml で `SharedLocalMemorySize=1 BarrierEnable=1` |
| `plan/ws101/tests/host/run.sh` | vkcs の p006 の shader と refuse を src から compile して dump・disasm/asm・lower に通す。refuse の検査の終了状態を pipe で失っていた誤りを直した（p002 の refuse/barrier・shared は受け入れに変わったので削除） |

## 確認

| 確認 | 結果 |
| --- | --- |
| `plan/ws101/tests/host/run.sh` | PASS。p006 の 5 kernel が compile、scoreboard 健全、Mesa の brw_disasm が全命令を受け（`DC mfence, bti 254`、`gateway barrier msg`、`sync bar`、`untyped atomic op, Surface = 254, SIMD8`）brw_asm で同じ bytes、descriptor の検査（atomsh: 5 atomic・2 fence・2 barrier など）。refuse 8 つ（p002 の 5 と p006 の 3）。解釈器: shared（64 group の転置）・reduce（4 group、barrier 8 回）・scan（3 group）・oddbar（3 group）・atomsh（4 group）が C の計算と一致。batch: reduce の IDD を genxml で確認 |
| `plan/ws031/tests/run-vk-host-tests.sh` spirv・lower・compile・eu・resdispatch・pipe | PASS（compiler の graphics の経路） |
| `plan/ws075/tests/guard/run.sh` | PASS（WS075 の囲いと scoreboard の試験。読むだけで実行） |
| `plan/ws031/tests/run-vk-gentool-test.sh` | 未実施（gentool が無い: `plan/ws031/mesa-refs` の symlink の先に build が無く exit 127。encoder の検査は Mesa の brw_disasm/brw_asm の往復で代えた） |
| 実機（5330 の passthrough、`plan/ws101/tests/hw/run-hw.sh`、`build/ws101-p006/hw-*-r1`）: vkcs（compute の組） | verdict PASS（15 of 15）: p005 の 9 と SHARED・REDUCE・ODD-BARRIER・ATOMIC-SHARED・LOOP・REFUSE。REFUSE は executor の log に 3 つの理由（Workgroup variables larger than the shared memory / workgroup barrier after a return / workgroup barrier in a loop not every invocation enters） |
| 実機: 回帰 vkx・vke1・vke2（既定の組） | PASS（9/9、6/6、17/17） |
| 実機: vkc（既定の組） | 1 回目は run-hw.sh の 20 分の上限（lock の待ちを含む）で打ち切られた（GPU は使っていない、`hw-vkc-r1` は空のまま）。上限を 1 時間にして再実行し PASS（9/9） |

## 決めたこと

- barrier の拒否は design §1.2 の「leaky な return の後」より強く、**OpReturn の後の全ての barrier** を断る（GLSL ES 3.10 の規則と同じ。
  if 変換では return した channel の thread も後ろの命令を通るが、loop を早く抜ける場合を簡単に確実に防ぐため）。
- SLM の fence は group が 1 thread でも出す（design §1.7「最初は常に出して正しさを先に確かめる」）。gateway の message は 1 thread の
  group では出さない（Mesa と同じ）。
- acquire の `sync.allwr` は出さない: この encoder は send ごとに dst（fence は応答）を待つ sync.nop を置くので、fence の後に何も先に走らない。
- 分割の既定の組から vkcs を外した（既定の組は余り 47 KB。vkcs は compute の組で走らせる）。

## 未実施・残り

- 実機での変異の試験（barrier を抜いた kernel で SHARED などが FAIL すること）は未実施。host の解釈器では確かめた: shared.comp と
  reduce.comp から barrier を抜いた module は `shared: FAIL t[0][1] = 0xcdcdcdcd`・`reduce: FAIL group 0 sum ...` で落ちる（解釈器は
  invocation を順に走らせるので、barrier が無いと後の invocation の書く前の SLM を読む）。
- 素の実機（passthrough でない）での確認は未実施。
- p007（indirect・0 の group・LENGTH・MANY）。
