<!-- awesome-plan project=zedbsd record=ws138-p002 -->

# ws138-p002: 1 回の commit で PNG に切り替え、QEMU の試験を依頼する

Status: cleared（2026-10-05 Q1: T1-169（settings-p009・p004・p007・boot PASS）、T1-171（FreeBSD の backend-test 9 step PASS）、T1-172（c7-contrast pass=72 fail=0 最小 4.68、greeter-wallpaper PASS、wallpaper-time PASS）で受け入れを満たす）。以前: in-progress（2026-10-05 P1 generation17。切り替えを commit。他の WS の file は Q1 が `apply-q1.sh` で掛ける。T1 の結果まで cleared にしない）
Disposition: normal
Parent: [WS138](../ws.md)
Queue: Q1 の 2026-10-05 の割り当て（ベータ2、p001 → p002）
設計: [ws.md](../ws.md) の D3・D8、U3・U5・U6・U7
依存: p001 の commit が main に統合済み（PNG を読む code と tree の PNG の 2 枚。p001 の clearance は要らない）。ws.md の U2・U3・U5・U6 の決め
時限の目安: 切り替え 3 h、試験の依頼の用意 1 h

## 改訂（2026-10-05、ユーザーの決定 U1〜U8 の反映、P1 generation17）

この節が下の本文の該当する手順に優先する（本文は判断の前に書いた）。

- **U4 PNG と JPEG だけ**: この commit で PPM の読み込みを消す: `userland/desktop/picture/wallpaper.c` の移行の間の P6 の道と、Settings の一覧の
  `.ppm`。手順 3 の 2（U6 の移行の code）は書かない。手順 6 の (b)「PPM を読めることの試験」は無くなる（`Small.ppm` の試験は PPM が拒まれる
  ことの試験に変える）。試験に JPEG の背景を 1 枚足す（`plan/ws138/tests/` に置き、`--file` で入れる）。
- **U3 Linux・FreeBSD の既定の背景は Birch-Lake.png**: 手順 2 の 3・4 の既定は `userland/desktop/keiland/wallpapers/Birch-Lake.png`。過去の build
  （`build/ws035-wallpaper/`）を見る道は消す。
- **U5 最大の圧縮**: generate.py も `write_png(…, filter='best')`（手順 2 の 1 の Up の固定をやめる）。build の時間の増え方を「結果」に記録する。
- **U6 何もしない**: 古い `.ppm` を `.png` に読み替える code は書かない（既定の背景に戻る）。install で古い `.ppm` を消す規則も書かない。
- **U7**: 同期の復号の道は p001 で消した（`zwl_glass_landscape` だけが残る）。手順 7 の 5 の「reset の同期の道を測る」は、thread の道
  （`ZWL GLASS wallpaper path=… ms=` の poll の行）を測る形にする。
- **U8 黒で合成**: p001 で実装済み。
- **U2**: 手順 4・5 の他の WS の試験の置き換えと `vmunix.mk` の差分は、自分で直さず、command・一覧・確かめを Q1 に渡し、Q1 が merge の時に main で
  掛ける。

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
   `guest.py run '/bin/wayland --testing --timeout=8 --width=1280 --height=800 --glass --wallpaper=/tmp/w.png > /tmp/wt.log 2>&1; grep -E "ZWL STARTUP step=wallpaper|ZWL GLASS no wallpaper" /tmp/wt.log'`
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

2026-10-05 P1 generation17。切り替えの commit と、Q1 が main で掛ける script（U2）まで。QEMU・FreeBSD の build は T1 に依頼する（未実施）。

### 自分の commit に入れた物

