<!-- awesome-plan project=zedbsd record=ws309-p009 -->

# ws309-p009 render を GPU の queue の唯一の持ち主に (2)(3): 遅らせた破棄、quiesce と main の vkDeviceWaitIdle を消す

- 親: [WS309](../ws.md)、設計 [design.md](../design.md) §4.1（レビュー 3）・§4.6・§4.7・§11 の (2)(3)。Queue: q1662-i06・i07（P1）。
- 状態: cleared（2026-10-29 13:1x P1 q1662-i07。QEMU Venus `grave-guest.sh`・`task-guest.sh` PASS。実機・FreeBSD の build は未実施）。
- 依存: p008。

## 済み（b5416a6fa、branch `agent/p1-ws309f`）

- 仕組み（compose.[ch]）: `struct kwl_grave`（record・その時に渡した描画の数・破棄の関数）、`compose->drawings_given`（frame と layer の LIST を渡す 2 か所で増やす）・
  `drawings_done`（`compose_completed` で増やす。描画は渡した順に終わる）・`graves`。`kwl_compose_bury(compose, import, destroy)`: 渡して終わっていない描画が無く、
  image の task（p008）も終わっていれば今壊す、そうでなければ墓に置き、`kwl_compose_poll` の後の `compose_graves` が条件の揃った物を壊す（待たない）。
  close は device の idle の後に全ての墓を壊す。墓の memory が無い時は fatal（使用中の物を壊せないため）。`--log-frames` で `KWL GRAVE destroyed=N`。
- 墓は record を持ち続ける（memset しない）: render が replay で list の copies の record（`struct kwl_import *`）を読むため。（i07 で list の copies を値にし、墓は record の写しを持つ形に変えた）
- shm（shm.c）: `kwl_shm_image_destroy` の `kwl_compose_quiesce` を消した。host image の list から外して（`image_unlink`、以後の frame は staging を取らない）`image_bury` で埋める。
- 確かめ: amd64 bin/wayland warning 0、arm64（`config/ci/config-rpi5.mk` の build/rpi5/bin/wayland、sysroot-arm64 を作って）・riscv64（`config-visionfive2-keiland.mk` の
  build/visionfive2/bin/wayland）exit 0・warning 0。QEMU Venus `task-guest.sh build/ws309/p009b ... build/amd64/bin/wayland` PASS（1 回目の p009a は guest が 240 s で SSH に
  答えず。image は p008 と同じで wayland は起動の後に入れるので、この変更とは無関係と見て同じ条件で 1 回やり直した。原因は未調べ）。ただし graves=0: この試験では shm の image の
  破棄は全て描画の無い時（client の切断の quiesce の後）で、墓の経路は通っていない（未確かめ）。vk_outside_max_us は 225〜246 ms のまま（p008 と同じ）で、wlshm の終わりの
  client の切断の `kwl_compose_quiesce`（objects.c:561）が残りの主と見ている。

## 済み（q1662-i07、branch `agent/p1-ws309g`、基点 9b40cc748、4feb84d18・cccd4b73b・4b9d7d3d7）

- draw list の copies を値で持つ（draw-list.[ch] の `struct kwl_list_copy`: staging・image・幅・行の範囲）。main が take の時に埋め、render の replay は image の record を読まない。
  `struct kwl_import` の `copy_first`・`copy_last` は不要になり消した。これで墓は record の写しを持ち（`struct kwl_grave` の `record`）、元の場所は bury の後すぐ再利用・free できる。
- `kwl_compose_bury`: 今壊すのは、render thread が動いていない時（起動の前・close の stop の後）か、書いている描画が無く（`drawing_open`、`kwl_compose_list_begin` で立て、渡した時と失敗の時に下ろす）、
  渡した描画が全て終わり、record の task も終わっている時。墓の描画の数は書いている描画を含む（walk の中で埋めた image がその描画にも使われ得るため。例: greeter の時計の大きな数字）。
- 解放を全て墓へ: `kwl_host_image_release`（unlink ＋ bury。shm の image・矢印・cursor の形・cursor plane の copy・glass の image の全て）、import（`import_let_go`: layout の待ちから外して bury。
  adopt の失敗と `kwl_import_destroy_with`）。descriptor set の返し（`kwl_compose_set_put`）は墓の破棄の中。`kwl_compose_task_wait`・`kwl_render_wait_task`（render-thread.[ch] の `task_done` も）を消し、
  host 試験 `plan/ws309/tests/render-task/` を take の数で確かめる形に直した。
