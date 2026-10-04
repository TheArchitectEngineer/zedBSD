<!-- awesome-plan project=zedbsd record=ws138-p001 -->

# ws138-p001: PNG の背景を読めるようにする（既定の path は変えない）

Status: planned
Disposition: normal
Parent: [WS138](../ws.md)
Queue: none
設計: [ws.md](../ws.md) の D1・D2・D4・D5・D6・D7・D8
依存: なし（始める時期は ws.md の U1。`platform/amd64/vmunix.mk` を触るのは U2 の Q1 の許可の後）
時限の目安: 実装 4 h

## 範囲

- **入る**:
  - compositor・Settings・Files が PNG を読めるようにする（PPM も読む）。画素の上限。compositor の起動の復号を thread で行う。
  - Settings の link（zedBSD・Linux・FreeBSD）。
  - tree に `Birch-Lake.png`・`Lakeside.png` を**足す**（`.ppm` は残す）。変換の道具。
  - host 試験（Settings の背景の page、Files の hero）。
- **入らない**（p002 で 1 回の commit で行う。D8）:
  - 既定の path（`wallpaper.ppm` → `.png`）、generate.py、make の data、`.ppm` の削除、試験の script の置き換え、移行（U6）。
  - この Phase の後も main の image と試験は今のまま動く。
- **所有する path**:
  - `userland/desktop/wayland/glass.c`・`main.c`（comment と usage の文だけ）
  - `userland/desktop/settings/look.c`・`settings.h`（comment）・`Makefile*`
  - `userland/desktop/files/ui-home.c`
  - `userland/desktop/wallpapers/ppm-to-png.py`（新）、`userland/desktop/keiland/wallpapers/`（PNG の 2 枚と README）
  - `plan/ws138/`
  - Q1 の許可の後: `platform/amd64/vmunix.mk`（Settings の link の規則）、`plan/ws089/tests/host-build.sh`・`host-wallpaper.sh`

## 始める前に読む物

1. `AGENTS.md`、`plan/guardrail.md` の「配置」の段落（compositor の OS の境界）、[ws.md](../ws.md)。
2. `plan/coding-style.md` の §14 の checklist。特に次の 4 つ。
   - 条件の中で関数を呼ばない。`memcmp` の結果も変数に受けてから比べる。
   - 関数の呼び出しごとに comment を付ける。
   - 意味のある関数の結果を、そのまま return しない（受けてから返す）。
   - 成功の return を失敗の return と分ける。
3. `include/libc/compat/png/png.h`（API）。手本は次の 2 つ。
   - `userland/desktop/files/thumb.c` の `thumb_png`（483-550）
   - `userland/desktop/picture/color-glyph.c:98-143`（compositor の binary の中の PNG の復号）
4. 下の手順に出てくる関数の今の code。

## 手順

### 1. 変換の道具と tree の PNG（D6）

1. `userland/desktop/wallpapers/ppm-to-png.py`（新、Python の標準 library だけ）を作る。
   - P6・最大値 255 の PPM を読み、8 bit・RGB（色の型 2）・非 interlace の PNG を書く。zlib の圧縮の程度は 9。
   - 行ごとの filter: 引数 `--filter=best`（既定）なら、None・Sub・Up・Average・Paeth のうち、その行の「filter 後の byte を符号付きで見た絶対値の和」が最小の物を選ぶ（libpng の既定の考え方）。
     `--filter=up` なら Up に固定する（p002 で generate.py が同じ関数を使う。U5）。
   - PNG を書く関数（chunk・IHDR・IDAT・IEND）は、`generate.py` から import できる形（`def write_png(path, width, height, rgb, filter)`）にする。
   - 書いた後に、自分で PNG を復号する（`zlib.decompress` と filter の逆）。RGB が元の PPM の画素と byte で同じことを確かめ、違えば exit 1。
   - 使い方: `python3 userland/desktop/wallpapers/ppm-to-png.py IN.ppm OUT.png`。
   - 置き場所の理由: README の出どころの link が切れないように、WS の終わりで消える `plan/ws138/tests` に置かない。
2. 変換する。
   ```sh
   python3 userland/desktop/wallpapers/ppm-to-png.py userland/desktop/keiland/wallpapers/Birch-Lake.ppm userland/desktop/keiland/wallpapers/Birch-Lake.png
   python3 userland/desktop/wallpapers/ppm-to-png.py userland/desktop/keiland/wallpapers/Lakeside.ppm userland/desktop/keiland/wallpapers/Lakeside.png
   ```
   - 大きさを記録する（見積もりは filter 0 で 2.1 MB と 0.2 MB）。
   - 別の道具でも確かめる: host に ImageMagick があれば `compare -metric AE a.ppm a.png null:` が 0。無ければ「未実施」と書く。
