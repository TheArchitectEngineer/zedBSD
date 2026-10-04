<!-- awesome-plan project=zedbsd record=ws127-p012 -->

# ws127-p012: Files の Tags の機能を削除する

Status: in-progress（q705-i01、P2 generation14、2026-10-05。実装・host 試験済み、QEMU（T1）待ち）
Disposition: normal
Parent: [WS127](../ws.md)
Queue: q705-i01（P2 generation14）

## ユーザーの指示（2026-10-04 夜、原文）

「FilesのTagsは、MacOSのパクリになるので、やめておきましょう。単純に削除でOKです。」

## 範囲

1. Files の Tags の機能を全部削除する: `userland/desktop/files/tags.c`、location の種類 `FM_LOCATION_TAG`、左の pane の Tags の section、file の tag の付け外しの menu・context menu、一覧・preview・info の tag の表示、検索の tag の条件、drag で tag を付ける操作、icon、help の文、spec の文書の Tags の節。参照は Files の source の約 20 file にある（Q1 の grep、2026-10-04）。
2. tag の保存に使っていた xattr: tag の他に xattr を使う機能（`info.c`・`task.c` の xattr の読み書き）が残るかを確かめ、tag だけの物は消す。Guardrail の「配置」の D14（Files の xattr を OS の境界の規則の対象外にする許可）と `plan/tools/keiland-os-boundary/check.sh` の許可の表から、不要になった項目を外す（Guardrail と checker の変更は Q1 に依頼）。
3. 既存の利用者の file に付いた tag の xattr は消さない（file を書き換えない）。読まなくなるだけ。
4. 試験: Tags を前提とする試験（files-regress の項目など）を削除か直す（見つけた担当がその場で、実行は T1）。Linux・FreeBSD の build（FreeBSD の extattr の読み替え）も合わせる。

## 受け入れ（案）

- Files の UI と source に Tags が残らない（`grep -i tag` で関係の無い物だけ）。Files の回帰が PASS（T1）。OS の境界の checker が PASS。3 OS の build warning 0。

## 結果（2026-10-05、q705-i01 P2 generation14）

- 削除した物: `userland/desktop/files/tags.c`（xattr `user.keiland.tags`・`~/.config/keiland/tags`・`tag-index`）、`FM_LOCATION_TAG`・`FM_SECTION_TAGS`・`FM_COLUMN_TAGS`・`FM_DRAG_TAG`・`FM_UNDO_TAGS`・`FM_ACTION_TAG_FIRST`・`struct fm_tag(s)`・`fm_entry.tags`・`fm_info.tags`・`fm_menu_state` の tag の欄・`app->tags`・`drag_tag`、`fm_tags_*`・`fm_action_toggle_tag`・`fm_tags_text`・`fm_icon_tag`、左の pane の Tags の section、Edit の Tags の submenu と context menu の Tags、Alt+1〜9、一覧・icon の tag の点、list の Tags の列、preview・Get Info の Tags の行、検索の `tag:NAME`、drag で tag を付ける操作、help の文。undo の before/after（tag だけが使っていた）も `fm_undo_push` から外した。3 つの Makefile から tags.c を外した。
- 残した物: xattr の読み書きは Get Info の属性の一覧（`info.c`）と copy の属性の複写（`task.c`）で使い続ける（範囲 2）。利用者の file に付いた `user.keiland.tags` は消さず、読まないだけ（範囲 3）。
- 試験の直し: `plan/tools/files/host-model.c`（tag の case 15 を削除、undo・info の呼び出しの引数）、`host-render.c`（menu state の出力から tags）、`host-p009.sh`（context menu の Work の行）、`host-p010.sh`（case 6 の tag への drag を削除）、`files-p005.sh`（case 1・2 の tag を削除）、`files-p008.sh`（case 6 の Edit > Tags を削除）。
- 確認: zedBSD の `bin/files`（`ZEDBSD_CONFIG=plan/ws089/tests/config-amd64-settings.mk BUILD=build/p2-q703`）warning 0、Linux（keiland-linux.mk）warning 0、FreeBSD は未実施（ユーザーの方針でベータ1 は不要）。host: `plan/tools/files/host-build.sh` の後 host-model・host-p009・host-p010・host-p013・host-p014・host-default・host-png が全て PASS（全行 ok）。OS の境界の checker PASS。`grep -i tag userland/desktop/files` は stage・storage の語だけ。
- Q1 に依頼: Guardrail の D14 の文（Files の xattr は `tags.c`・`info.c`・`task.c`）から tags.c を外す。`plan/ws071/spec.md` §20 タグ・§34 の tags の行に「削除（ws127-p012、2026-10-04 ユーザー）」の注記（他の WS の文書なので Q1 の判断）。
- 試験の依頼（T1、Q1 経由）: Files の回帰（`plan/tools/files/files-regress.sh`、直した files-p005・files-p008 を含む）。
