<!-- awesome-plan project=zedbsd record=ws035p106 -->

# ws035-p106: File Manager の名前の衝突の dialog（F-041 の衝突の部分）

Phase ID: `ws035-p106`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-28 main の割り当て 3。[F-041](../../future-work.md) のうち衝突の部分だけ）

## 範囲

files の copy・move・drop で、行き先に同じ名前があるとき Replace / Skip / Keep Both を尋ね、複数の item の操作では
「Apply to all」で残りの衝突に同じ答えを当てる。F-041 の残り（日本語の UI 文言と IME、`$topdir/.Trash-$uid`）は範囲外で
F-041 に残る。

## 実装（2026-09-28）

- **task**（`userland/desktop/files/ops.h`・`task.c`）: `enum fm_collision`（KEEP_BOTH・REPLACE・SKIP）、task ごとの
  `collisions[]`（source ごと、既定は keep both）・`replacing`・`skip_count`、`fm_task_collides()`（copy・move の source の
  名前が行き先で他の item に使われているか。自分自身は数えない）。`task_resolve()` が source の target を決める: keep both は
  従来の次の空き名（"name 2"）、skip は何もしない（result は NULL、失敗ではない）、replace はその item の削除（folder は中身ごと、
  既存の `task_plan_delete`）を先に計画し、同じ source をもう一度計画して元の名前へ copy・rename する（step は計画の順に走るので
  削除が先）。source を中に持つ item は replace できない（EINVAL）。duplicate・link・restore は従来どおり。
- **actions**（`actions.c`、`files.h`）: user の copy・move（paste・drop・`fm_action_transfer`）で衝突があれば task を queue に
  入れずに持ち（`collision_task`）、`FM_DIALOG_COLLISION` で最初の衝突から 1 つずつ尋ねる。答えは `fm_action_collision()`、
  「Apply to all」は `fm_action_collision_all()`（残りの衝突に同じ答え）、Esc は `fm_action_collision_cancel()`（操作をやめる）、
  Enter は Keep Both。全部答えたら queue へ。undo・redo は尋ねない（keep both）。log: `DIALOG collision index= name= left=`、
  `COLLISION answer=… index= all=`・`COLLISION all=`・`COLLISION cancel`。
- **dialog**（`ui-overlay.c`・`ui-input.c`）: 「"name" already exists」、「An item with this name is already in "folder".」
  「Replace it with the one you're copying/moving, or keep both?」、2 つ以上なら check box「Apply to all N conflicts」、button
  Skip・Keep Both（青、既定）・Replace（赤）。`overlay_button` に青の既定の色を足した。
- **host 試験**（`plan/tools/files/host-model.c`）: 2b（skip で元の item が残り result は NULL、replace で file と folder
  （中の余分な file ごと）が置き換わり "2" の名前が出ない、move の replace）。`host-build.sh` から `dnd.c`（Wayland の header が
  要る）を外した（それまで host の build が通らなかった）。

## 検証（2026-09-28）

- host: `plan/tools/files/host-build.sh` の後 `build/ws071-host/files-model DIR`: collision の 10 件 ok。4 件の FAIL（PNG の
  ENOTSUP・open の 3 件）は変更前の tree でも同じ（既存、この Phase の外）。
- guest（amd64、Venus、lean な files の image `build/ws071-files-d3`）: `plan/ws035/tests/zdesktop-p106.sh`（新）PASS:
  Documents の 6 item を Downloads（古い Budget.csv・Report.pdf がある）へ paste → 1 つ目（Budget.csv）の問いに
  「Apply to all 2 conflicts」→ Replace、2 つ目（Report.pdf）→ Skip（Budget.csv は新しい内容、Report.pdf は古いまま、他の 4 件は
  copy、"2" の名前なし）。もう一度 paste → 6 件の衝突、Apply to all を入れて Keep Both → 6 件の "name 2"、問いは 1 回。
  もう一度 paste → Esc で止まり task は増えない。Report.pdf を sidebar の Downloads へ drag（move）→ 問い → Skip で動かない。
- 画面: `build/ws035-shots/p106-20260928-dialog.png`・`-copied.png`・`-all.png`・`-drop.png`。
- 回帰 PASS: files-p004（paste・delete の dialog・undo）、files-p010（drag での move）。
- style-check: task.c・actions.c・ui-overlay.c・ui-input.c は 0。build warning 0。
- 実機: 未実施。

## 残り

- Replace は置き換えた item を消す（Trash に入れない）。undo は copy・move だけを戻し、消した item は戻らない。
- cut の paste を Esc で止めると clipboard は空になる（paste が始まった扱い）。
- 問いは source ごとに 1 つずつ（新旧の大きさ・日付の比較の表示は無い）。folder の merge（中身を合わせる）は無い。
