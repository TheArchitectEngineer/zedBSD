<!-- awesome-plan project=zedbsd record=ws103-p004 -->

# ws103-p004: compositor の dedicated の import、`RESOURCE_IMPORT`・`DESTROY` の削除

- Parent: [WS103](../ws.md)
- Status: cleared（2026-09-30 夜、q511-i01）
- Disposition: normal
- Queue: q511-i01
- Design: [design.md](../design.md) §2.3（compositor の側）・§2.5（buffer の部分）、§3 の p004

## 範囲

1. compositor（`userland/desktop/wayland/`）:
   - OS の backend の module `gpu-zedbsd.c` と OS 共通の `zwl-gpu.h`（新規）: `keiland_gpu_buffer_v1` の payload（64 byte の `gpu_image_descriptor`）を compositor の型
     `struct zwl_buffer_layout` に写し、Vulkan に渡す前に wire の値を確かめる（幅・高さが 0 でなく最大の次元以下、形式が 2 つのどちらか、stride が幅×4 以上で 4 の倍数、
     offset と大きさが溢れず allocation に収まる、memory type が数の内）。
   - `import.c`: wire の記述から作った image を dedicated で import する。device の拡張 `VK_KHR_get_memory_requirements2`・`VK_KHR_dedicated_allocation` を有効にする。
   - `zwl_gpu_import`・`GPU_RESOURCE_DESTROY`・`buffer->image`（`struct gpu_resource_import`）を削除。`ZWL IMPORT client= buffer=` の行を同じ形で保つ。
     `ZWL RELEASE … resource=` の行と、それを読む試験を直す。
2. libvulkan:
   - dedicated の無い image の capability の import を拒む（p003 から移した）。
   - dedicated の import が image を名指しするときは、allocation の capability（`GPU_ALLOCATION_IMPORT`）として受けず、image の capability としてだけ import する
     （実行の前に Q1 が見つけた穴: allocation の capability を送ると kernel の image の記述との照合を通らない）。
3. 試験: 偽の buffer を送る probe（allocation の capability を image の buffer として送る。Venus だけ）。

範囲の外: fence（p005・p006）、`/dev/gpu0` と `--gpu` の削除（p006）、HAL、toolchain。

## 完了の基準

1. build（warning 0）。
2. grep: compositor に `GPU_RESOURCE_IMPORT`・`GPU_RESOURCE_DESTROY`・`zwl_gpu_import` が無い。
3. QEMU の Venus: C1・C2、app の起動（import の `ZWL IMPORT` の行）、偽の buffer の probe が protocol の error で断られ、compositor は動き続ける。
4. host の試験（p003 の dedicated の試験と、`gpu-zedbsd.c` の wire の確かめ）。
5. boot test、5330 の passthrough の smoke。
6. 規約の全文。

## 記録（2026-09-30 夜、q511-i01、メインのエージェント Q1）

### 変更（commit `7ce84c77`・`0d3a44d8` ほか）

- compositor:
  - `zwl-gpu.h`（新規、OS 共通）: `struct zwl_buffer_layout`、`struct zwl_gpu_limits`、`zwl_gpu_buffer_wire_bytes`・`zwl_gpu_buffer_decode`・`zwl_gpu_buffer_handle_type`。
  - `gpu-zedbsd.c`（新規、zedBSD の backend、Makefile の `KEILAND_GPU_SOURCES`）: 64 byte の `gpu_image_descriptor` を decode し、Vulkan に渡す前に全ての値を確かめる
    （revision と長さ、0 でなく最大の次元以下、linear、2 つの形式、stride が幅×4 以上で 4 の倍数、offset の溢れ、最後の行が allocation の中、memory type が数の内）。
    compositor の中で UAPI の画像の型を読むのはこの file だけ（fence の型は p006 まで `zwl.h` に残る）。
  - `protocol.c`: `factory_request` は backend で decode し、`zwl_import_create` へ。`ZWL IMPORT client= buffer= width= height= bytes=` を成功で出す（import-launch は prefix を数える）。
  - `import.c`: layout から image を作り、`VkMemoryDedicatedAllocateInfo` で dedicated の import。handle の種類は backend から。
  - `compose.c`: device の拡張に `VK_KHR_get_memory_requirements2`・`VK_KHR_dedicated_allocation` を常に（5、fence があれば 7）。`compose_limits` で最大の次元と memory type の数を server に。
  - `display.c`: `zwl_gpu_import`（`GPU_RESOURCE_IMPORT` と `memcmp`）を削除。`objects.c`: `GPU_RESOURCE_DESTROY` を削除、大きさは layout から、`ZWL RELEASE` の行から `resource=` を削除
    （読む試験は無い）。`zwl.h`: `buffer->image` を `layout` に、server に `gpu_limits`。
