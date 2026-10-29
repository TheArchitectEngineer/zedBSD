<!-- awesome-plan project=zedbsd record=ws309-p012 -->

# ws309-p012 display の thread（T3）: swapchain・modeset・回転・hotplug の列挙・scanout の可否と claim・release・backlight、seat の pause

- 親: [WS309](../ws.md)、設計 [design.md](../design.md) §4.3（T3）・§4.1 の park と drain の行（レビュー 2）・§4.7（回転の壁紙）・§6（ゲームモード）。Queue: q1662-i08（P1）。
- 状態: in-progress（2026-10-29 q1662-i09 P1、branch `agent/p1-ws309i`、基点 518fd6e33）。
- 依存: p009（cleared）。p010 から回転の壁紙（`kwl_glass_size_prepare`、2026-10-29 Q1 の判断）。
- 受け入れに足す物（2026-10-29 Q1 の決定）: BUG-353（RTX 2070 で mview をゲームの表示（mailbox）で一度動かすと、後の mview の window が写らない）をこの中で直し、
  `plan/ws291/tests/rtx-mview-order.sh` で確かめる（10.0.10.3 の枠は Q1 が割り当てる）。

## 今（基点 09d969ab2、branch `agent/p1-ws309h`）の待ちの点

| 所 | 何が main で待つか |
| --- | --- |
| `kwl_compose_output_close`（compose.c:645） | `kwl_compose_park`（render の frame の終わりを最長 2 s 待つ）、`kwl_cursor_plane_close`（cursor thread の join、cursor-plane.c:219 の device の idle）、`kwl_device_wait_idle`、swapchain・surface の破棄、display の release |
| `compose_output_open_parked`（compose.c:~495） | `kwl_compose_output_prepare`（surface・pass・pipelines）、`kwl_os_display_acquire`、`vkdemo_display_create_swapchain*`、targets・turn・scene、`kwl_heads_config_load`（displays.conf の読み）、cursor plane の swapchain、layers |
| 呼ぶ所 | output-switch.c:230・242・256（hotplug の移動と戻し）、scanout.c:314（ゲームモードに入る）と leave の後の次の pass の open（display.c:645）、backend-host.c:51（seat の pause）、handoff.c:146（終了、待ってよい）、compose.c:1158-1160 |
| 回転（output-rotate.c:703-722） | `kwl_glass_size_prepare`（壁紙の read・decode・描画と layout の submit の待ち）、park、`kwl_device_wait_idle` |
| head（heads.c:321・1013・1076-1078） | park、head の swapchain の作成、`kwl_device_wait_idle` |
| backlight（displays-shell.c:417・713・681、backend-host.c:417・627） | `kl_backend_backlight_open/get/set`（zedBSD は ioctl、Linux は sysfs の file） |

- QEMU の計り（p010 の `resize-guest.sh`）: 出力の移動の区間の `KWL PERF pass` の pass_max_us は 2.29〜2.30 s（main の close・open）。この Phase の前後の比べに使う。

## 実装の手順（次の起動はここから。1 段ごとに build・host・QEMU で確かめて commit）

1. **非同期の park と drain**（render-thread.c・compose.c）: `kwl_render_park_request`（待たない）と、render が自分の submit の fence を全て待ってから parked を出口に置き wake で知らせる形
   （DRAINED。design §4.1 の park ＋ `vkDeviceWaitIdle` の行）。main の状態 `display_busy`（IDLE | BUSY）: BUSY の間は scheduler が output の frame を出さない。
   BUSY の間に来た次の変更は 1 つだけ覚える（最新で上書き）。終わりの unpark は今どおり。cursor の submit は cursor thread の stop と join で覆う（cursor の周りを変える前に Q1 に知らせる）。
