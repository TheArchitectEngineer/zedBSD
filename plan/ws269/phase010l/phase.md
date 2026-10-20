# ws269-p010l — WSI の shared の道と dc8200 の zero copy

- 親: [ws269](../ws.md)。設計: [design-p010.md](../design-p010.md) §7。
- Queue: q1390 / q1390-i01（P2、2026-10-20、前半）。状態: in-progress（host 試験・VF2 の build まで。実機（T1）は未実施。linear の SAMPLED の切り替えは off のまま）。

## 変えたもの

- `src/drivers/gpu/powervr/vk-memory.c`・`vk.h`: vkAllocateMemory の extension を `powervr_vk_memory_chain` で分ける。
  - `EXPORT_MEMORY_ALLOCATE_INFO`（handle type は opaque fd・dma-buf の `0x201` の中、0 は不可）は受ける。memory は `exported`。
  - `GPU_OP_RECORD_IMPORT_RESOURCE` は session の blob（`powervr_session_blob`）を identifier で引き、その buffer を retain して自分の resource で map する。memory は `imported`。知らない blob や小さすぎる blob は EINVAL。
  - それ以外の extension は ENOTSUP（FEATURE_NOT_PRESENT）。
  - export と import の storage は `powervr_resource_make(..., shareable=1)` で作る。これで `powervr_gpu_buffer_flags` が SLC_BYPASS を返す（p006 の R3）。
- `vk-image.c`: `EXTERNAL_MEMORY_IMAGE_CREATE_INFO` を `image_chain` で検める（next 0、handle type は `0x201` の中）。合えば普通の image として作り、`image->external` に記録する。別の extension・別の handle type は ENOTSUP。
- `vk-device.c`: `GET_PHYSICAL_DEVICE_PROPERTIES2`（ID の properties だけ）の identity を返す。中身は device UUID `zedBSD-powervrbx`、driver UUID `zedBSD-pvr-vk-01`、LUID なし、node mask 0、続けて普通の properties。properties の record は `device_record` に分けた。
- `image-state.h`・`.c`: `POWERVR_FORMAT_LINEAR_SAMPLED` を shared の道のただ 1 つの切り替えにした（`#ifndef` で host 試験から 1 にできる）。今は 0 で、linear の SAMPLED・SAMPLED_FILTER_LINEAR は出さない。開けるのは p010k2 の実機の後で、Q1 が決める。
- display（dc8200）の scanout の import は p006・p007 の物（`powervr_render_lookup`・`check_direct_scan`・`drv_powervr_dc_frame_fits`）をそのまま使う。export した memory の blob は memory の buffer を map するので、display が引く buffer は SLC を通らない。
- `userland/tests/vkimage/main.c`: external の image が作れること（VK_SUCCESS）を見る（前は FEATURE_NOT_PRESENT）。linear の sampled が出ないことは切り替えの off に合わせてそのまま見る。

## 確認

| 確認 | コマンド | 結果 |
| --- | --- | --- |
| executor の frame（切り替え 0） | `sh plan/ws269/tests/p009/run.sh build/p010l/p009-0 0` | 175 passed、0 failed |
| executor の frame（切り替え 1） | `sh plan/ws269/tests/p009/run.sh build/p010l/p009-1 1` | 175 passed、0 failed |
| p010 の host 試験 | `sh plan/ws269/tests/p010/run.sh build/p010l/p010` | 全部 0 failed |
| p006（SLC の flag） | `sh plan/ws269/tests/p006/run.sh build/p010l/p006` | PASS 95、FAIL 0 |
| VF2 の kernel | `make ZEDBSD_CONFIG=config/ci/config-visionfive2.mk BUILD=build/p010l/vf2 vmunix -j16` | rc 0、warning 0 |
| VF2 の selftest の image | `make ZEDBSD_CONFIG=plan/ws269/tests/p010/config-visionfive2-selftest.mk BUILD=build/p010l/img disk-image -j16` | rc 0、vkimage・powervr・libvulkan の warning 0 |

- 試験の中身（p009 の frame）:
  - external の image が作れる。export と import が作れる。import は blob 自身の buffer を map する。
  - export と import は shareable で、普通の allocation はそうでない。
  - 知らない blob の import は拒む。host allocation の handle type は FEATURE_NOT_PRESENT。
  - identity の reply の形（header・UUID・LUID・properties）。
  - 切り替え 0: linear の sampled が出ず、linear sampled の image の問い合わせは FORMAT_NOT_SUPPORTED。切り替え 1: linear の sampled と filter が出て、問い合わせが成功する。
  - `run.sh` の第 2 引数で切り替えを渡す。
- 試験の中身（p010 の image-host-test）: 640x480・800x600・1024x768・1280x720・1920x1080・3840x2160 の linear の BGRA8 の rowPitch が幅×4 になり、loader の pitch と同じで 32 の倍数（`dc8200.zerocopy=test` の条件）。1366 幅は 5472 に揃えられ、loader の 5464 と違うので copy の道になる。

## 未実施・残り

- 実機（T1）。切り替えが 0 なので、libvulkan は shared の道を試さない。T1 の依頼に足す文面: 「切り替えを 1 にした image で、boot に `dc8200.zerocopy=test` を付けて vkdemo を起動し、HDMI に画が出ることと、display の node が copy せずに plane を buffer に向けたことを写真で」（切り替えを開けると Q1 が決めた後）。
- `present_copy_shared` の clear・blit・copy を LINEAR の PBE で通すのは、render・transfer の側（P1 の q1388 の file）。今回は触っていない。
- `GET_PHYSICAL_DEVICE_IMAGE_FORMAT_PROPERTIES2`・`EXTERNAL_BUFFER_PROPERTIES` は route に無い（ENOTSUP のまま）。libvulkan が shared の道で尋ねるなら次の段で足す。
- 片付け（Q1 に依頼）: `build/p010l/`。

## q1393-i01（2026-10-20 P1）: LINEAR の dst の transfer（P1 の file の分）

- libvulkan の `present_copy_shared`（`wsi-swapchain.c`）が使うのは vkCmdClearColorImage（float の (0,0,0,1)、range は level 1・layer 1）、vkCmdBlitImage（NEAREST、z 0..1）、vkCmdCopyImage（同じ大きさ）。
- `vk-transfer.c` の直し: (a) range・subresource の layer の数に `VK_REMAINING_ARRAY_LAYERS` を受ける（1 layer の image なので同じ）。(b) vkCmdCopyImage の format の一致を texel の byte 数の一致に（copy は raw の texel、swapchain と native の B8G8R8A8・R8G8B8A8 の組を通す）。
- LINEAR の dst の扱いを読んで確かめた所: `image_surface` は `shape.linear`・`layout.row_pitch`・`layout.padded_width`（linear では行の texel 数）を PBE に渡し、clear・copy・blit とも P2 の `powervr_transfer_regs_build` の linear の stride になる。奇数の幅の linear は copy・clear は 1 行ずつ、blit は regs_build が断る（display の幅は偶数の見込み）。render-job の colour も render-pass の surface の linear をそのまま使う。
- 確かめ: p009 の run.sh の切り替え 0 と 1（`POWERVR_FORMAT_LINEAR_SAMPLED`）でどちらも 175 passed, 0 failed、p010 の host 試験 0 failed、VF2・riscv64-virt・amd64 の build warning 0。p009 の host 試験は transfer を stand-in にしているので、vk-transfer の request の組み立ては host では通していない（未実施）。実機は未確認。
