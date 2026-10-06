<!-- awesome-plan project=zedbsd record=ws121-p004 -->

# ws121-p004: `<video>` の DOM・layout・描画と最小の再生（autoplay muted）

Status: in-progress（2026-10-07 q831 P2）
Disposition: normal
Parent: [WS121](../ws.md)
Queue: q831（2026-10-07、P2）
依存: [p002](../phase002/phase.md)（libmedia）。p003（Range の loader）は後に回した: 今は remote の media を loader で全体を取る（64 MiB まで、U7 の既定）

## 範囲（正常系）

- `libbrowser/page/media.c`（新規）: page の `<video>`・`<audio>`（src か最初の `<source>` の src）ごとに libmedia の engine。local の file は path で、http(s) は page の loader で全体を取って memory の source で、data: は at once で。engine の wake を view の poll に、開いた時に video の大きさの bitmap を作り page を組み直し、絵が来るたびに bitmap に縮めて描き（`img_bitmap_renew` で新しい serial、GPU は取り直す）、page を描き直す（`media_generation`・`painted_media`）。`autoplay` で `muted` の物は開いたら再生。element は page の間 heap の root。
- layout: `<video>` は replaced（`<img>` と同じ道、絵は `page_image_of` から）、絵の無い間は 300x150（`layout/replaced.c`）。
- view: `browser_view_poll_fds` に engine の wake、`browser_view_timeout` に再生中の 10 ms、`browser_view_process` で `page_media_process` と paint だけの描き直し。
- build: libbrowser は libmedia.so を NEEDED に、package の依存に desktop/libmedia。WS074 の host の build（`plan/ws074/tests/host-build.sh`）に libmedia の source と `-ldl -lpthread`。
- 付随: `mediafile/mkv.c` の gcc の maybe-uninitialized（`cluster` の初期化）。

## 確かめ（2026-10-07、P2）

- host: `sh plan/ws121/tests/run-host-video.sh` → PASS 4（`<browser/browser.h>` だけで、`<video src=sample.mp4 autoplay muted>` の page を window の loop のように poll して CPU で描く: 開く前は 300x150 の空の箱、1 秒後に 320x240 の絵と組み直し（文書の高さ）、もう 1 秒で絵が進む）。PNG `build/review/ws121/host-video-strip.png`。
- host: `sh plan/tools/browser-component/run.sh plain` は CPU reference などの FAIL が出るが、変更の無い tree でも同じ FAIL（環境、変更前と同じ）。
- zedBSD: libbrowser.so・browser の build warning 0。style-check: media.c・replaced.c・box.c・decode.c・input.c・images.c 0、view.c は既存の 4 件だけ。

## 積み残し

[WS177 backlog-p2](../../ws177/backlog-p2.md) の WS121 の行。
