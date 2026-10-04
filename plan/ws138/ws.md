<!-- awesome-plan project=zedbsd record=ws138 -->

# WS138: 背景の画像を PPM から PNG に

<!-- awesome-plan-current:start -->
Status: planned
Primary Milestone: MG006
Related Milestones: MG007（Linux・FreeBSD の package の data）
Objectives: O2
Parent: [Master](../master.md)
Queue: none（Q1 が割り当てる）
Resume point: p001 から。F-071 の再考の契機は「S1 の実機試験の後」。始める時期は Q1・ユーザーが決める（U1）。
<!-- awesome-plan-current:end -->

## 目標

2026-10-04 ユーザー:「F-071、F-072、F-070、はWSを立てて計画を作り、他の能力が低いセッションで処理できるようにしてください。」

[F-071](../future-work.md) の内容。2026-10-03 user「S1 は今の PPM のままにして、PNG への切り替えは S1 の後の作業にします。」
- compositor の `glass.c` の `wallpaper_load`（P6 だけ）と、Settings の `look.c`（`.ppm` だけを一覧、既定 `wallpaper.ppm`）を、libpng-compat で PNG に対応させる。
- `userland/desktop/keiland/wallpapers/` の 2 枚（各 6MB の PPM）と、生成の背景、Linux・FreeBSD の data の定義を PNG にする。

## 事実（2026-10-04、P1 が source を読んで確かめた。design-reviewer の確かめを含む）

### 背景の画像を読む所（3 つの program）

| program | file・関数 | 今 | PNG の道具 |
| --- | --- | --- | --- |
| compositor | `userland/desktop/wayland/glass.c`。<br>起動: `prefetch_run`（1495、thread で file の bytes を読むだけ）→ `wallpaper_load`（1325、main の thread）→ `wallpaper_decode`（1356、P6・最大値 255 だけ）。<br>picture は file の bytes をそのまま持つ（`struct wallpaper_picture` の `data`・`pixels`（画素の始まりの offset）・`width`・`height`、105-113）。`wallpaper_row`（720）が RGB の 3 byte を拾う。<br>session の途中の変更は、thread の `loader_run`（1444）も `wallpaper_decode` を使う。<br>reset の時は、`settings.c:555` の `zwl_glass_wallpaper`（event loop の中、同期）。<br>file の上限は `GLASS_FILE_MAX` 16 MiB（63 行） | PPM だけ | compositor はすでに libpng-compat と libz-compat を link している（`wayland/Makefile:39`・`Makefile.linux:61`・`Makefile.freebsd:61`）。<br>同じ binary の `userland/desktop/picture/color-glyph.c:98-143` が、色の絵文字の PNG を復号している（手本） |
| Settings | `userland/desktop/settings/look.c`。<br>既定 `LOOK_DEFAULT_PICTURE` = `KEILAND_DATADIR "/keiland/wallpaper.ppm"`（60）。<br>一覧 `LOOK_PICTURES`（61）は `.ppm` の名前だけ（340-370）。tile は最大 `SE_WALLPAPERS` 8（`settings.h:561`）。<br>縮小 `look_thumbnail`（794-956）は P6 の header を読み、使う行だけを `pread` する。`look_ppm_number`（960） | PPM だけ | Settings は libpng-compat を link していない。<br>依存（`settings/Makefile:19`・`Makefile.linux:29`・`Makefile.freebsd:29`）と、**zedBSD の link の規則 `platform/amd64/vmunix.mk:1400-1414`** の両方を直す必要がある（REQUIRE だけでは link に効かない、`Makefile:296-307`） |
| Files | `userland/desktop/files/ui-home.c` の Home の hero。`home_ppm_load`（895）・`home_ppm_number`（983）で `app->wallpaper` を読む。<br>`app->wallpaper` は `FM_WALLPAPER` = `KEILAND_DATADIR "/keiland/wallpaper.ppm"`（`files.h:56`、`ui.c:103`） | PPM だけ | Files の `thumb.c` の `fm_image_load`（84）は PPM・PGM・PNG・JPEG・GIF・PDF を読める（上限 `THUMB_PIXELS_MAX` 16M 画素、`thumb.c:43`）。hero はこれに置き換えられる |

