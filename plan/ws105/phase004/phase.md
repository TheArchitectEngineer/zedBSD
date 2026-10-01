<!-- awesome-plan project=zedbsd record=ws105-p004 -->

# ws105-p004: libvulkan-compat (2): Wayland の WSI（`zwp_linux_dmabuf_v1`、implicit sync）

Status: planned
Disposition: normal
Parent: [WS105](../ws.md)
Queue: なし
依存: p003
実行者: phase-runner（high）。**始める前に [design.md](../design.md) の §4（特に §4.5・§4.6・§4.8・§4.11）を読む。**

## 目的

libvulkan-compat に Wayland の WSI を足す（決定 D6）: `VK_KHR_surface`・`VK_KHR_wayland_surface`・`VK_KHR_swapchain`。swapchain の image は後段で dma-buf を export できる形で作り、
`zwp_linux_dmabuf_v1` で compositor に送る。fence は implicit sync（dma-buf に sync_file を付ける）。この Phase の試験の相手は host の上の試験用の Wayland server
（`dmabuf-probe`）で、我々の compositor（p007）より先に WSI を確かめる。

## 作る・変える file（`userland/desktop/libvulkan-compat/`）

| file | 中身 |
| --- | --- |
| `functions.tsv` | O の行を足す: `vkCreateWaylandSurfaceKHR`・`vkDestroySurfaceKHR`・`vkGetPhysicalDeviceSurfaceSupportKHR`・`vkGetPhysicalDeviceSurfaceCapabilitiesKHR`・`vkGetPhysicalDeviceSurfaceFormatsKHR`・`vkGetPhysicalDeviceSurfacePresentModesKHR`・`vkGetPhysicalDeviceWaylandPresentationSupportKHR`・`vkCreateSwapchainKHR`・`vkDestroySwapchainKHR`・`vkGetSwapchainImagesKHR`・`vkAcquireNextImageKHR`・`vkAcquireNextImage2KHR`・`vkQueuePresentKHR`（全て export する） |
| `instance.c`・`device.c` | 我々の WSI の拡張を一覧に足す（instance: `VK_KHR_surface`・`VK_KHR_wayland_surface`。device: `VK_KHR_swapchain`、ただし design §4.6 の device の拡張を後段が持つ physical device だけ）。`vkCreateInstance`・`vkCreateDevice` で app の WSI の拡張を後段に渡さず、内部で要る拡張を足す（design §4.6） |
| `dispatch.c` | O の関数の名前を `vkGet*ProcAddr` で返す（app が拡張を有効にしたときだけ、design §4.7） |
| `wsi-wayland.c` | surface の作成・破棄、surface の問い合わせ、`zwp_linux_dmabuf_v1` の bind と `format`・`modifier` の event の受け取り（surface ごとの event queue、design §4.8） |
| `wsi-swapchain.c` | swapchain（image の作成、dma-buf の export、`wl_buffer` の作成、acquire・present、frame callback、release、`oldSwapchain`）。design §4.8 の手順そのまま |
| `linux-dmabuf-v1-protocol.c`・`linux-dmabuf-v1-client-protocol.h` | `zwp_linux_dmabuf_v1`・`zwp_linux_buffer_params_v1` の `wl_interface` の表と inline の request の関数。我々の libwayland の手書きの形（`userland/desktop/libwayland/*-protocol.c` と `userland/desktop/libwayland/zed-*-client-protocol.h`）を手本に書く。XML は `/usr/share/wayland-protocols/stable/linux-dmabuf/linux-dmabuf-v1.xml`（version 3 の範囲だけ） |
| `Makefile.linux` | 依存に `libwayland-client.so` を足す |

zedBSD の `userland/desktop/libvulkan/wsi-wayland.c` は、event queue・wrapper・frame callback・release の扱いの手本として読んでよい（複写しない、D7）。

細部（design §4.8 に無い物）:

- swapchain の image の数: `minImageCount` 以上で、FIFO なら `max(minImageCount, 3)`、MAILBOX なら `max(minImageCount, 4)`。
- `wl_buffer` は image ごとに 1 回作り、swapchain の破棄で destroy する。
- image の dma-buf の fd は 1 本を image の記録に持つ（implicit sync の ioctl 用）。`zwp_linux_buffer_params_v1.add` には `dup` を送る（送った後に閉じる）。
- present で `VkPresentInfoKHR.pResults` に結果を書く。
- 同じ thread からの呼び出しを前提にしてよい（Vulkan の規格: swapchain は外部で同期）。ただし instance・device の記録の表は mutex で守る。
- 失敗の値: compositor が `zwp_linux_dmabuf_v1` を持たない → `vkCreateSwapchainKHR` が `VK_ERROR_SURFACE_LOST_KHR`、stderr に 1 行。compositor が切れた → `VK_ERROR_SURFACE_LOST_KHR`。
- implicit sync の ioctl の予備の道（`ENOTTY`）は design §4.8 のとおり作る。試験では環境変数 `KEILAND_VULKAN_NO_IMPLICIT_SYNC=1` で予備の道に強制できるようにする（その道の試験のため）。

