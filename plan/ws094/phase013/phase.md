<!-- awesome-plan project=zedbsd record=ws094-p013 -->

# ws094-p013: L3c jpg と gif の thumbnail

Status: cleared（2026-09-30、host 試験と QEMU の Venus。実機は未実施）
Disposition: normal
Parent: [WS094](../ws.md)
Queue: main（Q1）の依頼（2026-09-30、P4、worktree `.claude/worktrees/ws090-widgets`、branch `wt/ws090`、`git merge main -m WIP` の後）

## 範囲と受け入れ

Files の `fm_image_load`（`userland/desktop/files/thumb.c`）は PPM・PGM・PNG しか読まず、jpg・gif は `ENOTSUP`（error=21）で汎用の
icon になっていた（p009 の見当）。Image Viewer（`userland/desktop/imageview/image.c`）と同じ読み方で jpg（libjpeg-compat、EXIF の向き）と
gif（libgif-compat、最初の frame）を読む。共通にできる code は写さずに共有する。imageview は大きく変えない。

受け入れ:
- host の試験で、正しい jpg と gif の読み込み、壊れた file の `EINVAL`、EXIF の向き。
- guest の perf100 の jpg・gif に thumbnail が出る画面。
- 回帰: host-model.sh・host-desktop.sh・boot test。

## 設計: 共有の decoder（`userland/desktop/picture/`）

Image Viewer の decoder のうち、2 つの program が同じに要る部分を `userland/desktop/picture/picture.c`・`picture.h` に移した。
`userland/desktop/artwork/mark.c` と同じく、source を各 program へ compile して入れる（library は作らない）。

| 関数 | 中身（Image Viewer から移したもの） |
| --- | --- |
| `keiland_picture_jpeg` | JPEG を file（`jpeg_stdio_src`）か memory（`jpeg_mem_src`）から読む。RGB・grey・CMYK（Adobe の反転を含む）、EXIF の向きを返す。辺・画素数の上限は呼び手が渡す（Image Viewer は 32768 の辺だけ、Files は 8192 の辺と 16 Mi 画素） |
| `keiland_picture_exif_orientation`・`keiland_picture_orient` | EXIF の APP1 の向きを読む。向き 2〜8 に従って回す・裏返す |
| `keiland_picture_gif_draw`・`keiland_picture_gif_first` | GIF の frame を screen に描く（透明色は残す）。最初の frame の picture を作る |
| `keiland_picture_premultiply` | 乗算済みの 0xAARRGGBB |

Image Viewer（`image.c`）の変更は置き換えだけにした。`image_jpeg` は file を開いて共有の decoder を呼び、error を今までと同じ文言
（damaged・too large・memory）に写す。`iv_image_orientation` は共有の関数を呼ぶ。GIF の動画の合成（frame の保持・待ち時間・dispose）と PNG は
image.c に残した。`struct image_picture` は `struct keiland_picture` にした（同じ member）。

Files（`thumb.c`）: 先頭の bytes で JPEG（`FF D8 FF`）と GIF（`GIF87a`・`GIF89a`）を見分ける。JPEG は memory から読んで EXIF の向きで回し、
GIF は `DGifOpen` に memory の読み手を渡して `DGifSlurp` の後、最初の frame を使う。画素は `fm_image` にそのまま渡す（複写しない）。
大きすぎる画像は PNG と同じ `EFBIG`、壊れたものは `EINVAL`。Files の preview（同じ `fm_image_load`）でも jpg・gif が読めるようになる。

## 変えたもの

