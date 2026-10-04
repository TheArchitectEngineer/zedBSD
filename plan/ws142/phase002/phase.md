<!-- awesome-plan project=zedbsd record=ws142-p002 -->

# ws142-p002: Super（Windows キー）単独で App Home を開く・閉じる

Status: in-progress（2026-10-05 P1 generation17 / q719-i01。途中で止めた: Q1 の優先の変更で q720 ws159-p006 を先にする。安全な commit の地点、build には未だ入れていない）
Disposition: normal
Parent: [WS142](../ws.md)
Queue: q719 / q719-i01（Q1 の投入）
Design: [ws142-p001](../phase001/phase.md) の C

## 範囲と受け入れ

- Super（左右の Meta）を、Shift・Ctrl・Alt を押さずに押して、1 秒以内に、他の key・pointer の button・wheel・タッチ無しに離すと App Home を開く（開いていれば閉じる）。Super+Tab・Super+L・Super+Alt+文字は今のまま。Super の押下・解放は今まで通り client にも渡す（D9 の案）。lock・greeter の間は何もしない。
- build warning 0、host の試験、QEMU（T1）: QMP の key で Super 単独 → `ZWL HOME open via=super`、もう一度 → `ZWL HOME close via=super`、Super+Tab は Wiseview（Home は開かない）、Super を押したまま click → 開かない。

## 進み（2026-10-05）

- 済: `userland/desktop/wayland/super-tap.c`・`super-tap.h`（純粋な判定。`zwl_super_tap_key(tap, key, state, others_held, now_ms)` が tap の解放で 1、`zwl_super_tap_cancel`）。style-check の指摘 0。まだ Makefile にも compositor にも入れていない（build に影響しない）。
- 残り（再開の手順）:
  1. `zwl.h`: `#include "super-tap.h"` と `struct zwl_super_tap super_tap;`（server）。
  2. `seat.c` `zwl_seat_key`: lock・greeter の判定の後、IME と Home の key の前で `zwl_super_tap_key(&server->super_tap, key, state, (server->modifiers & (shift 0x1 | control 0x4 | alt 0x8)) != 0, zwl_milliseconds())`、1 なら `zwl_home_toggle(server, "super")` を呼び、key は続けて流す（Home が出ている時の解放は Home が取る）。
  3. `zwl_seat_button`・`zwl_seat_axis` の先頭と `touch.c` の `touch_down` で `zwl_super_tap_cancel`。
  4. `home.c`: `zwl_home_toggle(server, via)`（出ている・開いている途中なら `zwl_home_dismiss` と同じ片付けで閉じる、それ以外は `home_open(server, progress, via)`）と `zwl.h` の宣言。
  5. `Makefile`・`Makefile.linux`・`Makefile.freebsd` に `super-tap.c`。
  6. host の試験 `plan/ws142/tests/host-super-tap.c`（単独の tap、1 秒を超える、他の key・Super の両方・Shift を押した状態・cancel・repeat・解放だけ）、QEMU の試験 `plan/ws142/tests/p002-guest.sh`（pen の image、QMP の key）。
