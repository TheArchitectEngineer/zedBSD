<!-- awesome-plan project=zedbsd record=ws104-p006 -->

# ws104-p006: compositor の session と OS の hook を zedBSD の module に

Status: planned
Disposition: normal
Parent: [WS104](../ws.md)
Queue: なし
実行者: phase-runner（high）

## 目的

compositor が OS の session の管理と話す部分と、Linux（WS105）で要る OS の差し込み口を、OS の module の形にする。

- **session**: zedBSD では sessiond と継いだ fd（greeter は `--auth-fd`、session は `--control-fd`）で話す（`handoff.c`）。Linux では sessiond は無く（決定 D11）、
  gdm が compositor を起動し、Log Out は compositor が終わるだけ。`handoff.c` を zedBSD の module にすれば、WS105 は同じ関数（`zwl_handoff_*`）の Linux 版を
  書くだけで済む。
- **OS の hook**: Linux では、(1) 起動の最初に seat（DRM と入力の device の権限）を得る、(2) logind の D-Bus の fd を main loop の poll に入れる、
  (3) 画面の DRM の fd を Vulkan に渡す（`vkAcquireDrmDisplayEXT`、決定 D8）が要る。zedBSD ではどれも何もしない。共通の code に呼び出しの口だけを作り、
  zedBSD の実装は空にする。

## 今の姿

- `handoff.c` の関数（`zwl.h:1011-1014` に宣言）: `zwl_handoff_wait`（50-127）・`zwl_handoff_release`（133-147）・`zwl_handoff_logout`（154-178）・`zwl_handoff_tick`（184-234）。
  呼ぶ所: `display.c:449`（wait）、`home.c:1471-1479`（Log Out）、`shell.c:1974`（tick）、`greeter.c`（release）。sessiond が居ない（fd が -1）とき、wait はすぐ戻り、
  logout は 0 を返して呼ぶ側が `zwl_request_stop()` で終わる。
- `greeter.c` の lock 画面（`zwl_lock`）は `control_fd < 0` なら何もしない。greeter の mode は `--auth-fd` が必須（sessiond だけが起動する）。
  **greeter.c はこの Phase では動かさない**（共通のまま。Linux では `--greeter` が使われないだけ）。
- main loop（`main.c` の `event_loop`、595-901）の poll の set: listener、client、入力の device、frame の fence、surface の acquire の fence。
- 画面: `compose.c` の `zwl_compose_output_prepare`（141-188、`vkdemo_display_open` と format の選択）→ `zwl_handoff_wait` → `zwl_compose_output_open`（194-246、swapchain）。

## 新しい境界（新しい header `zwl-os.h`）

```c
/* Takes what the compositor needs of the OS before it opens Vulkan (Linux: the seat).  Returns 0 or an errno value
   (startup fails).  zedBSD: nothing, 0. */
int zwl_os_open(struct zwl_server *server);

/* Gives it back at exit (service_cleanup).  zedBSD: nothing. */
void zwl_os_close(struct zwl_server *server);

/* The descriptors the OS module wants in the main loop's poll (Linux: logind's bus): how many, then filling them, then
   handling what poll reported.  zedBSD: 0, nothing, nothing. */
size_t zwl_os_poll_count(const struct zwl_server *server);
void zwl_os_poll_fill(struct zwl_server *server, struct pollfd *descriptors);
void zwl_os_poll_done(struct zwl_server *server, const struct pollfd *descriptors);

/* Called after the display is chosen and before the swapchain is made: lets the OS hand Vulkan the display's device
   (Linux: vkAcquireDrmDisplayEXT with the seat's DRM descriptor).  Returns VK_SUCCESS or the error (startup fails).
   zedBSD: VK_SUCCESS. */
VkResult zwl_os_display_acquire(struct zwl_server *server, VkPhysicalDevice physical, VkDisplayKHR display);
```

## 新しい file

| file | 中身 |
| --- | --- |
| `zwl-os.h` | 上の宣言（`<poll.h>`・`<vulkan/vulkan.h>` を include） |
| `zedbsd/handoff-zedbsd.c` | `handoff.c` を `git mv` した物（中身は include の path だけ直す） |
| `zedbsd/os-zedbsd.c` | `zwl-os.h` の zedBSD の実装（全て空）。file の先頭の注釈に「zedBSD needs nothing here: sessiond hands the seat over before the compositor starts (sessiond/seat.c), and libvulkan reaches the display itself (WS104 p006)」 |

## 手順

1. `git mv userland/desktop/wayland/handoff.c userland/desktop/wayland/zedbsd/handoff-zedbsd.c`。
2. `zwl-os.h`・`zedbsd/os-zedbsd.c` を作る。
3. 呼び出しを入れる:
   - `main.c`: `zwl_compose_open`（137）の前に `zwl_os_open`（失敗なら起動の失敗。今の他の起動の失敗と同じ扱い・同じ形の message）。`service_cleanup`（904-933）の最後に `zwl_os_close`。
   - `main.c` の `event_loop`: poll の set の数え（662-699）に `zwl_os_poll_count`、詰め（714-769）に `zwl_os_poll_fill`、poll の後（794-811 の近く、入力の device の処理の後）に `zwl_os_poll_done`。
     既存の index の計算を壊さないように、OS の entry は最後に置く。
   - `compose.c` の `zwl_compose_output_open` の、swapchain を作る前に `zwl_os_display_acquire`（失敗なら今の swapchain の作成の失敗と同じ扱い）。
     VkPhysicalDevice と VkDisplayKHR は compose の構造体にある物（`vkdemo_display_open` が選んだ物）を渡す。
4. `Makefile` の `KEILAND_ZEDBSD_SOURCES` に `zedbsd/handoff-zedbsd.c`・`zedbsd/os-zedbsd.c` を足し、`KEILAND_SOURCES` から `handoff.c` を外す。
5. `handoff.c` を名指しする script・文書を直す（`grep -rn 'wayland/handoff.c' --exclude-dir=history --exclude-dir=.claude --exclude-dir=build .`）。

## 確かめ

1. build、warning 0。
2. sessiond との受け渡しと Log Out: `criteria.sh ... C1`（`c1-boot-shutdown.sh` が greeter → login → session → Log Out → Shut Down を確かめる）と `C1 p126`。
3. compositor の基準の残り: `criteria.sh ... C2 C9`。
4. boot test。

## 完了の条件

- `wayland/` の直下に `handoff.c` が無く、`zwl-os.h` の 6 関数が共通の code から呼ばれている（`grep -n 'zwl_os_' userland/desktop/wayland/*.c`）。
- 確かめ 1〜4 が PASS。

## 結果

（実行の後に書く）
