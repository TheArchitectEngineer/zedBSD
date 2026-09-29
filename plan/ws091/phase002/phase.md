<!-- awesome-plan project=zedbsd record=ws091-p002 -->

# ws091-p002: 画像 viewer の実装

Status: cleared（2026-09-29。touch の guest での注入と実機は未実施、下の「未実施」）
Disposition: normal
Parent: [WS091](../ws.md)
Queue: main の依頼（2026-09-29、worktree `.claude/worktrees/ws091-imageview`、branch `wt/ws091`、main 3a80c07c に同期してから）
Approval: main の依頼「p002（実装）を始めてください」。J1〜J11 は既定を採用（main、2026-09-29）。他の担当の file への最小の差分
（home.c の 1 行、icons.c・icons.h の絵、`plan/ws035/demo/apps.conf` の 1 行、`config/ci/config-amd64.mk` と
`plan/ws075/demo/config-demo-hdmi.mk` への `imageview`、`platform/amd64/vmunix.mk` の link の規則）は main が branch の上で許可。

## 範囲と受け入れ

[design.md](../design.md) のとおりに、PNG・JPEG・GIF の表示、fit・拡大縮小・pan、touch の pinch と慣性、同じ folder の前後の画像、
`/bin/imageview FILE|FOLDER`、App Home への登録。受け入れ: build の warning 0、host の復号と表示の試験が PIL と一致、Venus の guest で
各操作の log と画面。

## 成果物（commit、すべて `WIP`）

| commit | 内容 |
| --- | --- |
| 25e73229 | `userland/desktop/imageview/`（約 11000 行、PDF Viewer の型から）と `platform/amd64/vmunix.mk` の `$(BUILD)/bin/imageview` の link の規則 |
| c45dba93 | App Home（`userland/desktop/wayland/home.c` の 1 行、`icons.c`・`icons.h` の絵 `image`）、`plan/ws035/demo/apps.conf` の 1 行、`config/ci/config-amd64.mk`・`plan/ws075/demo/config-demo-hdmi.mk` への `imageview`（hdmi は libz-compat・libpng-compat・libgif-compat も） |
| a1862de1 | host の試験 `plan/ws091/tests/host-imageview.c`・`run-host.sh` |
| 584eea59 | key の repeat の直し、拡大した画像を card の中で切る scissor、log の追加、guest の試験 `imageview-guest.sh`・`make-images.py` |

- file: `image.c`（magic で判定、PNG・JPEG・GIF の復号、EXIF の向き 1〜8、`--max-dimension`・64 Mpx までの半分への縮小、透明は 8 px の市松に合成、
  256 px までの縮小の段）、`folder.c`（同じ folder、隠し file を除く、数字を数として比べる自然順）、`view.c`（fit・zoom・pan・回転・swipe・
  前後の先読み・key・pointer・wheel）、`draw.c`（空の画面の Kei の印と Open…、壊れた画像の card、chip、message、chooser）、`present.c`（画像の
  texture と CPU の canvas の 2 層、画像は area の scissor で切る、3 倍以上は nearest）、`window.c`（xdg の全画面、key の repeat）、`menu.c`・
  `titlebar.c`（Previous・Next・i / n・−・+・Fit・Rotate・Full Screen）、`touch.c`（pinch・慣性・double tap・long press、libkeiland の gesture と scroller）。
- 動く GIF は disposal（BACKGROUND は透明、PREVIOUS は戻す）を合成した frame を持ち、20 ms 未満の delay は 100 ms。
- file の選択（Ctrl+O）は最小の一覧だけ。WS092 の共有の `keiland_file_chooser_*` ができたら置き換える（main の指示）。
- 画面の文字は Kei だけ（Keiland・libkeiland を出さない）。title は「名前 — Image Viewer」。

## 確認

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| build | `timeout 900 make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws091-amd64 build/ws091-amd64/bin/imageview`（wayland も同様） | rc 0、warning 0 |
| style | `python3 plan/tools/style-check.py userland/desktop/imageview/*.[ch]` | 残りは `image.c` の `if (setjmp(...) != 0)` の 1 件（setjmp は条件の中でしか使えない、design の例外） |
| host | `timeout 600 sh plan/ws091/tests/run-host.sh` | PASS。PNG（RGB・RGBA・16 bit gray・palette の tRNS・縮小の段 5）、JPEG（baseline・progressive・gray・EXIF 6・CMYK、PIL と差 2 以内）、GIF（静止・動く 3 frame・delay）、folder の順、view（fit・zoom・回転・quad・前後・先読み・swipe） |
| guest（QEMU、Venus） | `timeout 2400 sh plan/ws091/tests/imageview-guest.sh build/ws091-shots install home empty fit next zoom wheel portrait rotate alpha gif pixels broken swipe fullscreen chooser`（修正の後に `install fit next portrait rotate alpha gif fullscreen stop` を再実行） | 全部の expect_log が ok、zdesktop の ERROR 0 |

guest は main の `build/ws035-sq/hdd-image.img` の複写（`build/ws091-run/base.img`、`GUEST_RUNTIME=build/ws091-run`）に、この worktree の imageview・
wayland・library を入れて試した。判定は program 自身の log（`IMAGEVIEW SHOW/ZOOM/TURN/SWIPE/FULLSCREEN/CHOOSER`）を SSH で読んだもの。
画面（`build/ws091-shots/`）: home（App Home の Image Viewer の tile）、empty、fit、next（4032×2268 の JPEG）、zoom-100・zoom-drag（card の中で切れる）・
zoom-fit、wheel、portrait（EXIF 6 で縦）、rotate、alpha（市松の上）、gif-1・gif-2（別の frame）、pixels・pixels-zoom（1600 % で鋭い）、broken、swipe、
fullscreen（黒、暗い chip）、chooser。guest の復号の時間: 1672×941 の PNG 227 ms、4032×2268 の JPEG 304 ms、縦の JPEG 109 ms。

## 途中で直したこと

- 矢印・Home が 2 回効いた: 重い復号で loop が止まる間に、key の repeat が release を読む前に発火していた。`iv_window_repeat_wait` で待ち時間だけを
  求め、repeat は dispatch の後だけにした。Esc・F・R・0・1・Space・Enter・Home・End などの切り替えの key は repeat しない（全画面の切り替え中の
  Esc の repeat で全画面に戻っていた）。**PDF Viewer にも dispatch の前に repeat を発火する同じ型がある**（担当外、main に報告）。
- 拡大した画像が card の余白の上に出た: 画像の quad を area の scissor で切った。
- 壊れた画像の error は zedBSD の EINVAL（3）で、試験は `error=[1-9]` を見る。
- 全画面・swipe の直後の画面の取り込みが古い（「Display output is not active」）ことがある。Venus の scanout の取り込みの遅れで、log は正しい
  状態を示し、pointer を数回動かした後の取り込みは正しい。試験の fullscreen の step は待ちを長くした。

## 未実施・制限

- touch（pinch・慣性・double tap・long press）の guest での確認は未実施（`CONFIG_INPUT_TEST_INJECT=y` の image が要る）。host の view の試験と
  PDF Viewer と同じ部品の使い方まで。
- 実機（i915）は未実施。QEMU の Venus だけ。
- Files からの起動は WS093。
- sysroot: make がこの worktree の `build/amd64/sysroot` を作り直した（worktree の中だけ。共有の toolchain は触っていない）。clang・libcxx を含む
  image は build していない。

## Resume point

実装まで完了。次は p003（全文の規約と回帰、main の指示の後）。