- libvulkan（`memory.c`）: image の capability の import は dedicated が image を名指ししなければ `VK_ERROR_INVALID_EXTERNAL_HANDLE`（p003 から移した）。
  dedicated の import が image を名指ししたら、`GPU_ALLOCATION_IMPORT`（allocation の capability）を試さず image の capability としてだけ import する
  （実行の前に見つけた穴: allocation の capability だと kernel の image の記述との照合を通らなかった）。`memory_dedicated_image`。
- 試験: `userland/base/tests/gpu-forge`（`/bin/gpu-forge-test`、Vulkan で export した allocation の capability を image の buffer として送る）、`platform/amd64/vmunix.mk` の link の規則、
  `plan/ws103/tests/config-amd64-forge.mk`・`build-forge-image.sh`・`forge-guest.sh`、host の `gpu-zedbsd-host.c`・`run-gpu-zedbsd-host.sh`。

### 確かめ

| 基準 | 結果 |
| --- | --- |
| 1 build | compositor・libvulkan・probe: warning 0。forge の image と passthrough の image: rc 0、desktop と試験の warning 0 |
| 2 grep | compositor に `GPU_RESOURCE_IMPORT`・`GPU_RESOURCE_DESTROY`・`zwl_gpu_import`・`gpu_resource_import` は無い。`gpu_image_descriptor`・`GPU_PIXEL`・`GPU_IMAGE_LINEAR` は `gpu-zedbsd.c` だけ。残る GPU の ioctl は fence の 2 か所（p006） |
| 3 QEMU の Venus | `forge-guest.sh`: PASS。偽の buffer は `ZWL VULKAN_IMPORT_ERROR … result=-1000072003`（`VK_ERROR_INVALID_EXTERNAL_HANDLE`）・`ZWL IMPORT_ERROR` で断られ、probe は protocol の error（`GPUFORGE RESULT refused=1 error=67`）。compositor は動き続け、その後の wltest の buffer 3 つを dedicated で import（`ZWL IMPORT client=2 … 300x200`）、120 frame。`criteria.sh … C1 C2`: C1 p126 PASS、C1 c1-boot-shutdown PASS、C2 PASS（14/14） |
| 4 host の試験 | `run-gpu-zedbsd-host.sh`: 通常・ASan/UBSan PASS（17 件: 正しい 3 件、断る 14 件）。`run-dedicated-host.sh`: PASS（18 件） |
| 5 boot test、5330 | boot test PASS（`build/ws103/p004-boot/login.png`）。`c5-hw.sh build/ws103/p004-pt.img build/ws103/p004-hw 3`: PASS（34 回、最大 56 ms）、session の log に `ZWL IMPORT client` 39 行、error 0 |
| 6 規約 | 変更の範囲を `plan/coding-style.md` の checklist で見直した |

制限: 本物の image の capability に偽の記述を付ける端から端までの試験は無い（image の capability を作れるのは libvulkan の WSI の内部だけで、公開の API から作れない）。
その場合の照合は host の試験（dedicated の 18 件）で確かめた。bind の守り（別の image の bind を拒む）は、compositor が正しく使う限り通らない道で、端から端では試していない。
