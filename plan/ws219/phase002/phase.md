<!-- awesome-plan project=zedbsd record=ws219-p002 -->
# ws219-p002: 出典の記入、README・API-PROVENANCE の直し、名前の無い数に名前

Status: in-progress（2026-10-13 P2 実施。実装・build・host 試験は済み、QEMU の Venus の起動の回帰は T1 の結果待ち）
Parent: [WS219](../ws.md)

## 範囲（ユーザーの決定 2026-10-13、ws.md の「ユーザーの決定」）

1. 出典の記入（[p001](../phase001/phase.md) の (C) の表）。comment の中の Mesa の command の名前は引用として残し、出典を書き足す。
2. `userland/desktop/libvulkan/README.md:164` の古い文の直しと、`include/libc/vulkan/API-PROVENANCE.md:5-11` の出典を Khronos（Vulkan-Headers v1.3.269）に。
3. `include/uapi/gpu-op.h` に名前の無い数値 3 つ（1000384002・1000384005・reply の flag 1）の名前を置き、使う所を置き換える（値と動作は同じ）。
4. 対象外: zedBSD 自身の "venus" の名前の付け直し、capset の値への名前、license の表示（今のまま）。

## 変えた所

| file | 変更 |
| --- | --- |
| `include/uapi/gpu-op.h` | 頭の注記に出典（venus-protocol commit `ca19b6358d7cc491bc3e4de76f04c6700876a8fa`、virglrenderer 1.1.0 の header の git-ca1e9220、番号と reply の flag は `xmls/VK_EXT_command_serialization.xml`、178〜180 の引数と 2 つの record の型は `xmls/VK_MESA_venus_protocol.xml`）。`GPU_OP_FLAG_REPLY`（1U）・`GPU_OP_RECORD_IMPORT_RESOURCE`（1000384002U）・`GPU_OP_RECORD_QUEUE_TIMELINE`（1000384005U）を、それぞれの出典（bit 0 の `VK_COMMAND_GENERATE_REPLY_BIT_EXT`、extension 385 の offset 2・5）つきで追加 |
| `userland/desktop/libvulkan/wire.c` | 頭に wire の形の出典（`docs/VK_EXT_command_serialization.txt`、同 commit、code は zedBSD 自身）。`vulkan_command_begin` の `vulkan_write_u32(writer, 1)` → `GPU_OP_FLAG_REPLY` |
| `userland/desktop/libvulkan/memory.c:1139` | `1000384002U` → `GPU_OP_RECORD_IMPORT_RESOURCE` |
| `userland/desktop/libvulkan/device.c:555` | `1000384005U` → `GPU_OP_RECORD_QUEUE_TIMELINE` |
| `userland/desktop/libvulkan/context.c`（capset の要求の前） | 値だけを使うこと（capset の id 4、byte 0・4・152）と出典（virglrenderer 1.1.0 `src/venus_hw.h` の `struct virgl_renderer_capset_venus`） |
| `src/drivers/gpu/i915/render/transport.h`（頭） | MESA の command の名前と 178〜180 の引数の出典（`VK_MESA_venus_protocol.xml`、同 commit）。名前は引用として残した |
| `src/drivers/gpu/i915/render/memory.c:257` | `VkImportMemoryResourceInfoMESA` の名前と値の出典、`GPU_OP_RECORD_IMPORT_RESOURCE` との対応 |
| `src/drivers/gpu/venus/share.c:424` | `VkImportMemoryResourceInfoMESA` の出典 |
| `userland/tests/gpu-share/main.c:607` | 同上 |
| `include/libc/vulkan/API-PROVENANCE.md:5-11` | 入力を Khronos Vulkan-Headers `v1.3.269` の `include/vulkan/vulkan_core.h`（sha256 `8cb01233…8287`、GitHub の固定の URL）に。virglrenderer 1.1.0 の写しは byte で同じで Venus の宣言ではない、と注記 |
| `userland/desktop/libvulkan/README.md:164` | 「公開APIとprotocol IDの必要なライセンス表示は…」→ 公開 API の表示は `LICENSE-API`、command の番号と wire の形は引用した事実で引用元は gpu-op.h と wire.c の注記（第三者の license の表示は置いていない） |

