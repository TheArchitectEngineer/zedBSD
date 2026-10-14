<!-- awesome-plan project=zedbsd record=ws236-p010 -->

# ws236-p010: decode の depth（VPU に複数の picture を重ねる）

Parent: [WS236](../ws.md)
Status: in-progress（実装・host 試験・build 済み、T2 の Pi 4 実機待ち。2026-10-15 P3）
Phase disposition: normal
実行者: P3（worktree `/home/awe/zedBSD-worktrees/p3`、branch `agent/p3-ws236d`）
依存: [ws236-p008](../phase008/phase.md)（flight の仕組み）、[ws236-p009](../phase009/phase.md)（案 0 の見当）

## ユーザーの指示（原文、2026-10-15）

「Pi4ビデオの最適化、進めてください。Video.mp4を遅れなく再生できることをゴールとします。」

この Phase は p009 の「案 0」（decode の depth 1.0 の直し）だけを扱う。案 1・2（表示の道）はユーザーの判断待ちで、ここでは触らない。

## 原因（読みで確かめた）

1. libmedia の decode の submission（A）は fence を持たない（`vkvideo-runtime.c` の `vkQueueSubmit(video->queue, 1, &submit, VK_NULL_HANDLE)`）。
2. libvulkan の `queue_enqueue`（`userland/desktop/libvulkan/queue.c`）は fence の無い submission に私的な native の fence を付けて wire に載せる（marker の job の受け皿。Venus の strict host は submission の native の fence で marker を終えるので、wire から外すと Venus が壊れる見込み → libvulkan は変えない）。
3. kernel の `vulkan-queue.c` の `execution_step` は、fence の付いた execution を、自分の command buffer の decode の frame が image に入るまで終えなかった（p008、EINPROGRESS で step を返す）。私的な fence も区別できないので、全ての decode の execution が frame を待った。
4. `render-worker.c` の executor は session の execution を FIFO で 1 つずつ終える（head が終わるまで次は動かない）。よって A_k+1 は A_k の frame まで送られず、VPU の unit は常に 1 つ（T2 の T1-663: kernel の depth 1.0、frame 31〜41 ms、libmedia の wait 28〜32 ms）。
5. 補足: Pi の V3D は queue family が 1 つ（family 0 の queue 0）で、libmedia の `queue` と `transfer_queue` は同じ queue。よって wire の queue の区別では私的な fence を見分けられない。

## 直し（p009 の候補 (b) の形、kernel だけ）

execution の「queue の上での終わり」と「終わり（fence の signal・marker の完了）」を分けた。

- `render-worker.c`・`.h`、`render-device.h`（V3D の executor、WS141 の file）:
  - execution に `settle`（任意の callback）・`timeline`・`ordinal` を足した。settle を持つ execution は全ての action が走った時点で session の queue を離れ（`executed` を数える。次の execution が走る）、session の **settling list** で settle が 0 を返すまで待ってから `end`（fence の signal・payload の解放）する。
  - executor の 1 回の番: queue の先頭があればその step。settle の無い execution を終える前に、終えてよい settling の物を古い順に先に終える（終わりの順をなるべく保つ）。queue が空で settling だけの session は、その番で最古の物を少し（2 ms）待つ。stop・fault の時は settle を聞かずに ECANCELED・EIO で終える。settle の失敗は engine の失敗として fault。
  - marker（libvulkan の job の完了）: QueueSubmit の execution が queue に入る時、その queue の timeline の予約中の marker が 1 つだけなら、それが自分の marker（libvulkan は queue の mutex を持って reserve → submit → commit する）なので `ordinal` を教える。marker は自分の execution が settling の間は完了しない（fence の signal と marker の完了の順を保つ。libvulkan の私的な fence の reset、`vkWaitForFences` の marker の待ちがそのまま正しい）。後の execution の marker は前の settling を待たない。自分の execution が分からない marker（予約が同時に 2 つ以上）は、数の範囲の settling の物を全て待つ（慎重な側）。
  - settle を持たない execution（video の無い build、fence の無い submission、全ての GPU の仕事）の振る舞いは前と同じ。
- `vulkan-queue.c`: settle の loop を消し、command buffer の job が終わった時（dispose の前）にその command buffer の decode の飛んでいる物を execution の mark に足す。全ての action の後、fence があり mark に decode があれば settle を付ける。command buffer の無い submission（`vkQueueWaitIdle` の空の submit）は飛んでいる全ての decode を mark にする。execution に queue の timeline を渡す。
- `vulkan-video.c`・`.h`: `bcm2711_vulkan_video_settle_commands` を `mark_commands`・`mark_all`・`settle_mark` に置き換えた。mark は (session の generation, pts の上限)。generation は decoder を作るたびに増える番号で、session が消えた・作り直された時の古い mark は待たない。settle は frame が来ていれば collect せずに 0、来ていなければ collect（wait の時 2 ms、他は 0 ms）して EINPROGRESS。file の先頭の説明も新しい約束に直した。

## Vulkan の意味

