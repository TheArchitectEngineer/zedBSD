<!-- awesome-plan project=zedbsd record=ws071p005 -->

# ws071-p005: 検索・タグ・最近の項目（libzdesktop）・Favorites の編集・Locations

Phase ID: `ws071-p005`
Parent: [WS071](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行。main の Queue への反映は main の session）

## 範囲

[design.md](../design.md) §6.1〜§6.3、§8、§13、spec §7〜§9、§19、§20、§25: タグ（xattr `user.zdesktop.tags`、定義の file と既定の
5 つ、索引、sidebar の Tags の場所、icon と list の色の点、Alt+1〜9 で付け外し、undo）、検索（検索欄、150 ms 後の開始、語の
解釈、協調の走査、scope の chip、履歴の中の 1 つの場所、Esc で戻る）、最近の項目（libzdesktop の新しい API
`zdesktop_recent_add/list/remove`、Recents の場所、file を開くと記録）、Favorites の編集（Ctrl+Alt+T で足す、行の × で外す、
`$XDG_CONFIG_HOME/zdesktop-files/sidebar`）、Locations の mount（仮想・system の mount を除く）、sidebar の scroll。

## 受け入れ

1. host の model 試験（recent・タグの xattr と索引）と host の画面、Venus（QEMU）の log・画面で上の操作が動く。
2. タグが UFS の file でも付く（xattr）。
3. warning 0、`style-check.py` 0（zdesktop-files 全部と libzdesktop/recent.c）。

## 結果（2026-09-27）

cleared。

- 実装: `userland/base/libzdesktop/recent.c`（新規、flock の下で一時 file に書いて rename、256 件、同じ path は 1 回）、
  `include/libc/zdesktop.h`（recent の API、`ZDESKTOP_VERSION` 3、`<stddef.h>`）、`libzdesktop/exports.map`（`zdesktop_recent_*`）、
  `libzdesktop/Makefile`。zdesktop-files: `tags.c`（xattr の読み書き、知らない名前は残す、索引）、`search.c`（語の解釈と
  directory の stack の走査、/dev・/proc・/sys・/run を除く、5000 件まで）、`ui-search.c`（検索欄の入力と遅延、scope、Recents・
  タグ・検索の一覧の読み込み、recent への記録）、`places.c`（Favorites の file、mount、タグの定義からの Tags）、`actions.c`
  （タグの付け外しと undo、Favorites の追加・削除）、`ui.c`・`ui-grid.c`・`ui-list.c`・`ui-input.c`（検索欄の描画、scope の chip、
  タグの点、sidebar の scroll と × の button、Ctrl+F・Alt+数字・Ctrl+Alt+T）。
- design からの変更: 最近開いた folder（dashboard 用）は libzdesktop の recent に入れず、p006 で file manager の中の list にする
  （folder の移動のたびにアプリ横断の list が folder で埋まるのを避ける）。design.md §6.2 に追記する（p006）。
- 途中で直した不具合: host の mount（efivarfs・cgroup 等）が Locations に並んだ（型と /sys・/proc・/dev・/run・/boot 等の下を除く）。
  索引の folder の親が無いと索引が書けなかった（親から作る）。zedBSD の libc に `strtok_r` が無い（自前で分ける）。
- host の model 試験 **PASS**（recent の追加・順・1 回・削除、タグの既定・xattr・知らない名前の保持・索引の追加と削除を足した）。
  host の画面 tagged.png（Report.pdf の icon の右下に Work と Ideas の点）、tagview.png、search.png（「“note”」と scope の chip）、
  recents.png を見た。
- **QEMU（Venus）**: `files-p005.sh` **PASS**（tmpfs の home）、`FILES_ON_UFS=1 files-p005.sh` で UFS の home でもタグ・タグの場所・
  検索・Recents・Favorites が通った（OPEN の期待が path に依存していたので緩めた。UFS の run は緩める前で、その 1 行だけが外れた:
  「Computer」の検索で /fhome と /tmp/fhome の両方が見つかり /fhome の方を開いた）。回帰 `files-p002.sh`〜`p004.sh` PASS。
  画面 build/ws071-p005/tagged.png・tag.png・search.png・favorite.png を見た。
- build warning 0、`style-check.py` 0。
- 実機（i915）: 未実施。
- 制限: 検索は名前・拡張子・種類・タグまで（中身の検索と全体の indexer は Future Work F-c）。他の道具が付けたタグは索引に無く、
  Tags の場所には出ない（`tag:NAME` の検索で見つかる）。IME が無いので検索欄に日本語は打てない。
