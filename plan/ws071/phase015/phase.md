<!-- awesome-plan project=zedbsd record=ws071p015 -->

# ws071-p015: 付箋のように浮いたすりガラスの pane（とタブの見直し）

Phase ID: `ws071-p015`
Parent: [WS071](../ws.md)
Status: cleared（2026-09-27、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行）

## 範囲

ユーザーの言葉は [ws.md](../ws.md) の「2026-09-27 ユーザーの指示: タブと付箋のすりガラス」（参考画像は保存しない）。

1. 左（sidebar）・中央（content）・右（preview）の pane が窓の地を持たず、角丸と影で浮いた card になり、card の中はすりガラスでデスクトップが透ける。
   card の間はデスクトップがそのまま見える。layout は今のまま。
2. p016 を見たユーザーの指示でタブを作り直す（この Phase に含めた）: タブの行は content の card の上端の中、等幅、名前は中央、高さは文字の約 2.2 倍、
   選ばれたタブは青い文字・短い青の下線・少し明るい地、他は枠なしの灰の文字。× は選ばれたタブと pointer の下のタブだけに見える。2 つ以上のときだけ。
3. content も白で塗りつぶさない（すりガラスで透ける）。文字が読めるように薄い白の tint と節の題の濃さを合わせる。

compositor の部分は [ws035-p083](../../ws035/phase083/phase.md)（設計 [glass-design.md](../../ws035/glass-design.md)）。

## 実装（2026-09-27）

- `present.c`: PRE_MULTIPLIED の swapchain（surface が持てば）、canvas の shader は alpha をそのまま（`shaders/canvas.frag`、`shaders.h`）。
- `glass.c`（新）: glass の on・off と、frame ごとの card の list（変わったときだけ `zdesktop_glass_set_panels`）。`main.c` で open・refresh・close。
- `ui.c`: `fm_ui_panels`（sidebar・content の card・preview）、glass の地は `fm_canvas_clear`、sidebar の tint と節の題、layout の `card`
  （content の card、タブの行を含む）。`ui-grid.c`・`ui-preview.c`: glass のときは白 60 の tint だけ。`canvas.c`: `fm_canvas_clear`。
- `ui-tabs.c`: 行のタブに作り直した（p016 の足付きのタブ・pill・区切りは消した）。`files.h`: `fm_panel`、`FM_PANELS`、glass の色、`app->glass`、
  `layout.card`。
- 試験: `plan/ws071/tests/host-glass.c`（新、zdesktop の合成の CPU の真似）、`host-render.c` の `--glass=WALLPAPER`、`host-build.sh`、
  `files-p015.sh`（新）、`host-p013.sh`・`files-p013.sh` のタブの座標（(350,27)・× (655,27)、guest は × (595,27)）。

## 検証（amd64 だけ、2026-09-27）

- host: `host-build.sh`（-Werror）通る。`host-p013.sh` 16/16 ok。画面 `build/ws071-p015-host/{glass-one,glass-tabs,glass-preview,glass-home,opaque-tabs}.png`。
- guest（QEMU、Venus、lean image `build-files-image.sh build/amd64`、warning 0（外の package の警告だけ））: `files-p015.sh` PASS（GLASS on、
  zdesktop の log の card 2・3 つの位置、card の間の画素が壁紙（影の分だけ一様に暗い）、タブは panel を変えない、zdesktop の ERROR 無し）。
  画面 `build/ws071-p015/{one,preview,tabs}.png`。
- 回帰（同じ image）: menu-regress（p059 p062 p063 p064 p065 p068 p069 p070 p071 p072 p014 p076 p077 p078 p079 p080）全部 PASS。
  files-regress: p002〜p006・p012・p008・p014・p013 PASS、p007 は 1 回目に `PEEK … Meeting notes.txt` の log の確かめが早すぎて MISSING
  （f.log には行がある）、単独で 2 回流して 2 回 PASS（時間の揺れ、glass の最初の frame が少し遅いためと見る）。boot test PASS
  （`build/ws071-p015-boot/login.png`）。
- 規約: 新しい file（panels.c・panels.h・glass-protocol.c・libzdesktop/glass.c・zdesktop-files/glass.c・host-glass.c）の style-check 0。
  変えた file は ui-tabs.c・ui.c・ui-grid.c・ui-preview.c・canvas.c・present.c・main.c・shell.c・protocol.c（zdesktop・libwayland）・import.c・
  objects.c・wsi-wayland.c が悪化なし。`wsi-swapchain.c` は 63 → 64（既存の `goto cleanup` の形に合わせた 2 つ目の goto、規約 §14 の shared cleanup）、
  `host-render.c` は 55 → 56（既存の option の `strncmp` の連鎖に `--glass=` を足した 1 件）。
- 実機（i915）: 未実施。

画面（ユーザー向けの写し）: `/home/awe/zedBSD-rpi4/build/ws071-shots/p015-20260927-{venus-one,venus-preview,venus-tabs,venus-quicklook-on-glass,host-glass-one,host-glass-tabs,host-glass-preview,host-glass-home,host-opaque-tabs}.png`。

## 残り・判断が要る点

- ガラスの中身はぼかした壁紙（背後の他の窓はぼけない）: ws035-p057 の残り。
- 見た目（tint の白さ、節の題の濃さ、タブの地の明るさ）は主観なので反応で直す。
- Help の Shortcuts のカードのタブの行（p013 の残り、p011 で）。