| 項目 | 内容 |
| --- | --- |
| 既定の path | `sessiond.h`・`session.sh`・`look.c`（`LOOK_DEFAULT_PICTURE`）・`files.h`（`FM_WALLPAPER`）・`keiland-desktop.in`・`data/keiland.desktop`・`keiland-linux-deb/run.py` を `wallpaper.png` に。`main.c` の usage を `--wallpaper=/path.png` または `.jpg` に |
| PPM の削除（U4） | `wallpaper.c` の P6 の道を消した（PPM は EINVAL）。Settings の一覧から `.ppm` を外した。tree の `Birch-Lake.ppm`・`Lakeside.ppm` を `git rm`（README の表は PNG の 2 行だけにし、元の PPM は git の履歴と書いた） |
| generate.py（U5） | `ppm-to-png.py` の `write_png(…, 'best')` で `<名前>.png` を書く。5 枚を別々の process で作る（作れなければ順に）。画素は旧 generate.py の PPM と 5 枚とも同じ（`read_png` と `read_ppm` の比較） |
| `ppm-to-png.py` | best の filter の計算を速くした（5 形の filter を `zip` でまとめて作る `filter_rows`、符号付きの絶対値の和を `bytes.translate` で）。出力の bytes は変えていない: tree の 2 枚を元の PPM から作り直して `cmp` で同じ、generate.py の 5 枚も最適化の前後で `cmp` で同じ |
| make の data | `wallpapers/Makefile`・`keiland-linux.mk`・`keiland-freebsd.mk` を `.png` に。Linux・FreeBSD の既定は tree の `Birch-Lake.png`（U3）、`build/ws035-wallpaper` を見る道は消した。古い `.ppm` を消す install の規則は書かない（U6） |
| 文書 | `LINUX.md`・`README.freebsd.md`・`keiland/wallpapers/README.md` |
| 試験 | host 試験を PPM との比較から、Python の `read_png` の参照（`.rgb`）との比較に変え、PPM が EINVAL になることを足した。`plan/ws138/tests/wallpaper-time.sh`（新、手順 7 を下の形に変えた） |
| look.c の直し | ws089 の host build（gcc の `-Wformat-truncation`）で `snprintf` が警告になったので、名前を `%.63s` で区切った（p001 の code。main の ws089 の host build は p001 の統合から壊れていた。下の script の 3 と合わせて直る） |

手順 7 の改訂: U4 で PPM が読めなくなるので、「PNG と PPM の時間を比べる」は「PNG と JPEG の起動の時間（中央値）、PPM が errno 22 で拒まれること、session 中に thread の道で PNG・JPEG を選び既定に戻す時間」にした。

### Q1 が main で掛ける script（U2）

`plan/ws138/phase002/apply-q1.sh`。中身:

1. 手順 4 の 5 つの sed の規則を、`userland/`・`tools/`・`plan/history`・evidence・`plan/ws138` の外で背景の `.ppm` を指す file（2026-10-05 の main で 196 file。ws.md の 194 との差は 10-04 の後に増えた試験）に掛ける。
2. 規則で届かない所: `plan/ws075/demo/build-demo-image.sh`（loop）、`plan/ws099/tests/c7-contrast.sh`（一覧と basename）、`plan/ws089/tests/host-wallpaper.sh`（`$data` の既定と `Broken.png`）、
   `plan/tools/settings/settings-p003.sh`（`wallpapers/*.ppm` の 1 枚目）、`plan/tools/showcase/showcase.sh`（tree の `*.ppm` の loop）、`plan/ws089/tests/settings-p004.sh`（comment）、
   `plan/tools/keiland-launcher/check.py`・`plan/tools/files/host-render.c`（規則で済む。念のため）。
3. `plan/ws089/tests/host-build.sh` に compat の header の link と、`wallpaper.c`・libz/libpng/libjpeg-compat の compile を足す（`shared-*.o` の名前で、host-wallpaper.sh の link にも入る）。

自分の worktree で試しに掛けて確かめ、戻した（2026-10-05、main `dc78b6bd` の上）:

- 196 file が変わる。`.sh` の `sh -n`、`.py` の `py_compile` が全て通る。`git diff --check` が空。
- 残りの grep は手順 6 の (c) だけ: `check.py:37`（引数の試験）、`host-store.c:183`（key の形の試験）、`zdesktop-p108.sh:55・91`・`zdesktop-p109.sh:61・97`（一時の名前 `/tmp/wallpaper.ppm.saved`）、
  `host-wallpaper.sh:10・34`（画面の撮影の `page.ppm`）、`ppm-to-png.py:4`（変換の道具の入力）。
- `plan/ws089/tests/host-build.sh` と `host-wallpaper.sh`: PASS（4 枚の tile、`Broken.png` は error=22、縮小は PNG から。`build/ws089-host/wallpaper/page.png` を目で見た）。
- `plan/tools/keiland-launcher/check.py`: ALL PASS。`plan/tools/files/host-build.sh`: build できる。

### 確かめ（host だけ。QEMU・実機は未実施）

- `plan/ws138/tests/run-host-wallpaper-decode.sh`: 10 項目すべて ok（PPM は error 22）。
- zedBSD `make build/amd64/bin/wayland build/amd64/bin/settings build/amd64/bin/files`: rc 0、warning 0。`make ZEDBSD_KEILAND_WALLPAPERS=y build/amd64/wallpapers/Aurora.png`: 5 枚の `.png`。
- Linux `make -f userland/desktop/keiland-linux.mk all`: rc 0、warning 0。`share/keiland/wallpaper.png` は tree の `Birch-Lake.png` と同じ bytes、`wallpapers/` に 5 枚の `.png`
  （同じ build directory に以前の `.ppm` が残っているのは古い build の物。U6 で消さない）。FreeBSD は build していない（T1 の 3）。
