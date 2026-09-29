<!-- awesome-plan project=zedbsd record=ws087-p002 -->

# ws087-p002: 矢印キーの履歴（BUG-103 の修正）

Status: in-progress（2026-09-29、subagent の worktree `wt/ws087`）
Disposition: normal
Parent: [WS087](../ws.md)
Queue: main が割り当てた（2026-09-29、libedit の変更の許可つき: GNU Readline と同じ名前・型の追加だけ、既存の API と振る舞いを変えない、net・sh の build を確かめる）

## 目的と受け入れ条件

設計は [p001](../phase001/phase.md) の「p002」。ユーザーへの質問（(a) 新しい窓で上 / (b) 同じ窓でも上が効かない）の答えが来るまで (a) として進める。

1. 対話の shell（stdin が端末）が起動時に `HISTFILE`（unset なら `$HOME/.sh_history`、空なら file なし）を読み、fc と矢印の履歴に入れる。
2. 端末から読んだ空でない行を、読んだ直後に file の末尾へ 1 回の write で足す（O_APPEND、0600）。書けなくても shell は続ける。
3. 起動時に file が HISTSIZE の 2 倍の行を超えていたら、最後の HISTSIZE 行に縮める。
4. libedit に `stifle_history(int)` を足し、sh が起動時と HISTSIZE の変更時に呼ぶ（呼ばなければ 32 行のまま＝既存の振る舞い）。
5. 非対話の shell（script、`-c`、端末でない stdin）は読みも書きもしない。
6. 試験: host の pty（新しい shell で上が前の shell の命令を出す、HISTFILE、HISTSIZE の切り詰め、stifle で 32 行を超えて戻れる）、guest の ssh -tt、
   WS042 の差分試験（非対話が変わらない）と vi の host 試験、net と sh の build（warning 0）、boot test。

## 進み具合（Resume point）

- 実装中。
