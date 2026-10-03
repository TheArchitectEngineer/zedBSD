<!-- awesome-plan project=zedbsd record=ws035p133 -->

# ws035-p133: 壁紙の ppm の読みと拡縮を短くする

Phase ID: `ws035-p133`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-29、サブエージェント、worktree `wt/ws035`。QEMU の Venus、実機は未実施）
Phase disposition: normal
Queue: なし（2026-09-29 main の割り当て「ws035-p133: wallpaper の ppm の読みと拡縮の短縮（約 170 ms）」。読みの方法・拡縮の算法・cache は任せる。
起動の計測（`ZWL STARTUP`、1920x1280 で前後それぞれ 2 回）と、画面で壁紙が正しいことを確かめる。WS089 p007 の `zwl_glass_wallpaper()`
（設定の file での差し替え）も同じ速い読みを使い、壊さない）

## 範囲と原因

[p131](../phase131/phase.md) の後、compositor の起動の `wallpaper-picture`（`glass.c` の `wallpaper_fill`: ppm の読み、出力の大きさへの拡縮、
float への変換、mapped image への詰め）が 1920x1280 で約 165 ms。一時的な計測（`build/p133-probe.img`、計測の code は残さない）で分けると:

- ファイルの読み（3 MB、`/usr/share/keiland/wallpaper.ppm` 1280x800）: **75〜82 ms**。guest での warm の読み（`dd`）は約 10 ms なので、
  起動の後の初めての読み（disk からの cold read）が主。CPU の工夫では縮まない。
- 行の生成・mapped image への複写・ぼかしの block の平均（新しい行ごとの経路）: 計 約 22 ms。
- 変更前は、これに加えて出力の大きさの float の中間（1920x1280x3 の float、約 29 MB の確保と書き込み）、画素ごとの整数の除算 2 回と
  float の除算 3 回、`pack_pixel` での float からの変換があった。
- ぼかしの pass（`blur_pass`、約 80 ms）は範囲外（読みと拡縮ではない）。

## 設計と実装

`userland/desktop/wayland/glass.c`・`zwl.h`・`main.c`:

1. **先読み**（`zwl_glass_prefetch()`、新）: `main.c` が gpu を開く前（preferences を読んだ後なので path は確定している）に呼び、
   thread（pthread）で壁紙の file を読む。Vulkan の device・objects・arrow の作成（約 460 ms、GPU を待つ）と disk の読みが重なる。
   `wallpaper_load` は、先読みが同じ path を読んだならその bytes を受け取り（`prefetch_take`: join してから）、そうでなければその場で読む。
   先読みの後に preferences が別の path を選んだ場合は、先読みの bytes を捨てて読み直す。thread を作れなければその場で読む。
2. **一度の read**（`file_read`）: 16 MB の固定の buffer をやめ、`fstat` の大きさ（上限 `GLASS_FILE_MAX`）の buffer に読む（font の読みも同じ関数）。
3. **行ごとの拡縮**（`wallpaper_fill`・`wallpaper_row`・`landscape_row`）: 出力の列ごとの元の列（byte の offset）を表にし（出力の幅ぶん、
   呼ぶたびに作る）、元の byte からそのまま BGRA に詰める（`pack_pixel` と同じ値）。拡大で同じ元の行が続くときは前の行を使い回す。
   1 行の buffer を mapped image に `memcpy` する（mapped image は書くだけで読まない）。出力の大きさの float の中間は無くなった。
4. **ぼかしの入力**（`blur_add`・`blur_average`・`blur_fill`）: 行を作るときに 4x4 の block の和を byte で足し、block の最後の行で
   小さい画像の行（0..1 の float）にする。`blur_fill` は小さい画像を受け取り、pass と詰めだけを行う（pass そのものは変えていない）。
   ppm の壁紙では、平均が float の和から byte の和に変わるだけで、結果は丸めの範囲で同じ（画面の比較で bar のぼかしの部分は一致）。
   zdesktop が描く風景では、ぼかしの入力が 8 bit に丸めた色になる（差は 1/255 の半分以下）。