- client の切断（objects.c の `kwl_client_destroy`）: `kwl_compose_quiesce` を `kwl_compose_client_gone`（compose.c）に置き換え。fatal にした後、全ての slot の hold からその client の buffer を返し
  （`kwl_buffer_put`、completion は空の場所を飛ばす）、frame callback を list から外して告げずに retire する。image は墓が描画の終わりまで持つ。`KWL CLEANUP done` の `quiesce=` はこの外す時間。
- glass（glass.c）: 壁紙の変更（`kwl_glass_wallpaper_poll`）と landscape（`kwl_glass_landscape`）は `wallpaper_target`: host の staging を書いてよい時（`kwl_compose_host_writable`）は今の image に書き、
  そうでなければ新しい image（`wallpaper_renew`・`glass_image_renew`、版は前の物 +1、描き方を引き継ぐ）を作り古い物を墓へ。`kwl_glass_resize` は waits を消すだけ（解放は墓）。
  大きな数字の worker の無い経路も同じ（書けない時だけ新しい image）。`KWL GLASS wallpaper ... ms=N images=same|new` を足した。glass.c の `kwl_compose_quiesce`・`kwl_device_wait_idle` は 0 か所。
- 設計からの変え方（判断）: design §4.1 は「常に新しい image」だが、QEMU Venus では image の作成（4 MB ×4 の確保）が main で約 330 ms かかり（基点の quiesce＋idle＋copy は 86〜103 ms）、
  書いてよい時は今の image に書く方が待ちも確保も無い（実行中の frame は sampled の image を読み、次の copy は queue の順で後）。書けない時だけ二重の image にした。
- 残る main の quiesce と `vkDeviceWaitIdle`: seat の pause（backend-host.c）と park の手順（compose.c・heads.c・output-rotate.c・cursor-plane.c・`kwl_glass_size_swap`）は p012（T3 の drain）、終了（main.c）は原則 1 の例外。
- 確かめ（QEMU と実機を分ける。実機は未実施）:
  - build: amd64 `make ZEDBSD_CONFIG=plan/ws309/tests/config-amd64-p1.mk build/amd64/bin/wayland`、arm64 `config/ci/config-rpi5.mk` の build/rpi5/bin/wayland、riscv64
    `config/ci/config-visionfive2-keiland.mk` の build/visionfive2/bin/wayland、Linux `userland/desktop/keiland-linux.mk`（-Werror）: 全て exit 0・warning 0。
  - host: `sh plan/ws309/tests/render-task/run.sh`（ASan+UBSan・TSan）PASS。
  - QEMU Venus（image `build/ws309/img`、`config-amd64-p1.mk` に keiland-settings を足して作り直し）: 新しい `plan/ws309/tests/grave-guest.sh`（wlshm 6 窓の開閉、cursor の形の変化、
    窓の間に keiland-settings で壁紙を選び、landscape に戻す）`build/ws309/p009d` PASS: graves=2、壁紙 `ms=330 images=new`・landscape `ms=10 images=same`、`KWL CLEANUP done` 7 件全て quiesce=0、
    保持した窓の色 3750/3750。1 回目 `p009c`（全て新しい image の版）も PASS（graves=4、壁紙 321・328 ms）。`task-guest.sh` `build/ws309/p009e` PASS（tasks=6、graves=0）。
  - 比べ（同じ image、基点 9b40cc748 の wayland `build/ws309/base/wayland`、`build/ws309/p009c-base`、各 1 回）: 基点は graves=0 で grave-guest は FAIL（墓が無いので期待どおり）、
    2 つ目以降の 5 s の窓の pass_max_us は基点 361〜409 ms・新 366〜399 ms、vk_outside_max_us は基点 127〜261 ms・新 122〜250 ms で差は無い。切断の quiesce は基点で最大 25 ms → 0。
  - 残る遅さ（未確かめ・推定）: `KWL CLEANUP done` の surfaces= が両方で約 430 ms、vk_outside の約 230 ms は Vulkan の object の作成・破棄（Venus の確保・解放の往復）と見ている。
    待ちではないので p009 の範囲の外。p017 の計測と実機で確かめる。
- 未実施: 実機（RDNA・VF2）、FreeBSD の build、大きな数字の worker の無い経路（試験の経路が無い）、`kwl_glass_resize` の経路（出力の大きさの変化）、GPU buffer の client の切断（wlshm は shm だけ）。
