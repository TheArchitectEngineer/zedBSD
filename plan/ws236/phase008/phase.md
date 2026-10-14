<!-- awesome-plan project=zedbsd record=ws236-p008 -->

# ws236-p008: 再生の速さ: Video.mp4 を遅れなく

Parent: [WS236](../ws.md)
Status: in-progress（2026-10-15、P3）
Phase disposition: normal
実行者: P3（worktree `/home/awe/zedBSD-worktrees/p3`、branch `agent/p3-ws236b`）

## ユーザーの指示（原文、2026-10-15）

「Pi4ビデオの最適化、進めてください。Video.mp4を遅れなく再生できることをゴールとします。」

## 目標と合格

- 目標: Pi 4 実機の Video Player で、H.264 の動画を stream の速さで再生する。
- 合格（Pi 4 実機、T2）:
  1. `/home/kei/Video.mp4`（1920x1080 H.264＋AAC、6.25 秒）を最後まで再生し、`VIDEOPLAYER FRAMES` の late がほぼ 0（目安 5 以下）。
  2. 1080p30 の試験の mp4（`plan/ws236/tests/high-1080p30-10s.mp4`・`baseline-1080p30-10s.mp4`）も 30 fps（10 秒で shown 290 以上）。
  3. vkvideo-probe の 3 本が引き続き全 frame で参照と一致（速さの直しで画を壊さない）。
- 対象外: seek・長時間（耐久）・CPU の使用率の評価は記録だけ（別枠）。videoplayer の表示の道（CPU の縮小、WS122）は見るだけで、変えるなら Q1 へ。

## 出発点（p006 の実機の数、2026-10-15）

- 1080p30 は約 15 fps（shown 76・late 224）。Video.mp4 は shown 38・late 111（約 24 fps の 149 枚）。
- 計測の行: `avg_us send 150〜700 frame 31000〜37000 place 5000〜5900`、readback cached 約 2500 us × 2 / decode。
- 1 枚の直列: libmedia が decode と readback を submit して fence を待ち（send 0.2＋frame 32＋place 5＋readback 5 ≒ 42 ms）、
  その後 uncached の readback の map から 3 MB を memcpy（未計測、残りの約 20 ms の見込み）。VPU に渡る unit は常に 1 つ。

## 設計

### 1. kernel: decode を「送る」と「受ける」に分ける（vulkan-video.c）

- decode の event は unit を送るだけで終わる（待たない）。送った decode は session の「飛んでいる decode」（flight）の列に入る
  （pts、出力の image の view と status query の pool を retain、送った時刻、期限 500 ms）。列は最大 `VIDEO_FLIGHTS`。
- frame は来た順に（decode の順）受ける: pts で列の先頭と照らし、その image へ補正して写し（place）、status query を書き、
  frame を「image の cached の写し」（held）として decoder の memory に持つ。held は image ごとに 1 つ、数は出力の buffer に合わせて上限。
- 受ける時: (a) 送る時（来ている分を先に、列が満ちていれば待つ）、(b) その image を読む copy（NV12 の readback）の前（settle、
  native job の step を EINPROGRESS で返して他の session に順を譲る）、(c) RESET（全部を受けてから flush）、(d) session の解放。
- status query: flight を持つ query の END は available にせず、frame を受けた時に status と available を書く。
- Vulkan の意味の差（記録する）: decode の submission の fence は frame が image に入る前に signal し得る。image の中身を読む道
  （copy）は先に frame を待つので、command で読む限り結果は同じ。host が image の memory を map して直に読む使い方は対象外。

### 2. MMAL の client（rpi4-mmal.c）

- receive は frame の handle を返し、caller が release するまで buffer を持つ（次の receive で自動に返さない）。
- 出力の buffer の数を caller が指定（flight＋held＋1）。input の staging を 4 面に（各面は別の DMA の領域）、staging の待ちを
  timeout 0 で問えるようにする（executor を塞がない）。

### 3. libmedia: 先に submit する（vkvideo-runtime.c・vkvideo.c・h264-dpb）

- runtime に flight（bitstream・decode と transfer の command buffer・readback の buffer・fence・status の query）を
  `VKVIDEO_FLIGHTS` 本（aarch64 は 4、他は 1 で今と同じ順）。
- submit の順: decode の submission（A）を先に、readback（B）は A より 2 つ遅らせる（A_k, A_k+1, A_k+2, B_k, …）。kernel は A を送るだけで
  進むので VPU には複数の unit が渡る。B_k を待つ前に次の B を出し、userland の memcpy と executor の readback を重ねる。
- DPB: 飛んでいる picture の出力の slot は次の picture の setup に使わない（busy の mask）。slot の数は max_references＋flight。
- vkvideo.c: decode は submit だけして DPB の marking と表示の待ち列に入れ、表示の順で取り出す時に完了させる。表示の待ちを flight − 1 だけ深くする。
  seek（flush）は飛んでいる分を捨ててから待ち列を空にする。
- i915（x86）は flight 1 で、submit・待ち・写しの順は今と同じ。

### 4. 後の候補（数を見て決める）

- place（5 ms）を無くす: image へ写すのを image が読まれる時まで遅らせる。
- libmedia の uncached の readback の読み: HOST_CACHED の memory 型（WS141・libvulkan、Q1 へ）。
- frame の bulk を非同期に（executor の thread を bulk の完了まで塞がない）。

## 確かめ

- host: `plan/ws236/tests/run-h264-test.sh`・`run-mmal-wire-test.sh`・`run-vchiq-wire-test.sh`（回帰）。
- build（warning 0）: `make -j16 BUILD=build/p3/rpi4 ZEDBSD_CONFIG=config/ci/config-rpi4.mk build/p3/rpi4/vmunix`、libmedia（arm64・amd64）。
- 実機（T2、Q1 経由）: 合格 1〜3 と、計測の行（`v3d: video:`）・`VIDEOPLAYER` の行・`top`。

## 記録

（実装の後に足す）