3. **`.ppm` はまだ消さない**（p002）。
4. `userland/desktop/keiland/wallpapers/README.md`:
   - 表に `.png` の 2 行を足す。Origin は「同じ名前の `.ppm` から PNG に可逆に変換（画素は同じ、`userland/desktop/wallpapers/ppm-to-png.py`）」。
   - `.ppm` の行と、ws099-p019 の出どころの記述はそのまま保つ（p002 で `.ppm` の行を消す）。

### 2. compositor（`userland/desktop/wayland/glass.c`）

1. `#include <compat/png/png.h>` を足す（`picture/color-glyph.c:18` と同じ書き方）。wayland の Makefile はすでに libpng-compat を依存に持つ。
2. 定数を足す: `GLASS_WALLPAPER_SIDE_MAX 8192U` と `GLASS_WALLPAPER_PIXELS_MAX (16U * 1024U * 1024U)`。
   comment に、Files の `THUMB_PIXELS_MAX` と同じ値であることを書く。
3. `wallpaper_decode`（1356-）の始めで、形式を決める。
   1. `size >= 8` の時、`signature = memcmp(data, png_signature, 8U);` で変数に受ける。`png_signature` は file-scope の const の 8 byte の配列。
   2. `signature == 0` なら、`error = wallpaper_decode_png(data, size, picture);` で受けてから、成功と失敗を分けて返す。
   3. それ以外は、今の P6 の道。
4. 新しい static 関数 `wallpaper_decode_png(data, size, picture)` を作る。返り値は 0 か errno。流れは次のとおり。
   1. `memset(&png, 0, sizeof(png)); png.version = PNG_IMAGE_VERSION;`
   2. `ok = png_image_begin_read_from_memory(&png, data, size);` が 0 なら、`free(data)` して EINVAL。
   3. 幅・高さが 0、一辺が `GLASS_WALLPAPER_SIDE_MAX` を越える、`(uint64_t)幅 × 高さ` が `GLASS_WALLPAPER_PIXELS_MAX` を越える、のどれかなら、
      `png_image_free(&png)` と `free(data)` の両方をして EINVAL。
   4. `png.format = PNG_FORMAT_RGB;`、`pixels = malloc(PNG_IMAGE_SIZE(png));`。NULL なら、`png_image_free(&png)` と `free(data)` をして ENOMEM。
   5. alpha の背景（U8）: 決めが「空の色」なら、`png_color` にその色を入れて渡す。決まるまでは NULL。
   6. `ok = png_image_finish_read(&png, background, pixels, 0, NULL);`。finish は成功でも失敗でも image を free するので、この後に `png_image_free` を呼ばない。
   7. `free(data)`。memory から読む時は、data は finish の終わりまで要る。
   8. 失敗なら、`free(pixels)` して EINVAL。
   9. `picture->data = pixels; picture->pixels = 0; picture->width = png.width; picture->height = png.height;`
5. **起動の復号を thread に移す（D7）**。
   1. `struct glass_prefetch` の `data`・`size` を、`struct wallpaper_picture picture` と `int error` に変える。comment も直す。
   2. `prefetch_run`（1495）: `file_read` の後に `wallpaper_decode` まで行い、結果を `picture`・`error` に置く。
      読めなかった時は `error = errno`。
   3. `prefetch_take`（1513）を、復号済みの picture を返す形にする。返り値は「受け取った（1）か、自分で読む必要がある（0）」。
      path が違う時は、picture の data を free して 0 を返す。
   4. `wallpaper_load`（1325）: まず `prefetch_take` で picture を受け取る。受け取れなければ、今のとおり `file_read` と `wallpaper_decode`。
      prefetch の error は、受け取った時にそのまま返す。
6. comment と文を直す。
   - `struct wallpaper_picture` の comment（104 行）を、「A wallpaper read: a PPM file's bytes (pixels at an offset) or a PNG decoded to RGB (offset 0) …」の意味に。
   - 「a binary PPM」の comment: `glass.c:354・574・1320・1350` 付近、`main.c:461`。
   - `main.c:107` の usage の文（`--wallpaper=/path.…`）。
7. reset の同期の道（`settings.c:555` の `zwl_glass_wallpaper`）は変えない。時間は p002 で測る（U7）。

### 3. Settings（`userland/desktop/settings/look.c` と build）