- generate.py の時間（U5 の記録）: 旧（PPM）14.5 s。PNG（best・zlib 9）を順に作ると 106 s、filter の計算の最適化で 73 s、5 process で 15.9 s（64 core の host）。
  時間の大半は zlib の level 9（1 枚 8.5 s。level 6 なら 0.5 s で 10% 大きい）。process が作れない環境で順に作ると約 75 s。
- 5 枚の preview（`build/ws138-gen/preview.png`）を目で見た: Aurora・Dawn・Lagoon・Meadow・Twilight。
- `keiland-os-boundary`: PASS。style-check: 変えた C の file は base と同じ数（`wallpaper.c` は `setjmp` の 1 件だけ）。

### T1 への依頼（Q1 経由）

上の「試験の依頼」の 3 つ（Settings の image で settings-p009・p004・p007・`plan/ws138/tests/wallpaper-time.sh`、criteria の image で c7 と greeter の背景と boot-test、FreeBSD の backend-test）。
`apply-q1.sh` を掛けた main の上で流す（掛けないと試験の script が `.ppm` を探す）。

### T1-169 の結果と直し（2026-10-05）

T1-169（QEMU、証拠 `/home/awe/zedBSD-worktrees/t1/build/t1-169-out/`）: settings-p009・p004・p007・boot-test は PASS。残りの 3 つ:

1. **wallpaper-time.sh の PPM の拒否が errno=3**（期待 22）: decoder が正しい。zedBSD の EINVAL は 3（`include/uapi/errno.h` 24）で、22 は host（Linux）の値。
   試験の期待を `errno=3` に直した（`plan/ws138/tests/wallpaper-time.sh`）。同じ run の数字（記録だけ、判定しない）:

   | 項目 | PNG | JPEG |
   | --- | --- | --- |
   | 起動の `step=wallpaper`（中央値、ms） | 260 | 263 |
   | 起動の `step=wallpaper-picture`（中央値、ms） | 25 | 27 |
   | session 中に thread の道で選ぶ（`ZWL GLASS wallpaper path=… ms=`） | 89 | 92 |

   既定に戻す（風景、`path=-`）は 1791 ms。風景は画素ごとに計算して描く（`landscape_row`）ので、file を読む背景より遅い。同期の道（U7 で残した唯一の
   同期の道）で event loop を 1.8 秒止める。前からの性質で WS138 の範囲外だが、風景を一度描いた結果を保つ・thread で描く、を Future Work の候補として
   Q1 に伝えた。
2. **c7-contrast の FAIL（pass 54・fail 18）**: PNG 化とは関係が無い。測る箱（`C7_SETTINGS`・`C7_FILES`・`C7_INFO` の座標）が古い:
   Files の sidebar に Today と Home が足され（ws127-p011）、Locations と Recents が下に動いた。Settings の section の見出しも少し下に動いた。
   失敗した箱は文字の無い所を測っていた（f-side-recents は contrast 1.00 = 文字が無い）。T1-169 の画面で文字の位置を測り直した箱で、同じ画面を
   `c7-contrast.py` で測ると、6 枚の背景の全部で数える 3 項目が 4.5 以上（最小 4.68: f-group、Twilight と既定）。info の f-hint は 1.8 前後（前から info で
   数えない）。f-inactive の箱は今 Today（選ばれた行）に当たっていたので、使えない行の Desktop に移した（1.7〜2.0、info）。
   c7 は WS099 の file なので、直しは `plan/ws138/phase002/c7-boxes.diff` を Q1 が main で掛ける（`git apply`）。
3. **greeter の背景**: criteria の image は起動の時に kei を自動で login させるので greeter が出ない（T1-169 の `greeter.png` は session の画面で、
   既定の `wallpaper.png` の Birch-Lake が見える。session の既定の path が効いていることの証拠にはなる）。greeter を出して確かめる試験
   `plan/ws138/tests/greeter-wallpaper.sh` を足した（autologin を空にして `sessiond --graphical`、zdesktop-p095 と同じ手順、greeter の log の
   `step=wallpaper` と画面、終わったら autologin を戻す）。

残り: T1 に再依頼（c7 の diff を掛けた main で c7-contrast、wallpaper-time、greeter-wallpaper）、その結果の判定、p003（全文規約の見直し）。

## Q1 の判定（2026-10-05）

T1-172 PASS: c7-contrast（pass=72 fail=0、最小 4.68 ≥ 4.5）、greeter-wallpaper（greeter.png は Birch-Lake をぼかした login の画面）、wallpaper-time（PPM は errno 3 で拒否）。T1-169 の残り（settings-p009・p004・p007・boot）と T1-171 の FreeBSD の backend-test と合わせて **cleared**。既定に戻す時の 1.8 秒の同期の描画は範囲外（Future Work の候補）。
