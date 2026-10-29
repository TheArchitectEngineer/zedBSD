<!-- awesome-plan project=zedbsd record=ws314-p004 -->

# ws314-p004 `kl_app_post`・`kl_window_post` の実装と Mailer の移行

- 親: [WS314](../ws.md)。設計: [design.md](../design.md) §3。Queue: q1690 / q1690-i01（担当 P2、2026-10-30）。
- Status: cleared（2026-10-30。QEMU の Mailer の同期は未実施（IMAP の相手が無い）。判断 B（`kl_window_post` の意味の変更）は推しの形で実装、答えが違えば直す）
- Disposition: normal

## 目的・受け入れ

- 他の thread から呼べる `kl_app_post`（`KL_APP_POST`）と、data を持ち他の thread からも呼べる `kl_window_post`（KL_VERSION 97）。
  起こしは platform の層に閉じる。Mailer の done の pipe と `kl_app_watch_fd` を外す。
- build warning 0（zedBSD 3 arch・Linux）、host の試験。

## 実装

- `libkeiland/platform/platform.h`・`platform-posix.c`（新）: wake（`pipe2(O_CLOEXEC|O_NONBLOCK)`）、lock（mutex）、thread の id・
  起動（detach、全 signal を止めて始める）・名前（Linux・FreeBSD の `pthread_setname_np`、zedBSD は無し）。
- `ui/app.c`: post の list（node は `struct kl_app_event`）と lock と wake。app の thread で list が空なら直に queue へ、他は list の末尾へ
  （空 → 空でない時だけ wake）。dispatch は wake を poll の集合の最後に入れ、毎回 wake を空にして list を丸ごと取り、queue の空きの分を移し、
  残りは list の先頭へ戻して wake を鳴らし直す。窓宛ての post は窓が居なければ `KL_APP_POST`（window NULL）。`keiui_app_forget` は窓の
  `KL_WINDOW_POST` を queue と list の両方で `KL_APP_POST` に変える。`kl_app_close` は残りを捨てる。
- `ui/window.c`: `kl_window_post(window, code, data)` は int を返す。app の窓は `keiui_app_post_event`、単独の窓は今どおり同じ thread で積む。
- header: `keiland_app.h`（`KL_APP_POST`、`kl_app_event` に `code`・`data`・`process`・`instance` を一度に）、`keiland_window.h`
  （`kl_window_event.data`、`kl_window_post` の新しい形）、`keiland_version.h`（97）。exports.map を再生成（`kl_app_post`）。
- Mailer: `sync.c` の done の pipe を外し、`ml_sync_start(..., ready, context, ...)` の callback。「知らせ済み」の印（`told`）を
  `ml_sync_take` が 0 を返す時に下ろし、`ready` の失敗（post の ENOMEM）でも下ろす。`main.c` は `KL_APP_POST` の `ML_POST_RESULTS` で
  `ml_results`（止めた thread の古い post は sync が NULL なら無視）。`ml_sync_fd` を外した。sync.c は Keiland に依らない。

## 確かめ（2026-10-30）

- Linux（`keiland-linux.mk` の all、`build/ws314-p004-linux`）: make 0、warning 0。
- zedBSD: amd64（desktop の 19 program と userland/tests の 9）・arm64（19）は make 0・warning 0（`build/ws314-p004-*.log`）。
  riscv64 は p005 の header の書きかけを途中で拾って止まった（build の途中に source を変えた）。p005 の build（同じ tree に p004 を含む）で 3 arch を確かめる。
- host の試験 `plan/ws314/tests/post-test.c`（`sh plan/ws314/tests/host-test.sh post BUILD`）: libwayland-server の最小の server
  （`wl_compositor`・`xdg_wm_base` の global を名前だけ）に `kl_app_open` し、4 thread × 5000 の post と、app の thread からの 1000 の post
  （queue の 512 を超える）を `kl_app_dispatch(app, -1)` で受ける。PASS（21000 件、各 thread の順、余りの event 無し）。
  TSan で build した libkeiland（`-fsanitize=thread`）でも PASS・警告 0。
- 未実施: QEMU・実機での Mailer の同期（IMAP の相手が要る）。窓宛ての post の試験（窓を作れる compositor が host に無い）。
