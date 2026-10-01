<!-- awesome-plan project=zedbsd record=ws105-p007 -->

# ws105-p007: compositor の Linux の module (2): `zwp_linux_dmabuf_v1` の server と implicit sync

Status: planned
Disposition: normal
Parent: [WS105](../ws.md)
Queue: なし
依存: p006
実行者: phase-runner（high）。**始める前に [design.md](../design.md) の §5.2・§5.6 と §4.8（client の側）を読む。**

## 目的

compositor が Linux の GPU の client（libvulkan-compat の WSI を使う app）の buffer を受けて合成できるようにする（決定 D6・D17）。

## 作る・変える file

| file | 中身 |
| --- | --- |
| `wayland/linux/gpu-linux.c` | design §5.2 の全部: global `zwp_linux_dmabuf_v1` v3、bind の時の `format`・`modifier` の event、`create_params`、params の `add`・`create`・`create_immed`・`destroy`、検査と error、dma-buf の import（`zwl_import_adopt`）、log の行、`zwl_gpu_commit` の `DMA_BUF_IOCTL_EXPORT_SYNC_FILE(READ)`、拡張の一覧、`zwl_gpu_object_free`（buffer に残した fd を閉じる、params の記録を消す） |
| 共通: `zwl.h`・`protocol.c`・`objects.c` | design §5.6 の p007 の行: object の kind `ZWL_GPU_OBJECT`（`zwl_dispatch` で `zwl_gpu_request` へ）、`struct zwl_object` の `void *gpu_private`、`object_free` の中で `zwl_gpu_object_free(object)` を呼ぶ |
| `wayland/zedbsd/gpu-buffer-zedbsd.c` | `zwl_gpu_object_free` の zedBSD の実装（何もしない）と、`zwl_gpu_request` が `ZWL_GPU_OBJECT` を受けることは無いので EPROTO |
| `userland/desktop/wltest/Makefile.linux` | Vulkan の WSI の試験の client（依存 `libvulkan.so.1 libwayland-client.so`） |
| `userland/desktop/mview/Makefile.linux` | Model viewer（Vulkan の client。依存は zedBSD の `Makefile` と同じ） |

細部:

- params の `add` の fd は、`zwl_take_fd(client)`（wire の SCM_RIGHTS）で受ける。fd がまだ届いていなければ EAGAIN（今の `factory_request` と同じ）。
- `create`（非同期）は成功で `created(new_id)` の event（server の側で id を割り当てる。我々の compositor の server の id の割り当ての仕方を `objects.c` で確かめる。
  難しければ `create` は `failed` を返し、`create_immed` だけを受ける形でもよい。libvulkan-compat は `create_immed` を使う。結果に書く）。
- 1 つの params は 1 回だけ使える（2 回目は `already_used`）。
- import の失敗（Vulkan の error）は、`create` なら `failed`、`create_immed` なら `invalid_wl_buffer` の protocol の error（client は終わる）。log `ZWL IMPORT_ERROR client=... error=...`（zedBSD と同じ形）。
- server の `gpu_limits`（`compose_limits`）は Linux でも同じ値の意味（`maxImageDimension2D`）。

## 確かめ（完了の条件）

1. build（gcc・clang）warning 0、`elf-check.sh`・`makefile-sync.sh` PASS。
2. guest: compositor の上で `wltest`（zedBSD の WS103 の試験と同じ使い方。引数は `wltest/main.c` を見る）が 600 frame を出し、compositor の log に `ZWL IMPORT` の行が出る。
   screenshot に wltest の窓の絵（PNG をユーザーに見せる、`png-probe.py` で窓の中の色）。
3. guest: `mview` の窓が出て、model が描かれる（screenshot）。
4. implicit sync: compositor の `--log-frames` で acquire の fence の行（zedBSD の `ZWL ACQUIRE_FENCE` と同じ形を Linux の module からも出す）が commit ごとに出る。
5. 偽の buffer: dma-buf の大きさより大きい `offset + stride × height` の params を送る小さな client（`plan/tools/keiland-linux/dmabuf-forge.c`、host の libwayland でなく我々の
   libwayland-client で書く、`memfd` を udmabuf で dma-buf にする（`/dev/udmabuf`、guest で root）か、libvulkan-compat で export した本物の dma-buf の大きさを偽る）が
   `out_of_bounds` の protocol の error で切られ、compositor は動き続ける（その後 wltest が出る）。
6. 窓を閉じた後に compositor の fd が増え続けない（`ls /proc/$(pidof wayland)/fd | wc -l` を wltest の 5 回の起動・終了の前後で比べる）。
7. zedBSD の回帰（design §9.2。共通の file と zedbsd の module を変えたので、gpu-boundary の試験一式も: v1-check、host の 2 本、forge-guest・fence-guest）。

## 結果

（実行の後に書く。design §10 の V5 の結果も）
