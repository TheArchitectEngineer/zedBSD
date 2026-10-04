<!-- awesome-plan project=zedbsd record=ws138-p002 -->

# ws138-p002: 1 回の commit で PNG に切り替え、QEMU の試験を依頼する

Status: planned
Disposition: normal
Parent: [WS138](../ws.md)
Queue: none
設計: [ws.md](../ws.md) の D3・D8、U3・U5・U6・U7
依存: p001 の commit が main に統合済み（PNG を読む code と tree の PNG の 2 枚。p001 の clearance は要らない）。ws.md の U2・U3・U5・U6 の決め
時限の目安: 切り替え 3 h、試験の依頼の用意 1 h

## 範囲

- **入る**: 次を全て、**1 つの commit（1 回の merge）**にする（D8: 途中で main の image や試験が壊れないように）。
  - 既定の path（`wallpaper.ppm` → `wallpaper.png`）
  - generate.py の PNG、make の data（zedBSD・Linux・FreeBSD）
  - tree の `.ppm` の削除
  - 移行（U6）
  - 実行される試験・道具の script の置き換え（194 file）
  - 文書の直し（userland の文書だけ）
  - 加えて、背景の読み込みの時間を測る script と、T1/T2 への試験の依頼。
- **入らない**: plan の記録（`*.md`・evidence）の書き換え。全文規約の見直し（p003）。
- **所有する path**: ws.md の「関係する source の path」と、置き換える script（他の WS の物を含む。U2 の Q1 の許可が要る）。
  U2 で「Q1 が merge の時に sed を掛ける」に決まったら、手順 4 の置き換えは自分では行わず、command と一覧と確かめを Q1 に渡す。

## 手順

### 1. 対象の一覧を作る

```sh
mkdir -p plan/ws138/temp
git grep -lE 'wallpaper.*\.ppm|wallpapers/.*\.ppm|Birch-Lake\.ppm|Lakeside\.ppm|Aurora\.ppm' -- \
    '*.sh' '*.py' '*.mk' '*.c' '*.h' '*.in' '*.desktop' '*Makefile*' \
    ':!plan/history' ':!*/evidence/*' ':!plan/ws138/*' ':!.internal' \
  | sort > plan/ws138/temp/before.txt
wc -l plan/ws138/temp/before.txt
```

- `plan/ws138/temp/` は git に入れない（AGENTS.md）。
- 2026-10-04 の数え方では約 194 file（ws.md の表）。数が大きく違えば、その理由を「結果」に書く。
- plan の文書（`*.md`）と evidence は記録なので変えない。直す文書は userland の 3 つ（手順 3 の 6）だけ。

### 2. 生成と make の data

1. `userland/desktop/wallpapers/generate.py`:
   - `write_ppm` を、p001 の `ppm-to-png.py` の `write_png(…, filter='up')`（import）に置き換える（U5: filter は Up に固定）。
   - 書く名前は `<名前>.png`、表示は `wallpaper: …png`。
   - docstring（7-8 行の P6 の説明）を PNG に直す。
   - 同じ command は同じ bytes を書く（zlib の版が同じなら決まる）。
2. `userland/desktop/wallpapers/Makefile`: `.ppm` を `.png` にする（13・18 行）。
3. `userland/desktop/keiland-linux.mk:141-148`:
   - `.ppm` を `.png` にする。
   - 145 行の、`build/ws035-wallpaper/wallpaper.ppm` を見る部分を消す。U3 の (a) なら既定は `Aurora.png`、(b) なら `userland/desktop/keiland/wallpapers/Birch-Lake.png`。
   - 146 行の data の名前を `share/keiland/wallpaper.png` にする。
   - U6 で「install で古い `.ppm` を消す」に決まったら、install の規則で `$(DESTDIR)$(prefix)/share/keiland/wallpaper.ppm` と `wallpapers/*.ppm` を消す。
     install の規則の書き方は、その file の中の既存の install の規則に倣う。
4. `userland/desktop/keiland-freebsd.mk:142-149`: 同じく `.png` にする（U6 の install の消しも同じ）。

### 3. 既定の path と移行

