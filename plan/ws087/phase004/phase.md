<!-- awesome-plan project=zedbsd record=ws087-p004 -->

# ws087-p004: 規約の全文との照合、回帰（WS042 の sh の試験）

Status: cleared（2026-09-29、subagent の worktree `wt/ws087`）
Disposition: normal
Parent: [WS087](../ws.md)
Queue: main が割り当てた（2026-09-29）

## 範囲

WS087 が変えた source 全部（c1e02876 から。commit 7d6809f0・e1ff6904・eeb6cbf3・c8656c58・147fd4e8・6a8b5a44）:
`userland/base/sh/{history,input,main,builtins,alias,command,complete}.c`・`shell.h`・`alias.h`・`Makefile`、
`userland/base/libedit/readline.c`・`readline/readline.h`・`readline/history.h`・`README.md`、`src/drivers/platform/pcat/ps2-8042.c`
（`keyboard_build_capabilities()` だけ）。規約は `plan/coding-style.md` の全文。

## 照合

- `plan/tools/style-check.py`（機械で見る規則）: 変更前の同じ file と比べ、WS087 が足した違反は complete.c の 12 件（`(void)end` の前の comment、
  条件の中の `strchr`・`S_ISDIR`・`S_ISREG`・`access`、閉じ括弧の後の空行）と ps2-8042.c の 1 件（閉じ括弧の後の空行）。すべて直した。
  sh・libedit の WS087 の file は違反 0。ps2-8042.c の残り 31 件は WS087 の前からある（変更前 32 件、WS087 の関数の中は 0）。
- 読んで確かめた規則（道具が見ないもの）: 各 paragraph と loop・if・return の comment、comment が文を言い直していないこと、関数の最後の成功の return、
  意味のある呼び出しの結果を直接 return しないこと、ANSI C の宣言の位置、1 行 1 引数、file の変数と型の comment、public 関数の verb-and-object の comment、
  条件式の分解。直したもの: readline() の bell の `if` の前の空行と comment、`complete_ask()` の read の後の書き込みと check の分離、
  `history_file_read()` の読んだ長さの paragraph、`sh_history_size_changed()` の `(void)name` の comment。
- 例外: なし。`history_file_path()` の `home == NULL || home[0] == '\0'` と `prompt_directory()` の 2 節の条件は §6 の「副作用の無い 2 節」の範囲。

## 回帰

- host の WS042 の差分試験（`plan/tools/sh/sh-diff.py`、oils は main の `build/ws042/oils`）: **1438/1458**。WS087 の前の host-sh（d41be6ec）と失敗する
  case の集合が同じ（増減 0）。
- host: `plan/tools/sh/vi-host.py` 33/33、`plan/ws087/tests/complete-host.py` 29/29、`history-host.py` 17/17、`prompt-host.py` 8/8。
- build（amd64、`plan/tools/guest/build-ssh-image.sh build/amd64`、`-Werror`）: exit 0。WS087 の file に warning 0
  （log の warning は gmake の jobserver・host の perl の locale・toolchain の noct だけ）。
- QEMU（amd64）の serial console の対話の回帰（`plan/tools/sh/sh-interactive.py`）: **41/41**（syntax error、PS2、Ctrl-C、alias、here-document、fc、
  job control、emacs と vi の行編集）。
- QEMU の ssh -tt: 補完（`~/wo`→`~/work/`、`al`→`alp`+bell→一覧→`alpha.txt `、`ec`→`echo `）、新しい session の上の矢印で前の session の命令。
- boot test（`OUTPUT=build/ws087/boot-p004`）: PASS。

## 未実施

- guest の WS042 の差分試験（`plan/tools/sh/guest-batches.sh`、約 36 batch と再起動）: WS087 の変更は対話の shell（端末の line editor・履歴の file・prompt・
  補完）にしか届かず、非対話は host の差分試験で変更前と同じ結果なので省いた。
- aarch64 の build（試験は amd64 だけの指示）。実機での確認（main が預かった）。
