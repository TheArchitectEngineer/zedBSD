<!-- awesome-plan project=zedbsd record=ws087-p003 -->

# ws087-p003: Tab の補完

Status: cleared（2026-09-29、subagent の worktree `wt/ws087`）
Disposition: normal
Parent: [WS087](../ws.md)
Queue: main が割り当てた（2026-09-29。libedit の変更の許可: GNU Readline と同じ名前・型の追加だけ、既存の API と振る舞いを変えない）

## 目的と受け入れ条件

ユーザー（2026-09-29）:「/bin/shがタブキーの補間を使えません。使えるようにしたいです。」設計は [p001](../phase001/phase.md) の「p003」。

最初の語は命令（予約語・builtin・alias・関数・PATH の実行 file）、それ以外は path。候補が複数なら共通の部分まで、2 回目の Tab で一覧（bash に近く）。
vi mode（insert mode）と POSIX の振る舞いを壊さない。host の pty、guest の ssh と Terminal で確かめる。

## 実装

- `userland/base/libedit/readline/readline.h`・`readline.c`（GNU Readline と同じ名前・型の追加だけ）:
  `rl_completion_func_t`・`rl_linebuf_func_t`、`rl_attempted_completion_function`、`rl_completer_word_break_characters`（既定は GNU の既定）、
  `rl_char_is_quoted_p`、`rl_completion_append_character`（呼ぶ前に毎回 `' '`）、`rl_completion_query_items`（100）。
  - emacs mode と vi の insert mode の Tab: cursor から区切り（引用されていないもの）まで戻った語を関数に渡す。NULL → bell。1 つ → 置き換えて append。
    複数 → `[0]`（共通の部分）に置き換え、行が変わらず直前の key も Tab なら一覧、それ以外は bell（bell は行の再描画の後）。
  - 一覧: 行の末尾へ移って改行、`rl_completion_query_items` を超えたら `Display all N possibilities? (y or n)`（y・Y・空白で表示）、
    `TIOCGWINSZ` の幅（取れなければ 80）で列優先に並べ、prompt と行を書き直して cursor を戻す。
  - 関数が NULL（既定）なら Tab は今までどおり何もしない（`net` の tool は変わらない）。
  - `[1..]` は「一覧に出す文字列」とした（GNU では完全な match。呼び手の sh は表示用の名前を入れる）。header に書いた。
- `userland/base/sh/complete.c`（新規）: `sh_complete_init()` が上の hook を設定（区切り `" \t\n\"'\`<>=;|&()"`、backslash を数えて引用を判定）。
  - 命令の位置: 行頭、`; & | ( )` の後、予約語 `! { do elif else if then time until while` の後、代入の後。redirect の後の語と、`=` などの後に続く語は引数。
  - 命令: 予約語（`sh_reserved_word`）・builtin（`sh_builtin_name`）・alias（`sh_alias_name`）・関数（`sh_function_name`）・PATH の実行 file・
    ここの directory（`dir/`）。語に `/` があれば path（directory と実行 file だけ）。
  - path: 最後の `/` までを入力のまま残し、`~`・`~user` を展開して読む。`.`・`..` は出さず、dot file は頭が `.` のときだけ。directory は `/` を付け、空白は付けない。
  - 挿入する文字列は `" \t\n\\'\"\`$&|;<>()*?[]!#{}"` を backslash で引用。開いた引用符の中は補完しない（bell）。候補は並べ替えて重複を除く。
- `builtins.c`・`alias.c`・`command.c`: 名前を index で返す関数（上の 4 つ）。`shell.h`・`alias.h`: 宣言。`main.c`: 対話の shell で `sh_complete_init()`。
  `Makefile`: `complete.c`。

## 試験の結果

- host（`plan/ws087/tests/complete-host.py build/ws087/host-sh`）: **29/29**（共通の部分と bell、2 回目の一覧（実行できない file は出ない）、1 つの候補と空白、
  builtin、alias（補完して実行）、関数、path、directory、空白を含む名前の引用（実行できる）、dot file、一覧の directory と dot file、`./` の命令、`~`、
  pipe・redirect・代入・予約語の後、代入の値、候補なしの bell、開いた引用符、長い一覧の質問（n と y）、行の途中、vi の insert mode）。
- host の回帰: `history-host.py` 17/17、`prompt-host.py` 8/8、`plan/tools/sh/vi-host.py` 33/33。
- build（amd64、`-Werror`）: exit 0。libedit・sh の全 file・net の全 file が build し直され、warning 0。log の warning は gmake の jobserver・host の perl の
  locale・toolchain の noct（`userland/base/noct/.../interpreter.c`、この WS の外）だけ。
- QEMU の ssh -tt（login shell）: `cd ~/wo` Tab → `~/work/`、`ls al` Tab → `alp`+bell、Tab → `alpha.txt   alpine.txt` の一覧と行の書き直し、`h` Tab →
  `alpha.txt `、`ls bet` Tab → `beta\ dir/`（実行できる）、`net` Tab → bell（net と networkd）、`ec` Tab → `echo `、空行で Tab Tab →
  `Display all 247 possibilities? (y or n)`、n、vi mode で `cd da` Tab → `data/`。
- QEMU の Terminal（main の desktop の image の複写 `build/ws087/zd-fixed`、新しい sh は `/tmp/newsh`、QMP の key）: 上と同じ操作と `ls /usr/` Tab Tab の
  一覧（worktree の `build/ws087/shot-complete.png`）。
- boot test（`OUTPUT=build/ws087/boot-p003`）: PASS。

## 制限（範囲外として記録）

- 開いた引用符の中（`"My Doc<Tab>`）、変数名（`$HO<Tab>`）、`~user` の user 名の補完はしない。
- vi の command mode の補完（`\`・`*`・`=`、POSIX の vi-mode）は F-006 のまま。
- 画面の幅を超える行の再描画は既存の制限のまま（libedit は折り返しを扱わない）。
- 幅は byte 数で数える（UTF-8 の名前の列はずれることがある）。

## 未実施

- 実機での確認。WS042 の差分試験は p004 で流す。
