<!-- awesome-plan project=zedbsd record=ws035p132 -->

# ws035-p132: xdg_wm_base.destroy を、その binding から作った xdg_surface だけで判定する（BUG-112）

Phase ID: `ws035-p132`
Parent: [WS035](../ws.md)
Status: in-progress（2026-09-29、サブエージェント、worktree `wt/ws035`。main の wrap up の指示で途中で止めた: compositor の修正は実装・build まで、試験は未実施）
Phase disposition: normal
Bug: [BUG-112](../../bugs/BUG-112.md)
Queue: なし（2026-09-29 main の割り当て「BUG-112 の修正を ws035-p132 として」。修正してよい範囲: WS035 の source・plan/ws035・BUG-112 の ticket・
Bug Board の BUG-112 の行。libkeiland の回避（binding を持ち続ける、WS092）は変えない）

## 範囲と受け入れ

- `userland/desktop/wayland/protocol.c` の ZWL_WM の opcode 0（`xdg_wm_base.destroy`）は、client に生きている xdg_surface が 1 つでもあると
  protocol error（EPROTO → 接続を切る）にしていた。xdg-shell の定義どおり、destroy される binding から作った xdg_surface だけを対象にする。
- 確認（受け入れ）:
  1. 一つの client が 2 つの xdg_wm_base を bind し、片方（A）で窓を作ったまま、もう片方（B）を destroy しても切られない。
  2. 同じ binding（A）で窓が残っているまま A を destroy すれば、今までどおり error で切られる。
  3. 回帰: `zdesktop-p052.sh`・`zdesktop-p053.sh`、boot test。

## 実装（済み、2026-09-29）

- `userland/desktop/wayland/zwl.h`: `struct zwl_object` に `wm_base`（xdg_surface を作った xdg_wm_base。比べるだけで辿らない。binding は自分の
  生きた xdg_surface より長く生きる）。
- `userland/desktop/wayland/protocol.c`:
  - `get_xdg_surface` で `created->wm_base = object`。
  - destroy（opcode 0）は、`kind == ZWL_XDG_SURFACE && !dead && wm_base == object` の object が残っていれば EPROTO、無ければ destroy。
- ping（`toplevel.c` の `zwl_ping_send`）は client の生きた ZWL_WM を探すので、片方の binding の destroy の後も残りの binding で動く（読んだだけ、未試験）。
- 規約: `plan/tools/style-check.py userland/desktop/wayland/protocol.c` 0（変更前も 0）。
- build: `build/p132-build.sh`（`build-login-image.sh build/amd64 graphical`）を wrap up の時に background で実行。結果は下の「状態」。

## 試験の案（未実施）

guest の試験用の client が要る。案: `userland/base/tests/popup-probe/main.c`（ws035-p076 の試験の client、`/bin/popup-probe`）に mode を足す:
`--wm-probe`: registry で xdg_wm_base を 2 回 bind（A・B）→ A で toplevel を作り最初の configure まで → B を destroy して roundtrip
（切られなければ `WM-PROBE other-binding ok`）→ A を destroy して roundtrip（`wl_display_get_error` が EPROTO なら
`WM-PROBE same-binding error ok`）→ `WM-PROBE:PASS`。compositor の log には `ZWL ERROR`（1 回だけ、2 つ目の段）。
試験の script は `plan/ws035/tests/zdesktop-p132.sh`（`zdesktop-p076.sh` の起こし方を写す: greeter の service を止め、compositor を起こし、
`popup-probe --wm-probe` を SSH で走らせて出力を読む）。

## 状態（wrap up の時点）

- commit 済み: `zwl.h`・`protocol.c` の修正（この phase.md と同じ commit）。
- build: wrap up の時に実行した build の結果は、この phase.md の最後の行に追記した（`build exit=` と warning）。
- guest は `zdesktop-guest.sh stop` で止めた。

## Resume point

2026-09-29（wrap up）: 修正の実装と build まで。次の手順:
1. `git merge main` の後、`plan/ws035/tests/build-login-image.sh build/amd64 graphical` で build（warning 0 を確かめる）。
2. 上の「試験の案」の `popup-probe --wm-probe` と `zdesktop-p132.sh` を書き、受け入れ 1・2 を確かめる。
3. 回帰 `zdesktop-p052.sh`・`zdesktop-p053.sh`、`plan/tools/boot-test.sh`。長い計測は script にして background で。
4. BUG-112 の ticket を resolved にし、Bug Board の行を更新、ws.md の p132 の行を cleared に。
その後の候補（p133）: wallpaper の ppm の読みと拡縮（約 170 ms）の短縮。main の指示を待つ。

（wrap up の build: `build/p132-build.sh` → build exit=0、wayland の warning 0、protocol.c を再 compile、log に `Permission denied` なし。image は `build/amd64/hdd-image.img`、試験は未実施）