2. **T3 の lane `display`**（新しい display-thread.c、`kwl_lane_start(..., "display", 1, wake)`、compose の open の後・render の後に起動、§8 の順で止める）: job の種類
   `OUTPUT_CLOSE`（swapchain・surface の破棄、`kwl_os_display_release`）、`OUTPUT_OPEN(display, mode, rotation)`（acquire、surface、swapchain、images の写しを返す）、
   cursor plane の swapchain の作成と破棄（`kwl_cursor_plane_close/open` を thread の部分と main の部分（box と host image）に割る）。T3 は main の struct の field を書かず、完了の message に handle を入れる。
   main の done: targets・turn・scene・layers・glass・heads の config・`server->width/height`・window・pointer・protocol の event（今の `compose_output_open_parked` の後半）。
   pipelines（`kwl_compose_output_prepare`）は format が変わらない限り作り直さない（今の `output_prepared`）。作る時は main（queue を使わない）で、時間を `KWL STARTUP step=output-pipelines` で見る。
3. **呼ぶ所を T3 に**: output-switch（移動・戻し・`output_fail_back` は done で）→ scanout の enter・leave（`kl_backend_scanout_possible/open/close` を T3 の依頼に、答えの pass で main が判断）→
   seat の pause（backend-host.c:36-52、答えは T3 の完了の done で）→ head の open・close（heads.c）。handoff（終了）は今のまま待ってよい（原則 1 の例外）。
4. **回転**（output-rotate.c）: T3 の手順の中で swapchain、壁紙は p010 の A1（decode）→ T9（縮小と blur）。結果が来るまで古い壁紙を引き伸ばして描く（p010 の resize と同じ）。
   今の `kwl_glass_size_prepare`・`kwl_glass_size_swap` の「失敗したら全て元のまま」の契約は、T3 の失敗の message と main の done で保つ。
5. **backlight と列挙**: T3 の job。main は backlight の値の写し（T3 が open と set の後に読む）を持ち、Settings の get にはその写しで答える。set の失敗は `KWL DISPLAYS light error=` の log と
   次の get の値で見える（今は同期で client に errno を返している所があるので、変える時は client への答えの形を phase.md に書いてから）。列挙（`output_enumerate`）は T3、hotplug の 250 ms の tick は main のまま。
6. **BUG-353**: 3 の scanout の leave の書き直しの時に、leave の後の window の mode の open（display.c:645）と nvrtx の release・再 import の順を読む。原因が compositor の外（nvrtx）なら Q1 に返す。
7. 試験: `resize-guest.sh`（移動の区間の pass_max_us が下がるか、frame が止まらないか）、回転の QEMU（`--rotate` か Settings の回転、試験の道を探す）、ゲームモードの QEMU（gpu-forge の image の mview の spin が
   Venus で scanout を取れるか、取れなければ `KWL SCANOUT direct=0 reason=backend` の経路の確かめだけ）、`grave-guest.sh`・`hand-guest.sh` の回帰、RTX `rtx-mview-order.sh`（Q1 の枠）。

## q1662-i09 の進め方と判定の条件（2026-10-29 14:3x、走らせる前に決めた物）

- 形: 表示の変更を「操作」（begin → park を待たずに頼む → render が drain して PARKED を wake で返す → main の段・T3 の段 → end）にする。
  操作の間（display_busy）は frame・layer を want しない。T3 は lane `display`（1 本）。exit・handoff・seat の pause・まだ移していない同期の呼び手は、
  同じ段を event loop の thread でその場で走らせる（inline、待ってよい例外）。走っている操作の途中で同期の呼び手が来たら、操作の終わりまで待つ（settle）。
- T3 に置く物: cursor thread の stop と join、cursor plane の Vulkan の object と swapchain の破棄・作成、`vkDeviceWaitIdle`（T3 は待ってよい。cursor の present の分）、
  出力の swapchain・surface の破棄、`kwl_os_display_acquire/release`、swapchain の作成（fallback を含む）、ゲームモードの可否の問い（`kl_backend_scanout_possible`）。
