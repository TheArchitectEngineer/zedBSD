<!-- awesome-plan project=zedbsd record=ws314-p007 -->

# ws314-p007 libbrowser の network の thread と Browser の shell の移行

- 親: [WS314](../ws.md)。設計: [design.md](../design.md) §7。Queue: q1690 / q1690-i02（担当 P2、2026-10-30）。
- Status: cleared（2026-10-30。host（TSan も）と QEMU（zedBSD amd64）で確かめた）
- Disposition: normal
- ユーザーの決定（ws.md の原文）: 5「Browserに関して、ネットワークI/OはすべてKeiland非依存のlibbrowserに閉じ込める必要があります。KeilandのBrowserは、libbrowserのシェルに過ぎない、というのが設計意図です。直接にシェルがネットワーク接続やsocketやfdを触ってはいけません。」

## 目的・受け入れ

- libbrowser が自分の network の thread を持ち、結果は「起こしてほしい」callback で呼ぶ側に知らせる。Browser の shell は socket・pollfd・fd に触らない
  （`kl_app_watch_fd` を使わない）。DNS の thread と media の engine の起こしも同じ callback に。
- TLS の遅延の初期化・誤りの理由・cookie の jar を thread から安全に（design review 7）、request の寿命と cancel（review 9）、media の fd の登録と解除（review 8）。
- build warning 0、libbrowser の host の試験（TSan も）、QEMU で Browser が page を読む。

## 実装

- 公開（`include/browser/browser.h`、`BROWSER_API_VERSION` 3）: `browser_wake`（library の thread から呼ばれる。`browser_view_process` までに 1 回）、
  `browser_view_options` に `wake`・`wake_context`。`browser_view_poll_fds` を外し、`browser_view_process(view)` は引数の pollfd を外した。
  `browser_view_timeout` は page の timer と再生中の media だけ（network の時間切れは net の thread の中）。
- `net/loader.c`: loader ごとに thread 2 つ（`browser-net` 1、`browser-resolve` 1。名前は Linux・FreeBSD だけ、zedBSD の libc に無い）。
  - 呼ぶ側の thread: `net_loader_send` が request を作り（URL の誤り・http(s) 以外はその場で返す）、lock の下の incoming の list に積み、
    制御の pipe で net の thread を起こす。`net_request_cancel` は request に印（lock の下）を付けて起こす。callback は呼ばない。
    `net_loader_dispatch` が完了の list を取り、各 request の callback を終わった順に呼んでから request を放す（cancel 済みは callback 無しで放す）。
    `net_loader_pending`（終わっていない数）・`net_loader_wait`（headless：条件変数で news か時間切れまで）。
  - net の thread: poll（制御の pipe・DNS の答えの pipe・接続・watch の fd）→ 準備できた接続の状態を進める → incoming と cancel を取る →
    cache の答え・idle の期限・時間切れ → 終わった request を完了の list へ移し、`notified` が 0 → 1 の時だけ notify（= browser_wake）。
    状態の機械・接続の pool・cache は net の thread だけが触る。
  - request の寿命: UI の thread だけが放す。cancel の後の完了は callback を呼ばない。net の thread は socket・TLS を閉じてから完了の list に返す。
  - watch（media の engine の wake）: `net_loader_watch`・`net_loader_unwatch`。net の thread は鳴った fd を読み切って notify。unwatch は
    net の thread が新しい組を poll に取るまで待つ（その後に engine を close してよい、fd の番号の再利用を避ける）。
- `net/tls.c`: library の load と context は lock の下で 1 回。誤りの理由は thread ごと（`pthread_key`。`__thread` は zedBSD の libbrowser.so の
  link で `__tls_get_addr` が無く使えなかった）。loader は https の失敗の理由を request に写し、`net_loader_dispatch` が callback の前に
  呼ぶ側の thread の理由にする（`page_failure_reason` はそのまま）。`net_tls_set_error` を足した。
- `net/cookie.c`: jar に mutex（UI の thread の document.cookie・同期の取得と net の thread の request・response）。
- `page/network.c`・`page.h`: `page_net_create(notify, context, &loader)`、`page_net_pending`・`page_net_wait`・`page_net_process(loader)`。
  `page/media.c`: engine を開いたら wake を watch、page の release で unwatch してから close。`page_media_poll_fds` を外した。
  `page/script.c` の `script_settle_network` と `view/view.c` の `view_settle_network` は wait と dispatch に。
