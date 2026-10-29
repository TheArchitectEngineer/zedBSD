<!-- awesome-plan project=zedbsd record=ws314-p008 -->

# ws314-p008 `kl_app_watch_fd`・`kl_instance_fd` を公開の API から外す（KL_VERSION 102）

- 親: [WS314](../ws.md)。設計: [design.md](../design.md) §0・§12。Queue: q1690 / q1690-i02（担当 P2、2026-10-30）。
- Status: cleared（2026-10-30。zedBSD 3 arch・Linux・FreeBSD の build、host、QEMU で確かめた）
- Disposition: normal
- ユーザーの決定（ws.md の原文）: 1「fdはWindowsに移植できないので、このインタフェースは許容できないです。」、
  4「Settingsも、kl_instance_*からfdのAPIを削除して、instanceを指定してそのappにイベントをポスト可能にするのがいいです。」、
  6「Keilandデスクトップから、UNIX依存を削除する必要があります。それは設計意図から逸脱しています。」

## 目的・受け入れ

- 公開の API（`include/keiland/`）から `kl_app_watch_fd`・`KL_APP_FD`・`KL_APP_FD_READ`・`_WRITE`・`_HANGUP`・`KL_APP_FDS_MAX`・
  `kl_app_event.fd`・`kl_instance_fd` を外す。KL_VERSION 102。
- 利用者 0（grep）、build warning 0（zedBSD 3 arch と userland/tests の probe・installer、Linux、FreeBSD）、exports。

## 実装

- `keiland_app.h`: 上の定数・field・宣言を外した（event の kind の 2 は欠番と comment）。`kl_app_dispatch` と header の説明を直した。
  `keiland_instance.h`: `kl_instance_fd` を外した。`keiland_version.h`: 102 と履歴の行。`exports.map`: 2 つの symbol を外した。
- `libkeiland/ui/app.c`: watch の表・`kl_app_watch_fd`・`app_fd_events` を外し、`kl_app_dispatch` の poll は compositor と post の wake の 2 つ。
  `ui/window.h`: `fds`・`fd_events`・`fd_count` を外した。`instance.c`: `kl_instance_fd` を外した。`process.c`・`instance.c`・`ui/window.c`・
  `ui/app.c`: `event.fd = -1` を外した。
- `mailer/sync.c`: file の先頭の説明（`kl_app_watch_fd`）を今の形（ready の関数で app に post）に直した。
- `kl_app_event.ready` は `KL_APP_PROCESS` が使うので残した。
- FreeBSD の build で見つかった物（Q1 の指示で同じ commit で直した）: `libkeiland/platform/process-posix.c` の `environ` は FreeBSD では
  program の start file にあり libc.so に無いので、`-z defs` の libkeiland.so の link が `undefined symbol: environ` で失敗した（p005 から）。
  FreeBSD だけ weak の参照に（`#if defined(__FreeBSD__)`）。`plan/ws131/tests/host-session.sh`・`host-power.sh`（Q1 が所有に足した）の
  link に `libkeiland-backend/unsupported/notify-unsupported.c` を足した（`backend.c` の `kl_backend_notify_poll_done` が未定義、P1 も報告した既存の問題）。

## 確かめ（2026-10-30）

- grep: `kl_app_watch_fd`・`KL_APP_FD`・`kl_instance_fd`・`KL_APP_FDS_MAX` の利用者は userland・docs・plan/tools・plan/ws314/tests に 0
  （`keiland_version.h` の履歴の行だけ）。
- zedBSD（`build/tmp/p008/build-all.sh`）: amd64（desktop 19＋tests 9）・arm64・riscv64（19）・installer の image の installer、Linux（all、
  `build/ws314-p008-linux`）: make 0・warning 0。1 回目は libkeiland の中の `event.fd = -1`（process.c・instance.c・ui/window.c）が残り FAIL、直した。
  exports: 4 つの libkeiland.so（amd64・arm64・riscv64・Linux）の定義された動的 symbol に `kl_app_watch_fd`・`kl_instance_fd` が 0。
- host（Linux の build）: `host-test.sh {post,process,launch,language,instance}` と `preview-test.sh` PASS。`plan/ws131/tests/host-session.sh` 67/67、
  `host-power.sh` 6/6。
- FreeBSD（P1 の 15.1 の guest を読み取り専用の base に `GUEST_DIR`、overlay は `build/tmp/p008/freebsd-run3`、SSH_PORT 2245、
  `plan/tools/keiland-freebsd/backend-test.sh build/tmp/p008/freebsd-backend3`）: build・build-warnings (0)・install・audit・host-seat-freebsd・
  host-session・host-power・sync-rejected・dmabuf-export-rejected すべて PASS。1・2 回目は上の `environ` と notify の FAIL。
- QEMU（image `plan/tools/guest/test-image.sh plan/ws314/tests/config-amd64-browser.mk build/ws314-p008-img`）:
  `guest.sh build/p2-q/p008-guest build/ws314-p008-img` は `ws314-guest: PASS`（post・process・launch・language・instance、Terminal、新しい窓、
  Files の開く、thumbnail）、`guest-browser.sh build/p2-q/p008-browser build/ws314-p008-img` は `ws314-browser: PASS`
  （kl_app_dispatch が compositor と post の wake だけを待つ形で、Browser の非同期の取得が進む）。

## 残り

- なし（p009 で解説に新しい API を足す）。
