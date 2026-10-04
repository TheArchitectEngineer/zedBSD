<!-- awesome-plan project=zedbsd record=ws138-p002 -->

# ws138-p002: 試験と demo の script を PNG に、QEMU の試験の依頼

Status: planned
Disposition: normal
Parent: [WS138](../ws.md)
Queue: none
依存: p001（tree の PNG と、各 program の PNG の読み込みが commit 済みであること）
時限の目安: 置き換え 2 h、試験の依頼の用意 1 h

## 範囲

- **入る**: 背景の `.ppm` を指す試験と demo の script の置き換え（ws.md の表の 196 file から、p001 で直した物を除く）。
  背景の読み込みの時間を測る小さな script。T1/T2 への試験の依頼（p001 と p002 をまとめる）。
- **入らない**: program の source（p001 で済み。直しが要れば p001 の続きとして直し、ここに記録する）。全文規約の見直し（p003）。
- **所有する path**: 置き換える script（他の WS の `tests/`・`plan/tools/` を含む。ws.md の U2 の Q1 の許可が要る）、`plan/ws138/`。

## 手順

### 1. 対象の一覧を作る

```sh
grep -rlE 'wallpaper.*\.ppm|wallpapers/.*\.ppm|Birch-Lake\.ppm|Lakeside\.ppm|Aurora\.ppm' \
    plan/tools plan/ws*/tests plan/ws*/demo tools userland | sort > plan/ws138/temp/before.txt
wc -l plan/ws138/temp/before.txt
```

`plan/ws138/temp/` は git に入れない（AGENTS.md）。p001 の後なので、userland の file は出ないはず。出たら p001 の残りとして直す。

### 2. 機械的な置き換え

次の 5 つの規則だけを、一覧の file に掛ける。他の `.ppm`（画面の撮影の `page.ppm`、利用者の画像の試験の `Sunset.ppm`・`Tiny.ppm` など）は背景ではないので変えない。

```sh
xargs sed -i \
  -e 's#/usr/share/keiland/wallpaper\.ppm#/usr/share/keiland/wallpaper.png#g' \
  -e 's#share/keiland/wallpaper\.ppm#share/keiland/wallpaper.png#g' \
  -e 's#keiland/wallpapers/Birch-Lake\.ppm#keiland/wallpapers/Birch-Lake.png#g' \
  -e 's#keiland/wallpapers/Lakeside\.ppm#keiland/wallpapers/Lakeside.png#g' \
  -e 's#\(share/keiland/wallpapers/[A-Za-z$][A-Za-z0-9_${}-]*\)\.ppm#\1.png#g' \
  < plan/ws138/temp/before.txt
```

- 5 つ目の規則は、生成の背景の名前（`Aurora.ppm`・`$name.ppm`）を変える（例 `plan/ws089/tests/settings-p009.sh:111-112`）。
- `plan/ws075/demo/build-demo-image.sh:35` の `userland/desktop/keiland/wallpapers/*.ppm` の loop は、手で `*.png` にする。

### 3. 残りを一つずつ見る

```sh
grep -rnE 'wallpaper.*\.ppm|wallpapers/.*\.ppm|Birch-Lake\.ppm|Lakeside\.ppm|Aurora\.ppm' \
    plan/tools plan/ws*/tests plan/ws*/demo tools userland
```

出た行を一つずつ読んで、次のどちらかにする。

- (a) 背景を指していれば直す。
- (b) PPM を読めることを試す意図の行なら残し、残す理由を下の「結果」の表に「file:行・理由」で書く。
  例: p001 の `host-wallpaper.sh` の `Small.ppm`。

次の種類も確かめる。

- tree の背景の画素を読む script: `plan/tools/files/files-p015.sh:61` は PIL の `Image.open` で読むので、PNG でもそのまま動く。
- 背景の file の大きさや bytes を前提にする script: `grep -rn '6220817\|P6' plan/tools plan/ws*/tests` で探す。
  出れば、その script の意図に合わせて直す。

### 4. 背景の読み込みの時間の script（`plan/ws138/tests/wallpaper-time.sh`、新）

compositor が PNG と PPM の両方を読めることと、起動の時の読み込みの時間を比べる。

1. host で `python3` を使い、`userland/desktop/keiland/wallpapers/Birch-Lake.png` を復号して P6 の PPM を `OUT/Birch-Lake.ppm` に書く。
   p001 の `ppm-to-png.py` の逆の関数を、同じ file か `png-to-ppm.py` に置く。
