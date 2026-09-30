<!-- awesome-plan project=zedbsd record=ws101p007 -->

# ws101-p007: vkCmdDispatchIndirect、0 の group、vkcs の INDIRECT・LENGTH・MANY、G1 の passthrough の受け入れ

Phase ID: `ws101-p007`
Parent: [WS101](../ws.md)
Status: cleared（2026-09-30。5330 の passthrough で vkcs 21/21 PASS（新しい 6 step を含む）、実行器の回帰（vkx・vke1・vke2・vkc）PASS、glescompute の indirect も i915 で PASS）
Phase disposition: normal
Queue: main の割り当て（2026-09-30、サブエージェント `wt/ws101`。終わったら `--no-indirect` を外した glescompute を i915 で走らせる、main の指示）

## 範囲（[design.md](../design.md) §2.5・§5.1、§6 の p007）

i915 の実行器に `vkCmdDispatchIndirect`（wire の opcode 111）を足し、0 の group の indirect の dispatch、`.length()`、N = 4,000,000 の
dispatch を実機の試験の場面 `vkcs` に足し、G1（i915 の Vulkan の compute）の passthrough での受け入れの run をする。

## 変えた file

| file | 内容 |
| --- | --- |
| `src/drivers/gpu/i915/render/command.c` | opcode 111 の decode（`I915_GFX_OP_DISPATCH_INDIRECT`: buffer と offset）と実行 `i915_execute_dispatch_indirect`（compute の pipeline、bind された buffer、4 の倍数の offset、3 つの数が buffer に収まること、を検査して EINVAL。数の値は GPU が実行のときに読むので検査しない（Vulkan では 0 は何もせず、上限超えは未定義））。pipeline の検査は `i915_dispatch_ready` に共通化 |
| `src/drivers/gpu/i915/render/gfx.h` | op の種類と union の `dispatch_indirect` |
| `src/drivers/gpu/i915/render/compute.h`・`compute.c` | `struct i915_gfx_grid`（3 つの数、または indirect の数の GPU の address）。`drv_i915_gfx_dispatch`・`_write`・`_build` は grid を取る。indirect では slot の group 数を書かず、system の storage buffer（`gl_NumWorkGroups`）は indirect buffer の 3 word を指す。batch は IDL の後に `MI_LOAD_REGISTER_MEM` × 3（GPGPU_DISPATCHDIMX/Y/Z = 0x2500/0x2504/0x2508、PPGTT の address）、walker は Indirect Parameter Enable で自分の数は 0（anv の `compute_load_indirect_params` と同じ） |
| `src/drivers/gpu/i915/intel/genxml.h` | GPGPU_DISPATCHDIM の register と MI_LOAD_REGISTER_MEM の長さ |
| `src/drivers/gpu/i915/tests/render/compute.c`（vkcs） | 6 step: INDIRECT（G の byte 16 の 16×1×1 で ADD）、INDIRECT-ID（ID の 3×2×2 を G から、`gl_NumWorkGroups` も）、INDIRECT-GPU（同じ command buffer で GRID が数を GPU で書き、次の indirect の ADD がそれを読む。G は 0 で始まる）、INDIRECT-ZERO（x・y・z が 0 の 3 つの indirect、何も書かず、GPU は続く）、LENGTH（`.length()`: 400 byte の範囲の uint が 100、8 byte の head の後の 12 byte の struct が 457 byte の範囲で 37）、MANY（N = 4,000,000、62,500 group、16 MB の buffer 3 つを別々の GEM の object に置き、全 word を照合、submit の時間を log）。buffer の usage に INDIRECT |
| `tests/render/compute-shaders/grid.comp`・`length.comp`・`regenerate.py`、`tests/fixtures/compute-shaders-gen.inc` | 新しい 2 つの shader（fixture は追加だけ、既存の shader の word は変わらない） |
| `plan/ws101/tests/host/compute-batch-test.c`・`executor-test.c`・`run.sh` | grid の API へ。indirect の slot（system buffer が indirect の address）と batch（Mesa の genxml で MI_LOAD_REGISTER_MEM の RegisterAddress 0x2500・0x2508、MemoryAddress、UseGlobalGTT=0、walker の IndirectParameterEnable=1 と数 0、命令の並び）、実行器の indirect の拒否と address |
| `plan/ws031/tests/i915-vk-render-stubs.inc` | dispatch の stand-in を grid の API に（WS101 の節。stub への追加は main の許可の範囲） |
| `plan/ws101/tests/hw/gles/run-gles.sh` | glescompute の `--no-indirect` を外した |

