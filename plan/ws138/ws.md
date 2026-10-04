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

## 事実（2026-10-04、P1 が source を読んで確かめた）

### 背景の画像を読む所（3 つの program）

| program | file・関数 | 今 | PNG の道具 |
| --- | --- | --- | --- |
| compositor | `userland/desktop/wayland/glass.c` の `wallpaper_load`（1325）→ `wallpaper_decode`（1356、P6・最大値 255 だけ）。file は `prefetch_take`・`file_read` で全体を読み、picture は file の bytes をそのまま持つ（`struct wallpaper_picture` の `data`・`pixels`（画素の始まりの offset）・`width`・`height`、105-113）。`wallpaper_row`（720）が RGB の 3 byte を拾う。session の途中の変更は thread の `loader_run`（1440 付近）も `wallpaper_decode` を使う | PPM だけ | compositor はすでに libpng-compat と libz-compat を link している（`wayland/Makefile:39`・`Makefile.linux:61`・`Makefile.freebsd:61`）。wayland/ の `.c` で `png_image` を使う所は今は無い |
| Settings | `userland/desktop/settings/look.c`: 既定 `LOOK_DEFAULT_PICTURE` = `KEILAND_DATADIR "/keiland/wallpaper.ppm"`（60）、一覧 `LOOK_PICTURES`（61）の `.ppm` の名前だけ（340-370）、縮小 `look_thumbnail`（794-956、P6 の header を読み、使う行だけを `pread`）と `look_ppm_number`（960） | PPM だけ | Settings は libpng-compat を link していない（`settings/Makefile:19` の依存、`Makefile.linux:29`・`Makefile.freebsd:29`） |
| Files | `userland/desktop/files/ui-home.c` の Home の hero: `home_ppm_load`（895）・`home_ppm_number`（983）で `app->wallpaper`（`FM_WALLPAPER` = `KEILAND_DATADIR "/keiland/wallpaper.ppm"`、`files.h:56`、`ui.c:103`）を読む | PPM だけ | Files の `thumb.c` の `fm_image_load`（84）は PPM・PGM・PNG・JPEG・GIF・PDF を読める。hero はこれに置き換えられる |

### 既定の path と data の定義

- 既定の path `/usr/share/keiland/wallpaper.ppm`:
  - `userland/desktop/sessiond/sessiond.h:28`（`SESSIOND_WALLPAPER`。`sessiond/greeter.c:433` が greeter の compositor に `--wallpaper=` で渡す）
  - `userland/desktop/sessiond/session.sh:16`
  - Linux・FreeBSD の launcher `userland/desktop/wayland/keiland-desktop.in:41`、`userland/desktop/wayland/data/keiland.desktop:4`
- 生成の背景:
  - `userland/desktop/wallpapers/generate.py` が Aurora・Dawn・Lagoon・Meadow・Twilight の 5 枚を P6 で書く（`write_ppm`）。PNG を書く code（`write_preview`）はすでにある。
  - zedBSD は `userland/desktop/wallpapers/Makefile`（`ZEDBSD_KEILAND_WALLPAPERS := y` の時だけ `/usr/share/keiland/wallpapers/*.ppm`）。
  - Linux は `userland/desktop/keiland-linux.mk:141-148`。145 行は過去の build の `build/ws035-wallpaper/wallpaper.ppm` を、在れば既定に使う。
    これは WS136 の方針（過去の build/ を入力にしない）に反する。
  - FreeBSD は `userland/desktop/keiland-freebsd.mk:142-149`（既定 Aurora）。
- tree の 2 枚: `userland/desktop/keiland/wallpapers/Birch-Lake.ppm`・`Lakeside.ppm`（各 6,220,817 byte、1920x1080、git に入っている）と `README.md`（出どころ）。
  - 2 枚を image に入れる規則は make に無い。試験と demo の image の script が `--file` で入れる。
  - 例: `plan/ws075/demo/build-demo-image.sh:35-40` は `wallpapers/*.ppm` を全て入れ、`Birch-Lake.ppm` を `/usr/share/keiland/wallpaper.ppm` にする。
- PNG にした時の大きさの見積もり（2026-10-04、host の python の zlib、filter 0）:
  - Birch-Lake は約 2.1 MB。
  - Lakeside は約 0.2 MB。
  - 行ごとの filter（Sub・Up・Paeth）を選べば、さらに小さくなる見込み（推測）。