1. 既定の path を `.png` にする。
   - `userland/desktop/sessiond/sessiond.h:28`、`userland/desktop/sessiond/session.sh:16`（2 か所）
   - `userland/desktop/settings/look.c:60`（`LOOK_DEFAULT_PICTURE`）
   - `userland/desktop/files/files.h:55-56`（comment と `FM_WALLPAPER`）
   - `userland/desktop/wayland/keiland-desktop.in:41`、`userland/desktop/wayland/data/keiland.desktop:4`
   - `tools/release/keiland-linux-deb/run.py:255`
   - `plan/tools/keiland-launcher/check.py:44` の期待の文字列（37 行の `a picture.ppm` は引数の受け渡しの試験なので変えない）
   - `plan/tools/files/host-render.c:18` の既定の path の文字列
2. U6 の (a) に決まったら、移行を入れる（compositor と Settings）。
   - 規則: 設定の背景の path が開けず、`KEILAND_DATADIR "/keiland/wallpapers/"` の下の `.ppm` なら、末尾を `.png` に変えた path を試す。
   - compositor: `glass.c` の `wallpaper_fill`（585-）で、`wallpaper_load` が ENOENT を返した時に上の規則で 1 回だけ試す。
     新しい static 関数 `wallpaper_png_twin(path, out, size)`（変えた path を作るだけ）に分ける。
   - Settings: `look.c` の、今の選択（`kl_settings` の `wallpaper`）と tile の path を比べる所で、同じ規則の path も一致とみなす（今選ばれている tile に印が付く）。
     比べる所は `look_changed`・`look_read` を読んで探す。
   - 試すのは system の directory の下の `.ppm` だけ。利用者の file は変えない。
3. tree の `.ppm` を消す: `git rm userland/desktop/keiland/wallpapers/Birch-Lake.ppm userland/desktop/keiland/wallpapers/Lakeside.ppm`。
   README の表から `.ppm` の 2 行を消す。PNG の行の Origin に「元は同じ名前の PPM（git の履歴）」を足す。
4. `glass.c`・`ui-home.c` などで、p001 で直し残した `.ppm` の comment があれば直す（`grep -n 'ppm' userland/desktop/wayland/glass.c userland/desktop/settings/*.c userland/desktop/files/ui-home.c`）。
5. `userland/desktop/LINUX.md:33・42`、`README.freebsd.md:93`、`userland/desktop/keiland/wallpapers/README.md` の `.ppm` を `.png` にする。
   LINUX.md の「既定は既存のユーザー画像 cache」の文は、U3 の決めに合わせる。

### 4. 試験の script の機械的な置き換え

次の 5 つの規則だけを、`before.txt` の file のうち、手順 2・3 で直さなかった物に掛ける。

```sh
grep -v '^userland/\|^tools/' plan/ws138/temp/before.txt > plan/ws138/temp/scripts.txt
xargs sed -i \
  -e 's#/usr/share/keiland/wallpaper\.ppm#/usr/share/keiland/wallpaper.png#g' \
  -e 's#share/keiland/wallpaper\.ppm#share/keiland/wallpaper.png#g' \
  -e 's#keiland/wallpapers/Birch-Lake\.ppm#keiland/wallpapers/Birch-Lake.png#g' \
  -e 's#keiland/wallpapers/Lakeside\.ppm#keiland/wallpapers/Lakeside.png#g' \
  -e 's#\(share/keiland/wallpapers/[A-Za-z$][A-Za-z0-9_${}-]*\)\.ppm#\1.png#g' \
  < plan/ws138/temp/scripts.txt
```

- 5 つ目の規則は、生成の背景の名前（`Aurora.ppm`・`$name.ppm`）を変える（例 `plan/ws089/tests/settings-p009.sh:111-112`）。
- 他の `.ppm`（画面の撮影の `page.ppm`、利用者の画像の試験の `Sunset.ppm`・`Tiny.ppm` など）は背景ではないので、この規則に当たらない。

### 5. 手で直す所

規則で直らない、または規則で壊れる所。

1. `plan/ws075/demo/build-demo-image.sh:35-36`: `userland/desktop/keiland/wallpapers/*.ppm` の loop を `*.png` にする。
2. `plan/ws099/tests/c7-contrast.sh`:
   - 39 行を `ls /usr/share/keiland/wallpaper.png /usr/share/keiland/wallpapers/*.png 2>/dev/null' | grep '\.png$'` の形にする。
   - 41 行を `basename "$picture" .png` にする。
   - 直さないと、背景が 0 枚と数えられ、`C7: FAIL` になる（design-reviewer が写しで確かめた）。
