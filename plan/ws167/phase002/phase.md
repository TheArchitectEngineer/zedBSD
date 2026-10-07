<!-- awesome-plan project=zedbsd record=ws167-p002 -->
# ws167-p002: GPU の command の protocol の header と置き換え

Phase ID: `ws167-p002`
Parent: [WS167](../ws.md)
Status: cleared（2026-10-07 Q1 の判定: T1-313 で boot-test・zdesktop-p054 PASS、vkdemo は compositor 無しで frame=23・DONE と offscreen の readback。compositor が動く中の vkdemo の direct display の失敗は display の排他で、この変更とは無関係）（旧: test-wait（q833、P1、2026-10-07: 実装と host の確認、T1 待ち））
設計: [p001](../phase001/phase.md) §2・§5、H1〜H3（2026-10-05 ユーザー決定）

## 実装（2026-10-07、P1）

| 部分 | file | 中身 |
| --- | --- | --- |
| header（新、UAPI、H2） | `include/uapi/gpu-op.h` | 「Kei GPU command protocol」版 1。`enum gpu_op` の 145 個、名前は Vulkan の名前から `vk` を除いた大文字（H3、`GPU_OP_CREATE_INSTANCE`）、MESA の 3 つは働きの名前（`SET_REPLY_STREAM`・`SEEK_REPLY_STREAM`・`EXECUTE_STREAMS`）。各行の comment に Vulkan の名前。`GPU_OP_PROTOCOL_VERSION 1`、zedBSD だけの command の始まり `GPU_OP_OWN_FIRST 0x10000`。版 1 の番号は Venus の wire format 1 の再利用と明記、Google の表示は無し（H1） |
| libvulkan | `internal.h`（`#include <uapi/gpu-op.h>`）、14 の source と 2 つの生成の `.inc` | `VULKAN_OPCODE_vkX` の 155 か所を `GPU_OP_X` に |
| 生成の道具 | `tools/maintain-commands.noct`・`maintain-resources.noct`（`opName`）、`maintain-dispatch.noct` | 生成する名前を `GPU_OP_*` に。dispatch は virglrenderer の file を読まず、`opcodes.h` を作らない（引数は 2 つ） |
| i915 の実行器 | `src/drivers/gpu/i915/render/`（`internal.h`・`command.c`・`dispatch.c`・`fence.c`・`instance.c`） | 数字の `case NU:` の 61 か所と範囲・比較の 25 か所を `GPU_OP_*` に。comment を「Kei GPU command protocol」に |
| venus-frame | `userland/tests/venus-frame/client.c`・`venus-frame.c` | 自前の `enum venus_command`（9 個・17 個）を消して header を使う |
| license の記録 | `tools/release/license-components.json`、`include/libc/vulkan/API-PROVENANCE.md`、`userland/desktop/libvulkan/Makefile` | Venus の項を消し、provenance に「command の番号」の節、`LICENSE-PROTOCOL` の install を外す |
| 回帰の道具 | `plan/ws075/tests/vk-calls.py`（master の Tools） | 番号を `gpu-op.h` から読み、実行器の `case GPU_OP_*` を解く。出力は変更の前と同じ（diff 無し） |

残り: `userland/desktop/libvulkan/opcodes.h` と `LICENSE-PROTOCOL` の file の削除（もう参照されない。削除は Q1 の pipeline に依頼）。

## 確認（host、2026-10-07）

| 確認 | 結果 |
| --- | --- |
| 番号の照合（git の `opcodes.h` と新しい header、名前の対応と値） | 145 対 145、不一致 0、値の集合も同じ（送る byte は変わらない） |
| `make -j16 disk-image`（libvulkan・vkdemo・vmunix の i915 の実行器） | exit 0、自前の warning 0 |
| `venus-frame` の 2 つの file（clang、`-Werror`） | warning 0 |
| `vk-calls.py`（変更の前と後） | 同じ出力 |
| `grep Google`（libvulkan・include/uapi・src/drivers/gpu の source） | 0（i915 display の機種名 Lillipup を除く。消す 2 file を除く） |

未実施: QEMU（T1: Venus の vkdemo と compositor の起動）、実機の i915（対象外）、規約の見直し（p003）。