### 既定の path と data の定義

- 既定の path `/usr/share/keiland/wallpaper.ppm` を持つ所:
  - `userland/desktop/sessiond/sessiond.h:28`（`SESSIOND_WALLPAPER`。`sessiond/greeter.c:433` が greeter の compositor に `--wallpaper=` で渡す）
  - `userland/desktop/sessiond/session.sh:16`
  - Linux・FreeBSD の launcher `userland/desktop/wayland/keiland-desktop.in:41`、`userland/desktop/wayland/data/keiland.desktop:4`
  - `tools/release/keiland-linux-deb/run.py:255`（`--wallpaper=/opt/keiland/share/keiland/wallpaper.ppm`）
- 生成の背景:
  - `userland/desktop/wallpapers/generate.py` が、Aurora・Dawn・Lagoon・Meadow・Twilight の 5 枚を P6 で書く（`write_ppm`）。PNG を書く code（`write_preview`）はすでにある。
  - zedBSD は `userland/desktop/wallpapers/Makefile`（`ZEDBSD_KEILAND_WALLPAPERS := y` の時だけ `/usr/share/keiland/wallpapers/*.ppm`）。
  - Linux は `userland/desktop/keiland-linux.mk:141-148`。
    145 行は、過去の build の `build/ws035-wallpaper/wallpaper.ppm` が在れば既定に使う。これは WS136 の方針（過去の build/ を入力にしない）に反する。
  - FreeBSD は `userland/desktop/keiland-freebsd.mk:142-149`（既定 Aurora）。
- tree の 2 枚: `userland/desktop/keiland/wallpapers/Birch-Lake.ppm`・`Lakeside.ppm`（各 6,220,817 byte、1920x1080、git に入っている）と `README.md`（出どころ）。
  - 2 枚を image に入れる規則は make に無い。試験と demo の image の script が `--file` で入れる。
    例: `plan/ws075/demo/build-demo-image.sh:35-40` は `wallpapers/*.ppm` を全て入れ、`Birch-Lake.ppm` を `/usr/share/keiland/wallpaper.ppm` にする。
  - Birch-Lake の出どころ（ユーザーの提供、ぼかしの指示）の記録は [ws099-p019](../ws099/phase019/phase.md) にある（README からその link を保つ）。
- PNG にした時の大きさの見積もり（2026-10-04、host の python の zlib）:
  - Birch-Lake は約 2.1 MB、Lakeside は約 0.2 MB（filter 0）。
  - 生成の Aurora は、filter 0 で 1.23 MB、Up で 1.13 MB（design-reviewer の試し）。
- libpng-compat（`userland/base/libpng-compat/read.c`）の性質:
  - libpng の簡易 API の読み込みだけを持つ（`png_image_begin_read_from_file`・`_from_memory`・`png_image_finish_read`・`png_image_free`）。
  - 全ての色の型・bit 深さ・Adam7 を読む。一辺の上限は 32768、file の上限は 256 MiB。
  - `png_image_finish_read` は、成功でも失敗でも、自分で image を free する（328 行）。その後に `png_image_free` を呼ばない。
  - finish の中で、出力と同じ大きさの `raw` を malloc する（236-240）。
  - `background` が NULL で alpha を落とす時は、透明な画素も色をそのまま使う（857-862）。
  - 可変の大域の状態は無い（libz-compat の `inflate.c` も const だけ）。thread から呼んでよい。
- **復号の時間**: host（-O2）で 1920x1080 の RGB の復号が 188〜202 ms（design-reviewer の計測）。
  今の PPM は復号が無く、file の bytes をそのまま使う。だから PNG にすると、何もしなければ起動と reset が約 0.2 s 遅くなる。

