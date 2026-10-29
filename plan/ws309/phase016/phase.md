<!-- awesome-plan project=zedbsd record=ws309-p016 -->

# ws309-p016 Linux・FreeBSD の seat 経由の入力の open（J4）と、seat の callback の detach の ack を wake で受ける形

- 親: [WS309](../ws.md)、設計 [design.md](../design.md) §4.5（T6、レビュー 4 の close と fd の番号）・§9 の J4。Queue: q1662-i10（P1）。
- 状態: cleared（2026-10-30 q1662-i10 P1。Linux の guest PASS、FreeBSD の native の build と host-seat-freebsd PASS。let-go を待つ経路（`later=1`）は host 試験だけ。実機は未実施）
- 依存: p003（cleared）。

## 今の待ちの点（main 868318312）

| 所 | 何が main で待つか |
| --- | --- |
| `kl_backend_input_scan`（Linux・FreeBSD、`kl_backend_input_find_supported` は 0） | logind の `TakeDevice`（`seat-logind-linux.c` の `seat_take` → `seat_call`、同期の D-Bus の call）、seatd の `libseat_open_device`（FreeBSD）、capability の ioctl |
| `input_detach`（input.c、`kwl_input_forget`・`kwl_input_pause`・`kwl_input_resume`） | input thread の detach の ack（`kwl_input_thread_detach_wait`、通常 µs、止まった時 100 ms） |
| `kwl_input_detach_all`（seat の session の pause、backend-host.c:50） | 全ての device の detach の最後の ack |

- callback の契約（keiland-backend.h の seat の節）: `input_paused`・`input_gone` は「戻った後に seat が descriptor を閉じる／返す」。だから compositor は戻る前に input thread が
  その fd を読まない事を確かめている（ack の同期の待ち）。ack の前に閉じると fd の番号の再利用で input thread が別の file を読む（レビュー 4）。

## 手順（2026-10-29 の計画、下の「実装」で行った）

1. **detach の ack を wake で**（全 OS の compositor と backend の共通の口）:
   - backend の契約を変える: `input_paused`・`input_gone`・`session_paused` の後、seat は input の descriptor を閉じず（logind の `PauseDeviceComplete`・seatd の
     `libseat_disable_seat` の答えも保留し）、compositor の新しい `kl_backend_seat_device_let_go(backend, descriptor)`（と session の `kl_backend_seat_paused_done`）で閉じる・答える。
     keiland-backend.h の契約の文、Linux（seat-logind-linux.c・seat-direct-linux.c）・FreeBSD（seat-freebsd.c）・zedBSD（seat が無い、ENOTSUP のまま）を一緒に直す。
   - compositor（input.c）: `input_detach` は detach を頼むだけで戻る。input thread の `KWL_INPUT_DETACH_ASKED` の event（`apply_detached`）で `let_go` を呼ぶ。
     `kwl_input_detach_all` は全ての ack が来た時に `kl_backend_seat_paused_done`。ack が 100 ms 来ない時の今の扱い（compositor を終える、design 6.1 N-1）は tick の期限で保つ。
2. **seat 経由の open を待たずに**（J4）: logind の `TakeDevice` を `dbus_send_call`（serial）で送り、答えを `dbus_dispatch_messages` で受ける（bluetooth-linux.c が同じ形）。
   答えの fd で capability を読み、今の `input_found` で compositor に渡す。seat の状態（`seat_devices`・D-Bus の接続）は main のまま（thread に出さない: TakeDevice は
   TakeControl をした接続の unique name でしか通らないので、別の接続を T6 に持たせられない）。FreeBSD の libseat は `libseat_open_device` が同期なので、seatd の socket を T6 の
   lane で使えるかを先に調べる（libseat の内部の状態を main と共有するので、使えなければ同期のまま残して理由を書く）。
3. 試験: Linux の QEMU+KVM の guest（WS105 の道、[plan/tools.md](../../tools.md)）で logind の session の VT 切り替え（pause と resume）と入力の hotplug、`KWL INPUT` の行と
   `KWL PERF pass` の pass_max_us。FreeBSD の guest（WS108・WS109）で seatd。host 試験: input の detach の順（ack の前に let_go が呼ばれない）。

## 実装（2026-10-30 q1662-i10 P1、branch `agent/p1-ws309i`、WIP e3acf2530、基点 main 10c6b2664）

- 契約（`keiland-backend.h` の seat の節）: `input_paused`・`input_resumed`・`input_gone` は int を返す（1: compositor の別の thread がその前の descriptor を
  まだ読み得る。compositor は読まなくなった後に event loop から `kl_backend_seat_device_let_go(backend, descriptor)` を callback ごとに順に呼び、
  seat はその時に閉じる・service に答える。0: すぐ進めてよい）。`session_paused` の後、compositor は output を閉じ全ての input を読まなくなってから
  `kl_backend_seat_paused_done` を呼ぶ（callback の中では呼ばない）。`kl_backend_seat_device_open` は後で答える seat（logind）では -1・EINPROGRESS を返し、
  答えが来た時に `kl_backend_poll_done` から `input_found` で渡す（private の `kl_backend_input_offer`、evdev/input-evdev.c）。
- compositor: `input_detach` は待たない（`device->detach_sequence` と ledger の `kwl_seat_answers_asked`）。seat への答えは新しい `wayland/seat-answer.c`
  （答えの FIFO・ack の見た値・待ちの始まり）に積み、`kwl_backend_seat_tick`（main の pass ごと、`kwl_input_drain` の後）が
  `kwl_input_answer`（`kwl_input_thread_acked` を読み、ack の済んだ答えを順に `let_go`、100 ms を超えた ack の待ちは今と同じく compositor を終える）を呼ぶ。
  `session_paused` は detach と、display の変更が無い時に始める非同期の output の close（新しい `kwl_display_output_close`）を頼むだけで戻り、
  両方が済んだ tick で `paused_done`。resume が close の始まる前に来たら close を取り消す。ledger が満ちた時だけ ack をその場で待つ（上限 100 ms）。
  閉じる device の record は最後の detach の ack の event で消す（前の pause の detach の event で消さない）。
