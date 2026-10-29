<!-- awesome-plan project=zedbsd record=ws309-p016 -->

# ws309-p016 Linux・FreeBSD の seat 経由の入力の open（J4）と、seat の callback の detach の ack を wake で受ける形

- 親: [WS309](../ws.md)、設計 [design.md](../design.md) §4.5（T6、レビュー 4 の close と fd の番号）・§9 の J4。Queue: q1662-i10（P1）。
- 状態: planned（2026-10-29 q1662-i10 P1: 調べと手順まで、source の変更なし。この起動の残りの文脈では Linux・FreeBSD の guest の試験まで届かないため、次の起動へ）。
- 依存: p003（cleared）。

## 今の待ちの点（main 868318312）

| 所 | 何が main で待つか |
| --- | --- |
| `kl_backend_input_scan`（Linux・FreeBSD、`kl_backend_input_find_supported` は 0） | logind の `TakeDevice`（`seat-logind-linux.c` の `seat_take` → `seat_call`、同期の D-Bus の call）、seatd の `libseat_open_device`（FreeBSD）、capability の ioctl |
| `input_detach`（input.c、`kwl_input_forget`・`kwl_input_pause`・`kwl_input_resume`） | input thread の detach の ack（`kwl_input_thread_detach_wait`、通常 µs、止まった時 100 ms） |
| `kwl_input_detach_all`（seat の session の pause、backend-host.c:50） | 全ての device の detach の最後の ack |

- callback の契約（keiland-backend.h の seat の節）: `input_paused`・`input_gone` は「戻った後に seat が descriptor を閉じる／返す」。だから compositor は戻る前に input thread が
  その fd を読まない事を確かめている（ack の同期の待ち）。ack の前に閉じると fd の番号の再利用で input thread が別の file を読む（レビュー 4）。

## 手順（次の起動はここから）

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
