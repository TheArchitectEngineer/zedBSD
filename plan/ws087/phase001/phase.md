<!-- awesome-plan project=zedbsd record=ws087-p001 -->

# ws087-p001: BUG-103 の原因と、Tab の補完の設計

Status: cleared（2026-09-29、subagent の worktree `wt/ws087`）
Disposition: normal
Parent: [WS087](../ws.md)
Queue: main が割り当てた（subagent の実行。queue.md の記録は main）

## 目的と受け入れ条件

- BUG-103 の原因: line editor が有効になる条件、Terminal（`/bin/terminal`）・console・ssh が送る escape の形、どこで履歴の呼び出しが外れるか。
- Tab の補完の設計: 最初の語は命令（builtin・alias・関数・`PATH` の実行 file）、それ以外は path、複数の候補は共通の部分まで、
  2 回目の Tab で一覧（bash に近く）。vi mode と POSIX の振る舞いを壊さない。
- 修正の実装は p002・p003。この Phase は source を変えない。

## 調べたこと（コードを読んだ）

1. **line editor が有効になる条件**: `main.c` は `isatty(0) && isatty(2)` のとき interactive にし、`run_interactive()` が
   `sh_input_push_file(0, 1)`（SOURCE_TERMINAL）を積む。`input.c` の `fill_terminal()` は `isatty(fd)` なら `readline()`（`userland/base/libedit`）で
   1 行を読み、空でない行を `add_history()`（libedit の矢印用の履歴、`HISTORY_MAX` 32 行）と `sh_history_add()`（fc 用、`HISTSIZE` 既定 128 行）の両方に入れる。
   `set -o vi` なら `rl_editing_mode = 0`。端末でなければ行編集なしで 1 行ずつ読む。
2. **libedit の矢印**: emacs mode は `ESC [ A/B/C/D`・`ESC [ H/F`・`ESC [ 3 ~` と Ctrl-P/N/B/F/A/E を解する（`escape_key()`）。`ESC O A`（DECCKM の
   application mode）は解さないが、どの端末もその mode に入れる者がいない（sh も Terminal も DECCKM を出さない）ので関係しない。
   vi mode は insert で ESC を受けて command mode に入り、`[`・`A` を `k` に読み替える。
3. **Terminal が送る形**（`userland/desktop/terminal/keys.c`）: 上下左右は `ESC [ A/B/D/C`、Home/End は `ESC [ H/F`、Tab は `\t`、BS は `DEL`。
   compositor（`userland/desktop/wayland/seat.c`）は Wiseview・menu・App Home・lock/greeter が出ている間だけ矢印を取る。通常は evdev の code を
   そのまま Terminal に渡す。kernel の console（`src/kern/tty.c`）も非 canonical の読み手に `ESC [ A` などを渡す。USB HID（`usb-hid.c`）と
   PS/2（`ps2-8042.c`）の矢印は evdev の `KEY_UP` などに写る。
4. **Terminal と sh の間**: `forkpty()` の pty。kernel の pty の slave は `tty_ioctl_instance()` で `TCSETS` を受け、raw mode（`~ICANON ~ECHO ~ISIG`、`VMIN 1`）になる。

## 観測（QEMU、amd64）

guest の image は worktree で作った `build/amd64/hdd-image.img`（`plan/tools/guest/build-ssh-image.sh build/amd64`、`build/amd64/sysroot` は main の複写）と、
デスクトップの確認は main の `build/ws035-sq/hdd-image.img`（2026-09-29 05:04、sh・libedit・terminal・tty.c の最後の変更 03:44 より後）を
`guest.py start` で複写して起動（Venus、runtime `build/ws087/zd-run`）。

| 経路 | 手順 | 結果 |
| --- | --- | --- |
| host の pty（`build/ws087/host-sh`） | `echo one`、`echo two`、上、上、Enter；`ESC`・`[`・`A` を別々の write で | 呼び出し・再描画とも正しい（`one` が出る） |
| ssh -tt（pty、`sh -i` と login shell） | `plan/ws087/tests/pty-keys.py` | 同じ shell の中では上で直前の命令が出て、Enter で実行される |
| Terminal（zdesktop、QMP の send-key） | `--command="stty raw -echo; dd bs=1 of=/tmp/keys.bin"` で上下左右・Tab を押す | `033 [ A`、`033 [ B`、`033 [ D`、`033 [ C`、`\t`（期待どおり） |
| Terminal の中の `/bin/sh` | `echo x >> /tmp/count` Enter、上 Enter、上 Enter | `/tmp/count` が 3 行、画面にも同じ命令が 3 回（`build/ws087/shot2.png`） |
| 新しい shell（ssh の login shell） | 何も打たずに上、上、Enter | 何も出ない（履歴が空） |

WS042 の serial console の試験（`plan/tools/sh/sh-interactive.py`）でも上の矢印は通っていた。

## 結論（BUG-103）

