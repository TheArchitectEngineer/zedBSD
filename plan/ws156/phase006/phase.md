<!-- awesome-plan project=zedbsd record=ws156-p006 -->

# ws156-p006: Linux の D-Bus の通知（libkeiland-backend の `org.freedesktop.Notifications`）

Phase ID: `ws156-p006`
Parent: [WS156](../ws.md)
Status: in-progress / test-wait（2026-10-13 P3: 実装・host の試験（偽の bus と本物の session bus）・build（Linux・zedBSD amd64、warning 0）は済み。Linux の QEMU+KVM の guest の確かめは T1 待ち。cleared は Q1 の判定）
Phase disposition: normal
Queue: q955（2026-10-13 Q1。ユーザー「これ以上ベータ2に機能追加しないつもりなので、実装を終わらせましょう」、設計に無い機能は足さない）
設計: [p001 §11](../phase001/phase.md)（2026-10-05 ユーザー「Linuxではlibkeiland-backendにD-bus機能を入れて通知を取ればいいですね。」）

## 範囲

p001 §11.1〜§11.5 のとおり: Linux の backend が session bus で `org.freedesktop.Notifications`（Desktop Notifications Specification 1.2）を持ち、GTK・Qt・libnotify の app の通知を compositor の通知の表に入れる。zedBSD・FreeBSD は `ENOTSUP`。Keiland の app は今までどおり Wayland の `kl_system_notify`。

## 1. 実装

| file | 内容 |
| --- | --- |
| `userland/desktop/libkeiland-backend-linux/dbus-linux.c`・`.h` | session bus（`DBUS_SESSION_BUS_ADDRESS` の最初の `unix:path=`・`unix:abstract=`（`%XX` を戻す）、無ければ `$XDG_RUNTIME_DIR/bus`）。接続・EXTERNAL 認証・Hello を system bus と共用の関数に分けた（session bus は fd の交渉をしない）。header を一般化した frame の組み立て（METHOD_RETURN・ERROR・SIGNAL、REPLY_SERIAL、`as`）。method call の受け（`dbus_dispatch_messages`、path と member の検査、NO_REPLY_EXPECTED の印）、`dbus_send_return`・`dbus_send_error`・`dbus_send_signal`。読み: `dbus_read_byte`・`dbus_read_signature`・`dbus_read_array`・`dbus_align`・`dbus_skip_value`（basic・`v`・`a`・`(...)`・`{...}`、深さ 32 まで、長さ・残りを毎回確かめる）・`dbus_type_end` |
| `userland/desktop/libkeiland-backend-linux/notify-linux.c`（新） | `RequestName(DO_NOT_QUEUE)`（他が持てば `EEXIST`、奪わない）。`GetCapabilities`（body・actions・persistence）、`Notify`（signature を確かめ、`default` の action → ACTION、hint の `urgency` byte 2 → URGENT、他の hint は型を見て読み飛ばす。app 64・title 128・body 512 byte に UTF-8 の文字の境で切る。body の tag（`<`・任意の `/`・英字・`>` まで 1 行で 256 byte 以内）を外し、`a < b` の `<` は残す）、`CloseNotification`（送り手の物だけ取り消し、違っても空の答え）、`GetServerInformation`（Keiland・zedBSD・1.0・1.2）、`Introspect`（object と `/`・`/org`・`/org/freedesktop`、gdbus が先に読む）、`Ping`。不明は `UnknownMethod`、引数の誤りは `InvalidArgs`、compositor の断りは `LimitsExceeded`（EBUSY）・`AccessDenied`（EACCES）。signal `NotificationClosed`・`ActionInvoked(id, "default")`。送り手（unique name）ごとに backend の番号（64 個を LRU で）。bus が壊れたら閉じて poll から外す |
| `userland/desktop/libkeiland-backend/keiland-backend.h`・`backend-private.h`・`backend.c` | 公開の API: `struct kl_backend_notify_callbacks`（posted・withdrawn）、`kl_backend_notify_open`・`_closed`・`_activated`・`_close`。bus の fd は backend の poll（`kl_backend_poll_*`）に入れた（§11.3 の `_fd`・`_dispatch` の代わり、compositor の event loop を変えない）。backend の close で notify も閉じる |
| `userland/desktop/libkeiland-backend/unsupported/notify-unsupported.c`（新） | zedBSD・FreeBSD: `ENOTSUP`（`libkeiland-backend-zedbsd/sources.mk`・`libkeiland-backend-freebsd/Makefile.freebsd`、Linux は `Makefile.linux` に `notify-linux.c`） |
| `userland/desktop/wayland/notify-shell.c`・`notify.c`・`notify.h`・`kwl.h`・`main.c` | `kwl_notify_bus_open`（session の時だけ、greeter は開かない。`ENOTSUP` は黙る）。bus の送り手は client `NOTIFY_BUS_CLIENT（1<<62）+ 送り手の番号`、object 0。Wayland の client と同じ rate（10/秒）と Settings の `notify.allow.<app>`（`notify_allowed_name` に分けた）。`replaces` が送り手の物でなければ新しい通知。送り手が 32 個を持つ時は log の一番古い物を `kwl_notify_expire_oldest`（notify.c に新）で expired にして入れる（log に無ければ EBUSY）。close の理由の対応: EXPIRED → 1、DISMISSED・CLEARED → 2、WITHDRAWN → 3。click は `kl_backend_notify_activated` の後に dismiss（→ NotificationClosed 2） |

