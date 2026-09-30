<!-- awesome-plan project=zedbsd record=ws103-p006 -->

# ws103-p006: compositor の fence を poll だけに、GPU の fd と UAPI を無くす

- Parent: [WS103](../ws.md)
- Status: cleared（2026-10-01、q513-i01）
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

## 記録（2026-09-30 夜〜10-01、q513-i01、メインのエージェント Q1、ユーザーの自走の指示）

### 変更（commit `81b6d308`）

- `display.c`: `zwl_gpu_open` を削除。`zwl_fence_ready` は各 fd を `poll(POLLIN, 0)` で見る（0 なら待つ、EINTR も待つ、readable・POLLERR・POLLHUP・POLLNVAL・その他の失敗は済みとして閉じる）。
  `GPU_FENCE_QUERY` を削除。先頭の注釈に「compositor は自分の GPU の fd を持たない」。
- `protocol.c`: `factory_fence` は世代が 0 でない事だけを確かめて fd を持つ（`GPU_FENCE_QUERY` を削除。fence でない fd を送った client は自分の surface が進まないだけ）。
  使わなくなった `<sys/ioctl.h>` を外した（objects.c も）。
- `main.c`: poll の集合の fence を数える loop と埋める loop の両方で fatal の client を外す（再レビュー m5）。`/dev/gpu0` の open、`--gpu` の option と usage、`server.gpu` を削除。
- `zwl.h`: `uapi/gpu.h`・`uapi/gpu-fence.h` の include、`gpu`・`gpu_path`、`zwl_gpu_open` の宣言を削除（`uapi/input.h` は evdev のため残す）。
- 試験: `plan/ws103/tests/v1-check.sh`（新規、design §2.6）。

### 確かめ

| 基準 | 結果 |
| --- | --- |
| 1 build | compositor: warning 0。forge の image と passthrough の image: rc 0、desktop の warning 0 |
| 2 V1 | `v1-check.sh`: PASS（GPU の UAPI を include するのは `gpu-zedbsd.c` だけ、GPU の ioctl は 0、backend にも ioctl は無い、GPU の UAPI の header を `#error` にして 51 個の source が `-fsyntax-only` で通る、`/dev/gpu` と `--gpu` は無い）。対照: 同じ条件で `gpu-zedbsd.c` は `#error` で失敗する（検出できることの確かめ） |
| 3 QEMU の Venus | `fence-guest.sh`: fence 600 個が世代 1、poll の readiness で wltest 600 frame（65 秒。p005 の後は 61 秒、V4 は p007 で測る）。ws035 p054（signal を待つ fence・signal しない fence）PASS。`forge-guest.sh` PASS。`notes-gesture.sh`（ws079 の右上の swipe で Notes）PASS。C1 p126・c1-boot-shutdown・C2 PASS |
| 4 boot test、5330 | boot test PASS。`c5-hw.sh` PASS（34 回、最大 56 ms、import 39 回、error 0） |
| 5 規約 | 変更の範囲を checklist で見直した |
