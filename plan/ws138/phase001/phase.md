<!-- awesome-plan project=zedbsd record=ws138-p001 -->

# ws138-p001: PNG と JPEG の背景を読めるようにする（既定の path は変えない）

Status: in-progress（2026-10-05 P1 generation17。実装・host 試験・build は済み。QEMU は p002 とまとめて T1、その結果まで cleared にしない）
Disposition: normal
Parent: [WS138](../ws.md)
Queue: Q1 の 2026-10-05 の割り当て（ベータ2）
設計: [ws.md](../ws.md) の D2・D3・D5・D6・D7・D8 と、ユーザーの決定 U2〜U8（2026-10-04）
依存: なし
時限の目安: 実装 5 h

## 改訂（2026-10-05、ユーザーの決定 U1〜U8 の反映）

初版（判断の前に書いた）からの違い:

- **U4 PNG と JPEG だけ**: 背景の読み込みは PNG と JPEG にする。JPEG は既存の `libjpeg-compat`。PPM を読む code は**消す**が、main の image と
  試験は p002 まで既定の `wallpaper.ppm` を使うので、**PPM の読み込みは p002 の切り替えの commit で消す**（D8: main を途中で壊さない）。
  p001 の共通の復号は、PPM を「移行の間だけ」の形で持ち、p002 で取り除く。
- **U7 reset は thread の道**: 同期の復号の道（`zwl_glass_wallpaper`）を消す。file を読む変更はすべて `zwl_glass_wallpaper_begin`（WS135）を通し、
  同期のまま残すのは file を読まない「風景に戻す」だけ（`zwl_glass_landscape`）。
- **U8 黒で合成**: alpha のある PNG の透明な所は黒で合成する。
- **U5 最大の圧縮**: 変換の道具は行ごとに filter を選び（best）、zlib の圧縮は 9。p002 の generate.py も同じ。
- **U2**: 他の WS の file と `platform/amd64/vmunix.mk`（compositor と Settings の link の規則）は自分で直さず、Q1 が merge の時に main で掛ける差分を
  この phase.md に書く。
- **共通の復号**: 初版は compositor・Settings に別々に書く案だったが、PNG と JPEG の 2 形式になったので、1 つの source
  `userland/desktop/picture/wallpaper.c`（`kl_wallpaper_decode`）にまとめ、compositor と Settings が compile して使う。host 試験もこの関数に掛ける
  （`userland/desktop/picture/picture.c` は GIF の library に依るので使わない）。

## 範囲

- **入る**:
  - 共通の復号 `userland/desktop/picture/wallpaper.c`・`wallpaper.h`（新）: PNG（透明は黒で合成）・JPEG を RGB の 3 byte／画素に。画素の上限
    （一辺 8192、画素 16M）。移行の間だけ PPM（P6、最大値 255）。
  - compositor（`glass.c`）: 起動の prefetch の thread と、session 中の loader の thread で `kl_wallpaper_decode`。同期の復号の道を消す（U7）。
  - Settings（`look.c`）: 一覧に `.png`・`.jpg`・`.jpeg`（と移行の間の `.ppm`）、縮小は `kl_wallpaper_decode` の全体の復号から。
  - Files（`ui-home.c`）: hero を `fm_image_load` に（D5）。
  - tree に `Birch-Lake.png`・`Lakeside.png` を足す（`.ppm` は p002 で消す）。変換の道具 `userland/desktop/wallpapers/ppm-to-png.py`。
  - host 試験 `plan/ws138/tests/host-wallpaper-decode.c`（PNG・透明の黒・JPEG・上限・壊れた file）。
- **入らない**（p002）: 既定の path、generate.py、make の data、`.ppm` と PPM の読み込みの削除、試験の script の置き換え。

## 実装

### 1. 変換の道具と tree の PNG（D6・U5）

- `userland/desktop/wallpapers/ppm-to-png.py`: P6 を読み、8 bit RGB・非 interlace の PNG（行ごとに None・Sub・Up・Average・Paeth から「filter 後の byte を
  符号付きで見た絶対値の和」が最小の物、zlib 9）を書き、書いた PNG を自分で復号して元の画素と byte で比べる（違えば exit 1）。`write_png` は
  p002 の generate.py が import する。
- `Birch-Lake.png`・`Lakeside.png` を作り、README に PNG の行を足す（出どころと ws099-p019 の link は保つ）。

