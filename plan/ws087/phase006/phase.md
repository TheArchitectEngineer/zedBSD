<!-- awesome-plan project=zedbsd record=ws087-p006 -->

# ws087-p006: 既定の prompt で home directory を `~` にする

Status: cleared（2026-09-29、subagent の worktree `wt/ws087`）
Disposition: normal
Parent: [WS087](../ws.md)
Queue: main が割り当てた（2026-09-29）

## 目的と受け入れ条件

ユーザー（2026-09-29）:「/bin/shで、ホームディレクトリにいるときにプロンプトに /home/kei と表示されるので、これを ~ にできるようにしたいです。」

PS1 が無い対話の shell の既定の prompt（`user@host:cwd$ `）で、cwd が `$HOME` と同じなら `~`、`$HOME/` の下なら `~/…`（bash の `\w` と同じ）。
HOME が unset・空・`/` なら置き換えない。host の pty と guest の Terminal で確かめる。

## 実装

`userland/base/sh/main.c`: `sh_prompt_text()` の既定の prompt が `prompt_directory()` を通す。HOME の末尾の `/` は外して比べ、名前の境目
（`/home/kei2` は `/home/kei` の下ではない）を確かめる。PS1 が設定されているときの展開は変えていない。

## 試験の結果

- host（`plan/ws087/tests/prompt-host.py build/ws087/host-sh`）: **8/8**（HOME そのもの、下の directory、末尾 `/` の HOME、同じ接頭辞の兄弟、外、
  HOME=/、空、unset）。回帰: `history-host.py` 17/17、`plan/tools/sh/vi-host.py` 33/33。
- build（amd64、`-Werror`）: exit 0、warning 0（log の warning は host の perl の locale だけ）。
- QEMU の ssh -tt（login shell）: `root@kei:~$ `、`cd /usr/bin` → `/usr/bin`、`cd ~/work/deep` → `~/work/deep`、`cd` → `~`。
- QEMU の Terminal（main の desktop の image の複写に p005 の kernel、新しい sh を `/tmp/newsh` に置いて `--command=cd /root; exec /tmp/newsh -i`）:
  `~` → `cd docs` で `~/docs` → `cd /usr` で `/usr` → `cd` で `~`（worktree の `build/ws087/shot-prompt.png`）。
- boot test（`OUTPUT=build/ws087/boot-p006`）: PASS。

## 未実施

- 実機での確認。
