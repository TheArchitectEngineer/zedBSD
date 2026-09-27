<!-- awesome-plan project=zedbsd record=ws071p018 -->

# ws071-p018: 補強: 新しい窓が画面に収まる（xdg-shell の configure_bounds）

Phase ID: `ws071-p018`
Parent: [WS071](../ws.md)
Status: cleared（2026-09-27、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-27 main の指示でサブエージェントが worktree の branch で実行。WS071・WS070 の締めの後の補強）

## 範囲

p011 の「残り」の 1 番目: App Home から開いた zdesktop-files が 1120x720 で、1280x800 の画面の下にはみ出す
（`build/ws071-shots/p011-20260927-venus-files-from-home.png`、窓の下端 818）。

1. zdesktop が xdg-shell version 4 の `xdg_toplevel.configure_bounds` を送る（窓が自分で決める大きさの上限）。
2. zdesktop の新しい窓の置き場所を作業域に収める（cascade で右・下にはみ出さない）。
3. zdesktop-files が bounds を守る（自分の大きさを bounds に縮める）。
4. 同じ Phase で小さい見える残り: Help の Keyboard Shortcuts のカードにタブの key の行（p013 の「残り」）。

## 実装（2026-09-27）

- libwayland（client）: `xdg_wm_base`・`xdg_surface`・`xdg_toplevel` の記述を version 4 に、`xdg_toplevel` に event
  `configure_bounds`（"4ii"、opcode 2）。`event.c` に dispatch。`include/libc/wayland/xdg-shell-client-protocol.h` の
  `xdg_toplevel_listener` に `configure_bounds`（upstream の生成物と同じ位置）と `XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION`。
  `-Wmissing-field-initializers` のため、`xdg_toplevel_listener` を初期化子で作る既存の client（tests の 5 probe・wltest・wlshm・
  egltest・zdesktop-terminal・zdesktop-x11server）に `NULL` を足した（どれも version 1 で bind するので event は来ない）。
- zdesktop: `xdg_wm_base` を version 4 で広告。`zwl_window_send_configure` が、version 4 の窓で fullscreen・最大化でないとき、
  toplevel の configure の前に `configure_bounds` を送る（`ZWL BOUNDS client= surface= width= height=` を log）。bounds は glass の
  look では `zwl_glass_space`（新、shell.c: 出力から左右の余白、システムバー・浮いたタイトルバー・間・下の余白を引いた 1256x690）、
  plain では出力の大きさ。`zwl_glass_place` は cascade で右端・下端を越える窓を作業域の中へ戻す（入らない大きい窓は左上に揃えて右・下へ）。
- zdesktop-files（`window.c`・`window.h`）: `xdg_wm_base` を min(広告, 4) で bind、`configure_bounds` を記録し、configure が大きさを
  窓に任せる（0x0）とき自分の大きさを bounds に縮める。明示の大きさ（最大化・resize）はそのまま。
- `ui-help.c`: Keyboard Shortcuts に「Ctrl+T Ctrl+W New tab, close tab」「Ctrl+Tab Ctrl+Shift+Tab Next tab, previous tab」、key の列を
  170 → 200 px。

## 検証（amd64 だけ、Venus の guest、2026-09-27）

- `files-p018.sh`（新）PASS: 既定の大きさの zdesktop-files に `ZWL BOUNDS ... width=1256 height=690`、`ZFILES READY width=1120
  height=690`、`MAP x=80 y=98`（下端 788）。2 つ目の 1000x640 は cascade で x=172、下にはみ出さない y=148（前は 155）。App Home から
  開いても bounds と y=98。
- 回帰: files-p011（App Home）・files-p014（CONTROLS）・files-p008（menu と Help のカード）PASS。ws070 の titlebar-p010・p011・p013・
  menu-p002・menu-p003 PASS（ws070-p013 の欄）。build は warning 0（変えた component）、egltest は lean image に無いので `-fsyntax-only`
  で確認。style-check: 変えた file は 0 のまま（`xdg-shell-client-protocol.h` は前と同じ 10）。
- 画面（`/home/awe/zedBSD-rpi4/build/ws071-shots/`）: 前 `p011-20260927-venus-files-from-home.png`（下にはみ出す）、後
  `p018-20260927-venus-files-from-home.png`（下端 788 で収まる）、`p018-20260927-venus-one-window.png`、
  `p018-20260927-venus-cascade.png`、`p018-20260927-venus-help-shortcuts.png`。
- 実機（i915）: 未実施。boot test: 2026-09-27 のユーザーの指示で無し。

## 残り

- bounds を守らない client（zdesktop-terminal・X11 の窓・mview 等は version 1 で bind）の大きい窓は、置き場所が左上に揃うだけで
  まだ下にはみ出しうる。zdesktop から大きさを押し付ける configure は、固定の大きさを期待する client があるので入れていない。
- 出力の大きさが変わったとき（window mode の切り替え）に bounds を送り直していない（次の configure で送る）。
