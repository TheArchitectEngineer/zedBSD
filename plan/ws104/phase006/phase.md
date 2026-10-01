<!-- awesome-plan project=zedbsd record=ws104-p006 -->

# ws104-p006: compositor の session と OS の hook を zedBSD の module に

Status: cleared
Disposition: normal
Parent: [WS104](../ws.md)
Queue: q520 / q520-i01
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

/* Called after the swapchain is gone: gives the display back (Linux: vkReleaseDisplayEXT).  zedBSD: nothing. */
void zwl_os_display_release(struct zwl_server *server, VkPhysicalDevice physical, VkDisplayKHR display);
```

## 新しい file

| file | 中身 |
| --- | --- |
| `zwl-os.h` | 上の宣言（`<poll.h>`・`<vulkan/vulkan.h>` を include） |
| `zedbsd/handoff-zedbsd.c` | `handoff.c` を `git mv` した物（中身は include の path だけ直す） |
| `zedbsd/os-zedbsd.c` | `zwl-os.h` の zedBSD の実装（全て空）。file の先頭の注釈に「zedBSD needs nothing here: sessiond hands the seat over before the compositor starts (sessiond/seat.c), and libvulkan reaches the display itself (WS104 p006)」 |

## 手順（`<W>` は `ws104-p006`）

**正確な編集（行・code）は [edits-compositor.md](../edits-compositor.md) の「P006」と「`zwl_os_display_release`」にある。** survey で決めた差:

- `VkDisplayKHR` が今どこにも保存されていない（`compose_display` の local の変数）ので、`struct zwl_compose`（`compose.h`）に `VkDisplayKHR display;` を足して `compose_display` で入れる。
- hook は 7 つ（上の 6 つと `zwl_os_display_release`）。`zwl_os_display_release` は `zwl_compose_output_close` の、`vkdemo_display_close` の後で呼ぶ。

1. `git mv userland/desktop/wayland/handoff.c userland/desktop/wayland/zedbsd/handoff-zedbsd.c` し、26 行の include を root からの path に。
2. 編集する（edits-compositor.md の P006）。
3. 確かめ:
   ```
   ls userland/desktop/wayland/handoff.c 2>/dev/null | wc -l                           # 0
   grep -c 'zwl_os_' userland/desktop/wayland/main.c userland/desktop/wayland/compose.c   # main.c 5 以上（open・close・poll の 3）、compose.c 2 以上（acquire・release）
   ```
4. build と warning の数え（[commands.md](../commands.md) §1）。
5. compositor の基準（commands.md §5）: results.txt が全て PASS。C1 の 2 行（`zdesktop-p126`、`c1-boot-shutdown`: greeter → login → session → Log Out → Shut Down）が
   sessiond との受け渡しを確かめる。
6. boot test（`OUTPUT=build/ws104-p006/boot`）。
7. commit: `git commit -m WIP -- userland/desktop/wayland`

## 完了の条件

- 手順 3 が条件どおり、4〜6 が PASS。

## 結果

q520-i01 cleared（2026-10-01T04:14:16.734002+00:00）。

cleared（q520）。handoff.c を zedbsd/handoff-zedbsd.c へ移動（root からの include path 以外 byte 同一）、zwl-os.h の 7 hook と zedBSD の空実装を追加。main の open/close・poll と compose の display acquire/release が境界を使う。成功した acquire は swapchain / targets の失敗と通常 close で release、未 acquire の failure / prepare-only は release しない。amd64 disk-image exit 0、自前 warning 0。新 OS module/header の style-check 0。C1/C2/C9 13/13 と boot PASS、p072 の全 6 PNG と login PNG を目視し login は提示済み。READY/GO、RELEASED/LOGOUT、wire と既存 log の形式を保持。証拠: `plan/history/ws104/q520/`（boundary-checks.json、criteria-results.txt、login.png）。物理実機・Linux は未実施。WS 全体の全文規約の最終確認は p008。実装 3bfe50b9（WIP）、実行者 main / Codex Q1、未達条件なし。


Implementation: `3bfe50b946aac0d3bcd8a3ff60b0b1bf5cd23906`（WIP）。詳細 log: `build/ws104-p006/`（一時物）。実機・Linux は未実施。GitHub へは未公開。

### Checkpoint 2026-10-01T03:16:08.789251+00:00 / q520

p006 の実装手順を実際の終了経路に合わせて確認した。OS の display-acquire に失敗した経路と、prepare のみで swapchain を作らなかった経路は display を acquire していないので release しない。acquire が成功した後の swapchain / targets 失敗は surface / swapchain を閉じて release、通常の output_close も swapchain を閉じて release する。7 hook の API・Phase 構造・依存・受け入れ条件は不変。WS104 の自律実行承認内の所有権の対を保つ技術上の補正であり、zedBSD の hook は空実装、振る舞いは不変。後続 WS105 は acquire / release の対を実装する。p006 の開始前 snapshot にこの手順を含める。

Execution started UTC: 2026-10-01T03:44:22.362063+00:00。Approval: current user, 2026-10-01「お、いい調子ですね！その調子で、ws104の完了まで自律的に作業を進めてください。」。既存 WS104 p002〜p008 全範囲、依存順の 1 Phase Queue と検証・記録・WIP commit を承認。push / GitHub 公開は承認対象外。

### Checkpoint 2026-10-01T03:58:48.645530+00:00 / q520

実装 3bfe50b9（WIP）。zedBSD OS hook 7 個の空実装と main / compose の呼出しを追加、handoff は include path 以外 byte 同一。display release は成功した acquire と対になり、未 acquire の失敗 / prepare-only では呼ばない。amd64 disk-image exit 0、自前 warning 0、new OS module/header style-check 0。boot PASS、login PNG を目視・提示済み。C1 login/Log Out、boot/shutdown、C2 geometry、C9 p052 PASS、残りの共通回帰を継続中。Phase は in-progress。Master の fg012 は既存の WS104/105 の目的から反映し、fg010 は保持。実機・Linux 未実施、GitHub 未公開。
