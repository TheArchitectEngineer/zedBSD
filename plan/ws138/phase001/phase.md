<!-- awesome-plan project=zedbsd record=ws138-p001 -->

# ws138-p001: compositor・Settings・Files の PNG の背景と、tree・生成・make の data

Status: planned
Disposition: normal
Parent: [WS138](../ws.md)
Queue: none
設計: [ws.md](../ws.md) の D1〜D6
依存: なし（始める時期は ws.md の U1）
時限の目安: 実装 4 h

## 範囲

- **入る**: ws.md の「関係する source の path」の、試験の script 以外の全て。
  - `plan/ws089/tests/host-build.sh`・`host-wallpaper.sh`（Settings の host 試験。PNG を compile して試すため）。
  - `plan/tools/files/host-render.c` の既定の path の文字列。
- **入らない**: 他の試験と demo の script の置き換え（p002）。T への依頼（p002）。
- **所有する path**: 上の source、`userland/desktop/keiland/wallpapers/`、`plan/ws138/`。
  `plan/ws089/tests/host-build.sh`・`host-wallpaper.sh`、`plan/tools/files/host-render.c`、`plan/tools/keiland-launcher/check.py` は、
  他の WS の物なので Q1 の許可（ws.md の U2）の後に触る。

## 始める前に読む物

1. `AGENTS.md`、`plan/guardrail.md` の「配置」の段落（compositor の OS の境界）、[ws.md](../ws.md)。
2. `plan/coding-style.md` の §14 の checklist（特に「条件の中で関数を呼ばない」「関数の呼び出しごとに comment」「成功の return を分ける」）。
3. `include/libc/compat/png/png.h`（API）、`userland/desktop/files/thumb.c` の `thumb_png`（483-550、使い方の手本）。
4. 下の手順に出てくる関数の今の code。

## 手順

### 1. tree の 2 枚を PNG に（D6）

1. `plan/ws138/tests/ppm-to-png.py`（新、Python の標準 library だけ）を作る。
   - P6・最大値 255 の PPM を読み、8 bit・RGB（色の型 2）・非 interlace の PNG を書く。
   - 行ごとの filter は、None・Sub・Up・Average・Paeth のうち、その行の「filter 後の byte を符号付きで見た絶対値の和」が最小の物を選ぶ
     （libpng の既定の考え方）。U5 で filter 0 に決まったら、None だけでよい。
   - zlib の圧縮の程度は 9。
   - 書いた後に、自分で PNG を復号し（zlib.decompress と filter の逆）、RGB が元の PPM の画素と byte で同じことを確かめる。
     違えば exit 1。
   - 使い方: `python3 plan/ws138/tests/ppm-to-png.py IN.ppm OUT.png`。
2. 変換する。
   ```sh
   python3 plan/ws138/tests/ppm-to-png.py userland/desktop/keiland/wallpapers/Birch-Lake.ppm userland/desktop/keiland/wallpapers/Birch-Lake.png
   python3 plan/ws138/tests/ppm-to-png.py userland/desktop/keiland/wallpapers/Lakeside.ppm userland/desktop/keiland/wallpapers/Lakeside.png
   ```
   - 大きさを記録する（見積もりは filter 0 で 2.1 MB と 0.2 MB）。
   - 別の道具でも確かめる: host に ImageMagick があれば `compare -metric AE a.ppm a.png null:` が 0。無ければ「未実施」と書く。
3. `git rm userland/desktop/keiland/wallpapers/Birch-Lake.ppm userland/desktop/keiland/wallpapers/Lakeside.ppm`。
4. `userland/desktop/keiland/wallpapers/README.md`:
   - 表の File を `.png` にする。
   - Origin の後に「2026-xx-xx、ws138-p001 で PPM から PNG に可逆に変換（画素は同じ、`plan/ws138/tests/ppm-to-png.py`）」を足す。
   - 本文の「the name without `.ppm`」を「the name without the extension」にする。

### 2. compositor（`userland/desktop/wayland/glass.c`）

1. `#include <compat/png/png.h>` を足す。wayland の Makefile はすでに libpng-compat を依存に持つ。
   include の path が通らなければ、Files の `thumb.c:29` と同じ書き方にする。
2. 定数 `GLASS_WALLPAPER_SIDE_MAX 8192U` を足す。
3. `wallpaper_decode`（1356-）の始めで、`size >= 8` かつ先頭 8 byte が PNG の signature なら、新しい static 関数
   `wallpaper_decode_png(data, size, picture)` を呼んで返す。それ以外は今の P6 の道。