3. `plan/ws089/tests/host-wallpaper.sh`: 既定を `wallpaper.png → Lakeside.png` にする（p001 で `wallpaper.ppm → Lakeside.ppm` にしてある）。`Small.ppm` はそのまま残す（PPM の試験）。

### 6. 残りを一つずつ見る

```sh
git grep -nE 'wallpaper.*\.ppm|wallpapers/.*\.ppm|Birch-Lake\.ppm|Lakeside\.ppm|Aurora\.ppm' -- \
    '*.sh' '*.py' '*.mk' '*.c' '*.h' '*.in' '*.desktop' '*Makefile*' ':!plan/history' ':!*/evidence/*' ':!plan/ws138/*'
```

出た行を、次の 3 つのどれかに分け、「結果」の表（file:行・区分・理由）に書く。

- (a) 背景を指している → 直す。
- (b) PPM を読めることの試験 → 残す。例: `plan/ws089/tests/host-wallpaper.sh` の `Small.ppm`。
- (c) 背景の file を指さない → 残す。既知の物は次のとおり（2026-10-04 の写しでの試し）。

| file | 行 | 理由 |
| --- | --- | --- |
| `plan/ws089/tests/host-wallpaper.sh` | 10・34 付近 | 画面の撮影の `wallpaper/page.ppm` |
| `plan/tools/settings/host-store.c` | 167・168・183 | 設定の key の形の試験（`/a.ppm`・`a.ppm`・`relative.ppm`）。絶対・相対の path の試験で、file を読まない |
| `plan/tools/keiland-launcher/check.py` | 37 | 引数の受け渡しの試験（`a picture.ppm`） |
| `plan/ws035/tests/zdesktop-p108.sh` | 55・91 | 一時の名前の `/tmp/wallpaper.ppm.saved`（中身は PNG になるが、名前だけなので害が無い） |

### 7. 背景の読み込みの時間の script（`plan/ws138/tests/wallpaper-time.sh`、新）

compositor が PNG と PPM の両方を読めることと、起動の時の読み込みの時間を比べる。

1. host で `python3` を使い、`userland/desktop/keiland/wallpapers/Birch-Lake.png` を復号して、P6 の PPM を `OUT/w.ppm` に書く。
   p001 の `ppm-to-png.py` に逆の関数（`read_png`）を足して使う。
2. guest へ `guest.py put` で、`OUT/w.ppm` を `/tmp/w.ppm` に、tree の `Birch-Lake.png` を `/tmp/w.png` に置く（両方を `/tmp` に置き、disk の cache の差を揃える）。
3. PNG と PPM を交互に、各 4 回走らせる。最初の 1 回ずつは捨てる（cache の温め）。
   `guest.py run '/bin/wayland --timeout=8 --width=1280 --height=800 --glass --wallpaper=/tmp/w.png > /tmp/wt.log 2>&1; grep -E "ZWL STARTUP step=wallpaper|ZWL GLASS no wallpaper" /tmp/wt.log'`
   - 他の compositor が動いていれば先に止める（`plan/ws099/tests/c5-transitions.sh` の `stop_all` に倣う）。
   - `--timeout` の秒で終わることは、既存の script（`settings-p009.sh:24`）の書き方で確かめる。
4. `step=wallpaper ms=` と `step=wallpaper-picture ms=` の中央値を、PNG と PPM で表にして出す。`ZWL GLASS no wallpaper` の行が出たら FAIL。
5. reset の同期の道（U7）も 1 回測る。
   Settings の probe（`keiland-settings`）で背景を設定してから既定に戻し、`ZWL GLASS wallpaper path=… ms=` を取る。probe の使い方は `plan/ws089/tests/settings-p007.sh` に倣う。
6. 判定は「両方とも読めた」だけ。時間は記録だけで、判定しない（QEMU の host は GPU が無く、CPU の時間も他の負荷で揺れる）。
7. `GUEST_RUNTIME` は呼ぶ側が渡す（既定は `build/ws089-run`、settings-guest.sh と同じ）。

### 8. 自分で行う確かめ（QEMU は起動しない）