- main に残す物（この起動）: surface の作成と pipelines（`kwl_compose_output_prepare`、format が変わらなければ一度）、targets・turn・scene・layers・glass・heads の閉じ、
  `kwl_heads_config_load`（最初の open の一度）。ゲームモードの claim・release（`kl_backend_scanout_open/close`）は main のまま（scanout-zedbsd.c の static の
  `scanout_open` を main の `kwl_scanout_forget` が読むため。p013 で lock と一緒に移す、2026-10-29 Q1 の了承）。
- 判定（QEMU Venus、P1 の名前・port・directory、compositor の log の file を SSH で読む。console・serial の log は使わない）:
  1. host: `plan/ws309/tests/render-task/run.sh` PASS（ASan+UBSan・TSan、E: park は前の task の完了まで答えず、PARKED は完了の後に来る）。
  2. build: amd64 bin/wayland・Linux（keiland-linux.mk、-Werror）exit 0・warning 0。
  3. `resize-guest.sh`（出力の移動 2 回）PASS。加えて移動の区間の `KWL PERF pass` の pass_max_us を前（p010 の 2.29〜2.30 s）と比べて記録する
     （判定: 移動の区間の pass_max_us が 1 s 未満。swapchain の作成が main から出た事の確かめ）。
  4. `grave-guest.sh`・`hand-guest.sh` の回帰 PASS。
  5. ゲームモード: QEMU で mview の spin が scanout を取れるかを見る。取れなければ `KWL SCANOUT direct=0 reason=backend` の経路（可否の問いが T3 から答える）と、
     その後に window mode が続く事の確かめだけ。
  6. RTX（10.0.10.3、Q1 の枠）: `plan/ws291/tests/rtx-mview-order.sh` で BUG-353 の再現が消える事（o1 の a・c・e・f が全て出る）。
     （2026-10-29 15:0x 具体化、走らせる前）script は新しい `plan/ws309/tests/rtx-bug353.sh OUT VMUNIX WAYLAND RESTORE`（rtx-mview-order.sh と同じ順に
     各段の session.log を回収、/bin/wayland は rename で入れ替え、終わりに元の compositor と vmunix-va1 を戻して cksum で照合、Debian の RTX の vfio-pci を数える）。
     kernel は P2 が再現した vmunix-p010a4（63ebcaf71、cksum 869726610。stick の libvulkan と組で再現した物。compositor だけを変える比べ）。
     判定: a・c・f の shot の over15（Linux の参照と 16 段以上違う画素）が 1000 未満（P2 の o1 は a=2・c=2073252・f=2）、e（5000）は参照が無いので画像を見て記録、
     KWL FAILED・VULKAN_ERROR 無し。
  5'. 5 の script は新しい `plan/ws309/tests/scanout-guest.sh OUT IMAGE WAYLAND`（2026-10-29 15:0x、走らせる前）: 判定は compositor の READY、spin の window に
     KWL SCANOUT の行（direct=1、または direct=0 reason=backend … output=kept）、その後の still の mview が最後まで（MVIEW DONE run=still）、capture が画（spread 24 以上）、
     compositor が生きている、KWL FAILED・VULKAN_ERROR 無し。
- hand-guest の 1 回目の FAIL（2026-10-29 15:0x）: 3 画の認識の答えが無く 4 画だけ。見込みの原因: 最初の認識で templates を読む時間（QEMU）が 3 画の後の固定の 1.5 s を超え、
  4 画目の job が同じ key の 3 画の job を置き換えた（最新だけの決まり）。試験の直し（Q1 の指示）: 4 画目の前を、時間でなく log の
  `KWL OSK hand recognize strokes=3 ` を待つ形（上限 10 s）にした（`plan/ws309/tests/hand-guest.sh`）。
- 回転・head の open/close・backlight・列挙を T3 へは、この起動で届かなければ残りに書く。

## 注意

- 同時に P2（WS242）・P5（WS250）・P6（WS243）が nvrtx を触る。compositor（userland/desktop/wayland）は P1 だけ。libkeiland-backend-zedbsd の scanout-zedbsd.c を変える時は先に Q1 に知らせる。
- 判定の条件を変える時は、変える前に理由をこの phase.md に書き、Q1 に知らせる（2026-10-29 Q1）。