### 文字列 `.ppm` で背景を指す所（2026-10-04 の `git grep`、`plan/history` を除く）

| 種類 | file の数 | 扱い |
| --- | --- | --- |
| 実行される script・source（`*.sh`・`*.py`・`*.mk`・`*.c`・`*.in`・`*.desktop`・`Makefile`、evidence を除く） | 194 | p002 で置き換える。<br>`plan/ws*/tests/`・`plan/tools/` の他に、次を含む: `plan/ws005/phase020/build-rtl-image.sh`・`plan/ws005/phase024/build-p024-image.sh`・`plan/ws099/phase017/p076-framed.sh`・`plan/ws129/phase010/apphome.sh` |
| 過去の証拠（`*/evidence/*`） | 26 | 変えない（記録） |
| 文書（`*.md`・`*.txt`） | 32 | plan の記録は変えない。<br>`userland/desktop/LINUX.md`・`README.freebsd.md`・`keiland/wallpapers/README.md` と、header（`files.h`・`sessiond.h`）は直す |

一覧は p002 の手順 1 の command で出す。

## 設計（共通の決め）

- **D1 PPM も読み続ける**: 3 つの program は、file の先頭の bytes で形式を決める（PNG の signature `89 50 4E 47 0D 0A 1A 0A`、PPM の `P6`）。
  拡張子では決めない。PPM の読み込みは消さない。利用者が自分の PPM を選べる。
  - 利用者の `desktop.conf` に残る system の `…/keiland/wallpapers/<名前>.ppm` は、この WS がその file を消すので、PPM を読めても救えない。
    これは U6 で扱う。
- **D2 PNG は全体を復号する**: PNG は行を途中から読めない。`png_image_finish_read` で、RGB（`PNG_FORMAT_RGB`、3 byte／画素）の buffer に全体を復号する。
  - compositor は、復号した buffer を `struct wallpaper_picture` の `data` にし、`pixels`（offset）を 0 にする。`wallpaper_row` は変えずに使える。
  - **画素の数の上限**: 幅 × 高さ ≤ 16M（Files の `THUMB_PIXELS_MAX` と同じ）。一辺は 8192 以下。
    compositor と Settings の両方に掛ける（復号の中で出力と同じ大きさの `raw` も取るので、上限が広いと memory を 2 倍使う）。
- **D3 既定の名前**: 既定の背景を `/usr/share/keiland/wallpaper.png`（Linux・FreeBSD は `$prefix/share/keiland/wallpaper.png`）にする。
  tree の 2 枚は `Birch-Lake.png`・`Lakeside.png`、生成の 5 枚は `<名前>.png` にする。
- **D4 Settings の一覧**: `.png` と `.ppm` の両方を一覧する。名前は拡張子を除いて表示する。同じ名前が 2 つあれば 1 つにする（`.png` を取る）。
- **D5 Files の hero**: `home_ppm_load` を `fm_image_load`（thumb.c、すでに PNG を読む）に置き換え、`home_ppm_load`・`home_ppm_number` を消す。
- **D6 変換は画素を変えない**: tree の PPM を PNG にする時は、画素を 1 つも変えない。変換の後に、PNG を復号した RGB と元の PPM の画素を byte で比べる。
  `README.md` の出どころの記述と ws099-p019 への link は保ち、「PNG に可逆に変換（画素は同じ）」を足す。
- **D7 復号は thread で**: compositor の起動の時の PNG の復号は、`prefetch_run` の thread で行う（今は bytes を読むだけ）。
  `wallpaper_load` は、復号済みの picture を受け取る。これで ws035-p133 の「Vulkan の device を作る間に disk を読む」重ねが保たれる。
  reset の同期の道（`settings.c:555`）の時間は記録し、U7 で扱う。
- **D8 移行の順**: main が途中で壊れないように、2 段で入れる。
  - p001: 読める物を足すだけ（PNG の復号、tree への PNG の追加）。PPM の file と既定の path は変えない。
  - p002: 1 回の commit で切り替える（既定の path、生成、make の data、PPM の削除、全ての script の置き換え）。

