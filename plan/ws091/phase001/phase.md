<!-- awesome-plan project=zedbsd record=ws091-p001 -->

# ws091-p001: 画像 viewer の設計

Status: cleared（2026-09-29。判断の点 J1〜J11 は既定を選び、報告でユーザーに挙げた）
Disposition: normal
Parent: [WS091](../ws.md)
Queue: main の依頼（2026-09-29、worktree `.claude/worktrees/ws091-imageview`、branch `wt/ws091`、main 39df8623 から）
Approval: main の依頼（デモに必須、ユーザーの決定）「p001（設計）→ p002（実装）→ p003（規約・回帰）」。

## 範囲と受け入れ

PNG・JPEG・GIF の表示、fit・拡大縮小・pan、touch の pinch と慣性、同じ folder の前後の画像、命令の引数の file、App Home への登録の設計。
Kei の見た目（すりガラス、浮いた titlebar）。画面に Keiland・libkeiland を出さない。受け入れ: 設計の文書（p002 を迷わず実装できる粒度）。

## 成果物

[design.md](../design.md): 名前と範囲、file の構成（PDF Viewer の型）、command line と log、見た目（glass の card、浮いた chip、全画面、空の画面）、
Vulkan の 2 層の描画（画像の texture と CPU の UI）と縮小の段、表示の状態と操作（key・pointer・touch）、folder と前後、復号（EXIF の向き、動く GIF）、
menu と titlebar、他の担当の file への最小の差分（App Home・tile の絵・デモの一覧・program の一覧）、試験、判断の点 J1〜J11。

## 調べた事実（2026-09-29）

- PDF Viewer（`userland/desktop/pdfviewer/`、約 8800 行）は app ごとに window・present（CPU の canvas を Vulkan で見せる）・canvas・text・titlebar・
  menu・touch を持つ。desktop の app は共有の UI の library を持たず、この型を写して使う。glass は Files の `glass.c`（`keiland_glass`）。
- libkeiland の `keiland_gesture`（tap・double tap・long press・drag・pinch）と `keiland_scroller`（慣性・rubber band）と `keiland_motion` が
  touch の部品（`include/libc/keiland.h`）。PDF Viewer の `touch.c` がその使い方の見本。
- libpng-compat は simplified API（interlace は不可）、libjpeg-compat は baseline と progressive（APPn を `jpeg_save_markers` で読める → EXIF の向き）、
  libgif-compat は `DGifSlurp`・`DGifSavedExtensionToGCB`（動く GIF の frame と delay）。
- App Home は `/etc/keiland/apps.conf`（デモは `plan/ws035/demo/apps.conf`）か `userland/desktop/wayland/home.c` の組み込みの一覧。tile の絵は
  `userland/desktop/wayland/icons.c`。どちらも wayland の担当の file。
- shader は `glslc`・`spirv-val`（host にある）で SPIR-V にして header で tree に置く（`regenerate.py`）。i915 の compiler のため `gl_VertexIndex` を使わない。
- host に Python の PIL（11.1.0）・ImageMagick・cjpeg がある（試験の画像を作れる）。guest の touch の注入は `CONFIG_INPUT_TEST_INJECT=y` の image だけ。

## main・ユーザーへの依頼（p002 の前後）

- 他の担当の file の最小の差分（design.md §10）: `home.c` の 1 行、`icons.c`・`icons.h` の絵、`plan/ws035/demo/apps.conf` の 1 行、
  `ZEDBSD_USER_PROGRAMS` への `imageview`。p002 では自分の guest の中だけで代わりを置いて試す。
- 判断の点 J1〜J11（design.md §12）。

## Resume point

設計まで完了。次は p002（実装）。