- **同じ shell の中の矢印による履歴の呼び出しは、QEMU の Terminal・ssh・serial のどれでも動く。** line editor・Terminal の escape・pty・compositor に欠陥は見つからない。
- **shell ごとに履歴が空から始まる。** sh は履歴を file に保存も読み込みもしない（`HISTFILE`・`~/.sh_history` の扱いが無い）。Terminal の窓・tab・
  ssh の session を新しく開くたびに、上を押しても何も出ない。POSIX（XCU sh の `HISTFILE`）は「shell の起動時に HISTFILE（無ければ `$HOME/.sh_history`）
  を履歴の file として使ってよい」とし、bash も `~/.bash_history` を持つ。bash に慣れた利用者が新しい Terminal で上を押すと「たどれない」になる。
  **これが BUG-103 の有力な原因**である（QEMU で再現した形）。
- 付随: libedit の矢印の履歴は 32 行（`HISTORY_MAX`）で、fc の履歴（`HISTSIZE` 既定 128）より短い。
- **未確定**: ユーザーの 5330 の実機で「同じ窓で命令を打った直後でも上が効かなかった」のかは分からない。そうなら実機の key の経路
  （i8042・内蔵 keyboard）の別の不具合になり、QEMU では再現しない。実機での確認は未実施（ユーザーへの質問を main に依頼する）。

## 設計

### p002: 矢印の履歴（BUG-103）

1. **履歴の file**（sh、`history.c`・`main.c`・`input.c`）:
   - 対話の shell が `run_interactive()` に入る前（profile・`$ENV` を読んだ後。そこで `HISTFILE`・`HISTSIZE` を設定できる）に、
     `HISTFILE`（空でなければ）か `$HOME/.sh_history` を読み、各行を `sh_history_add()` と `add_history()` に入れる。
   - 端末から読んだ空でない行を、読んだ直後にその file の末尾に 1 行足す（`O_WRONLY|O_APPEND|O_CREAT`、mode 0600）。窓・tab が同時に開いていても、
     終わり方（窓を閉じる、kill）に依らず残る。書けなければ黙って続ける（履歴は補助で、shell の動作を止めない）。
   - file が `HISTSIZE` の 2 倍の行を超えていたら、起動時に最後の `HISTSIZE` 行へ縮める（一時 file に書いて rename）。
   - 非対話の shell（script、`-c`）は読みも書きもしない。
2. **libedit の履歴の大きさ**を `HISTSIZE` に合わせる: GNU Readline の `stifle_history(int)` を libedit に足し、`HISTORY_MAX` の固定の配列を
   可変にする。sh は起動時と `HISTSIZE` の変更時（`sh_var_hook`）に呼ぶ。**libedit の変更が要る**（下の依頼）。libedit を変えられないなら 1 だけで
   BUG-103 は直り、矢印は最新 32 行までという制限が残る。
3. 試験: host の pty（新しい shell で上が前の shell の命令を出す、`HISTFILE` の指定、`HISTSIZE` での切り詰め、2 つの shell の交互）、guest の ssh -tt、
   Terminal（QMP の send-key と画面）。WS042 の差分試験（`plan/tools/sh/sh-diff.py`）で非対話の振る舞いが変わらないこと。

### p003: Tab の補完

**libedit（GNU Readline と同じ名前の小さな部分）**:

- `rl_attempted_completion_function`（`char **(*)(const char *text, int start, int end)`）: Tab で呼ぶ。返す配列は GNU と同じく
  `[0]` が語 `[start, end)` と置き換える文字列（候補が 1 つならその候補、複数なら共通の部分）、`[1..]` が候補、NULL で終わり、全部 `malloc` で
  editor が free する。editor は `[1..]` を一覧に出すだけで挿入には使わない（sh は表示用の名前を入れる。GNU の filename の表示と同じ見え方）。NULL は候補なし。
- `rl_completer_word_break_characters`（語の区切り）と `rl_char_is_quoted_p`（`int (*)(char *line, int index)`、区切りの文字が引用されているか）:
  editor は point から区切りまで戻って `start` を決める。sh は `" \t\n\"'\`<>=;|&()"` と、backslash の直後を引用とする関数を設定する。
- `rl_completion_append_character`（既定 `' '`、補完の関数を呼ぶ前に毎回 `' '` に戻す）: 候補が 1 つのとき後ろに足す。sh は directory のとき `'\0'` にする。
- `rl_completion_query_items`（既定 100）: 一覧がこれを超えたら `Display all N possibilities? (y or n)` と聞く。
- Tab の振る舞い（GNU の `rl_complete` と同じ）: 候補なし → bell。1 つ → 置き換えて append。複数 → 共通の部分へ置き換え、行が変わらなければ bell。
  **直前の key も Tab で行を変えなかったとき** → 一覧: 行の末尾へ cursor を移して改行、候補を列に（`TIOCGWINSZ` の幅、取れなければ 80、列優先の並び、
  幅 = 最長 + 2）、prompt と行を書き直して cursor を戻す（readline は prompt を覚えておく）。