## 0 の group の扱い（design §2.5 の未確認）

Mesa の i965 は Gen7 だけ MI_PREDICATE で 0 の数の walker を飛ばし（`prepare_indirect_gpgpu_walker` の `if (devinfo->gen > 7) return;`）、
anv・iris は Gen8 以後で何もしない。WS101 も predicate を使わない。実機で INDIRECT-ZERO（x・y・z のそれぞれが 0 の 3 つの dispatch）が
何も書かず、GPU が止まらず、後の step（LENGTH・MANY）と後の回帰が通ることを確かめた。

## 確認

| 確認 | 結果 |
| --- | --- |
| `plan/ws101/tests/host/run.sh`（compiler・実行器・batch の host の試験、Mesa の genxml の decode） | PASS（dispatch-indirect: 29 命令 139 dword、4 つの期待が成り立つ） |
| `plan/ws031/tests/run-vk-host-tests.sh`（WS031 の実行器の host の試験。stub を変えたため） | PASS |
| glescompute の新しい shader（grid・length）の i915 の host の compile | 54 命令ずつ |
| vkcs の image の build（`I915_TEST_SET=compute`、`-Werror`） | PASS |
| style-check（変えた C の file。HEAD との差分で新しい違反） | 実機の run の後に直して 0（`compute-batch-test.c` の新しい関数は同じ file の既存の関数の形のまま）。直した後に host の試験と両方の試験の組（compute・all）の build を再確認（下） |
| `OUT=build/ws101-p007-hw plan/ws101/tests/hw/run-hw.sh r1 vkcs vkx vke1 vke2 vkc`（**5330 の QEMU passthrough**） | vkcs **21/21 PASS**（ONE〜REFUSE の 15 と、INDIRECT・INDIRECT-ID・INDIRECT-GPU・INDIRECT-ZERO・LENGTH・MANY）。MANY は N = 4,000,000 の submit が 6000 us（`clock_realtime` の分解能は未確認なので参考）。回帰: vkx 9/9、vke1 6/6、vke2 17/17、vkc 9/9 PASS。lock の待ちの大半は WS075 の H4 の run |
| style の修正の後の再確認: `plan/ws101/tests/host/run.sh`、vkcs（`I915_TEST_SET=compute`）と vkx（`all`、試験の kernel の大きさの上限の検査を含む）の image の build | 全て PASS（実機の run は修正の前の code。修正は comment と段落の分け方と、検査の値を変数に置くだけ） |
| `plan/ws101/tests/hw/gles-hw.sh build/ws101-p007-gles-hw`（5330 の passthrough、indirect を含む glescompute） | **PASS**: auto と default の両方で glescompute の indirect の step を含む全 step PASS、egltest の feedback・queries PASS。lock は 08:16:14〜08:22:26 |

判定は vkcs の verdict の行（`plan/ws075/tests/test-hw.sh` が写す run.log、既存の i915 の試験の方法）と guest の disk の program の log による。

## 未実施・制限

- 素の 5330（p012）。
- LENGTH の fragment の stage（design §5.1 は compute と fragment）。GLES の経路は SSBO を compute だけに許すので、compute だけにした。
- MANY の時間は参考（判定は一致だけ）。CPU との比較は p011 の G3。

## 残り

- なし（p011 は D2 の許可待ち）。