4. `wallpaper_decode_png` の流れ（手本は `thumb_png`）。返り値は 0 か errno。
   1. `memset(&png, 0, sizeof(png)); png.version = PNG_IMAGE_VERSION;`
   2. `ok = png_image_begin_read_from_memory(&png, data, size);` が 0 なら `free(data)` して EINVAL。
   3. 幅・高さが 0 か `GLASS_WALLPAPER_SIDE_MAX` を越えたら、`png_image_free`・`free(data)` して EINVAL。
   4. `png.format = PNG_FORMAT_RGB;`、`pixels = malloc(PNG_IMAGE_SIZE(png));`。NULL なら free して ENOMEM。
   5. `ok = png_image_finish_read(&png, NULL, pixels, 0, NULL);`
   6. ここで `free(data)`。memory から読む時、data は finish の終わりまで要る。
   7. 失敗なら `free(pixels)` して EINVAL。
   8. `picture->data = pixels; picture->pixels = 0; picture->width = png.width; picture->height = png.height;`
5. `struct wallpaper_picture` の comment（104 行）を「A wallpaper read: its RGB bytes (a PPM file's, or a PNG decoded) …」の意味に直す。
   `wallpaper_load` の comment（1320）、`main.c:461` の「a binary PPM」、`main.c:107` の usage の文（`--wallpaper=/path.ppm` の形なら `/path.png`）も直す。
6. thread（`loader_run`・prefetch）から呼ばれるので、libpng-compat が大域の可変の状態を持たないことを確かめる。
   `grep -n '^static [a-z_ ]*[a-z_]\+;\|^[a-z_ ]*[a-z_]\+ = ' userland/base/libpng-compat/read.c` で、file-scope の変数が無いか、const だけであること。
   結果を「結果」に書く。

### 3. Settings（`userland/desktop/settings/look.c` と Makefile）

1. `LOOK_DEFAULT_PICTURE` を `KEILAND_DATADIR "/keiland/wallpaper.png"` にする。
2. 一覧（340-370）は、名前の末尾が `.png` か `.ppm` の物を取る（D4）。
   - 末尾の比べ方は、今の `strcmp(entry->d_name + length - 4U, ".ppm")` に `.png` の比べを足す。
     規約で条件の中に関数の呼び出しを置けないので、結果を変数に受けてから比べる。
   - 表示の名前は今と同じく末尾の 4 文字を落とす。
3. `look_thumbnail` を分ける。
   1. 今の関数を `look_thumbnail_ppm` に改名する（中身は変えない）。
   2. 新しい `look_thumbnail_png(path, image)` を作る。
      - `png_image_begin_read_from_file` → 幅の上限（`LOOK_WIDTH_MAX`、高さも同じ上限）→ `PNG_FORMAT_RGB` で全体を復号する。
      - 今の ppm の版と同じ crop と 3x3 の平均の loop で、`fm_image` に詰める。
      - loop は「source の行の pointer を、PNG なら buffer の中、PPM なら `pread` した buffer」にすれば共有できる。
        重複が大きければ、行の取り出しを関数の pointer にせず、`row` を引数に取る static 関数 `look_thumbnail_row(sums, row, crop_left, crop_width)` に分ける。
   3. 新しい `look_thumbnail(path, image)` を作る。file の先頭 8 byte を読んで、PNG なら `look_thumbnail_png`、`P6` なら `look_thumbnail_ppm`、他は EINVAL。
4. `#include <compat/png/png.h>`。
5. Makefile の依存に libpng-compat と libz-compat を足す。
   - `userland/desktop/settings/Makefile:19` の依存の並びに `base/libz-compat base/libpng-compat`（Files の `files/Makefile:33` と同じ書き方）。
   - `Makefile.linux:29`・`Makefile.freebsd:29` の library の並びに `libpng-compat.so libz-compat.so`（Files の `Makefile.linux:55` と同じ）。
6. host 試験の build（Q1 の許可の後）。
   - `plan/ws089/tests/host-build.sh` に、libz-compat の `inflate.c`・`checksum.c` と libpng-compat の `read.c` の compile を足す
     （`plan/tools/files/host-build.sh:22` の `include/compat` の symlink と 38-46 行の loop が手本）。
   - `host-wallpaper.sh` の link に、その object を足す。
7. `plan/ws089/tests/host-wallpaper.sh` の data（21-25 行）を変える。
   - `wallpaper.png` → `Lakeside.png`。
   - folder に `Lakeside.png`・`Birch-Lake.png`、壊れた `Broken.png`。
   - **PPM を読めることの試験として 1 枚の PPM を足す**: `python3` で 64x40 の P6 を `Small.ppm` として作る。
   - 確かめ: 45 行の grep を `Broken.png … error=22` にし、`Small.ppm` が error=0 で読めたことの grep を足す。log の行の形は今の look.c の物を見る。

### 4. Files（`userland/desktop/files/ui-home.c`）

1. `home_hero`（380-）の `home_ppm_load(app->wallpaper, &app->hero_source)` を `fm_image_load(app->wallpaper, &app->hero_source)` にする。
   `fm_image_load` は `files.h:1535` に宣言がある。
2. `home_ppm_load`・`home_ppm_number` とその forward 宣言（87-88）を消す。
3. 先頭の comment（13 行の `wallpaper.ppm`）と `files.h:56` の `FM_WALLPAPER` を `.png` にする。
4. `plan/tools/files/host-render.c:18` の既定の path の文字列を `.png` にする（Q1 の許可の後）。

### 5. 既定の path

- `userland/desktop/sessiond/sessiond.h:28` → `wallpaper.png`。
- `userland/desktop/sessiond/session.sh:16` の 2 か所 → `wallpaper.png`。
- `userland/desktop/wayland/keiland-desktop.in:41`・`userland/desktop/wayland/data/keiland.desktop:4` → `wallpaper.png`。
- `plan/tools/keiland-launcher/check.py:44` の期待の文字列 → `wallpaper.png`（37 行の `a picture.ppm` は引数の受け渡しの試験なので変えなくてよい）。

### 6. 生成の背景と make の data

1. `userland/desktop/wallpapers/generate.py`:
   - `write_ppm` を `write_png` にする。手順 1 の変換の道具と同じ書き方（filter の選択、zlib 9）。`write_preview` の `chunk` を共有してよい。
   - 書く名前は `<名前>.png`、表示は `wallpaper: …png`。docstring の P6 の説明を PNG に直す。
   - 同じ command は同じ bytes を書く（今の性質を保つ。zlib の版が同じなら決まる）。
2. `userland/desktop/wallpapers/Makefile`: `.ppm` を `.png` にする（13・18 行）。
3. `userland/desktop/keiland-linux.mk:141-148`:
   - `.ppm` を `.png` にする。
   - 145 行の `build/ws035-wallpaper/wallpaper.ppm` を見る部分を消す（U3 の (a) なら Aurora.png、(b) なら `userland/desktop/keiland/wallpapers/Birch-Lake.png`）。
   - 146 行の data の名前を `share/keiland/wallpaper.png` にする。
4. `userland/desktop/keiland-freebsd.mk:142-149`: 同じく `.png` にする。
5. `userland/desktop/LINUX.md:33・42`、`README.freebsd.md:93`: `.ppm` を `.png` にする。
   LINUX.md の「既定は既存のユーザー画像 cache」の文は U3 の決めに合わせる。
6. `tools/release/keiland-linux-deb/run.py`: `grep -n ppm` で背景の path があれば `.png` にする（2026-10-04 の grep では無い）。

## 確かめ（自分で行う。QEMU は起動しない）

```sh
B=build/ws138-p001
make ZEDBSD_CONFIG=plan/ws089/tests/config-amd64-settings.mk BUILD=$B \
    $B/bin/wayland $B/bin/settings $B/bin/files 2>&1 | tee $B.log
grep -c 'warning:' $B.log                                  # 0
timeout 180 make -j16 keiland-linux KEILAND_LINUX_BUILD=build/ws138-linux 2>&1 | tee build/ws138-linux.log
grep -c 'warning:' build/ws138-linux.log                   # 0（host の gcc で Linux の build。QEMU は要らない）
ls build/ws138-linux/share/keiland/                         # wallpaper.png と wallpapers/*.png
timeout 30 sh plan/tools/keiland-linux/makefile-sync.sh     # PASS（Settings の依存を 3 つの Makefile で揃えた）
sh plan/tools/keiland-os-boundary/check.sh                  # PASS
sh plan/ws089/tests/host-build.sh && sh plan/ws089/tests/host-wallpaper.sh   # host-wallpaper: PASS、page.png を目で見る
sh plan/tools/files/host-build.sh && sh plan/tools/files/host-p014.sh        # PASS。home.png の hero が湖の絵（描いた絵ではない）
python3 userland/desktop/wallpapers/generate.py build/ws138-gen --preview=build/ws138-gen/preview.png   # 5 枚の .png
python3 plan/tools/style-check.py userland/desktop/wayland/glass.c userland/desktop/settings/look.c userland/desktop/files/ui-home.c --summary
git diff --check
```

- FreeBSD の build は、FreeBSD の試験の guest（WS137）で行う。p002 で T に依頼する。
- `host-p014.sh` が `--wallpaper` を渡さず既定の path を読む作りなら、host の既定の path の file は無い。その時は hero は描いた絵になる。
  どちらかを `host-render.c` の usage で確かめ、PNG を渡して見る手順を「結果」に書く。

## 受け入れの条件

1. 上の確かめが全て通る（warning 0、PASS、grep の確かめ）。
2. tree に `.ppm` の背景が無く、2 枚の PNG の画素が元と同じ（変換の道具の確かめ）。
3. 変えた関数が全文規約に合う（style-check の違反が増えていない）。
4. QEMU の確かめは p002 でまとめて T に依頼する。p001 は T の結果まで cleared にしない。

## 結果

（未実施）
