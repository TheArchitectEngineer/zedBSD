<!-- awesome-plan project=zedbsd record=ws093-p002 -->

# ws093-p002: 既定の対応（画像・text・HTML）

Status: cleared（2026-09-29）
Disposition: normal
Parent: [WS093](../ws.md)
Queue: main の依頼（2026-09-29、worktree `.claude/worktrees/ws093-open`、branch `wt/ws093`、main を merge してから）

## 範囲と受け入れ

[design.md](../design.md) §2: Files の組み込みの表（system の既定）に、png・jpeg・gif → Image Viewer、text 系 → Text Editor、html → Browser を
既定として足す。受け入れ: Venus の guest で double click・Enter・注入の touch の double tap で各 app がその file を開く、画面、build の warning 0、
boot test。

## 変えたこと

- `userland/desktop/files/apps.c` の `apps_builtins` に 3 行（`APPS_IMAGE_TYPES` = `image/png,image/jpeg,image/gif` → Image Viewer
  `/bin/imageview %f`、`text/html` → Browser `/bin/browser %f`、`APPS_TEXT_TYPES` → Text Editor `/bin/textedit %f`）。それぞれ `needs` を持つので、
  program の無い image（と host）では従来の既定のまま。Quick Look は他の画像（bmp・webp・ppm…）と png・jpeg・gif の 2 番目に残る。注釈を更新。
- `plan/tools/files/host-model.c` の期待は直す必要が無かった（host には `/bin/imageview` 等が無いので、host の既定は変わらない）。

## 確認

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| style | `python3 plan/tools/style-check.py userland/desktop/files/apps.c`、`git diff --check` | 0 件 |
| build | `sh build/ws093-build.sh`（`make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws093-amd64 build/ws093-amd64/bin/{files,imageview,textedit,wayland}`） | 4 つとも rc 0、warning 0 |
| host | `sh plan/ws093/tests/host-model.sh`（`plan/tools/files/host-build.sh` の複写に libkeiland の gesture.c・scroll.c・motion.c を足して build、`files-model` を一時 folder で） | open の 6 件はすべて ok。FAIL 1 件「picture: PNG not read yet (ENOTSUP)」は main の apps.c に戻しても同じく出る既存の失敗（thumbnail、この WS と無関係） |
| guest（QEMU、Venus、マウス） | main の `build/ws035-sq/hdd-image.img` の複写（`build/ws093-run`）で `plan/ws093/tests/open-guest.sh build/ws093-shots mouse` | PASS: png・jpeg・gif → Image Viewer、txt → Text Editor、html → Browser、pdf → PDF Viewer（double click）、txt を Enter → Text Editor。各 `ZFILES OPEN … app=… error=0`・`ZFILES LAUNCH … command=/bin/<app> '<path>'`・app の process・新しい window の map、zdesktop の ERROR 0。画面 `open-png/jpeg/gif/text/html/pdf/enter.png` |
| guest（情報の card） | 同じ guest で `open-guest.sh build/ws093-shots info` | PASS: Open with の pill が png「Image Viewer・Quick Look…」、txt「Text Editor・Terminal (less)…」、html「Browser・Text Editor・Terminal (less)・Terminal (ed)」（`info-*.png`） |
| guest（注入の touch） | main の pen の image `build/main-pen/hdd-image.img` の複写（`build/ws093-pen-run`）で `open-guest.sh build/ws093-shots touch` | PASS: png の行の double tap → Image Viewer、txt の行の double tap → Text Editor（Files の `TOUCH tap` 4 件、`touch-png.png`・`touch-text.png`） |
| boot | `OUTPUT=build/ws093-boot-test bash plan/tools/boot-test.sh build/ws093-run/disk.img` | PASS（`build/ws093-shots/boot-login.png`） |

- 判定は Files と zdesktop の log（SSH）・ps・画面。console・serial は読んでいない。起動された app の出力は Files が /dev/null に向けるので、
  zedBSD の ps は引数を出さないため、path は Files の LAUNCH の行と画面（title の file 名と中身）で確かめた。
- 画面は worktree の `build/ws093-shots/`。

## 見つけたこと（main へ）

- `plan/tools/files/host-build.sh` は link できない（`files/touch.c` が ws081-p010 から libkeiland の gesture・scroller・motion を使うが、
  script はそれらを build しない）。この WS では `plan/ws093/tests/host-model.sh` で補った。直すなら `plan/tools/` は main。
- `files-model` の「picture: PNG not read yet (ENOTSUP)」は既存の失敗（main でも出る）。

## 未実施・制限

- 実機（i915、touch panel）は未実施。確認はすべて QEMU。
- guest の image は main の既存の image の複写に、この worktree の files・imageview・textedit・wayland と library を入れたもの。

## Resume point

p003（Always Open With と利用者の一覧への書き込み）から。