- shell（`browser/shell/`）: `pollfd`・`kl_app_watch_fd`・`SHELL_NET_FDS`・自前の poll と 10 ms の上限を外した。wake は
  `kl_app_post(app, SHELL_POST_VIEW, NULL)`（app の pointer は atomic、窓の前の wake・post の失敗は `wake_missed` で次の dispatch を待たない）。
  窓ができたら 1 回 `browser_view_process`。`shell_release` は view（net の thread）を窓（app）より先に閉じる（kl_app_post の約束）。
- WS074 の試験（Q1 の承認、2026-10-30）: `plan/ws074/tests/host-loader.c`・`host-frame-load.c` を新しい API（`net_loader_wait`・`net_loader_dispatch`）に。
- 新しい試験: `plan/ws314/tests/loader-test.c`・`loader-test.sh [plain|tsan]`（試験の中の HTTP server で: 24 本同時、notify が dispatch まで 1 回、
  終わる前・終わって dispatch の前・他の callback からの cancel、接続の拒否、watch の鳴りと読み切り、unwatch、取得中の destroy）。
  QEMU: `plan/ws314/tests/guest-browser.sh`・`guest-browser-client.sh`・`config-amd64-browser.mk`・`pages/`。

## 確かめ（2026-10-30）

- host（`sh plan/ws074/tests/host-build.sh plain`）: warning 0。`run-loader-tests.py` 11/11、`run-http-tests.py` 15/15、`--async` 19/20、
  `run-frame-load-tests.py` 52/52、`run-fetch-tests.py` blocking 38/38・`--async` 38/38、`run-settle-tests.py` PASS。
  `sh plan/ws314/tests/loader-test.sh plain`・`tsan` PASS（TSan は 3 回くり返して 0）。
- TSan の engine（`CC` を `cc -fsanitize=thread` の wrapper にした `host-build.sh`、`build/tmp/p007-tsan-host`）: `run-loader-tests.py` 11/11、
  `run-http-tests.py --async` 19/20、`run-fetch-tests.py` 38/38×2、`run-frame-load-tests.py` 52/52、ThreadSanitizer の報告 0。
  （fetch の runner は終わりに Python の daemon thread の stderr の lock で abort したが、試験は 2 つとも 38/38 の後。）
- **既存の FAIL（この変更の前から）**: `run-http-tests.py --async` の `async-sheets`（`plan/ws074/tests/run-http-tests.py:70`、
  `("async-sheets", "/pages/sheets.html", ["background=#ffccccff", "background=#ffffddbb", "color=#ff660000"], "style")`）。
  3 つの期待の文字列がどれも style の dump に無く、dump には `background=#ffccffcc` などがある。変更前の tree（HEAD 267726590 を
  `git archive` した `build/tmp/p007-base`）でも同じ FAIL。WS074 へ（Q1）。
- zedBSD amd64（`sh plan/ws314/tests/build-zedbsd.sh p007 amd64 browser`、libbrowser・browser は amd64 だけの package）: make 0・warning 0。
  image（`plan/tools/guest/test-image.sh plan/ws314/tests/config-amd64-browser.mk build/ws314-browser-img`）: 0、libbrowser・browser の warning 0
  （warning は perl・openssh の package）。
- QEMU（2026-10-30、`sh plan/ws314/tests/guest-browser.sh build/p2-q/browser4 build/ws314-browser-img`、compositor は
  `/bin/wayland --testing --glass` 1280x800、host の `python3 -m http.server` の page を 10.0.2.2 から、判定は SSH で取った browser の log の行と
  host の server の log（console は読まない））: `ws314-browser: PASS`。first.html は同期で読み、その絵 2 つ（`picture.png?a`・`?b`）が
  loader で来て描かれ、link の click（QMP）で `ZBROWSER LOADING url=.../second.html` → `NAVIGATE ... title=WS314 second`（非同期の取得、
  net の thread の wake → `kl_app_post` で窓が起きた。入力が無くても進んだ）、その絵（`?c`）と script の XMLHttpRequest
  （`ZBROWSER CONSOLE ... ws314-done 200 ws314-data`）。画面: `build/p2-q/browser4/browser.png`（second の page と絵）。
  run1〜3 は試験の誤り（img の onload と script の `location.href` の代入による移動は libbrowser に無い、click の前の pointer の移動が
  足りない）で FAIL。1・2 でも first の絵は loader で来て描かれていた。

## 残り

- arm64・riscv64 は libbrowser・browser の package が無い（amd64 だけ）ので build していない。Linux（keiland-linux.mk）にも browser は無い。
- https の QEMU の試験はしていない（host の loader・http の試験の https で確かめた）。
