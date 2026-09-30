<!-- awesome-plan project=zedbsd record=ws103-p004 -->

# ws103-p004: compositor の dedicated の import、`RESOURCE_IMPORT`・`DESTROY` の削除

- Parent: [WS103](../ws.md)
- Status: in-progress
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

## 記録

（実行中）
