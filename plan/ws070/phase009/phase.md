<!-- awesome-plan project=zedbsd record=ws070p009 -->

# ws070-p009: glass の UTF-8 と glyph cache、role の icon

Phase ID: `ws070-p009`
Parent: [WS070](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示で WS071 のサブエージェントが計画・実行。main の Queue への反映は main の session）

## 範囲

[titlebar-design.md](../titlebar-design.md) §7、§8: zdesktop の glass の text を UTF-8 に（ASCII は今の atlas、他の文字は初めて描く
ときに第一の font か fallback font（`--fallback-font=`、既定 `/usr/share/fonts/zdesktop-fallback.ttf`）で atlas の cache の cell に描く、
最も長く使わない cell から空ける）、titlebar の control の role の icon を CPU で rasterize して atlas に 2 つの大きさで足す、
`glass_draw_icon`。窓の題名・menu の label の日本語が描ける。main の session の連絡（2026-09-27）で glass.c・compose.c は WS035 と
重ならないと確かめた。

## 受け入れ

1. Venus（QEMU）で日本語の題名の窓が浮いたタイトルバーと docked のシステムバーに正しく出る（画面）。log に 2 つの font と cache。
2. icon の一覧の host の画面、各 icon が四角の中に収まる。
3. 回帰: WS070（menu・titlebar-p008）、WS035 の zdesktop（p059・p064・p068）、WS071（files）。warning 0、style の悪化なし。

## 結果（2026-09-27）

cleared。

- 実装: `icons.c`・`icons.h`（新規: 15 の icon（戻る・進む・上・Home・検索・格子・一覧・列・並べ替え・絞り込み・sidebar・preview・
  ＋・×・…）を 24 単位の格子の stroke・ring・dot・角丸の box の表で持ち、各 pixel の中心からの距離で被覆を出す rasterizer）、
  `glass.c`（atlas を 1024×1024 に、font を開いたままにし fallback font も開く、ASCII の後ろに icon を 16・20 px で、残りを 48 px の
  cache の cell（最大 512）に、`glass_text_width`・`glass_draw_text` を UTF-8 に、`glass_draw_icon`、log `ZWL GLASS atlas ...`・
  `ZWL GLASS glyph codepoint=... face=...`）、`glass.h`、`zwl.h`（`fallback_font_path`）、`main.c`（`--fallback-font=`）、`shell.c`
  （印の文字を題名の最初の UTF-8 の 1 文字全部に: 日本語の題名で印が箱になっていた）、`Makefile`。
- 試験: `plan/ws070/tests/icons-host.c`（新規: icon の一覧の PPM と、被覆があり縁に掛からないことの検査）、`titlebar-p009.sh`
  （新規）、titlebar-probe の `--show=TITLE [--seconds=N] [--mode=menu|controls|tabs]`（窓を出し titlebar の model を渡し、event を
  印字。p010・p011 の画面の試験にも使う）。
- host: `icons-host` **PASS**、画面 build/ws070-host/icons.png を見た（4 つの大きさで形が崩れない）。
- **QEMU（Venus）**: `titlebar-p009.sh` **PASS**（faces=2、U+65E5 を fallback の font（face=1）で cache に、画面 build/ws070-p009/
  floating.png・docked.png を見た: 「日本語のタイトル — 表示の試験」が浮いたタイトルバーと docked のシステムバーに正しく、印も「日」）。
  回帰 `titlebar-p008.sh`・`menu-p003.sh`・WS035 `menu-regress.sh`（p059・p064・p068）・WS071 `files-regress.sh`（p002・p007・p008）
  **PASS**。
- build warning 0（変えた file）、style: 新しい file 0、glass.c 0→0・shell.c 0→0・main.c 8→8。
- 実機（i915）: 未実施。
- fallback font: **Droid Sans Fallback Full**（Apache-2.0、Google。Debian の fonts-droid-fallback の
  `/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf`、license は同 package の copyright file）。git には入れない。試験の image
  には WS071 の `build/ws071-fonts/`（`plan/ws071/tests/build-files-image.sh`、WS071 p002 から）から入れた。2026-09-27 main の依頼で
  main の `build/ws035-fonts/` にも `DroidSansFallbackFull.ttf` と `DroidSansFallback-LICENSE.txt` を置き（足しただけ）、
  `plan/ws035/tests/build-zdesktop-image.sh` があれば `/usr/share/fonts/zdesktop-fallback.ttf`（と license）として入れるようにした。
- 制限: cache の cell を追い出すとき、前の frame がまだその cell を読んでいれば 1 frame だけ乱れうる（zdesktop は frame ごとに
  待つので起きにくい）。1 つの文字列に 512 を超える種類の文字は描けない（cell が足りない）。
