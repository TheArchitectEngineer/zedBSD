<!-- awesome-plan project=zedbsd record=ws071p010 -->

# ws071-p010: PNG のサムネイルと窓の中の DnD

Phase ID: `ws071-p010`
Parent: [WS071](../ws.md)
Status: cleared（2026-09-27、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行）
依存: p007（サムネイル・Quick Look）

## 範囲

1. PNG のサムネイル・preview・Quick Look: [ws035-p040](../../ws035/phase040/phase.md)（libz-compat）と
   [ws035-p041](../../ws035/phase041/phase.md)（libpng-compat）の **decode の半分** をここで先に作る（inflate と PNG の読み）。
   deflate・encode は ws035-p040・p041 に残す。
2. [design.md](../design.md) §12 の窓の中の DnD（spec §16）: 項目を folder（grid・list）、sidebar の folder・タグ・Trash、tab に落とす。
   修飾: 無し = 同じ device で move・違う device で copy、Ctrl = copy、Ctrl+Shift = link。drag の間は項目の絵と印が pointer に付く。
   Favorites の題へ folder を落とすと足す、Favorites の中の drag で並べ替え。

## 実装（2026-09-27）

### libz-compat・libpng-compat（decode）

- `include/libc/compat/zlib.h`・`userland/base/libz-compat/`（`inflate.c`・`checksum.c`・`exports.map`・`Makefile`）: zlib の
  `inflateInit`・`inflateInit2`（zlib の stream と、windowBits が負の raw deflate。gzip は無し）・`inflate`（stored・fixed・dynamic の
  block。入力は stream が揃うまで貯めてから一度に解き、出力は空きの分ずつ渡す。入力も出力も少しずつ渡してよい）・`inflateEnd`・`inflateReset`・`uncompress`・`adler32`・`crc32`・`zlibVersion`（"1.3.2"）。本家のコードは使わない。
- `include/libc/compat/png.h`・`userland/base/libpng-compat/`（`read.c`・`exports.map`・`Makefile`）: libpng 1.6 の simplified API の
  読む側（`png_image_begin_read_from_memory`・`_from_file`・`png_image_finish_read`・`png_image_free`、`PNG_FORMAT_*`、
  `PNG_IMAGE_SIZE` 等）。色型は gray・gray+alpha・RGB・RGBA・palette の全部、bit 深度 1〜16（16 bit は上の byte）、`tRNS`、filter 5 種、
  CRC の検査。Adam7 の interlace は読まない（message を付けて失敗）。gamma・色空間の変換は無し。
- `platform/amd64/vmunix.mk`: `libz-compat.so`・`libpng-compat.so`（`/lib`）、zdesktop-files が両方を link する。WS074 の browser も
  libpng-compat を使う予定（main 経由で調整済み、衝突なし）。
- `thumb.c`: PNG の signature で `thumb_png`（BGRA で読み、alpha を掛けて canvas の premultiplied に）。サムネイル・preview・Quick Look は
  同じ `fm_image_load` を通る。

### DnD（新 `ui-drag.c`）

- 項目の press から 6 px 動くと選択の全部を drag する。選択済みの項目の press は選択を変えず（release で drag が無ければ今まで通りに
  変える: `press_deferred`）、複数の項目をそのまま運べる。
- 的: folder の項目（選択の外）、sidebar の folder（無い favorite は除く）・タグ・Trash（Trash を表示中は除く）、他の tab の folder、
  Favorites の題（folder を足す）。表示中の folder そのものは的にしない。
- 落とす: folder へは `fm_action_transfer`（新、`actions_start` の公開の入口: paste と同じ task・undo）で move / copy / link。
  move と copy は device（`st_dev`）で決め、Ctrl で copy、Ctrl+Shift で link。タグは全部が持っていなければ付ける（外すのは menu）。
  Trash は `fm_action_trash`。Favorites の題は `fm_places_add_favorite`。
- Favorites の並べ替え: favorite の folder の press は release で移る（drag できるように）。他の favorite の folder の上で離すと
  `fm_places_move_favorite`（新、places.c）でその位置へ（上へは前、下へは後）。sidebar の題の hit `FM_HIT_SECTION`（新、enum の最後）。
