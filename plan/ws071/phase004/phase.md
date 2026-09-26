<!-- awesome-plan project=zedbsd record=ws071p004 -->

# ws071-p004: file 操作（task・clipboard・rename・new folder・ゴミ箱・完全削除・undo/redo・進み）

Phase ID: `ws071-p004`
Parent: [WS071](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示でサブエージェントが worktree の branch で実行。main の Queue への反映は main の session）

## 範囲

[design.md](../design.md) §5.3〜§5.6、spec §15（Cut・Copy・Paste・Rename・Duplicate）、§22〜§24、§31〜§33: 協調の task（copy・move・
trash・restore・delete・duplicate・link）、file の clipboard、rename（F2、inline、拡張子を除いた部分の選択）、new folder
（Ctrl+Shift+N）、freedesktop.org Trash（Trash の場所、Put Back、Empty Trash）、完全削除の確認（Shift+Delete）、undo・redo
（Ctrl+Z・Ctrl+Shift+Z）、toolbar の進みの輪と task の一覧（cancel）、status の pill の進み。

## 受け入れ

1. host の model 試験（一時 directory）と host の画面、Venus（QEMU）の log・guest の file・画面で上の操作が動く。
2. 移動で失敗した source を消さない（copy の失敗のある source の削除の step は飛ばす）。
3. warning 0、`style-check.py` 0。

## 結果（2026-09-27）

cleared。

- 実装: `ops.h`（task・undo・clipboard の型）、`task.c`（計画の段: directory の iterator の stack で数項目ずつ、実行の段: 256 KiB ずつの
  copy・mode・時刻・xattr の写し、衝突は「name 2」・複製は「name copy」、自分の中への copy・move の拒否、同じ file system は rename・
  違えば copy と削除、copy の失敗した source の削除は飛ばす、cancel で書きかけの file を消す）、`trash.c`（freedesktop.org Trash
  specification の home trash: files と info、Path の URL escape、DeletionDate、名前の衝突は name.2）、`undo.c`、`clip.c`
  （`$XDG_RUNTIME_DIR/zdesktop-files.clipboard`、rename で置き換え）、`actions.c`（keyboard・button から呼ぶ操作、task の
  queue と完了の処理: undo の記録・失敗の message・読み直し・結果の選択）、`ui-overlay.c`（確認の dialog、task の一覧）、`dir.c`
  （Trash の一覧: 元の folder と削除日時）、`ui.c`（cut の項目の印、終わった操作の結果の選択、進みの輪）、`ui-grid.c`・`ui-list.c`
  （rename の欄、cut の項目を薄く、Trash の Put Back・Empty Trash の button、status の pill の進み）、`ui-input.c`（Delete・
  Shift+Delete・F2・Ctrl+C/X/V/D/Z・Ctrl+Shift+N/Z、dialog の Enter・Esc、rename 中の key と外の click で確定）。
- 途中で直した不具合: 内容の panel の地の hit を題名の button より後に記録していて button が押せなかった（地を先に記録）。
  XDG_DATA_HOME の親が無いとゴミ箱が作れなかった（親から作る）。**host の試験の最初の版が相対 path の XDG_DATA_HOME で host の
  利用者の ~/.local/share/Trash に file を作った**（試験が作った 2 つの file と空の directory を消した。試験は HOME と
  XDG_DATA_HOME を一時 directory にし、fm_trash_path は相対の XDG_DATA_HOME を使わない）。
- host の model 試験 `build/ws071-host/files-model build/ws071-host/model /dev/shm/ws071-model` **PASS**（32 項目: copy の中身・link・
  xattr のタグ、再 copy の「Report 2.pdf」「Folder.v1 2」、複製、自分の中への copy の拒否、rename の move、ゴミ箱の記録と name.2、
  Put Back、木の削除、file system をまたぐ move（/dev/shm の tmpfs へ: copy と削除）、undo・redo の stack、clipboard、空いた名前）。
- host の画面: rename.png（欄と拡張子を除いた選択）、trash.png（Trash と button）、paste.png、dialog.png（Empty the Trash?）、
  tasks.png（200 MB の copy の進みの輪と一覧）を見た（build/ws071-host/）。
- **QEMU（Venus）**: `files-p004.sh` **PASS**（F2 で Costs、Delete でゴミ箱と記録、Trash の Put Back、Ctrl+Z 2 回（put back と trash の
  取り消し）、Ctrl+C と Ctrl+V、Shift+Delete の確認と Enter、Ctrl+Shift+N と「Drafts」、Ctrl+Z で rename の取り消し、各段で guest の
  file を確かめた）。回帰 `files-p002.sh`・`files-p003.sh` PASS。画面 build/ws071-p004/rename.png・trash.png・dialog.png・folder.png
  を見た。
- build warning 0、`style-check.py userland/base/zdesktop-files/*.c` 0、host の build（gcc -Werror）0。
- 実機（i915）: 未実施。
- 制限（design の既定どおり）: 名前の衝突は常に Keep Both。redo した copy・trash は名前が変わると再び undo できないことがある
  （そのときは「Can't do that」を出して何もしない）。他の volume の file は home trash へ copy と削除で入る（`$topdir/.Trash-$uid`
  は Future Work）。