設計（§11.3）から変えた所（技術の判断、Q1 に報告）:
- 通知の番号は compositor の番号をそのまま D-Bus の番号にした（backend の対応の表を持たない。0 は使わない、回っても 0 を飛ばす点は同じ）。
- 送り手ごとの上限 32 と「超えたら log の一番古い物を落とす」は compositor の model で行う（backend は送り手の番号を posted に渡す。`posted` に `sender` と `int *error` を足した）。
- bus の fd は `kl_backend_notify_fd`・`_dispatch` でなく backend の poll に入れた。
- `posted`・`withdrawn` の中から `kl_backend_notify_closed` を呼んでよい（host の callback の「backend を呼び返さない」の例外、header に記した）。

## 2. 試験（host、QEMU なし）

| command | 結果 |
| --- | --- |
| `sh plan/ws156/tests/run-host-notify-dbus.sh build/p3-ws156-dbus`（新、`host-notify-dbus.c`: socket の偽の bus を子 process で、認証・Hello・RequestName に答え、SENDER 付きの call を送る） | plain・ASan+UBSan とも 41 checks ok、PASS。EXTERNAL で NEGOTIATE_UNIX_FD 無し、GetCapabilities・GetServerInformation、Notify の tag 外し・`<` を残す・urgency・default、置き換え、長い語の切り、LimitsExceeded・AccessDenied、別の送り手の CloseNotification は何も閉じない、自分の物は NotificationClosed(id,3) の後に空の答え、click は ActionInvoked→NotificationClosed(id,2)、Introspect、Ping、UnknownMethod、InvalidArgs（別の signature・短い body、その後も bus は続く）、壊れた frame で bus を閉じ poll から外れる |
| `sh plan/ws156/tests/run-host-notify-live.sh build/p3-ws156-live`（新、`host-notify-live.c`: `dbus-run-session` の本物の dbus-daemon、gdbus と dbus-send） | PASS: gdbus の GetServerInformation `('Keiland', 'zedBSD', '1.0', '1.2')`、gdbus の Notify（先に Introspect を読む）`(uint32 1,)` と post `flags=3 app="gdbus-app" title="Live title" body="live body"`、CloseNotification と gdbus monitor の `NotificationClosed (uint32 1, uint32 3)`、dbus-send の GetCapabilities |
| `sh plan/ws156/tests/run-host-notify-model.sh`（`kwl_notify_expire_oldest` の 3 checks を足した） | 41 passed、PASS |
| `sh plan/ws156/tests/run-host-notify-flow.sh` | PASS |
| Linux: `make -j16 -f userland/desktop/keiland-linux.mk KEILAND_LINUX_BUILD=build/p3-keiland-linux build/p3-keiland-linux/bin/wayland` | warning 0 |
| zedBSD: `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/p3-ws156-amd64 ZEDBSD_USER_PROGRAMS=wayland build/p3-ws156-amd64/bin/wayland` | warning 0 |
| `notify-unsupported.c` の host の compile（FreeBSD の build は FreeBSD の guest で、未実施） | warning 0 |
| `python3 plan/tools/style-check.py`（変えた C と試験） | 指摘 0 |
| `sh plan/tools/keiland-os-boundary/check.sh` | PASS |
| `sh plan/tools/keiland-linux/makefile-sync.sh` | 変更の前後とも同じ 7 件の FAIL（`userland/desktop/preview` の既存の物、この Phase と無関係） |

未実施: Linux の QEMU+KVM の guest の Keiland の session での確かめ（§3、T1）、FreeBSD の build、arm64 の build、実機。

## 3. T1 への依頼（Linux の QEMU+KVM、AGENTS.md の WS131 の例外: SSH と QMP の PNG）

- image: `plan/tools/keiland-linux/build-guest.sh` の gdm の guest（`build/keiland-linux/guest-gdm`）に、この Phase を merge した main の tree の `make keiland-linux` を `install-guest.sh` で入れる。guest に `libnotify-bin` を入れる（`notify-send`）。
- 流す物: Keiland の session（gdm）に login し、利用者の SSH の shell（session bus の環境、`DBUS_SESSION_BUS_ADDRESS` が session の物）で
  1. `gdbus call --session --dest org.freedesktop.Notifications --object-path /org/freedesktop/Notifications --method org.freedesktop.Notifications.GetServerInformation` → `('Keiland', 'zedBSD', '1.0', '1.2')`。
  2. `notify-send "Hello from Linux" "<b>bold</b> body"` → 画面の下の中央に popup が右から流れ、中央で 3 秒、左へ消える（QMP の PNG を 1 s ごとに 4 枚）。compositor の log に `KWL NOTIFY bus serving` と `KWL NOTIFY post bus sender=… title="Hello from Linux"`。
  3. `notify-send -u critical "Urgent" "body"` → `flags=1`（URGENT）。
  4. Super+N で log を開き、2・3 の通知が log にある（PNG）。「Clear all notifications」で消し、`gdbus monitor --session --dest org.freedesktop.Notifications` に `NotificationClosed (… uint32 2)`。
- 合否: 1〜4 の値と PNG（判定は Q1 の目視）、compositor の log に `bus-refused`・`notify: the session bus failed` が無い。
- 返す物: PNG の path、compositor の log の `KWL NOTIFY` の行、gdbus monitor の出力。

## 4. 残り

- T1 の結果を Q1 が判定するまで cleared にしない。
- FreeBSD の backend も session bus を持つので、後で `notify-linux.c` を共有できる（p001 §11.1、今は ENOTSUP）。
- 削除してほしい出力（Q1、P3 の worktree）: `build/p3-keiland-linux`（Linux の compositor の build）・`build/p3-ws156-amd64`（zedBSD の wayland の build）・`build/p3-ws156-dbus*`・`build/p3-ws156-live*`・`build/p3-ws156-model*`・`build/p3-ws156-flow*`、`build/p3-keiland-linux.log`・`build/p3-ws156-amd64.log`。
