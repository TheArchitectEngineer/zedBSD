<!-- awesome-plan project=zedbsd record=ws127-p011 -->

# ws127-p011: 左の pane の Home は ~/ の一覧に、今の抽象ホーム（dashboard）は「Today」という別の頁に

Status: planning
Disposition: normal
Parent: [WS127](../ws.md)
Queue: 未定

## ユーザーの要望と決定（2026-10-04 夜、原文）

「FilesでHomeを表示しているとき、~/が表示されず、意味論的にコンピュータ操作のHome、みたいなページ（抽象ホーム）が表示されている気がします。左側ペインでHomeをクリックしたら、きちんと~/を表示してほしいです。抽象ホームは残して、別なページ名にするのがいいです。Quick Accessは何か違うし、何という言葉がいいでしょうね？」→ 名前はユーザーのクリックの回答で **「Today」**。

## 今の状態（Q1 が source で確かめた）

- 左の pane の Home は `FM_LOCATION_HOME`（`userland/desktop/files/places.c:79`）で、`fm_location_name` が dashboard と home の folder の両方を「Home」と呼ぶ（`places.c:200-236`）。Home を開くと `ui-grid.c:75` で dashboard（`ui-home.c`: 壁紙の hero と挨拶、よく使う folder の card と件数、最近の file、最近開いた folder）を描き、`~/` の一覧ではない。

## ユーザーの決定（2026-10-05）

「WS127 p011	Today の置き場所と起動時の頁（ 左の pane の一番上、起動は Today）」→ Today は左の pane の一番上、Files の起動の時の頁は Today。

## 範囲

1. 左の pane の **Home** は `~/` の folder の一覧（普通の folder の location、`FM_LOCATION_FOLDER` で path は `$HOME`）にする。title bar・breadcrumb・history・戻る・新しい tab・Go の menu（`ui-menu.c:182`）・title bar の Home の control（`ui-titlebar.c:174`）も `~/` を開く。
2. 今の dashboard は残し、**「Today」** という名前の別の頁（新しい location の種類、例 `FM_LOCATION_TODAY`）にする。左の pane に Today の項目を Home の上に置くか、起動の時の既定の頁を Today にするか Home にするかは設計で決める（Q1 の案: 左の pane の一番上に Today、起動の既定は今と同じく dashboard＝Today）。icon は新しく（Home の家の icon と区別）。
3. 名前・icon・help（`ui-help.c`）・検索の語・spec の文書（spec §5・§19・§28）の更新。dashboard の中の「Home」の語（hero の文など）の見直し。
4. 試験: Files の回帰（files-regress・files-p00x）の期待（Home を開くと dashboard）を直す（見つけた担当がその場で直す、実行は T1）。

## 受け入れ（案）

- 左の pane の Home で `~/` の一覧が出る。Today で今の dashboard が出る。title bar の名前がそれぞれ「Home」「Today」。
- files-regress などの回帰が直した期待で PASS（T1）。C の全文の規約、build warning 0。
