<!-- awesome-plan project=zedbsd record=ws138 -->

# WS138: 背景の画像を PPM から PNG に

<!-- awesome-plan-current:start -->
Status: completed（2026-10-05。p001・p002 は T1-169・T1-171・T1-172 で cleared、p003 は Q1 が cleared）
Primary Milestone: MG006
Related Milestones: MG007（Linux・FreeBSD の package の data）
Objectives: O2
Parent: [Master](../master.md)
Queue: P1 generation17（2026-10-05）、p003 は q731
<!-- awesome-plan-current:end -->

## 目標

[F-071](../future-work.md)（2026-10-03 user「S1 は今の PPM のままにして、PNG への切り替えは S1 の後の作業にします。」、2026-10-04 user が WS を立てる指示）:
desktop の背景を PPM から PNG にする。ユーザーの決定 U4（2026-10-04）で **PNG と JPEG だけを読み、PPM を読む code は消す** に広げた。

## 結果

| 部分 | 結果 |
| --- | --- |
| 共通の復号 | `userland/desktop/picture/wallpaper.c`・`wallpaper.h`（`kl_wallpaper_decode`）。PNG（libpng-compat、透明な所は黒で合成: U8）と JPEG（libjpeg-compat、灰色は RGB に、CMYK は断る）を RGB の 3 byte／画素に。一辺 8192・16M 画素を越えたら EFBIG。形式は先頭の bytes で決める。PPM は他の file と同じく断る |
| compositor | 起動の prefetch の thread と session 中の loader の thread で読んで復号まで行う（ws035-p133 の重ねを保つ）。同期の復号の道を消し、file を読まない「風景に戻す」（`zwl_glass_landscape`）だけが同期（U7）。look を閉じる時、取られなかった prefetch の画素を解放する |
| Settings | Wallpaper の頁は `.png`・`.jpg`・`.jpeg` を一覧し、同じ名前は png→jpg→jpeg の順で 1 つ。縮小は全体の復号から。`wallpaper.c` と libpng・libjpeg・libz-compat を link |
| Files | Today の hero を `fm_image_load` に（Files の縮小表示と同じ復号） |
| 既定と data | 既定の背景は `/usr/share/keiland/wallpaper.png`（sessiond・session.sh・Settings・Files・Linux と FreeBSD の launcher・deb の smoke）。Linux・FreeBSD の既定は tree の `Birch-Lake.png`（U3）、過去の build を見る道は消した |
| tree の背景 | `userland/desktop/keiland/wallpapers/Birch-Lake.png`・`Lakeside.png`。元の PPM から可逆に変換（画素は同じ、行ごとに best の filter、zlib 9: U5）。PPM は消した（git の履歴にある） |
| 生成の背景 | `generate.py` が 5 枚を PNG（best の filter、zlib 9）で、5 つの process で並べて作る（15.9 s、旧 PPM は 14.5 s）。画素は旧 PPM と同じ |
| 道具 | `userland/desktop/wallpapers/ppm-to-png.py`（PPM を PNG に、書いた PNG を自分で復号して元の画素と比べる。`write_png`・`read_png` は generate.py と試験が使う） |
| 他の WS の file | 試験と道具の script 196 file の `wallpaper.ppm` などの置き換え、ws089 の host build、vmunix.mk の link、c7 の測る箱は、Q1 が main で掛けた（U2） |
| 全文規約 | p003 で 7 件を直した。例外は C の規格による `setjmp` の条件の 1 件 |

確かめ:

- host: 復号の host 試験（ASan/UBSan、10 項目）、ws089 の host-wallpaper、keiland-launcher の check、build（zedBSD・Linux の wayland・settings・files）は warning 0。
- QEMU（T1）: T1-169（settings-p009・p004・p007・boot-test PASS、wallpaper-time・c7・greeter は試験の側を直した）、T1-171、T1-172（c7-contrast pass=72 fail=0
  min 4.68、greeter-wallpaper PASS、wallpaper-time PASS）。FreeBSD の build は T1-169 で PASS。
- 起動の時の背景の読み込み（T1-169・T1-172、QEMU、記録だけ）: `step=wallpaper` の中央値は PNG 260 ms・JPEG 263 ms、`step=wallpaper-picture` は 25・27 ms。
  session の中で選ぶ時（thread の道）は PNG 89 ms・JPEG 92 ms。
- 実機: WS138 としての UAT は無い（背景の表示は 2026-10-05 の UAT の image に入った）。

## 制限・移管

- JPEG の EXIF の向きは見ない（背景の写真が横倒しになりうる）。
- 「風景に戻す」は同期の道で、QEMU で約 1.8 s event loop を止める（風景を画素ごとに計算して描くため。WS138 の前からの性質）。風景を一度描いた結果を
  保つか thread で描く案を Q1 に伝えた（Future Work の候補）。
- Settings・compositor の中で信頼できない画像を復号している。隔離された preview の command（[WS168](../ws168/ws.md)）に移す予定（Settings は v1、compositor は
  v2 の候補）。
- 古い install・`desktop.conf` に残る system の `.ppm` は何もしない（既定の背景に戻る: U6）。
- 残した試験: [plan/tools/wallpaper/](../tools/wallpaper/README.md)（復号の host 試験、背景の読み込みの時間、greeter の背景）。

## ユーザーの決定（2026-10-04、AskUserQuestion）

- U1: 2026-10-04 の UAT の後に始める。
- U2: 他の WS の試験の約 194 file と vmunix.mk の link の規則は Q1 が merge の時に main で掛ける。
- U3: Linux・FreeBSD の既定の背景は Birch-Lake.png。
- U4: PNG と JPEG だけに対応する。PPM を読む code は消す。
- U5: 全部最大の圧縮。
- U6: 消えた system の `.ppm` が desktop.conf に残っていても何もしない。
- U7: reset の時の復号は WS135 の thread の道に寄せる。
- U8: 透明な所は黒で合成する。

## Phase

| Phase | 内容 | 状態 |
| --- | --- | --- |
| p001 | 読めるようにする: 共通の復号、compositor の thread での復号、Settings・Files、tree の PNG、変換の道具、host 試験 | cleared（2026-10-05、T1-169・T1-172） |
| p002 | 1 回の commit で切り替える: 既定の path、generate.py、make の data、PPM の削除、他の WS の file の script（Q1）、T1 の試験 | cleared（2026-10-05、T1-169・T1-171・T1-172） |
| p003 | 全文規約の見直し、T の結果の反映、F-071・WS089・WS099 への結果 | cleared（2026-10-05、Q1） |

Phase の記録（phase.md、`apply-q1.sh`・`c7-boxes.diff`・`vmunix.mk.diff` を含む）は git の履歴にある（2026-10-05 の WS の完了で消した）。