- logind（seat-logind-linux.c）: 入力の `TakeDevice` は `dbus_send_call` で送り、答えは `dbus_dispatch_messages` で signal と同じ順に受ける（同じ path の 2 度目の
  open は EINPROGRESS）。`ReleaseDevice`・`PauseDeviceComplete` は `NO_REPLY_EXPECTED` で送り、待たない（同期の `dbus_call` は他の答えを捨てるので、
  open の後は使わない。primary の TakeDevice と終わりの ReleaseControl は同期のまま）。resume の古い fd・gone の fd・input の協調の pause の完了は
  let-go まで保留（resume が先に来たら完了を送らない）、primary の協調の pause の完了は `paused_done` まで保留（resume で取り消す）。
- seatd（seat-freebsd.c）: disable は `session_paused` と各 input の `input_gone` を伝え、1 を返した lease は let-go で、残りと primary は `paused_done` で閉じてから
  `libseat_disable_seat`。open の最中の disable はその場で全部。libseat の `libseat_open_device` は同期のまま（device の open は seatd の socket の往復で、
  libseat の内部の状態を main と共有するため thread に出さない。Linux の J4 だけを非同期にした）。
- 直した試験: `plan/ws131/tests/host-seat-freebsd.c`・`.sh`（API の変更に追従。`.sh` は main でも notify-unsupported.c が無く link できなかったので足した）。

## 確かめた事（2026-10-30）

- build: zedBSD `make ZEDBSD_CONFIG=plan/ws309/tests/config-amd64-p1.mk build/amd64/sysroot/usr/lib/libc.o build/amd64/dynamic/libc.so build/amd64/bin/wayland`
  exit 0・`warning:` 0。Linux `make keiland-linux KEILAND_LINUX_BUILD=build/ws309/keiland-linux`（-Werror）exit 0・`warning:` 0。
- host: `plan/ws309/tests/seat-answer/run.sh`（ASan+UBSan）17/17（ack の前に let-go が出ない、callback の順、pending、遅れ、満杯）。
  `plan/ws131/tests/host-seat-freebsd.sh`（ASan+UBSan）18/18（disable で何も閉じない → let-go で input → paused_done で primary と disable_seat）。
- `git diff --check` PASS。`makefile-sync.sh`・`keiland-os-boundary/check.sh` の FAIL は preview・installer・powervr・libbrowser の既存の物だけ（この差分の file は無い）。

## guest の試験（2026-10-30、ユーザーの許可「決めの事項は、許可します。」で guest の道具の自分の build/ の一時 file の削除が §4 の例外になった）

- Linux（Debian 13 の gdm の guest、`plan/tools/keiland-linux/build-guest.sh build/ws309/linux/guest-gdm gdm`、port 2327）:
  `plan/ws309/tests/seat-linux/seat-guest.sh build/ws309/linux/seat2 build/ws309/linux/guest-gdm build/ws309/linux/stage build/ws309/linux/uinput-key`
  **seat-guest: PASS**（`KWL SEAT logind session=…`、起動の時の入力 4 個は TakeDevice の答えを bus から受けて `KWL INPUT`、uinput の keyboard の差し込みで 5 個目、
  抜くと `KWL SEAT pause … type=gone`、SwitchTo と chvt の 2 回とも `KWL SEAT paused` → `KWL SEAT paused_done` → `KWL SEAT resumed`、compositor の pid 1380 が同じ、
  KWL FAILED 無し）。PNG: `build/ws309/linux/seat2/{session,vt,back,back2}.png`（back2 は chvt の往復の後に desktop と Settings の窓が戻った画面）。
  1 回目（seat1）は試験の script の誤り（uinput の keyboard に KEY_Z が無く compositor が keyboard と見なさない、VT の番号の読み違い）で FAIL、直して 2 回目。
- 観測できなかった所: この guest では kernel の revoke（ENODEV）が logind の signal より先に input thread に届き、全ての callback が `later=0` を返した。
  let-go まで閉じるのを待つ経路（`later=1`）は host 試験（seat-answer・host-seat-freebsd）だけで確かめた。
- FreeBSD: `plan/tools/keiland-freebsd/build-guest.sh` は 1・2 回目が mirror の `llvm19-19.1.7_4` の「fetch size does not match expected package size」、
  3 回目は自分の 45 分の timeout（load で最後の package の展開が遅い）、4 回目（上限 100 分）は pkg install まで済み、最後の `guest stop` が load で
  「QEMU is still alive」を返して止まった。QEMU が消えたのを確かめ、`guest.qcow2.new` を `guest.qcow2` に改名して（削除ではない）
  `GUEST_DIR=build/ws309/freebsd/guest GUEST_RUN=build/ws309/freebsd/run SSH_PORT=2337 plan/tools/keiland-freebsd/backend-test.sh build/ws309/freebsd/backend-test`
  （tree 325a9feaa、FreeBSD 15.1-RELEASE-p6）: build PASS・build-warnings 0・install・audit PASS、**host-seat-freebsd 18/18 PASS**、sync-rejected・dmabuf-export-rejected PASS。
  host-session・host-power は FAIL（`kl_backend_notify_poll_done` などが undefined: `plan/ws131/tests/host-session.sh`・`host-power.sh` の source の一覧に
  notify-unsupported.c が無い。この差分の前からの link の誤りで、WS131 の試験。host-seat-freebsd.sh は同じ誤りをこの Phase で直した）。
