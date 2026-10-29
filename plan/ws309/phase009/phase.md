<!-- awesome-plan project=zedbsd record=ws309-p009 -->

# ws309-p009 render を GPU の queue の唯一の持ち主に (2)(3): 遅らせた破棄、quiesce と main の vkDeviceWaitIdle を消す

- 親: [WS309](../ws.md)、設計 [design.md](../design.md) §4.1（レビュー 3）・§4.6・§4.7・§11 の (2)(3)。Queue: q1662-i06（P1）。
- 状態: in-progress（2026-10-29 12:3x P1。遅らせた破棄の仕組みと shm の image まで）。
- 依存: p008。

## 済み（b5416a6fa、branch `agent/p1-ws309f`）

- 仕組み（compose.[ch]）: `struct kwl_grave`（record・その時に渡した描画の数・破棄の関数）、`compose->drawings_given`（frame と layer の LIST を渡す 2 か所で増やす）・
  `drawings_done`（`compose_completed` で増やす。描画は渡した順に終わる）・`graves`。`kwl_compose_bury(compose, import, destroy)`: 渡して終わっていない描画が無く、
  image の task（p008）も終わっていれば今壊す、そうでなければ墓に置き、`kwl_compose_poll` の後の `compose_graves` が条件の揃った物を壊す（待たない）。
  close は device の idle の後に全ての墓を壊す。墓の memory が無い時は fatal（使用中の物を壊せないため）。`--log-frames` で `KWL GRAVE destroyed=N`。
- 墓は record を持ち続ける（memset しない）: render が replay で list の copies の record（`struct kwl_import *`）を読むため。
- shm（shm.c）: `kwl_shm_image_destroy` の `kwl_compose_quiesce` を消した。host image の list から外して（`image_unlink`、以後の frame は staging を取らない）`image_bury` で埋める。
- 確かめ: amd64 bin/wayland warning 0、arm64（`config/ci/config-rpi5.mk` の build/rpi5/bin/wayland、sysroot-arm64 を作って）・riscv64（`config-visionfive2-keiland.mk` の
  build/visionfive2/bin/wayland）exit 0・warning 0。QEMU Venus `task-guest.sh build/ws309/p009b ... build/amd64/bin/wayland` PASS（1 回目の p009a は guest が 240 s で SSH に
  答えず。image は p008 と同じで wayland は起動の後に入れるので、この変更とは無関係と見て同じ条件で 1 回やり直した。原因は未調べ）。ただし graves=0: この試験では shm の image の
  破棄は全て描画の無い時（client の切断の quiesce の後）で、墓の経路は通っていない（未確かめ）。vk_outside_max_us は 225〜246 ms のまま（p008 と同じ）で、wlshm の終わりの
  client の切断の `kwl_compose_quiesce`（objects.c:561）が残りの主と見ている。

## 残り（次の起動はここから）

1. 墓の経路の試験: 大きさを変える shm の client（surface_upload で古い image を埋める）か、frame の間に wl_surface を壊す client。
2. 他の解放を墓に: `kwl_host_image_release`（cursor・cursor plane の copy・glass）、`import_release`（import.c、GPU buffer の import、objects.c の retire の list との関係を読む）、
   descriptor set の返し（`kwl_compose_set_put` は墓の破棄の中で行われる）。p008 の `kwl_compose_task_wait`（image_release・import_release の頭）は墓に置き換えて消す。
3. client の切断の quiesce（objects.c:561、backend-host.c:50、main.c:1305）: frame の slot が持つ buffer・callback と、client の object の寿命。最大の残りの vk_outside。
4. glass の二重の image（design §4.1・§4.7）: 壁紙の `loader_copy`（glass.c:866 の quiesce＋`kwl_device_wait_idle`）、大きな数字（glass.c:2973）、glass.c:583・626 を、
   新しい image を作り古い物を墓に置く形に。
5. QEMU の回帰（task-guest.sh と、壁紙の変更・時計の数字・client の切断）と `KWL PERF pass` の vk_outside_max_us の比べ（基点 cb6c3f489 の wayland を同じ image で）。