- emacs mode と vi の insert mode の Tab で働く。関数が無ければ今と同じく Tab は無視（`net` の tool は変わらない）。vi の command mode の
  `\`・`*`・`=`（POSIX の vi-mode の補完）は F-006 のまま。

**sh（新しい file `userland/base/sh/complete.c`）**:

- 文脈: 語の前を空白を飛ばして見て、行頭・`;`・`|`・`&`・`(`（`&&`・`||`・`;;` を含む）の直後、予約語 `then do else elif if while until ! { time` の直後、
  命令の前の代入（`NAME=value`）の後なら**命令の位置**。それ以外は引数。
- 命令の位置で語に `/` が無い: 予約語・builtin（`builtins.c` の表を列挙する関数を足す）・alias（`alias.c` に列挙を足す）・関数（`command.c` に列挙を足す）・
  `PATH` の各 directory の実行 file（`access(X_OK)` の通常 file）・現在の directory の directory（`dir/` に続けて打てるように、bash と同じ）。
- 語に `/` がある命令の位置、または引数: path。語を directory の部分と名前の頭に分け、先頭の `~`・`~user` を展開して `opendir`、頭が合う entry
  （頭が `.` で始まらなければ dot file は出さない、`.`・`..` は出さない）。directory は `/` を付ける。
- 語の backslash を外して照合し、挿入する文字列は shell の特殊文字（空白、`\ ' " $ \` & | ; < > ( ) * ? [ ] ! # { } ~` の語頭）を backslash で
  引用する。共通の部分は引用を外した形で計算してから引用する（引用の途中で切れないように）。
- 候補は並べ替えて重複を除く。一覧には名前だけ（path は最後の `/` の後、directory は `/` 付き）。
- 制限（p003 の範囲外として記録する）: 開いた引用符の中（`"My Doc<Tab>`）の補完、変数名（`$HO<Tab>`）の補完、画面の幅を超える行の再描画（既存の制限）。
- 試験: host の pty（命令・builtin・alias・関数・path・directory・`~`・空白を含む名前・共通の部分・2 回目の一覧・候補なしの bell・vi の insert mode）、
  guest の ssh -tt、Terminal（QMP と画面）。WS042 の差分試験と vi の host 試験（`plan/tools/sh/vi-host.py`）。

## main への依頼（p002・p003 の前提）

1. **`userland/base/libedit/` の変更の許可**: p003 の Tab の補完は line editor（libedit）に補完の hook と一覧の表示を足さないと作れない
   （sh は `readline()` が raw mode で key を読んでいる間に Tab を見られない）。p002 の `stifle_history` も libedit。subagent の修正範囲は
   `userland/base/sh/` だけなので、`userland/base/libedit/readline.c`・`readline/readline.h`・`readline/history.h`・`README.md` を足してほしい。
   公開の関数と変数は GNU Readline と同じ名前・型の追加だけで、既存の API は変えない（`net` の tool は影響を受けない）。
2. **ユーザーへの質問**: 5330 で上が効かなかったのは、(a) 新しい Terminal を開いてすぐ上を押した（前の窓や前回の命令が出てほしかった）のか、
   (b) 同じ窓で命令を打った直後でも上で何も出なかったのか。(a) なら p002 の設計で直る。(b) なら実機の key の経路の不具合で、QEMU では再現しない。
   答えが来るまでは (a) として p002 を進め、(b) は p002 の実機の確認（未実施）で分かる。

## 実行したコマンド（worktree `/home/awe/zedBSD-rpi4/.claude/worktrees/ws087-sh`）

- `sh plan/tools/sh/build-host-sh.sh build/ws087/host-sh` → 成功。pty の試験は scratchpad の python（上の表の 1 行目）。
- `cp -a /home/awe/zedBSD-rpi4/build/amd64/sysroot build/amd64/sysroot`（1 回目の build が sysroot 無しで止まったため、main の複写）、
  `timeout 3600 sh plan/tools/guest/build-ssh-image.sh build/amd64` → exit 0、`build/amd64/hdd-image.img`。
- `GUEST_RUNTIME=build/ws087/guest plan/tools/guest/guest.sh start build/amd64/hdd-image.img`・`wait` → ready。`plan/ws087/tests/pty-keys.py`（2 通り）。
- デスクトップ: `guest.py start /home/awe/zedBSD-rpi4/build/ws035-sq/hdd-image.img`（Venus の option は `plan/ws035/tests/zdesktop-guest.sh` と同じ、
  renderer は main の `build/ws035-sq-venus/install` を読むだけ）、`/bin/wayland --glass`、`/bin/terminal`、`plan/tools/files/qmp-input.py` の key、
  `plan/ws035/tests/zdesktop-shot.py`（`build/ws087/shot1.png`・`shot2.png`）。
- 両方の guest を `guest.py stop` で止めた。

## 未実施

- 実機（5330）での確認。
- source の変更は無いので build（warning）・boot test はこの Phase では行っていない。

## 成果物

- `plan/ws087/tests/pty-keys.py`（ssh -tt で guest の sh に key を送り、各 key の後の出力を表示する）。
- `build/ws087/shot2.png`（Terminal で上と Enter が同じ命令を繰り返した画面、QEMU）。build/ は git に入らない。
