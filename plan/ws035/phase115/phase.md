<!-- awesome-plan project=zedbsd record=ws035p115 -->

# ws035-p115: files の folder の merge（F-050 の残り）と、Trash が別の file system にあるときの Replace の試験

Phase ID: `ws035-p115`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-28 main の割り当て「デモの仕上げ」2。[F-050](../../future-work.md) の残り）

## 範囲

(1) copy・move・drop で folder が同じ名前の folder に当たったとき、Replace・Skip・Keep Both の横に Merge（folder の既定）を出す。
Merge は中身を合わせ、中で名前が当たる item はもう一度尋ねる。undo が効く。(2) Replace で置き換えた item を Trash へ送るとき、
Trash が別の file system にある経路（copy と削除、[p110](../phase110/phase.md) の未試験）を試す。

## 設計と実装（2026-09-28、`userland/desktop/files`）

- **task の展開**（`ops.h`・`task.c`）: `FM_COLLISION_MERGE`。`fm_task_merge(task, index, &added)` は source の folder を task の中で
  その中身（名前の順）に置き換え、各 item の行き先を合わさる folder（`task->folders[]`、source ごとの行き先。NULL は task の destination）に
  する。答えは keep both から始まる。表（sources・results・failed・collisions・replaced・folders）は `task_grow()` で伸ばし、失敗のときは
  task を変えない。`fm_task_can_merge()`: 名前が当たり、両方が folder（link は除く）、どちらも他方を含まない。`fm_task_folder()`、
  `fm_task_set_folder()`、`fm_ops_mkdir_parents()`。copy・move の計画（`task_plan_source`・`task_resolve`・`fm_task_collides`）は
  source ごとの行き先を使う。
- **move の後始末**: move の merge は元の folder を `task->merged[]` に覚え、全 source の計画の後に新しい step `FM_STEP_RMDIR_EMPTY` を
  深い方から足す（空なら消す。skip した item が残れば folder も残り、失敗ではない）。
- **問い**（`actions.c`）: Merge の答えは `actions_collision_merge()`: 展開し、その中身から次の衝突を尋ねる。「Apply to all」+ Merge は
  task の中の全ての folder と folder の衝突を merge し、その後に folder でない最初の衝突を尋ねる（順序に依らない）。merge できない folder は
  message を出して skip。Enter は folder なら Merge、他は Keep Both（`actions_collision_default()`）。log `DIALOG collision ... merge=0|1`、
  `COLLISION answer=merge`、`COLLISION merge index= items= error= [all=1]`。
- **undo・redo**: copy の undo は従来どおり（結果を Trash、置き換えた item を RESTORE）。merge した folder そのものは元からあったので残る。
  move の undo は rename の前に元の folder を作り直す（merge が消した folder）。redo は `actions_redo_pairs()`: 1 つの task で item ごとに
  最初の行き先へ（`fm_task_set_folder`。従来は全部を `to[0]` の folder へ送っていた）。
- **dialog**（`ui-overlay.c`・`ui-input.c`・`files.h`）: folder と folder のときは card を 520 px にし、「A folder named "X" already exists」
  「There is already a folder with this name in "…".」「Merge the folders, or replace it with the one you're copying/moving?」、button は
  左から Skip・Keep Both・Replace（赤）・Merge（青、既定）。file の問いは従来の 460 px・3 つの button（既存の試験の座標を保つ）。
  merge の中の item の問いは行き先の folder の名前を出す。

## 検証（2026-09-28）

- host: `plan/tools/files/host-build.sh`、`build/ws071-host/files-model build/p115-host/model /dev/shm/kei035-p115`（worktree の ext4 と
  tmpfs の 2 つの file system）: 104 ok。新しい 2d（merge 19 件: 展開・入れ子の merge・中の file の Replace・copy の undo・move と空の folder の
  削除・skip した item の残り・move の undo・redo の item ごとの行き先）と 2e（Replace で Trash が別の file system: file と folder が
  copy で Trash へ、undo の RESTORE が copy で戻す、Trash に残らない 6 件）は全部 ok。FAIL 4 件（PNG の ENOTSUP・open の 3 件）は p106 から
  既存。
- guest（amd64、Venus、lean な files の image、worktree の `build/amd64`）: `plan/ws035/tests/zdesktop-p115.sh`（新）PASS:
  1. Desktop の Photos・Notes を Downloads（同名の folder がある）へ copy: Notes・Photos・Sub は merge（button と Enter）、中の a.txt は
     3 つの答えの問いで Replace。Downloads/Photos は a（新）・b・c・Sub/{deep,keep}、Notes は n1・n2、"2" の名前なし（`folder.png`・
     `file.png`・`merged.png`）。
  2. Ctrl+Z: `UNDO redo=0 kind=1 items=4`、`UNDO replaced items=1`、folder は元どおり（`undone.png`）。
  3. cut して paste、Apply to all + Merge: 2 つの folder と Sub が問いなしで merge（`all=1` 2 行）、a.txt だけ尋ね Skip。item は動き、
     空になった Desktop/Photos/Sub と Desktop/Notes は消え、a.txt の残る Photos は残る、clipboard は空（`moved.png`）。
  4. Ctrl+Z: `UNDO redo=0 kind=0 items=3`、消えた folder を作り直して item が戻る（`move-undone.png`）。
  5. XDG_DATA_HOME を root（UFS）、home を /tmp（tmpfs）にして Budget.csv を Replace: 古い file は別の file system の Trash へ copy され、
     Ctrl+Z の RESTORE で戻り Trash に残らない（`xfs-replaced.png`・`xfs-undone.png`）。zdesktop の log に ERROR なし。
- 回帰 PASS: zdesktop-p106（衝突の dialog）、zdesktop-p110（Replace の Trash と undo、cut の Esc）、files-p004（paste・delete・undo）、
  files-p010（drag の move）。
- build warning 0（files）。`plan/tools/style-check.py` task.c・actions.c・ui-overlay.c・ui-input.c・undo.c 0。
- 画面: `/home/awe/zedBSD-rpi4/build/ws035-shots/p115-20260928-{folder,file,merged,undone,moved,move-undone,xfs-replaced,xfs-undone}.png`。
- 未実施: 実機。drop（drag and drop）での merge は paste と同じ `actions_start` の経路なので guest では試していない。

## 残り

- merge した move の redo は元の folder（undo が作り直したもの）を空のまま残す（merge した folder を undo の記録に持たないため）。
- undo が作り直す folder の mode は 0755（元の mode・時刻は保たない）。
- merge した folder 自体の mode・時刻は行き先の folder のまま（中身だけを合わせる）。