1. `LOOK_DEFAULT_PICTURE` は**変えない**（p002）。
2. 一覧（340-370）: 名前の末尾が `.png` か `.ppm` の物を取る（D4）。
   - 末尾の比べ方は、今の `strcmp(entry->d_name + length - 4U, ".ppm")` に `.png` の比べを足す。どちらも結果を変数に受けてから比べる。
   - 表示の名前は、今と同じく末尾の 4 文字を落とす。
   - 同じ表示の名前が既にあれば足さない（D4）。`qsort` の後で隣同士を比べ、`.png` の方を残す。
3. `look_thumbnail` を分ける。
   1. 今の関数を `look_thumbnail_ppm` に改名する（中身は変えない）。
   2. 共有の部分を、新しい static 関数 `look_thumbnail_row(sums, row, crop_left, crop_width)` に分ける。中身は、1 つの source の行の 3 つの sample を `sums` に足す loop（今の 3 重の loop の中の 2 つ）。
      `look_thumbnail_ppm` もこれを使うように直す（画素は同じ）。
   3. 新しい `look_thumbnail_png(path, image)` を作る。
      - `png_image_begin_read_from_file` を呼ぶ。
      - 一辺は `LOOK_WIDTH_MAX`、画素の数は 16M を上限とし、越えたら `png_image_free` して EFBIG。
      - `PNG_FORMAT_RGB` で全体を復号する（finish の後は `png_image_free` を呼ばない）。
      - 今の ppm の版と同じ crop と 3x3 の平均で、`fm_image` に詰める（`look_thumbnail_row` を使う）。
   4. 新しい `look_thumbnail(path, image)` を作る。
      - file の先頭 8 byte を読み、PNG なら `look_thumbnail_png`、`P6` なら `look_thumbnail_ppm` を呼ぶ。他は EINVAL。
      - 呼んだ結果は変数に受けてから返す。
4. `#include <compat/png/png.h>`。`settings.h:569` の comment（「without .ppm」）を「without the extension」の意味に直す。
5. build の依存:
   - `userland/desktop/settings/Makefile:19` の依存の並びに `base/libz-compat base/libpng-compat`（Files の `files/Makefile:33` と同じ書き方）。
   - `Makefile.linux:29`・`Makefile.freebsd:29` の library の並びに `libpng-compat.so libz-compat.so`（Files の `Makefile.linux:55` と同じ）。
   - **zedBSD の link の規則**（Q1 の許可の後）: `platform/amd64/vmunix.mk` の `$(BUILD)/bin/settings`（1400-1414）に、次の 3 つを足す。手本は `bin/files`（1377-1393）。
     - 前提に `$(DYNAMIC_DIR)/libpng-compat.so $(DYNAMIC_DIR)/libz-compat.so`。
     - link に `-l:libpng-compat.so -l:libz-compat.so`。
     - check に `--needed libpng-compat.so --needed libz-compat.so`。
6. host 試験の build（Q1 の許可の後）:
   - `plan/ws089/tests/host-build.sh` に、libz-compat の `inflate.c`・`checksum.c` と libpng-compat の `read.c` の compile を足す。
     手本は `plan/tools/files/host-build.sh:22`（`include/compat` の symlink）と 38-46 行の loop。
   - `host-wallpaper.sh` の link に、その object を足す。
7. `plan/ws089/tests/host-wallpaper.sh` の data（21-25 行）を、数を変えない形にする（4 枚）。
   - 既定（`wallpaper.ppm`）: 今のまま `Lakeside.ppm`。既定の path は p002 で変える。
   - folder: `Lakeside.png`、`Small.ppm`（`python3` で作る 64x40 の P6。PPM を読めることの試験）、`Broken.png`（`not a picture` の text）。
   - 確かめの行は次のとおり。
     - 41・42・46 行の `count=4` は、そのまま。
     - 44 行の `error=0` の 3 件も、そのまま（既定・Lakeside.png・Small.ppm）。
     - 45 行は `Broken.png … error=22` にする。
   - 冒頭の comment（4-9 行）を新しい中身に直す。

### 4. Files（`userland/desktop/files/ui-home.c`）

1. `home_hero`（380-）の `home_ppm_load(app->wallpaper, &app->hero_source)` を、`fm_image_load(app->wallpaper, &app->hero_source)` にする。
   `fm_image_load` は `files.h:1535` に宣言がある。
2. `home_ppm_load`・`home_ppm_number` とその forward 宣言（87-88）を消す。使われなくなる `HOME_WALLPAPER_MAX`（51 行）も消す。
3. 先頭の comment（13 行）の「a binary PPM」の意味の所を「a picture (PNG or PPM)」に直す。`FM_WALLPAPER`（`files.h:56`）は p002 で変える。

## 確かめ（自分で行う。QEMU は起動しない）

