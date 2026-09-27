<!-- awesome-plan project=zedbsd record=ws035p103 -->

# ws035-p103: X11 の PRIMARY と desktop の primary selection の橋、zterm の語の選択と中 button

Phase ID: `ws035-p103`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-28 main の割り当て「the X11 PRIMARY bridge」。p100 の残り）

## 実装（2026-09-28）

- **xserver**（`wayland.c`・`selection.c`・`internal.h`・`rootless.c`）: p087 の CLIPBOARD の橋と同じ形で
  PRIMARY を橋渡しする。
  - desktop に `zwp_primary_selection_device_manager_v1` があれば bind し、seat の primary device を持つ。
  - desktop の primary selection に別の client の text があれば、server が X の PRIMARY を root window で持つ
    （前の X の持ち主には SelectionClear）。X の ConvertSelection には desktop から読んで答える（`x11_wayland_primary_read`）。
  - X の client が PRIMARY を持つと、server がその text を desktop の primary selection として出す（`x11_wayland_primary_own`）。
    desktop の client の要求は、X の持ち主に UTF8_STRING を頼んで fd に書く（`selection_bridge_ask`。CLIPBOARD と共有）。
  - X の持ち主が手放したり消えたりしたら、server の source を下ろす。
  - X の持ち主も desktop の primary の text も無い PRIMARY は、今までどおり desktop の clipboard の text で答える。
  - pipe の読みは CLIPBOARD と PRIMARY で共有する（`wayland_pipe_read`）。
- **zterm**（X client）: 左 button の double click で pointer の下の語（空白で区切った並び）を PRIMARY にする
  （`ZTERM-X PRIMARY set`）。中 button は PRIMARY を shell に paste する。SelectionRequest には CLIPBOARD・PRIMARY のそれぞれの
  text で答える。この Xlib では button の番号は event の `keycode`（detail）に入る。

## 検証（amd64、Venus の guest、2026-09-28）

- `plan/ws035/tests/zdesktop-p103.sh`（新）PASS:
  - Wayland から X: terminal で double click した `beta-gamma` を、zterm の中 click で paste した（`X11 PRIMARY read bytes=10`、
    `ZTERM-X PASTE bytes=10`、`p103-20260928-x-paste.png`）。
  - X から Wayland: zterm で double click した `xprimary-word` を、terminal の中 click で paste した（`X11 PRIMARY own`・
    `send`、`ZTERM-X SELECTION answered bytes=13`、`X11 SELECTION sent bytes=13`、`ZTERM PRIMARY paste received bytes=13`、
    `p103-20260928-w-paste.png`）。
- 回帰 PASS: zdesktop-p087（X と Wayland の CLIPBOARD の橋）。
- 規約: style-check は、x11server の 4 file が 0、zterm の main.c が HEAD と同じ 61。build warning 0。
- 実機: 未実施。

## 残り

- zterm の選択は double click の語だけで、選択の表示（反転）も drag・行の選択も無い。
- 橋の要求の待ち行列（`pending_sends`）は CLIPBOARD と PRIMARY で 1 本。X の持ち主が要求の順に答える前提（今までどおり）。
- 選択を消すとき（click）に source を下ろさない（最後の選択が残る。X と同じ）。
