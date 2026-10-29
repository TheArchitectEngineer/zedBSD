<!-- awesome-plan project=zedbsd record=ws282-p008 -->

# ws282-p008 `CAM_MEMORY_IMPORT`（GPU の memory に取り込む）と `camvk`

- Parent: [ws282](../ws.md)、Queue: q1667 / q1667-i05（P6、実装だけ・試験なし、branch agent/p6-ws282d）
- Status: in-progress（実装済み・試験未実施。受け入れは Pi 5 + capture の実機で、未実施。前提の p007 の Pi 5 の確認も未実施）

## 実装した事

- GPU 側（D9: WS282 の担当が書き Q1 が merge）: `include/drivers/gpu/gpu-share.h` に `get_allocation_backing`（ops の最後の field）と `drv_gpu_shared_backing()`、`gpu-scanout.h` に `DRV_GPU_BACKING_UNCACHED 8U`、`src/drivers/gpu/gpu.c` に `drv_gpu_shared_backing()`（GPU の capability の handle だけ、allocation だけの共有だけ、page の列の検査）、bcm2711 の `share.c` の `bcm2711_shared_allocation_backing()`（連続、buffer が uncached なら UNCACHED、image の共有は ENOTSUP）と `render-device.c` の配線。ops の field が増えたので、位置で初期化している 5 driver（amdrdna `node.c`・ghci `share.c`・i915 `resource.c`・nvrtx `share.c`・venus `share.c`）に `NULL` を 1 つずつ足した（`-Wmissing-field-initializers`）。
- `src/drivers/camera/cam.c`: `CAM_REQUEST_BUFFERS` の `CAM_MEMORY_IMPORT`（memory の無い slot を作る）、`CAM_QUEUE_BUFFER` の最初の 1 回で `handle_fd_get(fd, KERNEL_HANDLE_DRIVER)` → `drv_gpu_shared_backing()` → 連続・MMIO でない・UNCACHED・`bytes ≥ image_bytes`・page の境界を確かめ、満たさなければ `EXDEV`（backend に view が無い `ENOTSUP` も `EXDEV`）、`kern_pmem_map_uncached()` の view を slot の address に。handle は buffer の解放（`REQUEST_BUFFERS 0`・所有者の close・device の解放）まで保持し、view を外してから `handle_put`。2 回目以降の QUEUE は `fd = -1`（他は `EINVAL`）。import の buffer は camera の `mmap` を `EINVAL`、`QUERY`/`DEQUEUE` は `memory = IMPORT`・`mmap_offset 0`・`CAM_BUFFER_IMPORTED`。`CAM_CAP_IMPORT_GPU` は source が CPU で書き、kernel に GPU core と uncached の view がある時だけ `GET_INFO` に立つ。GPU core と uncached の view は kernel により無いので weak の宣言で確かめる（`dma.c` と同じ型）。
- `userland/tests/camvk/`（`main.c`・`luma.comp`・`shader.h`・`Makefile`）: YUYV を設定し、slot ごとに export 付きの Vulkan の storage buffer（host-visible を優先）を確保、`vkGetMemoryFdKHR` の fd で最初の QUEUE、compute（local size 32、1 invocation が 4 画素の Y を 1 word に）で gray に、fence の後に QUEUE し直す。frame の memory が host-visible なら CPU が同じ memory の Y と GPU の結果を全画素で比べる（前の frame を読んでいれば不一致）。最後の frame を `<o>.pgm`（と `<o>-cpu.pgm`）に、fps と counters を表示。最後の行 `camvk: PASS/FAIL`、終了 0/1/2（ENOSPC）/3/4（import 不可）。build は `platform/common/userland-dynamic.mk` の rule と `DYNAMIC_PORTABLE_PROGRAMS`、`platform/amd64/vmunix.mk` の basic の除外の列に `camvk`。`plan/ws282/tests/config-rpi5.mk` に `camvk`。

## 確かめた事（host と build だけ）

- `sh plan/ws282/tests/run-cam-host-test.sh`: 新しい case `import`（GET_INFO の CAP、IMPORT の slot、mmap の拒否、fd -1・不明の fd・memory の不一致は `EINVAL`、cached・小さい・ENOTSUP・MMIO・非連続は `EXDEV` で handle を残さない、適う memory で bind、frame が GPU の memory に入る、2 回目の QUEUE の fd、解放で handle と view が 0）を足し、通常と ASan/UBSan で全 case PASS。`run-uvc-host-test.sh` も PASS。
- vmunix: amd64・pcat・rpi4・rpi5・visionfive2・odroid-c5-keiland（mali）・amd64 + GHCI が rc 0・warning 0（log は worktree の `build/ws282/logs/p008-*.log`）。
- `bin/camvk`: `plan/ws282/tests/config-rpi5.mk`（arm64）と amd64 + camvk の config で rc 0・warning 0、`check-dynamic-elf.py` まで通った。SPIR-V は `spirv-val` で valid。

## 未実施

- Pi 5 + capture（または webcam）で `camvk` の PGM が `camcap` と一致、全 frame の CPU との一致、uncached の書きの速さ（fps と uvc の counters）。Pi 5 の Vulkan の export が allocation だけの共有（`GPU_CAP_ALLOCATION_SHARE`）を持つか、host-visible の型が export に使えるかは見ていない（実機で `camvk` が FAIL の理由を出す）。
- `plan/ws014/tests/gpu-sharing.c`・`gpu-scanout.c`（他の WS の古い host 試験、tools.md に無い）は流していない。