## 完了の条件

1. compositor・Settings・Files が PNG の背景を表示する。PPM の背景も今までどおり表示する。
2. tree の背景は `Birch-Lake.png`・`Lakeside.png` だけになる（`.ppm` は消す）。生成の背景は PNG になる。
   zedBSD・Linux・FreeBSD の data の定義（make）は PNG を入れる。
3. 実行される script と source が PNG を指す。
   p002 の `git grep` の確かめで、背景の `.ppm` を指す行は、理由の表（PPM を読む試験、背景でない `.ppm`）に載せた物だけになる。
4. build（zedBSD の wayland・settings・files、Linux と FreeBSD の build）が warning 0。host 試験（Settings の背景の page、Files の hero）が PASS。
5. T の QEMU の試験が PASS する（p002 の依頼の一覧。greeter の背景を含む）。
   起動の時の背景の読み込みの時間（`ZWL STARTUP step=wallpaper ms=` と `step=wallpaper-picture ms=`）を PPM と PNG で比べて記録する。
6. 変えた C が全文規約に合う（p003）。

## 関係する source の path

- `userland/desktop/wayland/glass.c`（`wallpaper_decode`・`prefetch_run`・`wallpaper_load` 付近）、`userland/desktop/wayland/main.c:107・461`（usage と comment）
- `userland/desktop/settings/look.c`・`settings.h:569`（comment）・`Makefile`・`Makefile.linux`・`Makefile.freebsd`
- `platform/amd64/vmunix.mk:1400-1414`（Settings の link の規則。WS138 の所有ではないので、Q1 の許可が要る、U2）
- `userland/desktop/files/ui-home.c`・`files.h:55-56`
- `userland/desktop/sessiond/sessiond.h:28`・`session.sh:16`
- `userland/desktop/wayland/keiland-desktop.in`・`data/keiland.desktop`
- `userland/desktop/wallpapers/generate.py`・`Makefile`（変換の道具 `ppm-to-png.py` もここに置く）
- `userland/desktop/keiland/wallpapers/`（2 枚と README）
- `userland/desktop/keiland-linux.mk`・`keiland-freebsd.mk`、`userland/desktop/LINUX.md`・`README.freebsd.md`
- `tools/release/keiland-linux-deb/run.py:255`、`plan/tools/keiland-launcher/check.py:44`
- 試験の script（194 file、p002）

## Guardrail の注意

- compositor は OS の header を持たない（Guardrail の「配置」、checker `plan/tools/keiland-os-boundary/check.sh`）。
  libpng-compat は OS の物ではないので、この規則には触れない。変えた後に checker を流して PASS を確かめる。
- libkeiland・libkeiland-backend は変えない見込み。
- 外部の package は使わない。libpng-compat は tree の中の物。
- 背景の画像は git に入れる data である。Birch-Lake の出どころ（README と ws099-p019 の記録）を落とさない。
- QEMU は自分で起動しない。T1/T2 に依頼する。
- 自分の worktree の `build/<名前>/` だけを使う。

## ユーザーの判断

- **ユーザーの決定（2026-10-04、AskUserQuestion）**:
  - U1: 今日（10/04）の UAT の後に始める（実際の開始は使用量のリセットの後）。
  - U2: 他の WS の試験の約 194 file と vmunix.mk の Settings の link の規則は、**Q1 が merge の時に main で sed を掛ける**。担当は command と確かめの一覧を渡す。
  - U3: Linux・FreeBSD の既定の背景は **Birch-Lake.png**（推奨と違う）。過去の build を見るのはやめる。
  - U4: **PNG と JPEG だけに対応する**（ユーザーの記入「PNGとJPEGのみ対応にする。」）。PPM を読む code は消す。JPEG の読み込み（libjpeg-compat）を範囲に足す。p001・p002 を直してから着手。
  - U5: **全部最大の圧縮**（generate.py も filter を選ぶ。build の時間が延びる）。
  - U6: 消えた system の `.ppm` が desktop.conf に残っていても**何もしない**（既定の背景に戻る）。
  - U7: reset の時の復号は **WS135 の thread の道（`zwl_glass_wallpaper_begin`）に寄せる**。
  - U8: 透明な所は**黒で合成**する。