```sh
grep '\.sh$' plan/ws138/temp/scripts.txt | xargs -n1 sh -n     # 文法
grep '\.py$' plan/ws138/temp/scripts.txt | xargs python3 -m py_compile
python3 plan/tools/keiland-launcher/check.py userland/desktop/wayland/keiland-desktop.in   # PASS
sh plan/ws089/tests/host-build.sh && sh plan/ws089/tests/host-wallpaper.sh
python3 userland/desktop/wallpapers/generate.py build/ws138-gen --preview=build/ws138-gen/preview.png   # 5 枚の .png、preview を目で見る
B=build/ws138-p002
make ZEDBSD_CONFIG=plan/ws089/tests/config-amd64-settings.mk BUILD=$B $B/bin/wayland $B/bin/settings $B/bin/files 2>&1 | grep -c 'warning:'   # 0
timeout 900 make -j16 keiland-linux KEILAND_LINUX_BUILD=build/ws138-linux && ls build/ws138-linux/share/keiland/ build/ws138-linux/share/keiland/wallpapers/
git diff --stat | tail -1                                          # 変えた file の数を記録
git diff --check
```

## 試験の依頼（T1/T2）

commit と Q1 への merge 依頼の後に依頼する。依頼の後は待たずに、Q1 が投入した次の仕事に移る。
image は 2 つ。同じ T が 1 つずつ順に QEMU で流す。合わせて 30 分程度の見込み（推測）なので、長い段は background で流す。

1. **Settings の image**: `SETTINGS_CONFIG=plan/tools/settings/config-amd64-settings.mk sh plan/ws089/tests/build-settings-image.sh BUILD`。
   - この config は、Settings の image に IME と `keiland-settings`（settings-p007 が要る probe）を足した物なので、下の 4 つを 1 つの image で流せる。
   - guest は `plan/ws089/tests/settings-guest.sh start BUILD/hdd-image.img`。
   - 流す物:
     - `plan/ws089/tests/settings-p009.sh`: 生成の 5 枚（PNG）の一覧・縮小・選択（`ZSETTINGS LOOK pictures ready count=6`、`ZWL GLASS wallpaper path=…/<名前>.png`）。
     - `plan/ws089/tests/settings-p004.sh`: 背景の page。
     - `plan/ws089/tests/settings-p007.sh`: 背景を変える。
     - `plan/ws138/tests/wallpaper-time.sh`: PNG と PPM の両方が読め、時間の表が出る。
   - 合否: 4 つが PASS。PNG を目で見て、縮小の tile が灰色の無地でなく絵であること。
2. **criteria の image**: `sh plan/ws099/tests/build-criteria-image.sh BUILD`。guest は `plan/ws035/tests/zdesktop-guest.sh start BUILD/hdd-image.img`。
   - `plan/ws099/tests/c7-contrast.sh`: 6 枚の背景の上の文字の contrast（T1-006 の時は pass=72 fail=0 min_contrast=4.68）。`wallpapers: 6` と出ること。
     `$name-files.png` の Files の hero が絵であることも目で見る。
   - **greeter の背景**: この image は graphical boot で、起動すると greeter が出る。
     - guest で `grep -E 'ZWL STARTUP step=wallpaper|ZWL GLASS no wallpaper' /var/log/greeter.log` を流す（`sessiond/greeter.c:48` の log）。
       `no wallpaper` が無く、`step=wallpaper` があること。
     - `plan/ws035/tests/zdesktop-shot.py` で greeter の画面を撮り、背景が湖の絵であることを目で見る。使い方は script の先頭。
   - 合否: c7 が PASS、greeter の log と画面の確かめ。
   - guest を止めてから、`OUTPUT=build/ws138-p002/boot plan/tools/boot-test.sh BUILD/hdd-image.img`: login prompt（boot-test は自分で QEMU を起こす。QEMU を 2 つにしない）。
3. **FreeBSD の build**: `timeout 5400 sh plan/tools/keiland-freebsd/backend-test.sh`（WS137 の guest。`keiland-freebsd.mk all` を warning 0 で含む）。
   - 合否: `summary.txt` の build の step が PASS。他の step の結果も返してもらう。

## 受け入れの条件

1. 手順 6 の grep の残りが、(b)・(c) の表に載せた行だけ。
2. 手順 8 の確かめが全て通る。
3. T の 3 つの依頼が PASS。画面の確認（Settings の tile、greeter、Files の hero）と、背景の読み込みの時間の表（起動と reset）を「結果」に書く。

## 結果

（未実施）
