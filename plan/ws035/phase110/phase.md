<!-- awesome-plan project=zedbsd record=ws035p110 -->

# ws035-p110: 衝突の Replace は Trash へ、cut の paste の Esc は clipboard を保つ（F-050 の一部）

Phase ID: `ws035-p110`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-28、サブエージェント）
Phase disposition: normal
Queue: なし（2026-09-28 main の割り当て 2。[F-050](../../future-work.md) の 2 つを promote）

## 範囲

[p106](../phase106/phase.md) の残りのうち 2 つ: (1) Replace は置き換えた item を消していたので、Trash へ移して undo で戻せるようにする。
(2) cut の paste を衝突の問いの Esc で止めると clipboard が空になっていたので、保つ。F-050 の残り（folder の merge）は範囲外で F-050 に残る。

## 実装（2026-09-28）

- **task**（`userland/desktop/files/task.c`・`ops.h`）: `task_resolve` の replace は、行き先の item を `fm_trash_path` の Trash へ移す
  step（trashinfo の記録 → 同じ file system なら rename、違えば copy と削除）を source の step の前に計画する。Trash の場所は
  source ごとに `task->replaced[]` に残る。Trash が無いときだけ従来どおり削除。Trash へ移す計画は新しい `task_plan_trash()` にまとめ、
  `FM_TASK_TRASH` もそれを使う（同じ step）。
- **undo**（`undo.c`・`ops.h`）: `fm_undo_item` に `replaced`（対ごとの Trash の場所、無ければ NULL）、`fm_undo_set_replaced()` が
  最新の変更に付ける。
- **actions**（`actions.c`）: `actions_record` が copy・move の対と一緒に置き換えた item の場所を記録する。undo: copy は複製を Trash へ、
  その後に置き換えた item を RESTORE（task は順に走るので名前が空いてから戻る）。move は rename で戻した後に RESTORE。redo: 戻した
  item を先に Trash へ、その後に copy・move（move は置き換えがあるとき rename でなく task、戻した item を上書きしないため）。
  log `UNDO replaced redo= items=`。
- **cut の Esc**（`actions.c`・`files.h`）: cut の paste で衝突の問いが出たときは、clipboard を消さずに `collision_cut` を立てる。
  答えが揃って task が queue に入った時に消す。Esc（`fm_action_collision_cancel`）は clipboard を保つ。log `COLLISION cancel index=
  clipboard=kept`。
- host 試験（`plan/tools/files/host-model.c`）の 2c: replace で置き換えた file と folder（中の file ごと）が Trash にあり、複製の
  TRASH と置き換えた item の RESTORE（undo と同じ 2 つの task）で元の file が元の名前に戻る。

## 検証（2026-09-28）

- host: `plan/tools/files/host-build.sh`、`build/ws071-host/files-model`: 77 件 ok（2c の 3 件を含む）。FAIL の 4 件（PNG の ENOTSUP、
  open の 3 件）は p106 の時点から同じ既存のもの。
- guest（amd64、Venus、lean な files の image `build/p110-files`）: `plan/ws035/tests/zdesktop-p110.sh`（新）PASS:
  1. Documents の 6 件を Downloads（古い Budget.csv・Report.pdf がある）へ copy、Apply to all で Replace。古い 2 件は
     `~/.local/share/Trash/files` に中身のまま、trashinfo と一緒に入る（`replaced.png`）。
  2. Ctrl+Z: `UNDO redo=0 kind=1 items=6`、`UNDO replaced redo=0 items=2`、restore の task。古い 2 件が元の名前に戻り、複製は無く、
     "2" の名前も無い（`undone.png`）。
  3. Documents の 6 件を cut して Downloads へ paste、Esc: `COLLISION cancel ... clipboard=kept`。`/tmp/files.clipboard` は cut の
     まま、何も動かない（`esc.png`）。もう一度 paste して Apply to all で Skip: 空いている名前の 4 件が動き、2 件は残り、clipboard は
     消える（`moved.png`）。
  zdesktop の log に ERROR なし。style の修正の後にもう一度走らせて PASS。
- 回帰 PASS: zdesktop-p106（衝突の dialog: Replace・Skip・Keep Both・Esc・drop）、files-p004（paste・delete の dialog・undo）。
- build warning 0（files）。`plan/tools/style-check.py` は task.c・actions.c・undo.c とも 0。
- 画面: `/home/awe/zedBSD-rpi4/build/ws035-shots/p110-20260928-{replaced,undone,esc,moved}.png`。
- 未実施: 実機。Trash が別の file system にあるときの replace（copy と削除の経路）は guest では試していない（host の試験も同じ file system）。

## 残り

- F-050 の folder の merge（中身を合わせる）は F-050 に残る。
- redo は、undo が戻した item をもう一度 Trash へ送ってから copy・move する。undo の記録の Trash の場所は最初の時のままなので、
  redo の後にもう一度 undo すると、Trash の名前が変わっていた場合は置き換えた item が戻らないことがある（edge case）。
- undo の RESTORE は空いた名前に戻す。undo の前に同じ名前の別の item ができていたら "name 2" になる。