| file | 変更 |
| --- | --- |
| `userland/desktop/picture/picture.c`・`picture.h` | 新規（上の共有の decoder） |
| `userland/desktop/imageview/image.c` | JPEG・EXIF・回転・GIF の frame の描画・乗算を共有の関数に置き換え、元の関数を消した |
| `userland/desktop/imageview/Makefile` | source に `picture.c` |
| `userland/desktop/files/thumb.c` | JPEG と GIF（上）。`fm_image_load` の説明 |
| `userland/desktop/files/Makefile` | source に `picture.c`、依存に `base/libjpeg-compat`・`base/libgif-compat` |
| `platform/amd64/vmunix.mk` | `bin/files` の link に `libjpeg-compat.so`・`libgif-compat.so`（必要な依存の検査にも）。依存の library は package の依存から自動で入る（`Makefile` の依存の閉包）ので、config は変えていない |
| `plan/tools/files/host-build.sh` | host の build に libjpeg-compat・libgif-compat・`picture.c` |
| `plan/tools/imageview/run-host.sh` | Image Viewer の host の build に `picture.c` |
| `plan/ws094/tests/host-thumb.c`・`host-thumb.sh` | 新規の host 試験（下） |
| `plan/ws094/tests/files-desktop-guest.sh` | `install` が `wayland`・`files` の put を 3 回まで試す（起動の直後は greeter の compositor の終わりが間に合わず put が失敗した。p008・p009 でも 1 回目が失敗していた） |

## 確認

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| build | `make -j16 ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk BUILD=build/amd64 build/amd64/bin/files build/amd64/bin/wayland build/amd64/bin/imageview …/libkeiland.so …/libgif-compat.so …/libjpeg-compat.so` | rc 0、warning 0（link の規則を足す前は `DGifOpen` などが未定義で失敗） |
| style | `plan/tools/style-check.py`（picture.c・picture.h・thumb.c・image.c・host-thumb.c）、`plan/tools/imageview/style-extra.py`、`git diff --check` | style-check は picture.c の `setjmp` の 1 件だけ（Image Viewer の元の code にも同じ 1 件があり、setjmp は条件の中に置く必要がある）。style-extra は thumb.c の既存の 3 件だけ |
| host: 新 | `plan/ws094/tests/host-thumb.sh` | PASS。JPEG の RGB・grey・CMYK が PIL と一致（worst 0）、EXIF の向き 1〜8 が PIL の `exif_transpose` と一致（5〜8 は辺が入れ替わる）、GIF の透明色・動画の最初の frame が一致、壊れた file 4 種（途中で切れた JPEG、中身がでたらめな JPEG と GIF、frame の無い GIF）が `EINVAL` |
| host: 回帰 | `plan/tools/files/host-model.sh`、`plan/ws094/tests/host-desktop.sh`、`plan/tools/imageview/run-host.sh` | PASS、PASS、PASS（Image Viewer の jpeg・exif・cmyk・gif の比較を含む） |
| guest: Files | `BIN=build/amd64 files-desktop-guest.sh build/ws094-shots/p013 install perf100`（image は `build/ws081/demo-win-venus.img` の複写。画像 20 のうち jpg 8・gif 4） | PASS。`ZFILES THUMB` の jpg・gif がすべて error=0（jpg 256x144、EXIF 6 の 03-portrait の複写は 144x256 で縦、gif 256x192）。画面 `build/ws094-shots/p013/perf100.png` に jpg・gif の thumbnail（縦の写真は正立） |
| guest: Image Viewer | `imageview-guest.sh build/ws094-shots/p013-iv install fit next portrait rotate alpha gif pixels broken` | PASS（jpg 4032x2268、EXIF の縦 1060x1882、gif 4 frame、壊れた png の拒否。`portrait.png`） |
| boot test | `OUTPUT=build/ws094-p013-boot plan/tools/boot-test.sh build/ws094-run/disk.img` | PASS（`build/ws094-p013-boot/login.png`） |

perf100 の数値（参考、QEMU）: (a) 1958・(a') 2937・(b) 1557・(c) 92 ms、SLOW-FRAME 0。thumbnail は最初の frame の後に作るので (a) は変わらない。

## 残り

- 実機での確認は未実施。
- GIF の thumbnail は `DGifSlurp` で全 frame を読む（最初の frame しか使わない）。大きい動画の GIF では無駄があるが、上限（64 MiB の file、8192 の辺）の中に収まる。