- **U1 始める時期**: F-071 は「S1 の実機試験の後」とある。S1 の実機試験の後に始める（推奨）か、今始めるか。
- **U2 他の WS の file**: 次を、この WS の担当が直してよいか（Q1 の許可で所有 path を広げる）。
  - 194 file の置き換え（ほとんどが、他の WS の `tests/` の `wallpaper.ppm` → `wallpaper.png` の機械的な置き換え）。
  - `platform/amd64/vmunix.mk` の Settings の link の規則。
  - 案: 衝突を減らすため、置き換えの sed は、Q1 が merge の時に main で掛ける形にもできる（p002 は command と確かめの一覧を渡す）。
- **U3 Linux・FreeBSD の既定の背景**: Linux の `KEILAND_LINUX_WALLPAPER` の既定は、今は過去の build の `build/ws035-wallpaper/wallpaper.ppm` が在ればそれ、無ければ Aurora である。案は 2 つ。
  - (a) 過去の build を見るのをやめ、Aurora.png にする（今の FreeBSD と同じ。推奨、最小の変更）。
  - (b) zedBSD の試験の image と同じ Birch-Lake.png にする。
- **U4 PPM を読む code を残すか**: D1 の推奨は残す。
- **U5 PNG の圧縮**: 推奨は次のとおり。
  - tree の 2 枚は、行ごとに filter を選ぶ（1 回だけの変換）。
  - generate.py は filter Up に固定する。純 Python で filter を選ぶと 1 枚約 18 s かかり、5% ほどしか縮まない。
  - zlib の版が違うと、生成の bytes が変わる。WS112 の再現の確認に関わる。
- **U6 古い設定と古い install**: 利用者の `desktop.conf` に、消えた system の `.ppm`（例 `/usr/share/keiland/wallpapers/Aurora.ppm`）が残っている時の扱い。
  - (a) compositor と Settings が、その path が無く、system の背景の directory の `.ppm` なら、同じ名前の `.png` を試す（推奨）。
  - (b) 何もしない（既定の背景に戻る）。
  - Linux・FreeBSD の `make install` を古い install に重ねると、古い `.ppm` が残る。install で消すか（推奨: 消す）。D4 の同じ名前の 1 つ化で、表示の重複は防ぐ。
- **U7 reset の同期の復号**: `settings.c:555` の同期の道で PNG を復号すると、event loop が約 0.2 s 止まる（host の値）。
  受け入れる（推奨、reset の時だけ）か、WS135 の thread の道（`zwl_glass_wallpaper_begin`）に寄せるか。
- **U8 透明な PNG**: alpha のある PNG の透明な所を、何の色にするか。推奨は、背景の色（compositor の風景の空の色）で合成する（`png_image_finish_read` の `background`）。

## Phase

| Phase | 内容 | 状態 | 依存 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 読めるようにする: compositor（thread で復号）・Settings・Files の PNG の読み込み、画素の上限、Settings の link、tree に PNG の 2 枚を足す（PPM は残す）、変換の道具、host 試験。既定の path は変えない | planned | — |
| [p002](phase002/phase.md) | 1 回の commit で切り替える: 既定の path、generate.py、make の data、PPM の削除、移行（U6）、194 file の置き換え、T への QEMU の試験の依頼（Settings、criteria と greeter、背景の読み込みの時間、FreeBSD の build） | planned | p001 の commit が main に統合済み |
| p003（phase.md は p002 の後に書く） | 全文規約の見直し（変えた C）、T の結果の反映、F-071 と WS089・WS099 への結果の案 | planned | p002 |