### 2. 共通の復号（`userland/desktop/picture/wallpaper.c`）

```c
struct kl_wallpaper_image { unsigned char *rgb; uint32_t width; uint32_t height; };  /* 3 byte／画素、行は詰める */
int kl_wallpaper_decode(const unsigned char *data, size_t size, struct kl_wallpaper_image *image);
```

- 先頭の bytes で形式を決める: PNG の signature、JPEG の `FF D8 FF`、（移行の間）`P6`。他は EINVAL。
- PNG: `png_image_begin_read_from_memory` → 上限の確かめ → `PNG_FORMAT_RGB`、`background` は黒 → `png_image_finish_read`（finish の後は
  `png_image_free` を呼ばない）。
- JPEG: libjpeg-compat、`out_color_space = JCS_RGB`（灰色も RGB に）、CMYK は EINVAL、上限の確かめは `jpeg_start_decompress` の後。EXIF の向きは
  見ない（背景の写真の向きは制限として記録）。
- 上限: 一辺 8192、画素 16M（Files の `THUMB_PIXELS_MAX` と同じ）。越えたら EFBIG。

### 3. compositor（`userland/desktop/wayland/glass.c`）

- `struct wallpaper_picture` は復号済みの RGB（`data`、offset の `pixels` は消す）。`wallpaper_row` はそのまま使える。
- prefetch の thread（`prefetch_run`）は読んで復号まで行う（D7）。`wallpaper_load` は復号済みを受け取る。loader の thread（`loader_run`）も同じ復号。
- 同期の `zwl_glass_wallpaper(server, path)` を `zwl_glass_landscape(server)`（file を読まない）にする。`settings.c` の `settings_apply_wallpaper` は、
  path が無ければ風景、有れば `zwl_glass_wallpaper_begin`（U7）。
- link: compositor は libpng-compat・libz-compat を既に持つ。libjpeg-compat を足す（`wayland/Makefile`・`Makefile.linux`・`Makefile.freebsd`、zedBSD の
  `vmunix.mk` の規則は U2 で Q1）。

### 4. Settings（`userland/desktop/settings/look.c`）

- 一覧: 末尾が `.png`・`.jpg`・`.jpeg`（と移行の間の `.ppm`）。同じ表示の名前が 2 つあれば `.png`、`.jpg`、`.jpeg`、`.ppm` の順で 1 つを残す。
- 縮小: `look_thumbnail` は file を読み、`kl_wallpaper_decode` で全体を復号して、今の crop と 3x3 の平均で詰める。P6 の行を `pread` する今の code
  （`look_ppm_number` など）は消す（PPM は共通の復号の移行の道が読む）。
- link: libpng-compat・libz-compat・libjpeg-compat（`settings/Makefile*`、zedBSD の `vmunix.mk` は U2 で Q1）。

### 5. Files（`userland/desktop/files/ui-home.c`）

- `home_ppm_load` を `fm_image_load` に置き換え、`home_ppm_load`・`home_ppm_number`・`HOME_WALLPAPER_MAX` を消す。

## Q1 が main で掛ける差分（U2）

`platform/amd64/vmunix.mk` の compositor（`bin/wayland`）と Settings（`bin/settings`）の link に `libjpeg-compat.so`（Settings は加えて
`libpng-compat.so`・`libz-compat.so`）を足す。差分は [vmunix.mk.diff](vmunix.mk.diff)（2026-10-05 の main の `fddf23fc` の vmunix.mk に対して作った）。

```sh
git apply plan/ws138/phase001/vmunix.mk.diff
```

この差分を自分の worktree にだけ当てて `bin/wayland`・`bin/settings`・`bin/files` を build し（warning 0）、commit の前に戻した。
`bin/wayland` の NEEDED は libvulkan・libtruetype・libkeiland・libpng-compat・libjpeg-compat・libz-compat・libc。
差分を当てずに merge すると、`wallpaper.o` の jpeg の記号が解決できず `bin/wayland`・`bin/settings` の link が失敗する。

## 確かめ（自分で行う。QEMU は起動しない）

- host 試験 `plan/ws138/tests/run-host-wallpaper-decode.sh`（ASan/UBSan）。
- `make` で zedBSD の `bin/wayland`・`bin/settings`・`bin/files` を warning 0（vmunix.mk の差分を自分の worktree にだけ当てて試し、commit しない）。
- `plan/tools/keiland-os-boundary/check.sh` が PASS。
- 変換の道具の画素の一致。
- QEMU は p002 でまとめて T1 に依頼する。

