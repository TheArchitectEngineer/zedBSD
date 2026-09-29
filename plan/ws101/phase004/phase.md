<!-- awesome-plan project=zedbsd record=ws101p004 -->

# ws101-p004: 実行器の batch（`render/compute.c`）

Phase ID: `ws101-p004`
Parent: [WS101](../ws.md)
Status: cleared（2026-09-30。host の試験 PASS（Mesa の genxml での decode を含む）、kernel の build PASS。GPU での実行は p005）
Phase disposition: normal
Queue: main の割り当て（2026-09-30、サブエージェント `wt/ws101`）

## 範囲（[design.md](../design.md) §2.2〜§2.4、§5.2 の batch、§6 の p004）

dispatch の slot（IDD・group の数・CURBE）と command（VFE・CURBE の load・IDD の load・walker・3D への戻り）、compute の scratch（768 thread 分）、
`transfer_pending`（dispatch と storage buffer を持つ draw）、HDC だけの PIPE_CONTROL、storage の register の range（draw と compute で共通）、
WS101 の genxml の decoder。

## 変えた file

| file | 内容 |
| --- | --- |
| `src/drivers/gpu/i915/render/compute.c`・`compute.h`（新） | `drv_i915_gfx_dispatch()`（session、uniform を読む kernel なら先に flush、scratch、instruction window の全体に kernel、op の begin・end、storage を持つ kernel なら `transfer_pending`）。`drv_i915_gfx_dispatch_write()`（slot を 0 にし、dynamic heap の +0x0 に IDD、+0x40 に group の数、+0x1000 に CURBE: compute の bind point の set で push data、system の storage buffer（`I915_IR_SYSTEM_SET`）に group の数の address と 12、その後に thread の ID の表）。`drv_i915_gfx_dispatch_build()`（context setup（3D）→ PC(CS stall・RT・depth、header に HDC) → PIPELINE_SELECT(GPGPU) → PC(CS stall・stall at scoreboard) → MEDIA_VFE_STATE → MEDIA_STATE_FLUSH → MEDIA_CURBE_LOAD → MEDIA_INTERFACE_DESCRIPTOR_LOAD → GPGPU_WALKER → MEDIA_STATE_FLUSH → PC(HDC・CS stall・DC flush・pipe control flush) → PIPELINE_SELECT(3D)） |
| `src/drivers/gpu/i915/render/command.c` | `i915_execute_dispatch()` が検査の後に `drv_i915_gfx_dispatch()` を呼ぶ（p003 の ENOTSUP を置き換え） |
| `src/drivers/gpu/i915/render/draw.c`・`draw.h` | scratch の compute の部分（pixel の後、page 境界、per-thread × `I915_GFX_CS_SCRATCH_IDS`）、`drv_i915_gfx_scratch()`（dispatch 用の公開の wrapper）、storage buffer を持つ draw が `transfer_pending` を立てる、`struct i915_gfx_kernels` の前方宣言 |
| `src/drivers/gpu/i915/render/state.c`・`state.h` | `drv_i915_gfx_write_push()`（公開の wrapper）、push data が system の block を飛ばす、storage の register の dword 2 に range（`VK_WHOLE_SIZE` と buffer の端で切る、4 GiB 以上は 0xFFFFFFFF。draw も compute も）、`struct i915_gfx_kernels` に `cs_scratch_bytes`・`cs_scratch_offset` |
| `src/drivers/gpu/i915/render/batch.c`・`batch.h` | `drv_i915_batch_pipe_control_hdc()`（header に HDC pipeline flush、RT flush 無し） |
| `src/drivers/gpu/i915/render/heap.h` | `I915_GFX_CS_SCRATCH_IDS`（16×8×6 = 768）、`I915_GFX_DYN_INTERFACE`・`_GROUP_COUNTS`・`_CURBE`、`I915_GFX_CURBE_BYTES` |
| `src/drivers/gpu/i915/intel/genxml.h` | GPGPU の packet の header・長さ・field の位置（Mesa 25.0.7 の gen120/gen110/gen80/gen60.xml と genX_pipeline.c・genX_cmd_compute.c、sha256 を記載） |
| `platform/amd64/vmunix.mk` | `render/compute.c` |
| `plan/ws031/tests/i915-vk-render-stubs.inc` | 末尾に `drv_i915_gfx_dispatch()` の stub（呼び出しの数と最後の group を記録して 0）。main の許可（2026-09-30）による追加だけ |
| `plan/ws101/tests/host/executor-test.c` | dispatch が stub に group を渡すこと、command buffer の実行が 0 で dispatch を 2 回目に 4×2×1 で呼ぶこと（ENOTSUP の期待を置き換え） |
| `plan/ws101/tests/host/compute-batch-test.c`（新） | compute.c を別名で TU に含め、add.spv・ids.spv の pipeline を wire で作り、手で作った set で slot を書いて word ごとに確かめ、batch と IDD を file に書く |
| `plan/ws101/tests/host/genxml-check.py`（新） | Mesa 25.0.7 の `intel_genxml.py` で gen120.xml を import の連鎖ごと読み、batch の全 dword を render engine の命令として decode（未知の header・field の外の bit は FAIL）、命令・field を Mesa の名前で照合（`exact` は列挙しない field が 0）、末尾の命令の順、GPGPU への切り替えの前後と 3D に戻る前の PIPE_CONTROL、IDD（`INTERFACE_DESCRIPTOR_DATA`） |
| `plan/ws101/tests/host/run.sh` | 6. として batch の試験と genxml の照合（`GENXML` で genxml の dir を変えられる） |