```sh
B=build/ws138-p001
make ZEDBSD_CONFIG=plan/ws089/tests/config-amd64-settings.mk BUILD=$B \
    $B/bin/wayland $B/bin/settings $B/bin/files 2>&1 | tee $B.log
grep -c 'warning:' $B.log                                   # 0
build/llvm/bin/llvm-readelf -d $B/bin/settings | grep NEEDED # libpng-compat.so と libz-compat.so がある
timeout 900 make -j16 keiland-linux KEILAND_LINUX_BUILD=build/ws138-linux 2>&1 | tee build/ws138-linux.log
grep -c 'warning:' build/ws138-linux.log                    # 0（host の gcc。QEMU は要らない）
readelf -d build/ws138-linux/bin/settings | grep NEEDED      # libpng-compat.so と libz-compat.so
sh plan/tools/keiland-os-boundary/check.sh                   # PASS
sh plan/ws089/tests/host-build.sh && sh plan/ws089/tests/host-wallpaper.sh   # host-wallpaper: PASS、page.png を目で見る（Small の tile も絵）
sh plan/tools/files/host-build.sh
sh plan/tools/files/host-run.sh --fresh --wallpaper=$PWD/userland/desktop/keiland/wallpapers/Birch-Lake.png draw=build/ws138-p001/hero-png.ppm
sh plan/tools/files/host-run.sh --fresh --wallpaper=$PWD/userland/desktop/keiland/wallpapers/Birch-Lake.ppm draw=build/ws138-p001/hero-ppm.ppm
#   どちらも dashboard（--start 無し）の hero が湖の絵。2 つの PNG を目で見て、同じであること
python3 plan/tools/style-check.py --summary userland/desktop/wayland/glass.c userland/desktop/settings/look.c userland/desktop/files/ui-home.c
git diff --check
```

- `make keiland-linux` は空の build の directory だと時間がかかる（上限 900 s は推測）。時間切れは記録して、もう一度流す。
- `makefile-sync.sh` は source の一覧だけを比べ、library の一覧は比べない。だから library は `readelf -d` で確かめる。
- style: 作業の前に同じ `--summary` を取っておき、関数ごとに増えていないこと。新しい関数と新しい行の違反は 0。
- FreeBSD の build は p002 で T に依頼する（WS137 の guest）。
- compositor の PNG の復号は、host 試験が無い。p002 の T の試験で確かめる。p001 だけでは T に依頼しない（p002 とまとめる）。

## 受け入れの条件

1. 上の確かめが全て通る（warning 0、NEEDED、PASS、目で見た hero）。
2. tree の PNG の 2 枚の画素が元と同じ（変換の道具の確かめ）。
3. 既定の path・generate.py・make の data・試験の script は変わっていない（`git diff --stat` で確かめる）。main の image と試験は今のまま動く。
4. QEMU の確かめは p002 でまとめて T に依頼する。p001 は T の結果まで cleared にしない。

## 結果

（未実施）

## 着手前に直す点（2026-10-04 ユーザーの判断の反映、Q1。詳細は [ws.md](../ws.md) の「ユーザーの決定」）

この Phase の本文は判断の前に書いた。着手する担当は、最初に次を本文（範囲・受け入れ・手順）へ反映してから実装する。

- U3: Linux・FreeBSD の既定の背景は **Birch-Lake.png**（Aurora ではない）。`KEILAND_LINUX_WALLPAPER` の既定から過去の build（`build/ws035-wallpaper/`）を見る道を消す。
- U4: 背景は **PNG と JPEG だけ**に対応する。PPM を読む code は消す（compositor・Settings・Files の背景の読み込み）。JPEG は既存の `libjpeg-compat` を使う。試験に JPEG の背景を 1 枚足す。
- U5: PNG は**全部最大の圧縮**（tree の 2 枚も generate.py の生成も、行ごとに filter を選ぶ）。build の時間の増え方を phase.md に記録する。
- U6: desktop.conf に残った古い system の `.ppm` には**何もしない**（既定の背景に戻る。`.png` に読み替える code は書かない）。
- U7: Settings の reset の時の復号も **WS135 の thread の道（`zwl_glass_wallpaper_begin`）**に寄せる（同期の復号の道を消す）。
- U8: alpha のある画像の透明な所は**黒で合成**する。
- U2: 他の WS の試験の約 194 file と `platform/amd64/vmunix.mk` の Settings の link の規則は、担当が直さず、**Q1 が merge の時に main で sed を掛ける**。担当は command と確かめの一覧を phase.md に書いて渡す。
- U1: 開始は 2026-10-04 の UAT の後（17 時の利用枠の回復の後）。
