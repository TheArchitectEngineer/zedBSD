<!-- awesome-plan project=zedbsd record=ws071p007 -->

# ws071-p007: preview pane・Quick Look・サムネイル

Phase ID: `ws071-p007`
Parent: [WS071](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行。main の Queue への反映は main の session）

## 範囲

2026-09-27 に元の p007（preview pane、Quick Look、Get Info、MIME、開く）を大きさで分けた。Get Info と開く・別のアプリで開くは
[ws071-p012](../ws.md)。この Phase は [design.md](../design.md) §3（preview pane）、§9（サムネイル、Quick Look）、spec §17、§18:

- サムネイル（`thumb.c`）: PPM（P6）・PGM（P5）の読み込み、256 px の長辺への縮小、64 枚の LRU の cache（path と mtime）、
  main loop の 1 回に 1 枚。PNG は署名で見分けて `ENOTSUP`（decode は p010）。icon 表示・list 表示・dashboard の画像の icon を
  サムネイルに。
- preview pane（`ui-preview.c`、◨ と Ctrl+Alt+P）: 選択が 1 つならサムネイルか大きな icon、名前、種類（中身の判定
  `fm_mime_sniff` を含む）、大きさ、更新日時、タグ、text は先頭の 12 行。選択が複数なら数と合計。選択が無ければ今の場所。
- Quick Look（Space で開閉、Esc、card の外の click、×）: 窓の中の overlay。画像は収まる最大、text は先頭 60 行、他は大きな
  icon と情報。←→ で前後の項目へ（選択が複数ならその中）。
- 仕事がある間（task・search・サムネイル）は main loop が眠らない（`fm_ui_busy`）。

出発点: 前の agent が rate limit で止まったときの未 commit の `thumb.c`（branch `salvage/ws071`、commit ab6b36f0）を
`git cherry-pick -n` で取り込み、見直して続けた。

## 受け入れ

1. host の画面（preview pane の 1 つ・複数・text・画像、Quick Look の画像・text）と host の model 試験（PPM・PGM の読み込み、
   壊れた header の拒否、縮小の大きさ）。
2. Venus（QEMU）の log・画面で preview pane、Quick Look（Space・←→・Esc）、サムネイルが動く。
3. warning 0、`style-check.py` 0（新しい file）・既存の file は悪化させない。回帰（p002〜p006）PASS。

## 結果（2026-09-27）

cleared。

- 出発点の salvage（`thumb.c`、未検証）の見直し: `app->thumbs` 等の宣言が無く build できない、PNG の decoder
  （`fm_image_png`）を前提にしている、`snprintf` の header 抜け、規約の違反（空行・条件の中の呼び出し）、decode の画素数に上限が
  無い（8192×8192 の 256 MiB）を直し、PNG は署名だけ見て `ENOTSUP`（p010 で decode）にして書き直した。
- 実装: `thumb.c`（PPM・PGM の読み込み、`fm_image_thumbnail`、64 枚の LRU、`fm_thumb_tick` が 1 回に 1 枚、`fm_image_fit`）、
  `peek.c`（新規: 表示中の 1 file の中身の型 `fm_mime_sniff`、先頭 60 行・16 KiB、Quick Look の画像）、`ui-preview.c`（新規: preview
  pane の 1 つ・複数・選択なし、Quick Look の画像・text・その他の card、Space・Esc・←→・外の click・×）、`ui-grid.c`（画像の icon を
  サムネイルに: icon・list・dashboard の recent）、`ui.c`（pane と Quick Look の描画、tick でサムネイル、`fm_ui_wait`）、`main.c`
  （仕事がある間は poll を待たない）、`ui-input.c`（Space、Ctrl+Alt+P、Quick Look の key と click）、`mime.c`（`fm_mime_text`）、
  `ui-home.c`（dashboard の recent の entry に path）、`files.h`、`Makefile`。
- 試験: `make-home.sh` に Sunset.ppm（256×160）と Ramp.pgm（64×48）を足した（Pictures が 4 項目に。`files-p006.sh` の期待を 2→4）。
  `host-model.c` に 16 節（PPM・PGM・壊れた header・16 bit・巨大な幅・PNG の ENOTSUP・縮小・fit・cache・peek）。
  `files-p007.sh`（新規）、`files-regress.sh`（新規: phase の guest 試験を順に流す）。
- host: `files-model` **PASS**（57 項目）。画面 build/ws071-host/p007-one.png・p007-many.png・p007-look.png・p007-look-tiny.png・
  p007-text.png・p007-look-text.png・p007-look-other.png を見た。
- **QEMU（Venus）**: `files-p007.sh` **PASS**（サムネイル 3 枚、pane、Quick Look の開閉・→・Esc・外の click、text の peek）。画面
  build/ws071-p007/preview.png・look.png・text.png・look-text.png を見た。回帰 `files-p002.sh`〜`p006.sh` PASS
  （`files-regress.sh`、build/ws071-p007-reg/）。
- build warning 0（guest の image）、host の build 0、`style-check.py` 0（zdesktop-files 全部）、diff の空白検査 0。
- 実機（i915）: 未実施。
- 制限: Quick Look の小さい絵は bilinear で拡大するのでぼける（Tiny.ppm 2×2）。PNG・JPEG のサムネイルは p010 と Future Work F-d。