ws031 の `run-vk-host-tests.sh` の実行器の一覧に compute.c は足していない（stub の `drv_i915_gfx_dispatch()` と重複するため。stub だけで link が通る）。

## 確認（host。QEMU・実機は未実施）

| コマンド | 結果 |
| --- | --- |
| `plan/ws101/tests/host/run.sh` | PASS。p002（compile・disasm/asm・lower）、p003（実行器。dispatch は stub に 4×2×1 で渡り、group 0 は呼ばない）、p004: add の slot（IDD の word 2・5・6・7、group の数、push constant n、3 つの storage の address と range 1024、8 thread の ID）、`VK_WHOLE_SIZE` と端を越える range が buffer の端で切れる、ids の slot（system の storage buffer が slot の group の数を指し 12 bytes）。genxml: add・ids とも 26 命令 127 dword を decode、add の 13 項目・ids の 4 項目が一致（VFE の max threads 671/447・URB・CURBE の割り当て・scratch（ids: per-thread 2 = 4 KiB、base 0x20000）、CURBE の load の長さと 0x1000、IDD の load の 32 と 0、walker の thread 数・group の x/y/z（ids は 3×2×5）・right/bottom mask、MEDIA_STATE_FLUSH、末尾の 10 命令の順、3 つの PIPE_CONTROL の flag、IDD の preemption 無効・per-thread 4・thread 数・cross-thread の長さ、その他の field 0） |
| 変異の試験（compute.c を一時的に変えて `compute-batch-test`＋`genxml-check.py`） | walker の y と z の入れ替え → genxml が FAIL、VFE の URB entries の位置の誤り → field の外の bit で FAIL、walker の後の MEDIA_STATE_FLUSH の削除 → 順で FAIL、IDD の cross-thread の位置の誤り → C の検査で FAIL、scratch の pointer の shift の誤り → FAIL、walker に余分な dword → 未知の dword で FAIL。元に戻して（`cmp` で同一を確認）PASS |
| `sh plan/ws031/tests/run-vk-host-tests.sh <each>` | spirv・lower・resdispatch・eu・compile・pipe PASS。cmd（link の失敗）・res・sync・cmdbuf は FAIL だが、HEAD（p004 の前）の `git archive` の tree でも同じ assertion の行で落ちる既存の不足（WS075 の p024 が直す）。cmdbuf は `test_blend_state` で止まるので、その後の段（draw の push data を含むかもしれない）は両方とも走っていない |
| `make ZEDBSD_CONFIG=/home/awe/zedBSD-rpi4/config.mk BUILD=build/ws101-p002 -j16 vmunix` | PASS（warning 0、amd64 vmunix check PASS、`drv_i915_gfx_dispatch`・`_build` が vmunix に在る） |
| `git diff --check` | OK |

## 決めたこと

- dispatch は draw と同じく自己完結の op（op ごとに 3D の context setup、GPGPU への切り替えと 3D への戻り）。PIPE_CONTROL の flag は anv
  （Gen12.0 の `flush_pipeline_select()`）と TGL の PRM に合わせ、Generic Media State Clear は出さない（anv が hang のために出さない）。
- IDD の SLM の大きさと barrier は 0（XXX。compiler の binary がまだ SLM・barrier を持たない。p006 が binary の値を入れる）。design §6 の
  「IDD の全 field（SLM・barrier は binary の値）」のうち、この 2 つは p006 で満たす。
- MEDIA_VFE_STATE の Maximum Number of Dual-Subslices（dword 4）は 0（anv の Gen12.0 も設定しない）。
- 4 GiB 以上の range は 0xFFFFFFFF（dword 2 は 32 bit）。
- clang-format は PATH にも共有の `build/llvm/bin` にも無く、実行していない。

## 未実施・残り

- GPU での実行（p005 の vkcs の場面、5330 の passthrough）。batch が hardware で正しく動くことは未確認（host の decode だけ）。
- storage buffer を持つ draw の `transfer_pending` と draw の push data の range は host の試験に無い（draw.c は host の試験の外、cmdbuf の
  試験は既存の不足で途中で止まる）。build だけ。p005 の回帰（vkx・vke1・vke2・vkc）で見る。
- boot test は未実施（graphics の経路の変更は storage を持つ draw の flush の追加と range の word だけで、graphics の回帰は p005 で実機の
  vkx などで見る）。
