<!-- awesome-plan project=zedbsd record=ws309-p008 -->

# ws309-p008 render を GPU の queue の唯一の持ち主に (1): fence 付きの task と queue の lock

- 親: [WS309](../ws.md)、設計 [design.md](../design.md) §4.1（レビュー 1）・§4.2・§11 の (1)、J2。Queue: q1662-i04（P1）。
- 状態: in-progress（2026-10-29 P1。queue の lock は済み、fence 付きの task は未着手）。
- 再開点（2026-10-29 05:0x P1 q1662-i05、branch `agent/p1-ws309e`、ラップアップ）: source の変更は無い。下の「残り」1 から。
  - build の手順（確かめた）: worktree の `build/` に main の `build/` の `NoctLang`・`distfiles`・`llvm`・`llvm-build`・`llvm-source`・`sources`・`ws035-sq-venus` の symlink を作り、
    `make ZEDBSD_CONFIG=plan/ws309/tests/config-amd64-p1.mk build/amd64/bin/wayland -j16`（sysroot は make が作る。基点 11781c470 で exit 0・warning 0）。
  - 調べた事（実装の案、未実装）: task の完了は FIFO の順に刈るので、main は `compose->task_done`（終わった task の最後の番号）を持ち、image に `task`（その遷移の task の番号、0 は無し）を置けば
    「`task == 0 || task <= task_done` なら ready」で済む（TASK_DONE で list を歩かない）。render は step の初め（park の外）に受けた task を全て queue の lock の下で fence を付けて submit
    （answer の list より先に受けた task が job より先の submission order になり、barrier の scope は submission order で queue の中の後の submit に及ぶ）、BLOCKED で task が in flight なら
    最古の task の fence を KWL_RENDER_POLL_NS 待って WORKED（render_main が cond で眠り込まない）。ops に `submit_task`・`wait_task` を足す（compose.c:1128 の ops の組み立て）。
    pending の image を外す所: draw-list・cursor box・cursor plane の copy・frame の host の copy の list（shm.c の `list->copies`、`compose->host_images`）。
    image の破棄が task の in flight と重なる時（shm.c:198 の quiesce の前）の扱いは p009 の遅らせた破棄までは quiesce に頼る（要確認: quiesce が task の fence を覆うか）。
  - 前の P1 の未実施（まだ未実施）: p008 の queue lock の後の arm64・riscv64 の build、Linux/FreeBSD の build（kl_backend_options に field を足した分）。
- 依存: p002。

## 済み（6eab69097）

- J2 の queue の lock: `compose->queue_lock`（compose.h、`kwl_compose_open` で init、close で destroy）と `kwl_queue_lock/unlock`、`kwl_device_wait_idle`（compose.c）。
  render thread の submit と present（compose.c の job の実行、submit から最後の head の present まで）、cursor thread の submit と present（cursor-thread.c、`cursor->queue_lock`）、
  main の submit＋`vkQueueWaitIdle`（shm.c の 2 か所、import.c）と全ての `vkDeviceWaitIdle`（compose.c・heads.c・cursor-plane.c・output-rotate.c・glass.c ×4）を lock の下に。
  libvulkan（zedBSD）は queue の中で mutex を持つが、Vulkan の外部同期の規則と Linux（Mesa）のため compositor が守る。
- amd64 bin/wayland warning 0。QEMU Venus `plan/ws309/tests/font-guest.sh build/ws309/p008a ... wayland-p008a ...` PASS（回帰の確かめだけ）。

## 残り（次の起動はここから）

1. main の自分の command pool（`compose->task_pool`、main の thread だけが使う。今の `compose->pool` は render thread の frame の物）。
2. render の message に task（`KWL_RENDER_TASK`: command buffer、fence、印）を足す（render-thread.[ch]）。render は受けた task を後に受けた frame・layer の job より先に、
   queue の lock の下で fence を付けて submit し、fence を `vkGetFenceStatus`（または次の poll）で見て、終わったら出口へ TASK_DONE（main の wake）。main は command buffer と fence を解放。
3. `image_layout`（shm.c、今は `kwl_render_wait_idle` ＋ submit ＋ `vkQueueWaitIdle`）を task に: host の書き込みを済ませてから遷移を task_pool に記録して渡す。
   image（`struct kwl_import` か host image）に `pending` を足し、TASK_DONE まで draw-list・cursor box・cursor plane の copy に出さない（呼ぶ所: shm.c:893・896 の host image の作成 ← shm の初回・size の変化、
   cursor.c:759 の cursor の形、cursor-plane.c:644 の copy の host image、glass の画像）。起動（`setup_batching`、READY の前）は今のまま。
4. `import_layout`（import.c:293・310 の `kwl_render_wait_idle`、KWL_LAYOUTS_MAX を超えた時）も同じ task に。
5. `kwl_render_wait_idle` の呼び手が無くなれば消す（設計 §4.1 の表の最後の行）。
6. 試験: host（render thread の task の順と TASK_DONE、可能なら）、QEMU Venus（shm の client（weston-simple-shm 相当の probe）、cursor の形の変更、`KWL PERF pass` の pass_max_us）。
   注意: cursor thread は frame の無い時も staging を GENERAL で読む（設計 §4.1）ので、cursor の形の image は TASK_DONE まで box に出さない。
