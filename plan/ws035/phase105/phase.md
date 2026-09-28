<!-- awesome-plan project=zedbsd record=ws035p105 -->

# ws035-p105: mview と xserver の窓も configure_bounds を守る（F-044）

Phase ID: `ws035-p105`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-28 main の割り当て 2。[F-044](../../future-work.md) を promote）

## 範囲

F-044「他の client も `configure_bounds` を守る（terminal、xserver の窓、mview）」。terminal は p086 で既に xdg-shell 4 の
`configure_bounds` を守っていたので、残りの mview と xserver の窓だけを files（ws071-p018）と同じ形にする。

## 実装（2026-09-28）

- **mview**（`userland/desktop/mview/window.c`・`mview.h`・`main.c`）: `xdg_wm_base` を version 4 まで bind、
  `configure_bounds` を覚え、大きさの無い configure では頼んだ大きさ（`--size`）を bounds に収める（terminal・files と同じ規則）。
  harness 向けに `MVIEW WINDOW run=… width=… height=… bounds=WxH` を出す。
- **xserver**（`userland/desktop/xserver/wayland.c`）: `xdg_wm_base` を version 4 まで bind、窓ごとに bounds を覚える。大きさの無い
  configure で X の窓が bounds より大きいとき、bounds に切った大きさを pending にし、次の `x11_wayland_dispatch` で server の
  `configure` callback（rootless の resize と Expose）に渡す。configure は窓を開く roundtrip の中で来るので、server が窓を
  持つ前に resize しないよう dispatch まで待つ。log `X11SERVER BOUNDS window=… width=… height=… was=WxH`。

## 検証（amd64、Venus の guest、lean な files の image `build/ws071-files-d3`、2026-09-28）

- `plan/ws035/tests/zdesktop-p105.sh`（新）PASS（zdesktop --glass 1280x800、bounds 1256x690）:
  1. `mview --windowed --size=1600x1000` → `MVIEW WINDOW width=1256 height=690`、窓の全体が見える。
  2. `zterm -geometry 300x100`（root の中で 1240x720）→ `X11SERVER BOUNDS width=1240 height=690 was=1240x720`、窓の全体が見える。
  3. `mview --size=640x460` は自分の大きさのまま。zdesktop の log に ERROR なし。
- 画面: `build/ws035-shots/p105-20260928-mview.png`・`-zterm.png`・`-small.png`。
- 回帰 PASS: x11-p003（xserver の rootless・入力・docking での resize・close）、zdesktop-p062（mview の docking）。
- build warning 0（変えた file）。style-check: xserver の wayland.c は 0、mview の window.c・main.c は変更前と同じ数（既存）。
- 実機: 未実施。

## 残り

- xserver: X の client が後から自分で窓を大きくした（ConfigureWindow）ときは bounds に切らない（desktop からの configure の
  ときだけ）。