| 項目 | 前（p008） | 後（p010） |
| --- | --- | --- |
| decode を含む submission の fence | 自分の decode の frame が image に入ってから signal | 同じ（settling の後に signal）。host が image を map して直に読む vkvideo-probe はそのまま |
| fence の無い decode の submission | libvulkan の私的な fence のため frame まで queue を塞いだ | queue を塞がない（settling で待つのは私的な fence と marker だけ） |
| 後の submission の fence | 前の decode の frame を（queue の順で）待っていた | 前の submission の decode を待たない（p008 の設計の「自分の decode だけ」の約束どおり）。GPU の command は image の frame を先に待つので読む値は同じ |
| `vkQueueWaitIdle`・`vkDeviceWaitIdle`（空の submit と fence） | 前の decode の execution が queue を塞いでいたので結果として待った | 飛んでいる全ての decode を mark にして待つ |
| semaphore | 順の点で signal・wait（変更なし） | 同じ。payload の `pending` が下がるのは end（settling の後） |
| result status query の available | frame を受けた時（変更なし） | 同じ |
| command buffer の再利用 | job は step の中で dispose（変更なし） | 同じ。settling の execution は command buffer に触らない（mark は dispose の前に作る） |

## 確かめ（host と build、2026-10-15、P3）

| 確かめ | 結果 |
| --- | --- |
| `make -j16 BUILD=build/p3/rpi4 ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/p3/rpi4/vmunix`（VIDEO=y） | warning 0、`arm64 vmunix check: PASS`、cksum 1299747588 8216576 |
| 同じ tree を VCHIQ・VIDEO=n で（`build/p3/config-rpi4-novideo.mk`、build の中の一時の config） | warning 0、PASS |
| `sh plan/ws236/tests/run-render-settle-test.sh`（新規、render-worker.c の本物と stub、ASan/UBSan） | PASS: (a) settling の execution は次を走らせ、自分の marker だけを止める (b) 自分の execution を知らない marker は覆う settling を待つ (c) stop は settle を聞かずに ECANCELED で終える (d) settle の失敗は fault |
| `sh plan/ws141/tests/vulkan-device-host-test.sh build/p3/ws236d-devhost`（WS141 の executor・queue・fence の回帰、非 video） | PASS 31 行（変更前と同じ） |
| `run-h264-test.sh`・`run-mmal-wire-test.sh`・`run-vchiq-wire-test.sh` | all PASS |
| `plan/tools/style-check.py`（変えた file） | 新しい指摘 0（render-worker.c は以前の指摘が 2 つ減った） |
| `git diff --check` | 問題なし |

- libvulkan・libmedia は変えていない（`queue.c` の私的な fence はそのまま）。i915・Venus の kernel も変えていない。amd64 の libvulkan・libmedia の build と WS202 の host 試験は、変えた file を含まないので流していない。
- 未実施: Pi 4 実機（下の T2 への依頼）。

## T2 への依頼（Pi 4 実機 kei@10.0.30.2、QEMU 不要、約 30 分）

1. build（agent/p3-ws236d、または Q1 の merge の後の main）: `make -j16 BUILD=build/<T>/rpi4 ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/<T>/rpi4/vmunix`（warning 0）。差し替えは kernel だけ（libvulkan・libmedia は main のまま）。
2. Pi: `/boot/boot0/vmunix` を退避して差し替え、sync、reboot。
3. 起動後 `dmesg | grep -E 'rpi4-(vchiq|mmal)|v3d: (vk|video|EIO|execution failed)'` を保存。`v3d: execution failed` が無いこと。
4. vkvideo-probe 3 本（p006 と同じ、`--expect --time --dump`）: rc 0、全 frame MATCH、dump-compare で differing 0（fence の付いた decode を host が直に読む道）。
5. Video Player: `XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 videoplayer --timeout-s=15 /home/kei/Video.mp4 2>/tmp/vp-video.log`、同じく `--timeout-s=25` で `/home/kei/ws236/high-1080p30-10s.mp4`・`baseline-1080p30-10s.mp4`。再生中に別の ssh で `top -b -n 3 -d 2`。
   - 報告: 各 log の `MEDIA backend=`・`FRAMES`・`DONE`・`Vulkan Video:`・`VIDEOPLAYER DRAW` の行、top、再生の後の dmesg の `v3d: video:` の全行と `skipped`・`no decode in flight`・`rpi4-mmal: stall`・`v3d: EIO`・`v3d: execution failed` の行。
6. 合否（この Phase）: (a) 4 が PASS、(b) kernel の `v3d: video:` の **depth が 1.0 を超える**（目安 2 以上）、(c) libmedia の `wait` が p008 の 28〜32 ms より下がる、(d) 再生が最後まで進み、videoplayer・compositor が落ちず、`v3d: execution failed` が無い。Video.mp4 の late・1080p30 の shown は記録だけ（表示の CPU の約 60 ms／枚が残るので、遅れなくなるのは案 1 の後の見込み）。
7. 止め所: Pi が応答しなくなったら止めて報告（再起動の要否はユーザー）。終わったら Q1 の指示で kernel を戻すか残す。

## 残り・危険

- render-worker.c は V3D の全ての仕事（compositor を含む）の executor。settle を持たない execution の道は前と同じに保ったが、Pi の desktop 全体の回帰（compositor の描画・窓の操作）は実機で見る必要がある（上の 5 の再生中の画面と、`v3d: execution failed` の無いこと）。
- `vkQueueWaitIdle` の空の submit は飛んでいる全ての decode を待つ。後の submission の fence は前の submission の decode を待たない（Vulkan の仕様より緩い、p008 からの約束。GPU の command・probe は影響を受けない）。
- 表示の道（videoplayer の CPU の NV12→RGB と縮小、約 60 ms／枚）は案 1（ユーザーの判断待ち）。この Phase だけでは Video.mp4 の late は大きくは減らない見込み。