5. 起動（`wallpaper_create`）と設定での差し替え（`zwl_glass_wallpaper`、WS089 p007）は、どちらも `wallpaper_fill` → `wallpaper_load` を通るので、
   同じ経路を使う。差し替えのときは先読みは無く（起動で受け取り済み）、その場で一度の read で読む。

規約: `plan/tools/style-check.py userland/desktop/wayland/glass.c userland/desktop/wayland/main.c` 0、`git diff --check` 0。

## 検証

**QEMU（amd64、Venus の guest 1920x1280、graphical の login の image。autologin の kei の session の compositor、`/run/user/1000/session.log`。
前 `build/p133-before.img`（main を取り込んだ後、変更前）、後 `build/p133-after.img`、2 回ずつ、`build/p133-measure.sh`）**:

| `ZWL STARTUP` | 前 | 後 |
| --- | --- | --- |
| wallpaper-picture（読み・拡縮・詰め・block の平均） | 163・166 ms | **38・41 ms** |
| wallpaper（2 つの画像の作成とぼかしを含む） | 443・453 ms | **328・338 ms** |
| compose の計 | 1202・1187 ms | 1087・1172 ms |
| session の READY まで（sessiond の `session ready waited_ms`） | 1433・1439 ms | 1320・1409 ms |

- compose の計と READY は glyphs（181〜290 ms）など他の段のばらつきが大きく、wallpaper の段の短縮（約 115 ms）ほどは揃って見えない。
- 途中の版（行ごとの拡縮だけ、先読み無し）: wallpaper-picture 134・102 ms（読みの cold read が残った）。
- 画面: `build/ws035-shots/p133/before-1.png`・`before-2.png`・`after-1.png`・`after-2.png`（1920x1280、`zdesktop-check.py` PASS）。
  前後の比較（PIL）: system bar より下（y ≥ 36、2,388,480 画素）は **全画素一致**。bar の x < 1400（ぼかしを使う部分）も一致。
  x ≥ 1456 の差は時計の文字幅（14:08 と 14:13）で desktops の縮図が 4 px ずれたもの（ずらせば差は 5 以下）。
- 設定での差し替え（WS089 の `plan/ws089/tests/settings-p007.sh`、変更後の image、1280x800）: PASS。`ZWL GLASS wallpaper path=/usr/share/keiland/wallpaper.ppm ms=102`、
  風景への戻し、設定の file の壁紙での起動（先読みと preferences の path が同じ）、ERROR 無し。
  画面 `build/ws035-shots/p133-prefs/wallpaper.png`・`removed.png`・`restart.png`。この image には `/bin/settings` が無いので、窓の不透明度の段は
  compositor の log だけで確かめた。

回帰（`build/p133-regress.sh`、`build/p133-after.img`、試験ごとに guest を起こし直す）: 全て PASS。
- `zdesktop-p126.sh` 2 周（1280x800）: login・Log Out の替わり目で黒 0・文字 console 0（`build/ws035-shots/p133-cycles/`）。
- `zdesktop-p052.sh`・`zdesktop-p053.sh`（`build/ws035-shots/p133-p052/`・`p133-p053/`）。
- `zdesktop-p128.sh`（1920x1280、Files の四隅と辺の resize、`build/ws035-shots/p133-p128/`）。
- boot test（`plan/tools/boot-test.sh build/p133-after.img`、GPU の無い q35: greeter は表示が無く終わり console の login）PASS、
  `build/ws035-shots/p133-20260929-boot-test.png`。

build（worktree の `build/amd64`、graphical の login の image）: desktop・wayland の warning 0、log に `Permission denied` なし。
main を取り込んだ後の build では、外部の package（openssh・openssl・perl の locale）と NoctLang の warning が出る（この Phase の source ではない）。

未実施: 実機（i915）。

## 残り

- ぼかしの pass（`blur_pass`、1920x1280 で約 80 ms、CPU）はそのまま。すりガラスの効果をやめるかをユーザーが WS075 の計測で決める（p135 は保留）ので、
  その決定の後に扱う。
- 先読みの thread は、compositor の Vulkan が使えず glass が作られないとき、読んだ bytes（3 MB）を process の終わりまで持つ（join されない）。

## Resume point

2026-09-29: cleared。