2. guest へ `guest.py put` で `/tmp/w.ppm` に置く。PNG は image の `/usr/share/keiland/wallpaper.png` を使う。
3. 次を 3 回ずつ、PNG と PPM で交互に走らせる。
   `guest.py run '/bin/wayland --timeout=8 --width=1280 --height=800 --glass --wallpaper=PATH > /tmp/wt.log 2>&1; grep "ZWL STARTUP step=wallpaper" /tmp/wt.log'`
   - 他の compositor が動いていれば先に止める。止め方は `plan/ws099/tests/c5-transitions.sh` の `stop_all` に倣う。
   - `--timeout` の秒で終わることを、既存の script（`settings-p009.sh:24`）の書き方で確かめる。
4. `step=wallpaper ms=` と `step=wallpaper-picture ms=` の中央値を、PNG と PPM で表にして出す。
   `ZWL GLASS no wallpaper` の行が出たら FAIL。
5. 判定は「両方とも読めた」だけ。時間は記録だけで、判定しない（QEMU の host は GPU が無く、CPU の時間も他の負荷で揺れる）。

### 5. 自分で行う確かめ（QEMU は起動しない）

```sh
sh -n <置き換えた sh の script の全て>          # 文法の確かめ（xargs で回す）
python3 -m py_compile <置き換えた .py の全て>
sh plan/ws089/tests/host-wallpaper.sh            # p001 と同じく PASS
git diff --stat | tail -1                         # 変えた file の数を記録
```

## 試験の依頼（T1/T2、p001 と p002 をまとめて）

commit と Q1 への merge 依頼の後に依頼する。依頼の後は待たずに、Q1 が投入した次の仕事に移る。
image は 2 つ。同じ T の 1 つの QEMU で順に流す。合わせて 20 分程度の見込み（推測）。

1. **Settings の image**: `SETTINGS_CONFIG=plan/tools/settings/config-amd64-settings.mk sh plan/ws089/tests/build-settings-image.sh BUILD`。
   この config は Settings の image に IME と `keiland-settings`（settings-p007 が要る probe）を足した物なので、下の 4 つを 1 つの image で流せる。
   guest は `plan/ws089/tests/settings-guest.sh start BUILD/hdd-image.img`。
   - `plan/ws089/tests/settings-p009.sh`: 生成の 5 枚（PNG）の一覧・縮小・選択（`ZSETTINGS LOOK pictures ready count=6`、`ZWL GLASS wallpaper path=…/<名前>.png`）。
   - `plan/ws089/tests/settings-p004.sh`: 背景の page。
   - `plan/ws089/tests/settings-p007.sh`: 背景を変える。
   - `plan/ws138/tests/wallpaper-time.sh`: PNG と PPM の両方が読め、時間の表が出る。
   - 合否: 4 つが PASS。PNG は目で見て、縮小の tile が灰色の無地でなく絵であること。
2. **criteria の image**: `sh plan/ws099/tests/build-criteria-image.sh BUILD`。guest は `plan/ws035/tests/zdesktop-guest.sh start BUILD/hdd-image.img`。
   - `plan/ws099/tests/c7-contrast.sh`: 6 枚の背景の上の文字の contrast。T1-006 の時は pass=72 fail=0 min_contrast=4.68。
   - `plan/tools/boot-test.sh BUILD/hdd-image.img`: greeter の背景が出る（PNG を目で見る）。
   - 合否: 2 つが PASS。C7 の min_contrast を前回と比べて記録する（画素は同じなので変わらない見込み）。
3. **FreeBSD の build**: `timeout 5400 sh plan/tools/keiland-freebsd/backend-test.sh`（WS137 の guest。`keiland-freebsd.mk all` を warning 0 で含む）。
   - 合否: `summary.txt` の build の step が PASS。backend の試験の他の step の結果も返してもらう。

Files の hero は p001 の host 試験（`host-p014.sh` の home.png）で見る。
guest の Files の試験は、Files の image の回帰（`plan/tools/files/files-regress.sh`）を Q1 が別の依頼で流す時に一緒に見る。この Phase の受け入れには入れない。

## 受け入れの条件

1. 手順 3 の grep の残りが、(b) の理由の表に載せた行だけ。
2. `sh -n`・`py_compile` が全て通る。
3. T の 3 つの依頼が PASS。PNG の画面の確認と、背景の読み込みの時間の表を「結果」に書く。

## 結果

（未実施）
