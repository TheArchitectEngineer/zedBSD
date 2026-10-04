<!-- awesome-plan project=zedbsd record=ws127-p012 -->

# ws127-p012: Files の Tags の機能を削除する

Status: planning
Disposition: normal
Parent: [WS127](../ws.md)
Queue: 未定

## ユーザーの指示（2026-10-04 夜、原文）

「FilesのTagsは、MacOSのパクリになるので、やめておきましょう。単純に削除でOKです。」

## 範囲

1. Files の Tags の機能を全部削除する: `userland/desktop/files/tags.c`、location の種類 `FM_LOCATION_TAG`、左の pane の Tags の section、file の tag の付け外しの menu・context menu、一覧・preview・info の tag の表示、検索の tag の条件、drag で tag を付ける操作、icon、help の文、spec の文書の Tags の節。参照は Files の source の約 20 file にある（Q1 の grep、2026-10-04）。
2. tag の保存に使っていた xattr: tag の他に xattr を使う機能（`info.c`・`task.c` の xattr の読み書き）が残るかを確かめ、tag だけの物は消す。Guardrail の「配置」の D14（Files の xattr を OS の境界の規則の対象外にする許可）と `plan/tools/keiland-os-boundary/check.sh` の許可の表から、不要になった項目を外す（Guardrail と checker の変更は Q1 に依頼）。
3. 既存の利用者の file に付いた tag の xattr は消さない（file を書き換えない）。読まなくなるだけ。
4. 試験: Tags を前提とする試験（files-regress の項目など）を削除か直す（見つけた担当がその場で、実行は T1）。Linux・FreeBSD の build（FreeBSD の extattr の読み替え）も合わせる。

## 受け入れ（案）

- Files の UI と source に Tags が残らない（`grep -i tag` で関係の無い物だけ）。Files の回帰が PASS（T1）。OS の境界の checker が PASS。3 OS の build warning 0。
