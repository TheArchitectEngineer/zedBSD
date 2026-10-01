<!-- awesome-plan project=zedbsd record=ws105-p008 -->

# ws105-p008: app の Linux の build と install の data

Status: planned
Disposition: normal
Parent: [WS105](../ws.md)
Queue: なし
依存: p007
実行者: phase-runner（high）か phase-runner-mid（機械的な部分が多い）

## 目的

決定 D21 の app を Linux で build・install し、guest の compositor の App Home から起動して使えるようにする。

## 作る file

`Makefile.linux` を作る package（依存は各 package の zedBSD の `Makefile` の `ZEDBSD_USERLAND_PACKAGE` の 10 番目の引数と同じ。SONAME で書く）:

| package | 種類 | 備考 |
| --- | --- | --- |
| `userland/desktop/libkeiui` | library `libkeiui.so` | |
| `userland/base/libpdf` | library `libpdf.so` | `sha2.h`・`md5.h` は `linux-compat`（p002）の `libkeiland-compat.a` を link |
| `userland/desktop/terminal` | program | `openpty` は glibc の libc（2.34 から libutil は libc に入った）。古い glibc なら `-lutil` |
| `userland/desktop/files` | program | `sys/xattr.h` は Linux と同じ形。`sha2.h` は linux-compat |
| `userland/desktop/settings` | program | network・音の頁は p010 まで「無い」と出る（仮の backend） |
| `userland/desktop/notes` | program | linux-compat |
| `userland/desktop/textedit` | program | |
| `userland/desktop/imageview` | program | |
| `userland/desktop/pdfviewer` | program | |
| `userland/desktop/kuidemo` | program | |
| `userland/desktop/ime` | program `libexec/keiland-ime` と辞書（`ime/dict` の data。zedBSD の `Makefile` が image に入れる path を `KEILAND_DATADIR` の下に写す） | |

install の data（`keiland-linux.mk` か各 `Makefile.linux` の `KEILAND_LINUX_DATA`）:

- wallpaper: design §3.2（`generate.py`）。zedBSD の image で `/usr/share/keiland/wallpaper.ppm` と `wallpapers/` に入る物を、`share/keiland/` に。
  zedBSD の image の作り方（`plan/ws089/tests/build-settings-image.sh`・demo の image の script）を見て、同じ物を同じ名前で作る。
- `/etc/keiland/` の設定（`apps.conf`・`open-with`・`desktop` など）: zedBSD の image に入る物があれば `etc/keiland/` に。無い物は作らない（compositor・app は無いときの既定で動く）。
- 各 app の data（`/usr/share/...` に入る icon・model など）: 各 package の zedBSD の `Makefile` の 13 番目の引数（`DEST=SRC`）を見て、`/usr/share/X` → `share/X`、
  `/etc/X` → `etc/X`、`/bin/X` → `bin/X`、`/usr/libexec/X` → `libexec/X` に写す。

source が Linux で compile できない所（zedBSD の libc だけの関数・header）が見つかったら:

1. 関数が `linux-compat` で足せる小さな物（OpenBSD の便利な関数など）なら、`userland/desktop/linux-compat/` に足す（src/libc の source を compile する形が先）。
2. app の code の中の OS の違いなら、止めて main に報告する（その app の OS の部分を `linux/` の file に分けるかを決める）。macro の block で逃げない。

## 確かめ（完了の条件）

1. `make keiland-linux`（gcc・clang）warning 0、`elf-check.sh`・`makefile-sync.sh` PASS。
2. guest（p006 の起動の手順）: App Home から各 app を起動し（`guest.sh click` で tile を押す。tile の位置は App Home の screenshot から）、10 秒後に窓が出て process が生きている
   （`guest.sh ssh pidof <program>`）。app ごとに screenshot を撮り、PNG をユーザーに見せる。
3. Terminal: `guest.sh type 'echo keiland-linux-ok'`・`guest.sh key ret` の後の screenshot に文字が出る（目で確かめ、結果に書く。文字の判定の道具は作らない）。
4. Files: home の directory の一覧が出る。Text Editor: 文字を打てる。Image Viewer: `share/keiland/wallpapers/` の画像を開ける。PDF Viewer: 小さな PDF
   （`plan/` の試験の PDF があれば。無ければ省いて「未実施」）。Settings: 頁を移れる（network・音は p010）。IME: 日本語の入力の切り替えが zedBSD と同じ key で効くか（効かなければ記録）。
5. 各 app を閉じて compositor が動き続ける。
6. zedBSD の回帰（共通の file を変えたなら、design §9.2）。

## 結果

（実行の後に書く）
