<!-- awesome-plan project=zedbsd record=ws087 -->

# WS087: /bin/sh の対話の行編集（矢印キーの履歴と Tab の補完）

<!-- awesome-plan-current:start -->
Status: completed（QEMU の証拠。実機の確認は main が預かった）
Completed: 2026-09-29（subagent の worktree `wt/ws087`）
Primary Milestone: MG002
Related Milestones: MG006
Objectives: O1
Parent: [Master](../master.md)
Queue: なし
Resume point: なし（新しい要求は新しい WS として立てる。この WS は再開しない）
<!-- awesome-plan-current:end -->

## 目標（2026-09-29 ユーザー）

「/bin/shが上下キーでヒストリーをたどれないようです。たどれるようにしたいです。」
「/bin/shがタブキーの補間を使えません。使えるようにしたいです。」
追加（同日）:「/bin/shで、ホームディレクトリにいるときにプロンプトに /home/kei と表示されるので、これを ~ にできるようにしたいです。」

- 対話の `/bin/sh`（Terminal・console・ssh）で、上下の矢印キーで履歴をたどれる（[BUG-103](../bugs/BUG-103.md)）。
- Tab で補完できる: 行の最初の語は命令、それ以外は path。候補が複数なら共通の部分まで、2 回目の Tab で候補の一覧（bash に近い振る舞い）。
- 既存の vi mode（ws042-p008）と POSIX の sh の振る舞い（WS042 の試験）を壊さない。vi mode の残り（[F-006](../future-work.md)）はこの WS の外。

## 結果

- **BUG-103 の原因は 2 つ**:
  1. sh が履歴を file に保存しないので、新しい shell（Terminal の窓・tab・ssh の session）の履歴が空だった。→ `HISTFILE`（unset なら `$HOME/.sh_history`、
     空なら無し）を対話で端末の shell が起動時に読み（最新 `HISTSIZE` 行、2 倍を超えた file は切り詰め）、読んだ行を直後に 1 回の write で追記（0600）。
     libedit に GNU Readline の `stifle_history()` を足し、矢印の履歴を `HISTSIZE` まで伸ばした（呼ばなければ従来の 32 行）。
  2. kernel の PS/2 keyboard（`src/drivers/platform/pcat/ps2-8042.c`）が E0 の key（矢印・Home・End・PageUp/Down・Insert・Delete・右 Ctrl/Alt・Meta）を
     capability に入れず、input layer がその event を捨てていた（5330 の内蔵 keyboard は PS/2、QEMU の既定の USB keyboard では起きない）。
     → capability を plain と E0 の両方の表から作る。QEMU で PS/2 だけにして evdev と Terminal に届くことを確かめた。
- **Tab の補完**: libedit に GNU Readline と同じ名前の hook（`rl_attempted_completion_function` ほか）と、候補の列の一覧（多いときは y/n で聞く）。
  sh の `complete.c` が命令（予約語・builtin・alias・関数・PATH の実行 file・ここの directory）と path（`~` の展開、directory に `/`、dot file、
  backslash の引用）を返す。emacs mode と vi の insert mode。
- **prompt**: 既定の prompt（PS1 が無いとき）で `$HOME` を `~`・`~/…` と出す（bash の `\w` と同じ。HOME が空・`/` なら置き換えない）。
- 検証（QEMU amd64 と host）: host の pty の試験（補完 29/29、履歴 17/17、prompt 8/8、vi 33/33）、WS042 の host の差分試験 1438/1458（WS087 の前と
  同じ失敗の集合）、serial の対話の回帰 41/41、ssh -tt と desktop の Terminal（QMP の key）で履歴・補完・prompt、boot test。build は amd64 で warning 0。
- 規約: WS087 の source に `plan/tools/style-check.py` の違反 0（ps2-8042.c は WS087 の前からある 31 件、変えた関数の中は 0）。読んで確かめる規則も照合した（p004）。

## 制限・移管

- **実機（Dell Latitude 5330）での確認は未実施**。手順（evdev の code 103、Terminal の `ESC [ A`、同じ窓と新しい窓の履歴、prompt の `~`）は main が預かった。
  BUG-103 は実機の確認まで scheduled のまま。
- 補完しないもの: 開いた引用符の中、変数名（`$HO`）、`~user` の user 名。一覧の幅は byte 数（UTF-8 の名前は列がずれうる）。画面の幅を超える行の再描画は
  libedit の既存の制限。vi の command mode の補完は F-006。
- guest の WS042 の差分試験（guest-batches）と aarch64 の build は行っていない（変更は対話の経路だけ、試験は amd64 だけの指示）。
- 試験は `plan/ws087/tests/` に残した（`complete-host.py`・`history-host.py`・`prompt-host.py`・`pty-keys.py`）。`plan/tools/sh/` への移動と
  master の Tools 節への登録は main に依頼した（subagent の範囲外）。
- 補完の設計は libedit の `readline/readline.h` の comment と `userland/base/sh/complete.c` の冒頭にある。

## Phase 一覧

Phase の記録（phase.md）は削除した。git の履歴にある（最後の版は f8dcd6af）。

| Phase | 内容 | Status |
| --- | --- | --- |
| ws087-p001 | BUG-103 の原因と Tab の補完の設計 | cleared（新しい shell の履歴が空が有力な原因） |
| ws087-p002 | 履歴の file と `stifle_history` | cleared（host 17/17。PS/2 の件を発見） |
| ws087-p003 | Tab の補完 | cleared（host 29/29、QEMU の ssh と Terminal） |
| ws087-p004 | 規約の全文との照合、回帰 | cleared（差分 1438/1458 で変化なし、対話 41/41） |
| ws087-p005 | PS/2 の E0 の key が捨てられる（BUG-103 の第 2 の原因） | cleared（PS/2 だけの QEMU で evdev と Terminal に届く） |
| ws087-p006 | 既定の prompt の `~` | cleared（host 8/8、QEMU の ssh と Terminal） |