- 描画: 的の四角を青の縁と薄い塗りで、pointer の右下に項目の絵（52 px、影付き）、2 つ以上なら数の badge、copy は緑の「+」、link は矢印。
  favorite の drag は入る位置の青い線と名前の pill。Esc で取りやめ（`DRAG cancel`）。
- log: `ZFILES DRAG start items=N` / `start place=N path=`、`DRAG target kind=folder|tag|trash|favorites|place|none`、
  `DRAG drop operation=move|copy|link|trash|tag|favorites|reorder|none …`、`DRAG cancel`。
- 窓の外（他の窓・他のアプリ・デスクトップ）への DnD と、zdesktop の titlebar の path の段（CONTROLS、ws071-p014 から zdesktop が描く）
  への drop は zdesktop の data device が要るので残す（design §12、Future Work 候補 F-i）。

### 試験の道具

- `plan/ws071/tests/make-home.sh`: `Desktop/Screenshot.png`（120x80 RGB）と `Desktop/Logo.png`（64x64 palette、透明の色）を足す
  （Pictures・Documents の項目の数を使う既存の試験を変えないため Desktop に）。
- `host-render.c`: `mods=`・`drag=`・`items` の action。`host-build.sh`: compat の source と `include/compat`。

## 検証（amd64 だけ、2026-09-27）

- host: 新 `plan/ws071/tests/host-png.sh` PASS（62 件: Python の zlib の level 0・1・6・9 の空・1 byte・繰り返し・text・乱数を一度に・
  少しずつ inflate、壊れた Adler-32・切れた stream を拒む。PIL の書いた gray 1〜16・gray+alpha・RGB 8/16・RGBA 8/16・palette 1〜8 と tRNS・
  各 filter を RGBA・gray・RGB・BGRA・ARGB に読んで PIL と一致、interlace と壊れた CRC を拒む）。
- host: 新 `plan/ws071/tests/host-p010.sh` PASS（move・Ctrl の copy・Ctrl+Shift の link・2 つを sidebar の Documents へ・Trash・タグ・
  他の tab・Esc・drag 無しの click・表示中の folder に落とすと何もしない・Favorites の題・Favorites の並べ替え、file の結果も確かめる）。
  画面 `build/ws071-p010-host/{move,copy,multi,favorite,reorder,desktop}.png`。
- guest（QEMU、Venus）: 新 `plan/ws071/tests/files-p010.sh` PASS: Desktop の Logo.png（64x64 palette）と Screenshot.png（120x80）の
  THUMB error=0、Logo.png を sidebar の Pictures へ drag（DRAG start・target・drop move、TASK move done、file が Pictures に）、
  Projects/zedBSD で README.md を docs へ（move）、Makefile の drag を Esc で取りやめ（DRAG cancel、drop は増えない、file は元のまま）、
  Favorites の Pictures を Desktop の上へ（reorder、sidebar の list の先頭が Pictures）。画面 `build/ws071-p010/{thumbs,drag,
  drag-folder,moved,drag-place,reordered}.png`。
- 回帰（同じ image）: files-regress（p002〜p008・p012〜p015・p009・p017）PASS（選択済みの項目の press と favorite の click を release に
  移したが既存の試験は変えずに通る）。host の host-p009・p013・p014 PASS（`host-run.sh --fresh` が clipboard も消すように: 前の
  試験の Ctrl+C が残ると host-p009 の Paste の無効が崩れた。試験の環境の問題でコードの問題ではない）。files-regress の既定に p010 を足した。
- boot test PASS（`build/ws071-p010-boot/login.png`）。
- 規約: 新しい file（`ui-drag.c`、libz-compat・libpng-compat、compat の header、`host-png.c`）の style-check 0、変えた file は悪化なし。
- 実機（i915）: 未実施。

## 結果

cleared。画面（ユーザー向けの写し）: `/home/awe/zedBSD-rpi4/build/ws071-shots/p010-20260927-venus-{thumbs,drag,drag-folder,moved,
drag-place,reordered}.png`、`p010-20260927-host-{move,copy,multi,favorite,reorder,desktop}.png`。

## 残り

- 窓の外への DnD（zdesktop の `wl_data_device`）、titlebar の path の段への drop: Future Work 候補 F-i のまま。
- drag 中の自動の scroll、tab の上で待つと tab が開く（spring-loaded）: 無し。
- libz-compat の deflate・libpng-compat の encode: ws035-p040・p041 に残す。
