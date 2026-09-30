<!-- awesome-plan project=zedbsd record=ws103-p006 -->

# ws103-p006: compositor の fence を poll だけに、GPU の fd と UAPI を無くす

- Parent: [WS103](../ws.md)
- Status: in-progress
- Disposition: normal
- Queue: q513-i01
- Design: [design.md](../design.md) §2.4（compositor の側）・§2.5・§2.6、§3 の p006

## 範囲

`userland/desktop/wayland/` だけ。

1. `factory_fence`: fd と世代（0 でない事だけ）を受けて持つ。`GPU_FENCE_QUERY` を削除。
2. `zwl_fence_ready`: 各 fd を `poll(POLLIN, 0)` で見る。readable・POLLERR・POLLHUP・POLLNVAL なら済み、どれでもなければ待つ。`GPU_FENCE_QUERY` を削除。
3. main loop の poll の集合から fatal の client の surface の fence を外す（再レビュー m5）。
4. `/dev/gpu0` の open（`zwl_gpu_open`）、`--gpu` の option、`server->gpu`・`gpu_path` を削除。
5. `zwl.h` から `uapi/gpu.h`・`uapi/gpu-fence.h` を外す（GPU の UAPI を読むのは `gpu-zedbsd.c` だけ）。`uapi/input.h`（evdev）は残す。
6. `plan/ws103/tests/v1-check.sh`（design §2.6）。

## 完了の基準

1. build（warning 0）。
2. `v1-check.sh` PASS（V1）。
3. QEMU の Venus: fence の試験（`fence-guest.sh`、ws035 p054）、forge-guest、C1・C2、Notes（WS079-p010 に当たる試験）。
4. boot test、5330 の passthrough の smoke。
5. 規約の全文。

## 記録

（実行中）
