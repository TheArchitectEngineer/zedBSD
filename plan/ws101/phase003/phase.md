<!-- awesome-plan project=zedbsd record=ws101p003 -->

# ws101-p003: 実行器の object と記録（compute pipeline・bind point・vkCmdDispatch）

Phase ID: `ws101-p003`
Parent: [WS101](../ws.md)
Status: cleared（2026-09-30。host の試験 PASS、kernel の build PASS。dispatch の batch は p004、GPU は p005）
Phase disposition: normal
Queue: main の割り当て（2026-09-30、サブエージェント `wt/ws101`）

## 範囲（[design.md](../design.md) §2.1、§6 の p003）

vkCreateComputePipelines（opcode 66）、compute の kernel を instruction window の全体に置けること、per-thread の ID の表、
bind point の分離（pipeline と descriptor set）、vkCmdDispatch（opcode 110）の記録、compute の module を graphics の stage に使う誤りを断る検査。

## 変えた file

| file | 内容 |
| --- | --- |
| `src/drivers/gpu/i915/render/gfx.h` | `struct i915_gfx_pipeline` に `bind_point`・`compute`・`cs_binary`・`thread_ids`・`threads`・`right_mask`。op `I915_GFX_OP_DISPATCH` と `u.dispatch`、descriptor の op に `bind_point`。draw の state に compute の pipeline・set・dynamic offset。`I915_GFX_MAX_GROUP_COUNT`（65535）、`drv_i915_gfx_compute_prepare()` |
| `src/drivers/gpu/i915/render/pipeline.c`・`pipeline.h` | `drv_i915_gfx_create_compute_pipelines()`（graphics と同じ枠。stage が COMPUTE でない・specialization constants・未知の module は作成の失敗、record は最後まで読む。公開の前の失敗は全 pipeline を release して free） |
| `src/drivers/gpu/i915/render/pipeline-prepare.c` | module の entry point の stage と使う stage の一致の検査（両方向）、`drv_i915_gfx_compute_prepare()`（compile、window の全体 48 KiB・push constants・cross-thread 1 KiB・group の上限の検査、thread の ID の表と right mask）、release が compute の binary と表も解放 |
| `src/drivers/gpu/i915/render/objects.c` | opcode 66 の routing |
| `src/drivers/gpu/i915/render/command.c` | vkCmdBindDescriptorSets の bind point を記録、vkCmdDispatch の記録、実行の bind point の分離（`i915_execute_bind_pipeline`・`i915_execute_bind_set`）、`i915_execute_dispatch`（compute の pipeline が無い・group の数の上限の超過は EINVAL、group 0 は何もしない、それ以外は XXX で ENOTSUP。batch は p004） |
| `src/drivers/gpu/i915/render/instance.c` | `maxComputeWorkGroupCount` を `I915_GFX_MAX_GROUP_COUNT` から |
| `plan/ws101/tests/host/executor-test.c`（新）・`run.sh` | wire を通した試験（ws031 の stub を読み取りで再利用、command.c を試験の TU に含める） |

## 確認

| コマンド | 結果 |
| --- | --- |
| `TMPDIR=… plan/ws101/tests/host/run.sh` | PASS（p002 の compiler の試験と p003 の実行器の試験）。add.comp の compute pipeline が bind point COMPUTE・8 thread・right mask 0xff で公開、vertex の module・specialization・未知の module は作成の失敗で何も公開しない、session を閉じると何も残らない。(4,2,3) の group の表（3 thread の全 channel の x・y・z・index）。compute の module を graphics の両 stage に使うと作成の失敗、vkdemo の pipeline は作れて bind point GRAPHICS。記録: compute の pipeline・compute の bind point の set・4×2×1 の dispatch の op。bind point の分離（互いに上書きしない）。dispatch: pipeline 無しと 65536 group は EINVAL、group 0 は 0、他は ENOTSUP、command buffer の実行は dispatch で ENOTSUP で止まる |
| 変異の試験（stage の検査を一時的に外す） | 試験が「compute の module の graphics pipeline が作れてしまう」で FAIL（検査が効いている）。元に戻して PASS |
| `sh plan/ws031/tests/run-vk-host-tests.sh <each>` | resdispatch・pipe PASS。cmd（link の失敗）・res・sync・cmdbuf は FAIL だが main の checkout の同じ試験と同じ所で落ちる既存の不足（WS075 の p024 が直す）。cmdbuf は早い段で止まるので、その後の段は両方とも走っていない |
| `make ZEDBSD_CONFIG=/home/awe/zedBSD-rpi4/config.mk BUILD=build/ws101-p002 -j16 vmunix` | PASS（warning 0、vmunix check） |
| `git diff --check` | OK |

## 決めたこと

- dispatch の実行（batch）は p004 で `render/compute.c` に書く。command.c から compute.c を呼ぶと、ws031 の host の試験（command.c を link し
  draw.c の関数を `i915-vk-render-stubs.inc` の stub で置き換える）の link が壊れる。p003 では compute.c を作らず、dispatch は検査の後に
  ENOTSUP で断る。**p004 の前に main に依頼**: `plan/ws031/tests/i915-vk-render-stubs.inc` に `drv_i915_gfx_dispatch()` の stub を足し
  （または run-vk-host-tests.sh の実行器の一覧に compute.c を足す）ことの許可か、main による適用。
- 失敗した graphics pipeline の作成は pipeline を release も free もしない（graphics の既存の XXX）。p003 の試験は各試験の開始の印からの
  leak を数える。

## 未実施

- GPU での実行（p004 の batch、p005 の実機）。
- boot test（実行器は compute の pipeline を作れるようになったが、dispatch は ENOTSUP で断るので、graphics の経路は bind point の既定
  （GRAPHICS）で今までどおり）。