触らなかった物（範囲外）: 同じ値に別の名前を付けている `I915_VK_IMPORT_MEMORY_RESOURCE`（i915 `render/memory.c:260`）、`VULKAN_MEMORY_IMPORT`・`VULKAN_TIMELINE_RECORD`（bcm2711）、`STRUCTURE_DEVICE_QUEUE_TIMELINE_INFO`（venus-frame）、`device.c:553` の `1000145003U`（Vulkan 1.1 の公開の `VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2`）。i915・bcm2711 の transport の他の comment（`transport.c:59,203,229,282`）は transport.h の頭の出典で足りるとした。`userland/tests/venus-frame/README.md` の「Protocol provenance」は既に版つきの引用があるので変えていない。

出典の値は p001 で取得した venus-protocol の XML（P2 の worktree の `build/tmp/ws219/src/`）で確かめた: `VK_EXT_command_serialization.xml:553-555` が 178〜180、`:574` が `VK_COMMAND_GENERATE_REPLY_BIT_EXT` の bitpos 0、`VK_MESA_venus_protocol.xml:250,258,261` が extension 385 と offset 2・5。

## 確かめ（2026-10-13、P2 の worktree `/home/awe/zedBSD-worktrees/p2`）

| コマンド | 結果 |
| --- | --- |
| `make -j8 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws219-p002 vmunix build/ws219-p002/dynamic/libvulkan.so` | rc 0、warning 0、kernel include check PASS、amd64 vmunix check PASS（i915 `render/memory.o`・venus `share.o` を含む） |
| `make … BUILD=build/ws219-p002 build/ws219-p002/bin/gpu-share-test` | rc 0、warning 0、check-dynamic-elf 通過 |
| `sh plan/ws083/tests/run-host-libvulkan-native.sh`（device.c・context.c の capset を含む） | PASS（plain、ASan/UBSan） |
| `sh plan/ws083/tests/run-host-libvulkan-status.sh`・`run-host-libvulkan-video.sh` | PASS |
| `sh plan/ws141/tests/vulkan-device-host-test.sh build/ws219-device-host`（wire.c・codec.c） | PASS |
| `sh plan/ws141/tests/vulkan-stream-host-test.sh build/ws219-stream-host`（wire.c と native の transport） | PASS |
| `sh plan/ws129/tests/license-inventory-test.sh` | PASS |
| `sh plan/ws083/tests/run-host-video-roundtrip.sh` | FAIL（`src/drivers/gpu/i915/compiler/spirv.c: No such file or directory`）。変更前の tree（`git stash` で戻して）でも同じ FAIL で、この Phase とは関係の無い既存の不具合（試験が消えた file を参照している）。Q1 に報告 |

未実施: `plan/tools/gpu-boundary/run-dedicated-host.sh`（`dedicated.c` は変えていない。script の trap が host の rm をするので流していない）。QEMU の Venus の起動（T1 への依頼、下）。実機（5330 の passthrough）は未実施。

## T1 への依頼

- 内容: QEMU の Venus（virtio-gpu の Venus の capset、host の virglrenderer）で、この branch の image が graphical に起動し、libvulkan を使う compositor が画面を出すこと。変更は値の同じ名前の置き換え（reply の flag 1、sType 1000384002・1000384005）と comment だけなので、Venus の起動の回帰の既存の手順（`plan/tools/boot-test.sh` で login prompt／desktop の PNG）を 1 回でよい。
- 見たい印: 起動して画面が出る（PNG）。libvulkan の `vkCreateDevice`・memory の import（gpu-share-test を流せるなら `vulkan_wsi_shared_image_import` が成功）。
- 判定: Q1。T1 の結果が出るまでこの Phase は cleared にしない。

## 残り・再開条件

- T1 の結果を Q1 が判定したら cleared。
- 削除の依頼（Q1）: p001 の比較の相手と結果 `build/tmp/ws219/`、p001 の scratchpad の道具 `ws219tools/`、この Phase の build の出力 `build/ws219-p002/`・`build/ws219-p002-*.log`・`build/ws219-device-host/`・`build/ws219-stream-host/`、ws083 の試験が作った `build/tmp/ws083-*`（いずれも P2 の worktree の中）。