## 受け入れの条件

1. 上の確かめが通る。2. tree の PNG の画素が元と同じ。3. 既定の path・generate.py・make の data・試験の script は変えていない。
4. p001 は T1 の結果（p002 の依頼）まで cleared にしない。

## 結果

2026-10-05 P1 generation17。実装・host 試験・build まで。QEMU の確認は p002 の切り替えとまとめて T1 に依頼する（未実施）。

| 項目 | 結果 |
| --- | --- |
| 変換の道具 `userland/desktop/wallpapers/ppm-to-png.py` | 行ごとに best の filter、zlib 9。書いた PNG を自分で復号して元の画素と比べる |
| tree の PNG | `Birch-Lake.png`・`Lakeside.png`（1920x1080）。元の PPM と画素が同じ（道具の自己確認、ImageMagick の `compare -metric AE` が 0、host 試験の `same`） |
| 共通の復号 | `userland/desktop/picture/wallpaper.c`・`wallpaper.h`（`kl_wallpaper_decode`）。PNG（透明は黒で合成）・JPEG（CMYK は EINVAL）・移行の PPM。一辺 8192・画素 16M を越えたら EFBIG |
| compositor | `glass.c`: prefetch の thread と loader の thread が読んで復号まで行う。`wallpaper_picture` から offset の `pixels` を消した。`zwl_glass_wallpaper` を消して `zwl_glass_landscape`（file を読まない）に。`settings.c` の `settings_apply_wallpaper` は path が無ければ風景、有れば `zwl_glass_wallpaper_begin`（U7） |
| Settings | `look.c`: 一覧は `.png`・`.jpg`・`.jpeg`・`.ppm`、同じ名前は png→jpg→jpeg→ppm の順で 1 つ。並びは拡張子を除いた名前。縮小は file を読んで（16 MiB まで、ordinary file だけ）全体を復号して作る。`look_ppm_number` と `pread` の code を消した |
| Files | `ui-home.c`: hero を `fm_image_load` に。`home_ppm_load`・`home_ppm_number`・`HOME_WALLPAPER_MAX` を消した |
| link | `wayland/Makefile*` に `wallpaper.c` と libjpeg-compat。`settings/Makefile*` に `wallpaper.c` と libpng・libz・libjpeg-compat。zedBSD の `vmunix.mk` は上の U2 の差分（Q1） |

確かめ（host だけ。QEMU・実機は未実施）:

- `plan/ws138/tests/run-host-wallpaper-decode.sh`（ASan/UBSan、libz/libpng/libjpeg-compat を source から build）: 10 項目すべて ok
  （tree の PNG 2 枚が PPM と同じ画素、透明が黒・不透明はそのまま、JPEG q95 の平均差 3 以内、灰色の JPEG が灰色、CMYK の JPEG が EINVAL、
  幅 8193 が EFBIG、文字の file と切れた PNG が EINVAL、移行の PPM が 1920x1080）。
- zedBSD の `make build/amd64/bin/wayland build/amd64/bin/settings build/amd64/bin/files`（vmunix.mk の差分を当てて）: rc 0、warning 0（`-Werror`）。
- Linux の `make -f userland/desktop/keiland-linux.mk` の `bin/wayland`・`bin/settings`・`bin/files`: rc 0、warning 0。FreeBSD は build していない（Makefile の差分は Linux と同じ形）。
- `plan/tools/keiland-os-boundary/check.sh`: PASS。
- `plan/tools/style-check.py`: 新しい file は `wallpaper.c:203` の `setjmp` の条件だけ（C の規格が `setjmp` を条件の中に置くことを求める。`picture.c:294` と同じ）。
  変えた file は base と同じ数（`glass.c` 4、`settings.c` 1、`look.c` 0、`ui-home.c` 0）。

残り（p002）: 既定の path の切り替え、`.ppm` と PPM の読み込みの削除、generate.py、make の data、試験の script（U2 で Q1）、T1 の QEMU の確認。
制限: JPEG の EXIF の向きは見ない。

補足: prefetch が取られないまま look が閉じたとき（起動の失敗の道）は、`zwl_glass_close` が thread を join して復号済みの画素を解放する（`prefetch_drop`。従来は file の bytes が残っていた）。
