<!-- awesome-plan project=zedbsd record=ws035p087 -->

# ws035-p087: X11 と Wayland の clipboard の橋（zdesktop-x11server）

Phase ID: `ws035-p087`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-27 main の割り当て「X11 ↔ Wayland clipboard bridge in zdesktop-x11server (CLIPBOARD/PRIMARY selections ↔
wl_data_device selection, text/plain;charset=utf-8 ↔ UTF8_STRING/STRING), and configure_bounds for X toplevels if simple」）

## 範囲

zdesktop-x11server は atom・一般の property・selection を持たず（InternAtom も BadRequest）、libX11（zedBSD の小さな Xlib）と zterm
にも clipboard が無かった。

1. x11server: InternAtom・GetAtomName・DeleteProperty・一般の ChangeProperty/GetProperty（mode、offset、delete）・SetSelectionOwner・
   GetSelectionOwner・ConvertSelection・SendEvent、SelectionClear/Request/Notify の event（ICCCM の流れ）。
2. 橋: 他の desktop の client の text が selection になったら x11server が CLIPBOARD を（root window で）持ち、X client の
   ConvertSelection に自分で答える（desktop から読む。X の owner が無い PRIMARY も同じ text）。X client が CLIPBOARD を持つ間は
   x11server の wl_data_source が text（`text/plain;charset=utf-8`・`text/plain`・`UTF8_STRING`・`STRING`）を出し、desktop の client の
   受け取りは X の owner へ SelectionRequest（requestor は root、property は `ZED_SELECTION`）、owner の SelectionNotify で fd へ書く。
3. libX11: `XInternAtom`・`XSetSelectionOwner`・`XGetSelectionOwner`・`XConvertSelection`・`XChangeProperty`・`XGetWindowProperty`・
   `XDeleteProperty`・`XSendEvent`（SelectionNotify）、selection の event の構造体と解読。
4. zterm: Ctrl+Shift+C で画面の text を CLIPBOARD に（TARGETS・UTF8_STRING・STRING に答える）、Ctrl+Shift+V で貼る。

## 実装（2026-09-28）

- `userland/base/zdesktop-x11server/selection.c`（新規）: 上の 1・2 の X 側。`internal.h`（atom・property・selection の表と宣言）、
  `protocol.c`（request の振り分け、WM_NAME・icon 以外の property は selection.c へ）、`window.c`（window の破棄で property と
  selection を忘れる）、`server.c`（最初に atom を作る）、`rootless.c`（callback）、`Makefile`。
- `wayland.c`: `wl_data_device_manager` の bind、seat の data device、selection の offer（自分の source のものは取らない）、
  `x11_wayland_selection_own`・`_drop`・`_has_text`・`_read`（pipe、2 秒まで）、source の send を selection.c へ、key と button の
  serial を記録。
- libX11 `xlib.c`、`include/libc/X11/Xlib.h`・`X.h`、zterm `main.c`。

## 設計の判断・制限

- 全体を 1 つの thread の server のまま: desktop の text を読むとき最大 2 秒待つ（X client は待たされる）。
- property の値（format 32）と SendEvent の event は client の byte order のまま保存・転送する（zedBSD の client はすべて LSB で同じ）。
- PRIMARY は X の中の selection。X の owner が無ければ desktop の clipboard の text を返す（Wayland の primary selection は無い）。
- X の toplevel の configure_bounds は入れていない: X の窓の大きさは X client が決め、x11server が bounds で X の窓を作り直す
  仕組みが要る（簡単ではない）。F-044 のまま。
- INCR（大きな転送）・MULTIPLE・時刻の比較は無し。

## 検証（amd64、Venus の guest、2026-09-28）

- `plan/ws035/tests/zdesktop-p087.sh` PASS: Wayland → X（zdesktop-terminal の Select All・Copy、zterm を click すると
  `X11 CLIPBOARD selection text=1`・CLIPBOARD の owner が desktop、zterm の Ctrl+Shift+V で `X11 CLIPBOARD read bytes=N`・
  `ZTERM-X PASTE bytes=N`）、X → Wayland（zterm の Ctrl+Shift+C で `X11 CLIPBOARD own`、terminal の Ctrl+Shift+V で
  `X11 CLIPBOARD send`・zterm の `SELECTION answered requestor=0x1`・`X11 SELECTION sent bytes=N`・`ZTERM PASTE bytes=N`）。
  最初の実行で SendEvent の SelectionNotify の property の offset の誤り（24 → 20）を見つけて直した。
- 回帰 PASS: x11-p003（zterm の rootless・入力・docked・閉じる、`GUEST_RUNTIME=build/ws071-run`）、zdesktop-p079（clipboard）、
  zdesktop-p086（terminal のタブ）。
- 規約: `selection.c` の style-check 0、`wayland.c` 0、`xlib.c`・zterm `main.c`・`Xlib.h` は前と同数（186・61・4）。build warning 0。
- 画面（`/home/awe/zedBSD-rpi4/build/ws035-shots/`）: `p087-20260928-venus-two.png`、`-x-paste.png`（zterm に terminal の text）、
  `-w-paste.png`（terminal に zterm の text）。
- 実機（i915）: 未実施。boot test: 2026-09-27 のユーザーの指示で無し。

## 残り

- X の toplevel の configure_bounds（上）。desktop の text を読む間の待ちを非同期に。INCR。
- zterm の選択（mouse での範囲選択）は無い（画面全体を copy）。