## 試験の道具（`plan/tools/keiland-linux/`、main が merge）

| file | 中身 |
| --- | --- |
| `dmabuf-probe.c`（と生成物） | design §7.3 の試験用の Wayland server。**host の libwayland-server と wayland-scanner を使う**（`wayland-scanner server-header`・`private-code` で `linux-dmabuf-v1.xml` から作る。生成物は `build/keiland-linux/test/` に置く）。`wl_compositor`（v4: `create_surface`、surface の `attach`・`damage_buffer`・`frame`・`commit`）と `zwp_linux_dmabuf_v1`（v3）だけを持つ。socket は `$XDG_RUNTIME_DIR/<name>` か `--socket PATH`。frame の callback は commit から 16 ms 後に done を送る。出力は design §7.3 の `PROBE frame=N pixel=0xAARRGGBB waited_ms=M` の行と、終わりに `PROBE RESULT frames=N` |
| `wsi-probe-client.c` | 我々の libwayland-client と libvulkan-compat で、`--frames N` 回 clear して present する client。色は frame の番号で `0xffff0000`・`0xff00ff00`・`0xff0000ff` を巡る（B8G8R8A8 の memory の並びでの値を正しく計算する）。`--resize` で 30 frame ごとに大きさを変えて swapchain を作り直す。`--mailbox` |

build:

```
cc -o build/keiland-linux/test/dmabuf-probe plan/tools/keiland-linux/dmabuf-probe.c build/keiland-linux/test/linux-dmabuf-v1-protocol.c \
   -Ibuild/keiland-linux/test $(pkg-config --cflags --libs wayland-server)
cc -o build/keiland-linux/test/wsi-probe-client plan/tools/keiland-linux/wsi-probe-client.c -Iuserland/desktop/keiland \
   -Lbuild/keiland-linux/lib -l:libwayland-client.so -lvulkan -Wl,-rpath,/opt/keiland/lib
```

（`wsi-probe-client` は **system の** `wayland-client.h` を使ってはいけない。`-Iuserland/desktop/keiland` を先に置き、`pkg-config wayland-client` を使わない。）

実行（host、design §7.3）:

```
export XDG_RUNTIME_DIR=$PWD/build/keiland-linux/test/xdg; mkdir -p -m 0700 $XDG_RUNTIME_DIR
build/keiland-linux/test/dmabuf-probe --socket $XDG_RUNTIME_DIR/probe-0 --frames 90 > build/keiland-linux/test/probe.out &
WAYLAND_DISPLAY=probe-0 LD_LIBRARY_PATH=build/keiland-linux/stage/opt/keiland/lib build/keiland-linux/test/wsi-probe-client --frames 90
wait; grep -c '^PROBE frame=' build/keiland-linux/test/probe.out
```

## 確かめ（完了の条件）

1. build（gcc・clang）warning 0、`elf-check.sh`・`makefile-sync.sh` PASS、`vk-chain-test`・`interpose-check.sh` が今も PASS（p003 の (4) の「WSI の拡張が無い」は、
   `VK_KHR_wayland_surface`・`VK_KHR_swapchain` が**有り**、`VK_KHR_xcb_surface` が無い、に直す）。
2. `dmabuf-probe` が 90 frame を受け、各 frame の pixel が client の色の列と一致する（`PROBE frame=N` の色が N に対して期待どおり）。
3. implicit sync: 各 frame の `waited_ms` が出ている（fence を待った）。`KEILAND_VULKAN_NO_IMPLICIT_SYNC=1` でも 2 が PASS する（予備の道）。
4. `--resize` で 90 frame、大きさの違う frame が probe に届き、client が error なく終わる。
5. `--mailbox` で 90 frame。
6. 終わりに client の process が leak なく終わる（`valgrind` が host にあれば `--leak-check=full` で我々の file の leak が 0。無ければ「未実施」）。
7. design §10 の V3 の結果（lavapipe の modifier の一覧、LINEAR か否か）を記録する。

## 結果

（実行の後に書く）
