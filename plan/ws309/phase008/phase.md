<!-- awesome-plan project=zedbsd record=ws309-p008 -->

# ws309-p008 render を GPU の queue の唯一の持ち主に (1): fence 付きの task と queue の lock

- 親: [WS309](../ws.md)、設計 [design.md](../design.md) §4.1（レビュー 1）・§4.2・§11 の (1)、J2。Queue: q1662-i04・i05・i06（P1）。
- 状態: cleared（2026-10-29 12:2x P1 q1662-i06。QEMU Venus `task-guest.sh` PASS。arm64・riscv64 の build は b5416a6fa で exit 0・warning 0。FreeBSD の build と実機は未実施）。
- 依存: p002。

## 済み（6eab69097）

- J2 の queue の lock: `compose->queue_lock`（compose.h、`kwl_compose_open` で init、close で destroy）と `kwl_queue_lock/unlock`、`kwl_device_wait_idle`（compose.c）。
  render thread の submit と present（compose.c の job の実行、submit から最後の head の present まで）、cursor thread の submit と present（cursor-thread.c、`cursor->queue_lock`）、
  main の submit＋`vkQueueWaitIdle`（shm.c の 2 か所、import.c）と全ての `vkDeviceWaitIdle`（compose.c・heads.c・cursor-plane.c・output-rotate.c・glass.c ×4）を lock の下に。
  libvulkan（zedBSD）は queue の中で mutex を持つが、Vulkan の外部同期の規則と Linux（Mesa）のため compositor が守る。
- amd64 bin/wayland warning 0。QEMU Venus `plan/ws309/tests/font-guest.sh build/ws309/p008a ... wayland-p008a ...` PASS（回帰の確かめだけ）。

## 済み（q1662-i06、db2cce02e ほか、branch `agent/p1-ws309f`、基点 cb6c3f489）

- render thread の task（render-thread.[ch]）: main が作る message（`KWL_RENDER_TASK_DONE`、番号・command buffer・fence）を `kwl_render_task` で渡す。
  render は step の初めと、LIST の answer を取る同じ lock の下で受けた task を取り、job より先に submit（ops `submit_task`、queue の lock の下）。
  fence は ops `wait_task` で見て、渡した順に同じ message を出口へ返す（submit の失敗もその順で）。park の間も task は submit と刈り取りを続け（swapchain に触れない）、
  stop は task が全て終わるまで待つ。`kwl_render_wait_task(number, ms)` は render の側の完了の番号を待つ（main の解放の前の待ち）。`kwl_render_wait_idle` は消した。
- compose（compose.[ch]）: main だけが使う `task_pool`（TRANSIENT）、記録中の task（`task_command`・`task_open`）、`task_given`・`task_done`・`task_error`。
  `kwl_compose_task_record`・`_current`・`_flush`・`_ready`・`kwl_compose_tasks_busy`・`kwl_compose_task_wait`。flush は render への LIST の answer の直前（frame と layer の 2 か所）と
  event loop の poll の直前（`kwl_compose_give_tasks`、kwl.h）、batch_end（render の動作中）、close（stop の前）。TASK_DONE で command buffer と fence を解放（`--log-frames` で `KWL TASK done`）。
- 使う所: `image_layout`（shm.c、render の動作中は task に記録、開始の前は今のまま batch か単独の submit＋wait）、`kwl_host_image_batch_end`（回転の時は flush だけ）、
  `import_layout`（KWL_LAYOUTS_MAX を超えた分、render の動作中は task）。`struct kwl_import` に `task`。
- 設計からの変え方（判断）: draw-list の image は `pending` の印を見ない。render は渡された順（task が先、LIST が後）に submit し、barrier は submission order で後の submit に及ぶので足りる。
  代わりに、task が飛んでいる間は host の staging の書き込みをしない（`compose_may_draw`・`kwl_compose_host_writable`・`kwl_layers_tick` が task の完了まで待つ。main は待たず、TASK_DONE の wake で次の pass）。
  cursor thread は render の順の外で読むので、cursor plane（`kwl_cursor_plane_update`）は image と copy の task の完了まで box に出さない（`plane_ready`）。
  task の完了の前の image の解放（作ってすぐ消す時）は `kwl_compose_task_wait` で待つ（5 s で fatal）。p009 の遅らせた破棄で置き換える。
- 確かめ: amd64 `make ZEDBSD_CONFIG=plan/ws309/tests/config-amd64-p1.mk build/amd64/bin/wayland` exit 0・warning 0。Linux の compositor
  （`make -f userland/desktop/keiland-linux.mk KEILAND_LINUX_BUILD=build/ws309/keiland-linux build/ws309/keiland-linux/bin/wayland`、-Werror）exit 0。
  host 試験 `sh plan/ws309/tests/render-task/run.sh`（ASan+UBSan・TSan）PASS（task と LIST の順、完了の順、submit の失敗、park 中の task、stop が task を待つ）。
  QEMU Venus `plan/ws309/tests/task-guest.sh build/ws309/p008b build/ws309/img/hdd-image.img build/ws309/img/bin/wayland`: KWL TASK done 6（全て result=0）、wlshm 6 窓の
  WLSHM DONE、保持した窓の色が画面の中央の 3750/3750、FAILED・VULKAN_ERROR なし。比べ（同じ image に基点 cb6c3f489 の wayland、`build/ws309/p008b-base`、各 1 回）:
  2 つ目以降の 5 s の窓で pass_max_us 375〜392 ms（基点 492〜500 ms）、vk_outside_max_us 128〜245 ms（基点 249〜499 ms）。残る vk_outside は shm の image の破棄の quiesce（p009）と見ている（未確かめ）。
  画面は基点と時計の文字の他は同じ。
- 未実施: FreeBSD の build、実機（RDNA・VF2）、import の溢れ（33 個以上の新しい GPU buffer を 1 frame で）の経路の試験、回転（batch_end の task）の試験。
