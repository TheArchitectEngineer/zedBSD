<!-- awesome-plan project=zedbsd record=ws105-p006 -->

# ws105-p006: compositor の Linux の build と module (1): seat-direct・入力・session（wl_shm の client まで）

Status: planned
Disposition: normal
Parent: [WS105](../ws.md)
Queue: なし
依存: p005、**WS104 の完了**（compositor の `zwl-gpu.h`・`zwl-input.h`・`zwl-os.h` と `zedbsd/` の module）
実行者: phase-runner（high）。**始める前に [design.md](../design.md) の §5（特に §5.1・§5.3・§5.4・§5.6）を読む。**

## 目的

compositor（`userland/desktop/wayland/`、program `wayland`）を Linux で build し、guest の VT で root で起動して、wallpaper・system bar・wl_shm の client の窓を出し、
pointer と key を届ける。GPU の client の buffer（`zwp_linux_dmabuf_v1`）は p007。

## 作る・変える file

| file | 中身 |
| --- | --- |
| `userland/desktop/wayland/Makefile.linux` | compositor の program `wayland`（bin）。source は zedBSD の `Makefile` の `KEILAND_SOURCES` から `KEILAND_ZEDBSD_SOURCES` を除いた物と `linux/*.c` と、`userland/desktop/vkdemo/display.c`・`artwork/mark.c`・`picture/color-glyph.c`。依存 `libvulkan.so.1 libtruetype.so libkeiland.so libpng-compat.so libz-compat.so`、system `-lm -lpthread`。zedBSD の `KEILAND_FONT_DATA` と同じ font と license（`share/fonts/`・`share/licenses/keiland-fonts/`）と emoji の font（design §3.2）を `KEILAND_LINUX_DATA` で入れる（compositor が文字を描くのに要る）。`keiland-x11` は入れない（xserver は範囲の外、D21。App Home の X の項目が `access()` で隠れるか確かめ、隠れなければ結果に書く） |
| `wayland/linux/seat-linux.h` | linux の file の間の内部の宣言（seat の open・close・device の open・close、DRM の fd） |
| `wayland/linux/os-linux.c` | `zwl-os.h` の Linux の実装（design §5.1）: seat の選択（この Phase は `direct` だけ）、VT の `KD_GRAPHICS`・`K_OFF` と戻し、既定の socket、`zwl_os_display_acquire`（`vkAcquireDrmDisplayEXT`）、poll は 0 個 |
| `wayland/linux/seat-direct-linux.c` | root で `/dev/dri/card0`（`KEILAND_DRM_DEVICE`）と入力の device を開く |
| `wayland/linux/input-linux.c` | `zwl-input.h` の Linux の実装（design §5.3）。`zedbsd/input-zedbsd.c` と同じ形で書き、device の open・close は seat の関数 |
| `wayland/linux/handoff-linux.c` | `zwl_handoff_*`（design §5.4） |
| `wayland/linux/gpu-linux.c`（この Phase は空の形） | `zwl_gpu_global_interface()` が NULL（GPU の global を出さない）、`zwl_gpu_request` は EPROTO、`zwl_gpu_commit` は何もしない、拡張は instance の `VK_EXT_direct_mode_display`・`VK_EXT_acquire_drm_display` だけ、frame の fence は 0（D22） |
| 共通: `protocol.c` | `zwl_gpu_global_interface()` が NULL なら GPU の global を registry に出さない（design §5.6） |
| 共通: `main.c` | `--socket` が与えられたかの flag（無ければ。design §5.6） |
| `userland/desktop/keiland-linux.mk` | `KEILAND_LINUX_PACKAGES` に wayland と、compositor が要る library の残り（`libkeiui` は compositor が要らなければ p008） |
| `userland/desktop/wlshm/Makefile.linux` | 試験の client（wl_shm だけ、依存 `libwayland-client.so`） |

共通の file を変えたので zedBSD の回帰（design §9.2）を流す。

**compositor の code に `#if defined(__linux__)` を書かない**（zwl-evdev.h の外）。OS で違う所は `linux/` の file の関数にする。共通の file の他の変更が要るなら止めて main に報告。

## guest での起動の手順

```
make keiland-linux && make keiland-linux-install DESTDIR=$PWD/build/keiland-linux/stage
plan/tools/keiland-linux/guest.sh start
plan/tools/keiland-linux/install-guest.sh
plan/tools/keiland-linux/guest.sh ssh 'openvt -c 7 -s -- sh -c "/opt/keiland/bin/wayland --session --glass --socket=/run/keiland-0 --timeout=600000 > /tmp/wayland.log 2>&1"'
plan/tools/keiland-linux/guest.sh ssh 'for i in $(seq 30); do grep -q "ZWL READY" /tmp/wayland.log && break; sleep 1; done; cat /tmp/wayland.log | head -20'
plan/tools/keiland-linux/guest.sh screenshot build/keiland-linux/p006-desktop.png
plan/tools/keiland-linux/guest.sh ssh 'XDG_RUNTIME_DIR=/run WAYLAND_DISPLAY=keiland-0 /opt/keiland/bin/wlshm &'
plan/tools/keiland-linux/guest.sh screenshot build/keiland-linux/p006-wlshm.png
```

（compositor の引数は zedBSD の `/etc/keiland/session`（`session.sh`）と同じ。`--timeout` などは `main.c` の `parse_options` を見て合わせる。`ZWL READY` の行は compositor の log
（stdout）で、compositor 自身の log は判定に使ってよい。guest の serial の log は使わない。）

## 確かめ（完了の条件）

1. build（gcc・clang）warning 0、`elf-check.sh`・`makefile-sync.sh` PASS。
2. guest: compositor が起動し `ZWL READY` を出す。screenshot に wallpaper と上の system bar が出る（`png-probe.py` で system bar の帯の色と wallpaper の色が違う、PNG をユーザーに見せる）。
3. guest: `wlshm` の窓が出る（screenshot の差。wlshm の描く色を `png-probe.py` で）。
4. 入力: `guest.sh move` で pointer を動かした後の screenshot で cursor の位置が変わる。`guest.sh click` で wlshm の窓に focus が移る（titlebar の色の変化など、zedBSD の C2 の試験の判定と同じ物を使う）。
   `guest.sh key super`（または App Home を開く key、zedBSD の試験と同じ）で App Home が開く。
5. Log Out（App Home の Log Out）で compositor が終わり、console に戻る（screenshot）。VT が `KD_TEXT` に戻り、key が console に届く。
6. SIGTERM で終わっても console に戻る（`guest.sh ssh 'pkill -TERM -x wayland'`）。
7. zedBSD の回帰（design §9.2: build、v1-check、`criteria.sh ... C1 C2 C9`、keiland-os-boundary の check、boot test）。

## 結果

（実行の後に書く）