- libpng-compat（`userland/base/libpng-compat/read.c`）の性質:
  - libpng の簡易 API の読み込みだけを持つ（`png_image_begin_read_from_file`・`_from_memory`・`png_image_finish_read`・`png_image_free`）。
  - 全ての色の型・bit 深さ・Adam7 を読む。一辺の上限は 32768、file の上限は 256 MiB。
  - `background` が NULL で alpha を落とす時は、色をそのまま使う（862 行付近）。

### 文字列 `.ppm` で背景を指す試験の script（他の WS の物を含む）

2026-10-04 の grep の結果。

| 指す物 | file の数 | 行の数 |
| --- | --- | --- |
| guest の `/usr/share/keiland/wallpaper.ppm` | 174 | 190 |
| tree の `userland/desktop/keiland/wallpapers/Birch-Lake.ppm` | 28 | 53 |
| tree の `Lakeside.ppm` | 1 | 2 |
| 合わせて（`plan/tools`・`plan/ws*/tests`・`plan/ws*/demo`・`tools`・`userland`） | 196 | — |

file の一覧は次の command で出る（p002 の手順 1）。

```sh
grep -rlE 'wallpaper.*\.ppm|wallpapers/.*\.ppm|Birch-Lake\.ppm|Lakeside\.ppm|Aurora\.ppm' plan/tools plan/ws*/tests plan/ws*/demo tools userland
```

## 設計（共通の決め）

- **D1 PPM も読み続ける**: 3 つの program は、file の先頭の bytes で形式を決める（PNG の signature `89 50 4E 47 0D 0A 1A 0A`、PPM の `P6`）。
  拡張子では決めない。PPM の読み込みは消さない。
  - 理由 1: 利用者の `desktop.conf` に `.ppm` の path が保存されていることがある。
  - 理由 2: 利用者が自分の PPM を選べる。
- **D2 PNG は全体を復号する**: PNG は行を途中から読めない。`png_image_finish_read` で RGB（`PNG_FORMAT_RGB`、3 byte／画素）の buffer に全体を復号する。
  1920x1080 で約 6 MB で、今の PPM の file 全体を持つのと同じ量である。
  - compositor は、復号した buffer を `struct wallpaper_picture` の `data` にし、`pixels`（offset）を 0 にする。`wallpaper_row` は変えずに使える。
  - 一辺の上限は 8192 とする（Settings の `LOOK_WIDTH_MAX`、Files の `THUMB_SIDE_MAX` と同じ）。
- **D3 既定の名前**: 既定の背景を `/usr/share/keiland/wallpaper.png`（Linux・FreeBSD は `$prefix/share/keiland/wallpaper.png`）にする。
  tree の 2 枚は `Birch-Lake.png`・`Lakeside.png`、生成の 5 枚は `<名前>.png` にする。
- **D4 Settings の一覧**: `.png` と `.ppm` の両方を一覧する。名前は拡張子を除いて表示する。
- **D5 Files の hero**: `home_ppm_load` を `fm_image_load`（thumb.c、すでに PNG を読む）に置き換え、`home_ppm_load`・`home_ppm_number` を消す。
- **D6 変換は画素を変えない**: tree の PPM を PNG にする時は、画素を 1 つも変えない。変換の後に、PNG を復号した RGB と元の PPM の画素を byte で比べる。
  `README.md` の出どころの記述は保ち、「2026-xx-xx に PNG に可逆に変換（画素は同じ）」を足す（Birch-Lake の出どころは
  [ws099-p019](../ws099/phase019/phase.md) の記録）。

## 完了の条件

1. compositor・Settings・Files が PNG の背景を表示する。PPM の背景も今までどおり表示する。
2. tree の背景は `Birch-Lake.png`・`Lakeside.png` だけになる（`.ppm` は消す）。生成の背景は PNG になる。
   zedBSD・Linux・FreeBSD の data の定義（make）は PNG を入れる。
3. 試験と demo の script が PNG を指す。`grep` の確かめ（p002）で、背景の `.ppm` を指す行が 0。
   残すのは、PPM を読めることの試験として意図して残す物だけで、残す理由を phase.md に書く。
