<!-- awesome-plan project=zedbsd record=ws035p132 -->

# ws035-p132: xdg_wm_base.destroy を、その binding から作った xdg_surface だけで判定する（BUG-112）

Phase ID: `ws035-p132`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-29、サブエージェント、worktree `wt/ws035`。受け入れ 1〜3 を QEMU の Venus で確認。実機は未実施）
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
- 2026-09-29（引き継ぎの後）: 拒むときの error を xdg-shell どおり xdg_wm_base の `defunct_surfaces`（code 1、`WM_ERROR_DEFUNCT_SURFACES`）で
  送る（`zwl_error_code`。今までは wl_display の汎用の code だった）。client の `wl_display_get_protocol_error` が xdg_wm_base と code 1 を得る。
- 規約: `plan/tools/style-check.py userland/desktop/wayland/protocol.c userland/base/tests/popup-probe/main.c` 0、`git diff --check` 0。
- build: `build/p132-build.sh`（`build-login-image.sh build/amd64 graphical`）を wrap up の時に background で実行。結果は下の「状態」。

## 試験（2026-09-29 実施）

guest の試験用の client が要る。案: `userland/base/tests/popup-probe/main.c`（ws035-p076 の試験の client、`/bin/popup-probe`）に mode を足す:
`--wm-probe`: registry で xdg_wm_base を 2 回 bind（A・B）→ A で toplevel を作り最初の configure まで → B を destroy して roundtrip
（切られなければ `WM-PROBE other-binding ok`）→ A を destroy して roundtrip（`wl_display_get_error` が EPROTO なら
`WM-PROBE same-binding error ok`）→ `WM-PROBE:PASS`。compositor の log には `ZWL ERROR`（1 回だけ、2 つ目の段）。
試験の script は `plan/ws035/tests/zdesktop-p132.sh`（`zdesktop-p076.sh` の起こし方を写す: greeter の service を止め、compositor を起こし、
`popup-probe --wm-probe` を SSH で走らせて出力を読む）。
実装: `popup-probe --wm-probe`（上のとおり。2 段目は `wl_display_get_error == EPROTO` と、protocol error が xdg_wm_base の code 1 であることまで見る。
sysroot の xdg-shell の header は古く `XDG_WM_BASE_ERROR_DEFUNCT_SURFACES` が無いので probe が定数を持つ）と `plan/ws035/tests/zdesktop-p132.sh`
（probe の出力、zdesktop の `ZWL ERROR … code=1` がちょうど 1 回、zdesktop が生きていること、画面）。

## 状態（wrap up の時点）

- commit 済み: `zwl.h`・`protocol.c` の修正（この phase.md と同じ commit）。
- build: wrap up の時に実行した build の結果は、この phase.md の最後の行に追記した（`build exit=` と warning）。
- guest は `zdesktop-guest.sh stop` で止めた。

## 結果（2026-09-29、cleared）

- build: `build/p132-build.sh`（`build-login-image.sh build/amd64 graphical`、main を取り込んだ後）exit 0、warning 0、`Permission denied` 0。
  1 回目は probe の `XDG_WM_BASE_ERROR_DEFUNCT_SURFACES` が sysroot の header に無く error（probe に定数を置いて直した）。
- 受け入れ 1・2: `plan/ws035/tests/zdesktop-p132.sh build/ws035-shots/p132` → `p132: PASS`（QEMU の Venus、`zdesktop-guest.sh start build/amd64/hdd-image.img`）。
  probe: `WM-PROBE other-binding ok`、`WM-PROBE same-binding error ok interface=xdg_wm_base code=1`、`WM-PROBE:PASS`、exit 0。
  zdesktop: `ZWL ERROR client=1 object=5 code=1 reason=xdg_surfaces of this binding live` がちょうど 1 回、zdesktop は生きたまま
  （`build/ws035-shots/p132/after.png`: 切られた client の窓が消え、desktop が描かれている）。
  注: guest の start の直後に走らせた 1 回目は guest の `/tmp` に log が出来ず FAIL（起動の途中だったと見る。同じ guest で手で走らせると PASS、
  script の 2 回目も PASS）。script は p076 と同じく guest が上がってから走らせる前提。
- 受け入れ 3（回帰）: `zdesktop-p052.sh` PASS、`zdesktop-p053.sh` PASS（`build/ws035-shots/p132-p052/`・`p132-p053/`）。
  boot test（`OUTPUT=build/p132-boot-test plan/tools/boot-test.sh build/amd64/hdd-image.img`、GPU の無い q35: greeter は表示が無く終わり console の login）
  PASS、`build/ws035-shots/p132-20260929-boot-test.png`。
- 未実施: 実機。libkeiland の回避（binding を持ち続ける、WS092）はそのまま（外すかは WS092 の判断）。

## Resume point

2026-09-29: cleared。次の候補（p133）: wallpaper の ppm の読みと拡縮（約 170 ms）の短縮。main の指示を待つ。