4. build（zedBSD の wayland・settings・files、Linux と FreeBSD の flag での compile）が warning 0。host 試験（`plan/ws089/tests/host-wallpaper.sh`、Files の
   `plan/tools/files/host-p014.sh`）が PASS。
5. T の QEMU の試験が PASS する（p002 の依頼の一覧）。
   起動の時の背景の読み込みの時間（`ZWL STARTUP step=wallpaper ms=` と `step=wallpaper-picture ms=`）を PPM と PNG で比べて記録する。
6. 変えた C が全文規約に合う。

## 関係する source の path

- `userland/desktop/wayland/glass.c`（`wallpaper_decode` 付近）、`userland/desktop/wayland/main.c:461`（comment「a binary PPM」と usage の文）
- `userland/desktop/settings/look.c`・`Makefile`・`Makefile.linux`・`Makefile.freebsd`
- `userland/desktop/files/ui-home.c`・`files.h:56`
- `userland/desktop/sessiond/sessiond.h:28`・`session.sh:16`
- `userland/desktop/wayland/keiland-desktop.in`・`data/keiland.desktop`
- `userland/desktop/wallpapers/generate.py`・`Makefile`
- `userland/desktop/keiland/wallpapers/`（2 枚と README）
- `userland/desktop/keiland-linux.mk`・`keiland-freebsd.mk`、`userland/desktop/LINUX.md`・`README.freebsd.md`
- `tools/release/keiland-linux-deb/run.py`（`.ppm` の path があれば）、`plan/tools/keiland-launcher/check.py`
- 試験の script（196 file、p002）

## Guardrail の注意

- compositor は OS の header を持たない（Guardrail の「配置」、checker `plan/tools/keiland-os-boundary/check.sh`）。libpng-compat は OS の物ではないので、
  この規則には触れない。変えた後に checker を流して PASS を確かめる。
- libkeiland・libkeiland-backend は変えない見込み。
- 外部の package は使わない。libpng-compat は tree の中の物。
- 背景の画像は git に入れる data である。Birch-Lake の出どころ（ユーザーの提供、署名 LEEKING26）を README から落とさない。
- QEMU は自分で起動しない。T1/T2 に依頼する。
- 自分の worktree の `build/<名前>/` だけを使う。

## ユーザーの判断

- **U1 始める時期**: F-071 は「S1 の実機試験の後」とある。S1 の実機試験の後に始める（推奨）か、今始めるか。
- **U2 他の WS の試験の script**: 196 file の置き換え（ほとんどが他の WS の `tests/` の、`wallpaper.ppm` → `wallpaper.png` の機械的な置き換え）を、
  この WS の担当がまとめて行ってよいか。Q1 の許可で所有 path を広げる（推奨）。
- **U3 Linux・FreeBSD の既定の背景**: Linux の `KEILAND_LINUX_WALLPAPER` の既定は、今は過去の build の `build/ws035-wallpaper/wallpaper.ppm` が在ればそれ、
  無ければ Aurora である。案は 2 つ。
  - (a) 過去の build を見るのをやめ、Aurora.png にする（今の FreeBSD と同じ。推奨、最小の変更）。
  - (b) zedBSD の試験の image と同じ Birch-Lake.png にする。
- **U4 PPM を読む code を残すか**: D1 の推奨は残す。消すなら、利用者の保存した `.ppm` の設定が既定の背景に戻る。
- **U5 PNG の圧縮の程度**: 行ごとの filter を選んで小さくする（generate.py と変換の道具で、Python の標準 library だけで書ける）か、filter 0 で簡単にするか。推奨は filter を選ぶ。

## Phase

| Phase | 内容 | 状態 | 依存 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 3 つの program の PNG の読み込み（D1・D2・D4・D5）、既定の path、tree の 2 枚の可逆の変換、generate.py の PNG、zedBSD・Linux・FreeBSD の make の data、host 試験 | planned | — |
| [p002](phase002/phase.md) | 試験と demo の script の置き換え（196 file）、残りの grep の確かめ、T への QEMU の試験の依頼（Settings の背景の page、criteria の C7、Files、boot、背景の読み込みの時間）、Linux・FreeBSD の guest の build の依頼 | planned | p001 |
| p003 | 全文規約の見直し（変えた C）、T の結果の反映、F-071 と WS089・WS099 への結果の案 | planned | p002 |
